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

static double azimuthOf(const QPointF& a, const QPointF& b)
{
    return std::atan2(b.x() - a.x(), b.y() - a.y());
}

// Identical to AlignmentSolver::lineIntersect (azimuth-based).
static bool lineIntersectAz(const QPointF& A, double azA,
                             const QPointF& B, double azB,
                             QPointF& pt)
{
    const double sinA = std::sin(azA), cosA = std::cos(azA);
    const double sinB = std::sin(azB), cosB = std::cos(azB);
    const double det  = sinB * cosA - sinA * cosB;
    if (std::abs(det) < 1e-9) return false;
    const double dx = B.x() - A.x(), dy = B.y() - A.y();
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
// For a chained alignment  T0 → [SCS1] → T1 → [SCS2] → T2 → …
// we want exactly these grips:
//
//   • One Vertex grip at each outer end:
//       T0.startPI  (T0 is not anyone's tangentAfter)
//       Tn.endPI    (Tn is not anyone's tangentBefore)
//
//   • One Midpoint (triangle) grip at each IP:
//       IP_k = intersection of infinite lines through T_before and T_after
//       Moving IP_k → sets T_before.endPI = T_after.startPI = newIP
//
// No other grips are emitted.  This prevents duplicate/phantom grips
// appearing after a drag because each editable point has exactly one grip.

QVector<GripPoint> AlignmentGripProvider::computeGrips() const
{
    QVector<GripPoint> grips;
    if (!m_edit) return grips;

    const QVector<railway::EditableElement>& elems = m_edit->elements();

    // ── Pass 1: collect all unique (tangentBefore, tangentAfter) IP groups ───

    // ip_groups[k] = {bIdx, aIdx}
    struct IPGroup { int b; int a; };
    QVector<IPGroup> groups;
    QSet<QPair<int,int>> seen;

<<<<<<< HEAD
<<<<<<< HEAD
=======
=======
>>>>>>> 20d6d81 (H Alignment grips dbg1.)
<<<<<<< HEAD
    for (const auto& el : elems) {
        if (el.mode != railway::ConstraintMode::Floating) continue;
        const int b = el.tangentIdxBefore;
        const int a = el.tangentIdxAfter;
        if (b < 0 || a < 0 || b >= elems.size() || a >= elems.size()) continue;
        const QPair<int,int> key(b, a);
        if (seen.contains(key)) continue;
        seen.insert(key);
        groups.append({b, a});
=======
<<<<<<< HEAD
>>>>>>> 56324d0 (H Alignment grips dbg1.)
=======
=======
>>>>>>> 2604ab0 (H Alignment grips dbg1.)
>>>>>>> 20d6d81 (H Alignment grips dbg1.)
            gp.onDrag = [this, i](const gp_Pnt& np, bool /*snapped*/) {
                m_edit->moveStartPI(i, toQPointF(np));
                m_edit->solve();  // → changed() → AlignmentRenderer::refresh()
            };

            grips.append(gp);
        }

        // 每個元素的 endPI（對 Tangent：切線遠端；對 Arc：不額外加，
        // 因為 Fixed Arc 只有 startPI = 圓心；Floating 元素 endPI 由 solver 管理）
        // 僅對 Fixed Tangent 加 endPI grip，避免冗餘。
        if (el.type == railway::EditableElementType::Tangent &&
            el.mode == railway::ConstraintMode::Fixed)
        {
            GripPoint gp;
            gp.id       = QString("align_e%1").arg(i);
            gp.position = gp_Pnt(el.endPI.x(), el.endPI.y(), 0.0);
            gp.type     = GripType::Vertex;
            gp.enabled  = true;

            gp.onDrag = [this, i](const gp_Pnt& np, bool /*snapped*/) {
                // movePI(i) 對 Tangent 更新 endPI（見 AlignmentDocument.cpp 的實作）
                m_edit->movePI(i, toQPointF(np));
                m_edit->solve();
            };

            grips.append(gp);
        }

        // ── Float / Free 元素的幾何中心點 Grip（三角形，GripType::Midpoint）──
        if (el.mode == railway::ConstraintMode::Floating ||
            el.mode == railway::ConstraintMode::Free)
        {
            QPointF mid = (el.startPI + el.endPI) * 0.5;
            GripPoint gp;
            gp.id       = QString("align_m%1").arg(i);
            gp.position = gp_Pnt(mid.x(), mid.y(), 0.0);
            gp.type     = GripType::Midpoint;   // 渲染為三角形
            gp.enabled  = true;

            gp.onDrag = [this, i](const gp_Pnt& np, bool /*snapped*/) {
                // Floating/Free 元素：平移中心點 = 移動 startPI（solver 重算 endPI）
                const QVector<railway::EditableElement>& els = m_edit->elements();
                if (i >= els.size()) return;

                const railway::EditableElement& cur = els[i];
                QPointF oldMid = (cur.startPI + cur.endPI) * 0.5;
                QPointF delta  = toQPointF(np) - oldMid;

                m_edit->moveStartPI(i, cur.startPI + delta);
                m_edit->solve();
            };

            grips.append(gp);
        }
<<<<<<< HEAD
<<<<<<< HEAD
=======
>>>>>>> 6bc721d (H Alignment grips dbg1.)
>>>>>>> 56324d0 (H Alignment grips dbg1.)
=======
>>>>>>> 6bc721d (H Alignment grips dbg1.)
=======
>>>>>>> 2604ab0 (H Alignment grips dbg1.)
>>>>>>> 20d6d81 (H Alignment grips dbg1.)
    }

    if (groups.isEmpty()) return grips;

    // ── Pass 2: determine which tangent indices appear as "before" vs "after" ─
    //
    // A tangent that appears as tangentAfter (a) in some group has its
    // startPI as the "inner end" of that group's IP → do NOT emit a Grip①
    // for that tangent's startPI.
    //
    // A tangent that appears as tangentBefore (b) in some group has its
    // endPI as the "inner end" → do NOT emit a Grip② for that tangent's endPI.

    QSet<int> isBefore;   // tangent indices used as tangentBefore in any group
    QSet<int> isAfter;    // tangent indices used as tangentAfter  in any group

    for (const auto& g : groups) {
        isBefore.insert(g.b);
        isAfter .insert(g.a);
    }

    // ── Pass 3: emit grips ────────────────────────────────────────────────────

    QSet<int> emittedOuterStart;  // bIdx already emitted as Grip①
    QSet<int> emittedOuterEnd;    // aIdx already emitted as Grip②

    for (const auto& g : groups) {
        const auto& T0 = elems[g.b];
        const auto& T1 = elems[g.a];

        if (T0.type != railway::EditableElementType::Tangent) continue;
        if (T1.type != railway::EditableElementType::Tangent) continue;

        // ── Grip ①: T0.startPI  (only if T0 is not anyone's tangentAfter) ──

        if (!isAfter.contains(g.b) && !emittedOuterStart.contains(g.b)) {
            emittedOuterStart.insert(g.b);
            GripPoint gp;
            gp.id       = QString("outer_s_%1").arg(g.b);
            gp.position = toGpPnt(T0.startPI);
            gp.type     = GripType::Vertex;
            gp.enabled  = true;
            const int bi = g.b;
            gp.onDrag = [this, bi](const gp_Pnt& np, bool) {
                m_edit->moveStartPIDirect(bi, toQPointF(np));
                m_edit->solve();
            };
            grips.append(gp);
        }

        // ── Grip ②: T1.endPI  (only if T1 is not anyone's tangentBefore) ───

        if (!isBefore.contains(g.a) && !emittedOuterEnd.contains(g.a)) {
            emittedOuterEnd.insert(g.a);
            GripPoint gp;
            gp.id       = QString("outer_e_%1").arg(g.a);
            gp.position = toGpPnt(T1.endPI);
            gp.type     = GripType::Vertex;
            gp.enabled  = true;
            const int ai = g.a;
            gp.onDrag = [this, ai](const gp_Pnt& np, bool) {
                m_edit->movePIDirect(ai, toQPointF(np));
                m_edit->solve();
            };
            grips.append(gp);
        }

        // ── Grip ③: IP = intersection of infinite lines T0 and T1 ───────────

        const double azT0 = azimuthOf(T0.startPI, T0.endPI);
        const double azT1 = azimuthOf(T1.startPI, T1.endPI);

        QPointF ip;
        if (!lineIntersectAz(T0.startPI, azT0, T1.startPI, azT1, ip)) {
            qDebug() << "[AlignmentGripProvider] T0" << g.b << "& T1" << g.a
                     << "parallel — no IP grip";
            continue;
        }

        {
            GripPoint gp;
            gp.id       = QString("ip_%1_%2").arg(g.b).arg(g.a);
            gp.position = toGpPnt(ip);
            gp.type     = GripType::Midpoint;
            gp.enabled  = true;
            const int bi = g.b, ai = g.a;
            gp.onDrag = [this, bi, ai](const gp_Pnt& np, bool) {
                const QPointF newIP = toQPointF(np);
                m_edit->movePIDirect      (bi, newIP);   // T0.endPI   → newIP
                m_edit->moveStartPIDirect (ai, newIP);   // T1.startPI → newIP
                m_edit->solve();
            };
            grips.append(gp);
        }

        qDebug() << "[AlignmentGripProvider] group(T0=" << g.b << ",T1=" << g.a << ")"
                 << " T0.start=" << T0.startPI
                 << " IP=" << ip
                 << " T1.end=" << T1.endPI;
    }

    qDebug() << "[AlignmentGripProvider] computeGrips:" << grips.size()
             << "grips," << groups.size() << "IP group(s)";
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
