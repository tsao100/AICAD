// src/cad/grips/AlignmentGripProvider.cpp
#include "AlignmentGripProvider.h"
#include "railway/AlignmentDocument.h"
#include "command/alignment/AlignmentEditCommand.h"
#include "core/Application.h"
#include "ui/UIManager.h"

#include <QDebug>
#include <QLineF>
#include <QSet>
#include <QUndoStack>
#include <Precision.hxx>

namespace aicad::cad {

// ── helpers ───────────────────────────────────────────────────────────────────

static QPointF toQPointF(const gp_Pnt& p) { return QPointF(p.X(), p.Y()); }
static gp_Pnt  toGpPnt (const QPointF& p) { return gp_Pnt(p.x(), p.y(), 0.0); }

/// Two-line intersection (infinite lines).  Returns true and sets `pt` on success.
static bool lineIntersect(QPointF a1, QPointF a2,
                          QPointF b1, QPointF b2,
                          QPointF& pt)
{
    // Direction vectors
    const double dx1 = a2.x() - a1.x(), dy1 = a2.y() - a1.y();
    const double dx2 = b2.x() - b1.x(), dy2 = b2.y() - b1.y();
    const double denom = dx1 * dy2 - dy1 * dx2;
    if (std::abs(denom) < 1e-10) return false;  // parallel
    const double t = ((b1.x()-a1.x())*dy2 - (b1.y()-a1.y())*dx2) / denom;
    pt = QPointF(a1.x() + t*dx1, a1.y() + t*dy1);
    return true;
}

// ── Construction ──────────────────────────────────────────────────────────────

AlignmentGripProvider::AlignmentGripProvider(railway::HorizontalAlignmentEdit* edit)
    : m_edit(edit)
{
    Q_ASSERT(edit);
}

// ── computeGrips ──────────────────────────────────────────────────────────────

QVector<GripPoint> AlignmentGripProvider::computeGrips() const
{
    QVector<GripPoint> grips;
    if (!m_edit) return grips;

    const QVector<railway::EditableElement>& elems = m_edit->elements();

    // ── 1. Collect unique tangent endpoint positions ─────────────────────────
    //
    // A Fixed Tangent contributes two editable points: startPI and endPI.
    // Adjacent tangents share a common vertex — deduplicate by position.
    //
    // Key insight: for a sequence like  T0 → [SCS] → T1 → [SCS] → T2,
    // T0.endPI and T1.startPI may be very close (solver connects them).
    // We emit one grip per unique position.

    struct TangentEndpoint {
        QPointF pos;
        int     tangentIdx;   // index of the tangent in m_elems
        bool    isEnd;        // false = startPI, true = endPI
    };

    QVector<TangentEndpoint> endpoints;
    constexpr double kMergeDist = 0.01;  // 1 cm — treat as same point

    auto addEndpoint = [&](const QPointF& pos, int idx, bool isEnd) {
        for (auto& ep : endpoints) {
            if (QLineF(ep.pos, pos).length() < kMergeDist) return; // duplicate
        }
        endpoints.append({pos, idx, isEnd});
    };

    for (int i = 0; i < elems.size(); ++i) {
        if (elems[i].type != railway::EditableElementType::Tangent) continue;
        addEndpoint(elems[i].startPI, i, false);
        addEndpoint(elems[i].endPI,   i, true);
    }

    // ── 2. Build GripPoints for tangent endpoints ────────────────────────────

    for (const auto& ep : endpoints) {
        GripPoint gp;
        gp.id       = ep.isEnd
                      ? QString("t_end_%1").arg(ep.tangentIdx)
                      : QString("t_start_%1").arg(ep.tangentIdx);
        gp.position = toGpPnt(ep.pos);
        gp.type     = GripType::Vertex;
        gp.enabled  = true;

        const int  tidx  = ep.tangentIdx;
        const bool isEnd = ep.isEnd;

        gp.onDrag = [this, tidx, isEnd](const gp_Pnt& np, bool) {
            if (isEnd)
                m_edit->movePIDirect(tidx, toQPointF(np));       // moves endPI
            else
                m_edit->moveStartPIDirect(tidx, toQPointF(np)); // moves startPI
            m_edit->solve();
        };

        grips.append(gp);
    }

    // ── 3. IP grips for Floating / SCS curves ────────────────────────────────
    //
    // Each Floating curve (CircularArc, SpiralIn) references two tangents via
    // tangentIdxBefore / tangentIdxAfter.  The IP is the intersection of the
    // infinite lines through those tangents.  Moving the IP moves both
    // tangents' adjacent endpoints simultaneously, keeping the alignment smooth.

    // Track which (before,after) pairs we've already emitted a grip for.
    QSet<QPair<int,int>> emittedIPs;

    for (int i = 0; i < elems.size(); ++i) {
        const auto& el = elems[i];
        if (el.tangentIdxBefore < 0 || el.tangentIdxAfter < 0) continue;
        // Only emit once per (before, after) tangent pair
        QPair<int,int> key(el.tangentIdxBefore, el.tangentIdxAfter);
        if (emittedIPs.contains(key)) continue;
        emittedIPs.insert(key);

        const auto& tb = elems[el.tangentIdxBefore];
        const auto& ta = elems[el.tangentIdxAfter];

        if (tb.type != railway::EditableElementType::Tangent) continue;
        if (ta.type != railway::EditableElementType::Tangent) continue;

        QPointF ip;
        if (!lineIntersect(tb.startPI, tb.endPI, ta.startPI, ta.endPI, ip))
            continue;  // parallel tangents — skip

        GripPoint gp;
        gp.id       = QString("ip_%1_%2").arg(el.tangentIdxBefore).arg(el.tangentIdxAfter);
        gp.position = toGpPnt(ip);
        gp.type     = GripType::Midpoint;   // triangle marker — visually distinct
        gp.enabled  = true;

        const int bIdx = el.tangentIdxBefore;
        const int aIdx = el.tangentIdxAfter;

        gp.onDrag = [this, bIdx, aIdx](const gp_Pnt& np, bool) {
            const QVector<railway::EditableElement>& els = m_edit->elements();
            if (bIdx >= els.size() || aIdx >= els.size()) return;

            const QPointF newIP = toQPointF(np);
            const auto& tb2 = els[bIdx];
            const auto& ta2 = els[aIdx];

            // Move the inner ends of the two adjacent tangents to the new IP.
            // "Inner end" = endPI of tangentBefore, startPI of tangentAfter.
            m_edit->movePIDirect(bIdx, newIP);        // moves tb.endPI
            m_edit->moveStartPIDirect(aIdx, newIP);   // moves ta.startPI
            m_edit->solve();
        };

        grips.append(gp);
    }

    return grips;
}

// ── IGripProvider callbacks ───────────────────────────────────────────────────

void AlignmentGripProvider::onGripDragBegin(const QString& gripId)
{
    Q_UNUSED(gripId)
    if (!m_edit || !m_edit->parentDocument()) return;
    m_beforeSnapshot = m_edit->parentDocument()->toJson();
}

void AlignmentGripProvider::onGripDrag(const QString&, const gp_Pnt&)
{
    // Handled entirely by onDrag lambdas in computeGrips()
}

void AlignmentGripProvider::onGripDragEnd(const QString& gripId,
                                          const gp_Pnt& startPos,
                                          const gp_Pnt& endPos)
{
    if (startPos.Distance(endPos) < Precision::Confusion()) return;

    auto* doc = m_edit ? m_edit->parentDocument() : nullptr;
    if (!doc) return;

    command::AlignmentEditCommand::push(
        doc, m_beforeSnapshot, doc->toJson(), "Grip Move");
    Q_UNUSED(gripId)
}

} // namespace aicad::cad
