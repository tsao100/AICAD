// src/cad/grips/AlignmentGripProvider.cpp
#include "AlignmentGripProvider.h"
#include "railway/AlignmentDocument.h"
#include "command/alignment/AlignmentEditCommand.h"
#include "core/Application.h"
#include "ui/UIManager.h"

#include <QDebug>
#include <QSet>
#include <cmath>
#include <Precision.hxx>

namespace aicad::cad {

// ── file-local helpers ────────────────────────────────────────────────────────

static QPointF toQPointF(const gp_Pnt& p) { return QPointF(p.X(), p.Y()); }
static gp_Pnt  toGpPnt (const QPointF& p) { return gp_Pnt(p.x(), p.y(), 0.0); }

/** Azimuth of vector a→b, radians (Easting/Northing: atan2(dE, dN)). */
static double azimuthOf(const QPointF& a, const QPointF& b)
{
    return std::atan2(b.x() - a.x(), b.y() - a.y());
}

/**
 * Infinite-line intersection by azimuth — identical to AlignmentSolver::lineIntersect.
 *
 * A + t·(sinAzA, cosAzA)  =  B + s·(sinAzB, cosAzB)
 *
 * Returns true and sets `pt` on success (non-parallel lines).
 */
static bool lineIntersectAz(const QPointF& A, double azA,
                             const QPointF& B, double azB,
                             QPointF& pt)
{
    const double sinA = std::sin(azA), cosA = std::cos(azA);
    const double sinB = std::sin(azB), cosB = std::cos(azB);
    const double det  = sinB * cosA - sinA * cosB;   // = sin(azB - azA)

    if (std::abs(det) < 1e-9) return false;   // parallel

    const double dx = B.x() - A.x();
    const double dy = B.y() - A.y();
    const double t  = (dx * (-cosB) - (-sinB) * dy) / det;

    pt = QPointF(A.x() + t * sinA, A.y() + t * cosA);
    return true;
}

// ── Construction ──────────────────────────────────────────────────────────────

AlignmentGripProvider::AlignmentGripProvider(railway::HorizontalAlignmentEdit* edit)
    : m_edit(edit)
{
    Q_ASSERT(edit);
}

// ── computeGrips ──────────────────────────────────────────────────────────────
//
// For each unique (tangentIdxBefore=T0, tangentIdxAfter=T1) pair referenced
// by any Floating element, emit exactly THREE grips:
//
//   ① T0.startPI  — outer end of the entry tangent           (Vertex)
//   ② T1.endPI    — outer end of the exit  tangent           (Vertex)
//   ③ IP          — intersection of lines T0 and T1          (Midpoint/triangle)
//
// Dragging ①: moves T0.startPI (keeps T0 direction, shortens/lengthens).
// Dragging ②: moves T1.endPI   (keeps T1 direction, shortens/lengthens).
// Dragging ③: moves T0.endPI + T1.startPI together → changes IP → curve updates.
//
// All other points (T0.endPI, T1.startPI, arc/spiral cut-points) are NOT grips.
// One set of three grips per unique (T0,T1) pair — SCS groups share the same
// pair and are deduplicated via QSet.

QVector<GripPoint> AlignmentGripProvider::computeGrips() const
{
    QVector<GripPoint> grips;
    if (!m_edit) return grips;

    const QVector<railway::EditableElement>& elems = m_edit->elements();

    QSet<QPair<int,int>> emitted;
    QSet<QString>        emittedGripIds;   // prevent duplicate outer-end grips

    for (int i = 0; i < elems.size(); ++i) {
        const auto& el = elems[i];

        // Only Floating elements carry the tangent pair that defines an IP.
        if (el.mode != railway::ConstraintMode::Floating) continue;

        const int b = el.tangentIdxBefore;
        const int a = el.tangentIdxAfter;
        if (b < 0 || a < 0 || b >= elems.size() || a >= elems.size()) continue;

        const QPair<int,int> key(b, a);
        if (emitted.contains(key)) continue;
        emitted.insert(key);

        const auto& T0 = elems[b];
        const auto& T1 = elems[a];

        if (T0.type != railway::EditableElementType::Tangent) continue;
        if (T1.type != railway::EditableElementType::Tangent) continue;

        // ── Compute IP ───────────────────────────────────────────────────────

        const double azT0 = azimuthOf(T0.startPI, T0.endPI);
        const double azT1 = azimuthOf(T1.startPI, T1.endPI);

        QPointF ip;
        if (!lineIntersectAz(T0.startPI, azT0, T1.startPI, azT1, ip)) {
            qDebug() << "[AlignmentGripProvider] Tangents" << b << "&" << a
                     << "are parallel — no IP grip";
            continue;
        }

        // ── Grip ①: T0.startPI ──────────────────────────────────────────────

        {
            const QString id = QString("t0start_%1").arg(b);
            if (!emittedGripIds.contains(id)) {
                emittedGripIds.insert(id);
                GripPoint gp;
                gp.id       = id;
                gp.position = toGpPnt(T0.startPI);
                gp.type     = GripType::Vertex;
                gp.enabled  = true;

                gp.onDrag = [this, b](const gp_Pnt& np, bool) {
                    m_edit->moveStartPIDirect(b, toQPointF(np));
                    m_edit->solve();
                };
                grips.append(gp);
            }
        }

        // ── Grip ②: T1.endPI ────────────────────────────────────────────────

        {
            const QString id = QString("t1end_%1").arg(a);
            if (!emittedGripIds.contains(id)) {
                emittedGripIds.insert(id);
                GripPoint gp;
                gp.id       = id;
                gp.position = toGpPnt(T1.endPI);
                gp.type     = GripType::Vertex;
                gp.enabled  = true;

                gp.onDrag = [this, a](const gp_Pnt& np, bool) {
                    m_edit->movePIDirect(a, toQPointF(np));
                    m_edit->solve();
                };
                grips.append(gp);
            }
        }

        // ── Grip ③: IP ──────────────────────────────────────────────────────

        {
            GripPoint gp;
            gp.id       = QString("ip_%1_%2").arg(b).arg(a);
            gp.position = toGpPnt(ip);
            gp.type     = GripType::Midpoint;   // triangle marker
            gp.enabled  = true;

            gp.onDrag = [this, b, a](const gp_Pnt& np, bool) {
                const QPointF newIP = toQPointF(np);
                // Move the inner ends of both tangents to the new IP.
                // T0.endPI   → newIP  (entry tangent inner end)
                // T1.startPI → newIP  (exit  tangent inner end)
                m_edit->movePIDirect(b, newIP);
                m_edit->moveStartPIDirect(a, newIP);
                m_edit->solve();
            };
            grips.append(gp);
        }

        qDebug() << "[AlignmentGripProvider] IP group (T0=" << b << ", T1=" << a << ")"
                 << "  T0.start=" << T0.startPI
                 << "  IP="       << ip
                 << "  T1.end="   << T1.endPI;
    }

    qDebug() << "[AlignmentGripProvider] computeGrips:" << grips.size()
             << "grips for" << emitted.size() << "IP group(s)";
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
    // All movement handled by onDrag lambdas in computeGrips().
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
