// src/cad/grips/AlignmentGripProvider.cpp
#include "AlignmentGripProvider.h"
#include "railway/AlignmentDocument.h"

#include <QDebug>
#include <QPointF>
#include <Precision.hxx>

namespace aicad::cad {

// ── Construction ─────────────────────────────────────────────────────────────

AlignmentGripProvider::AlignmentGripProvider(railway::HorizontalAlignmentEdit* edit)
    : m_edit(edit)
{
    Q_ASSERT(edit);
}

// ── IGripProvider::computeGrips ───────────────────────────────────────────────

QVector<GripPoint> AlignmentGripProvider::computeGrips() const
{
    QVector<GripPoint> grips;
    if (!m_edit) return grips;

    const QVector<railway::EditableElement>& elems = m_edit->elements();

    for (int i = 0; i < elems.size(); ++i) {
        const railway::EditableElement& el = elems[i];

        // ── PI 端點 Grip（橙色方塊，GripType::Vertex ← 最接近「Position」語義）──
        // 每個元素的 startPI
        {
            GripPoint gp;
            gp.id       = QString("align_s%1").arg(i);
            gp.position = gp_Pnt(el.startPI.x(), el.startPI.y(), 0.0);
            gp.type     = GripType::Vertex;   // 渲染為橙色方塊（Position grip）
            gp.enabled  = true;

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
    }

    return grips;
}

// ── IGripProvider::onGripDragBegin ───────────────────────────────────────────

void AlignmentGripProvider::onGripDragBegin(const QString& gripId)
{
    Q_UNUSED(gripId)
    if (!m_edit) return;

    // 快照所有元素的 PI 座標（供 Undo 使用）
    m_snapshots.clear();
    const QVector<railway::EditableElement>& elems = m_edit->elements();
    m_snapshots.reserve(elems.size());
    for (int i = 0; i < elems.size(); ++i) {
        Snapshot s;
        s.elemIdx = i;
        s.startPI = elems[i].startPI;
        s.endPI   = elems[i].endPI;
        m_snapshots.append(s);
    }

    qDebug() << "[AlignmentGripProvider] Drag begin:" << gripId;
}

// ── IGripProvider::onGripDrag ─────────────────────────────────────────────────

void AlignmentGripProvider::onGripDrag(const QString& /*gripId*/, const gp_Pnt& /*newPos*/)
{
    // 實際移動邏輯已在 GripPoint::onDrag lambda 中執行
    // （GripManager 呼叫 gp.onDrag → movePI + solve）
    // 此函式保留作為 override 佔位，無需額外動作。
}

// ── IGripProvider::onGripDragEnd ─────────────────────────────────────────────

void AlignmentGripProvider::onGripDragEnd(const QString& gripId,
                                          const gp_Pnt& startPos,
                                          const gp_Pnt& endPos)
{
    // 移動距離極小時視為取消（防手抖誤觸）
    if (startPos.Distance(endPos) < Precision::Confusion()) {
        qDebug() << "[AlignmentGripProvider] Cancelled (no movement):" << gripId;
        return;
    }

    qDebug() << "[AlignmentGripProvider] Grip committed:" << gripId
             << "to" << endPos.X() << endPos.Y() << endPos.Z();

    // solve() 已在 onGripDrag → gp.onDrag 中呼叫，無需重複。
    // 若需要 Undo Command，此處可推送 AlignmentEditCommand：
    //   QJsonObject before = buildSnapshotJson();
    //   QJsonObject after  = m_edit->toJson();
    //   CommandHistory::push(new AlignmentEditCommand(before, after, m_doc));
    // （Step 17 實作後補充）
}

// ── Private helpers ───────────────────────────────────────────────────────────

// static
AlignmentGripProvider::ParsedGrip
AlignmentGripProvider::parseGripId(const QString& id)
{
    ParsedGrip result;
    // 格式：
    //   "align_s<idx>"  → start PI
    //   "align_e<idx>"  → end PI
    //   "align_m<idx>"  → midpoint
    if (id.startsWith("align_s")) {
        result.elemIdx = id.mid(7).toInt();
        result.isStart = true;
        result.isMid   = false;
    } else if (id.startsWith("align_e")) {
        result.elemIdx = id.mid(7).toInt();
        result.isStart = false;
        result.isMid   = false;
    } else if (id.startsWith("align_m")) {
        result.elemIdx = id.mid(7).toInt();
        result.isStart = false;
        result.isMid   = true;
    }
    return result;
}

// static
QPointF AlignmentGripProvider::toQPointF(const gp_Pnt& p)
{
    return QPointF(p.X(), p.Y());
}

} // namespace aicad::cad
