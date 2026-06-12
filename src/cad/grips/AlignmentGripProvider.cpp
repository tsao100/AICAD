// src/cad/grips/AlignmentGripProvider.cpp
#include "AlignmentGripProvider.h"
#include "railway/AlignmentDocument.h"
#include "command/alignment/AlignmentEditCommand.h"
#include "core/Application.h"
#include "ui/UIManager.h"

#include <QDebug>
#include <QPointF>
#include <QUndoStack>
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

        // ── startPI Grip ──────────────────────────────────────────────────────
        {
            GripPoint gp;
            gp.id       = QString("align_s%1").arg(i);
            gp.position = gp_Pnt(el.startPI.x(), el.startPI.y(), 0.0);
            gp.type     = GripType::Vertex;
            gp.enabled  = true;

            gp.onDrag = [this, i](const gp_Pnt& np, bool /*snapped*/) {
                m_edit->moveStartPIDirect(i, toQPointF(np));
                m_edit->solve();   // emit changed() → AlignmentRenderer::refresh()
            };

            grips.append(gp);
        }

        // ── endPI Grip（Fixed Tangent only）──────────────────────────────────
        if (el.type == railway::EditableElementType::Tangent &&
            el.mode == railway::ConstraintMode::Fixed)
        {
            GripPoint gp;
            gp.id       = QString("align_e%1").arg(i);
            gp.position = gp_Pnt(el.endPI.x(), el.endPI.y(), 0.0);
            gp.type     = GripType::Vertex;
            gp.enabled  = true;

            gp.onDrag = [this, i](const gp_Pnt& np, bool /*snapped*/) {
                m_edit->movePIDirect(i, toQPointF(np));
                m_edit->solve();
            };

            grips.append(gp);
        }

        // ── Midpoint Grip（Floating / Free）──────────────────────────────────
        if (el.mode == railway::ConstraintMode::Floating ||
            el.mode == railway::ConstraintMode::Free)
        {
            QPointF mid = (el.startPI + el.endPI) * 0.5;
            GripPoint gp;
            gp.id       = QString("align_m%1").arg(i);
            gp.position = gp_Pnt(mid.x(), mid.y(), 0.0);
            gp.type     = GripType::Midpoint;
            gp.enabled  = true;

            gp.onDrag = [this, i](const gp_Pnt& np, bool /*snapped*/) {
                const QVector<railway::EditableElement>& els = m_edit->elements();
                if (i >= els.size()) return;
                const railway::EditableElement& cur = els[i];
                QPointF delta = toQPointF(np) - (cur.startPI + cur.endPI) * 0.5;
                m_edit->moveStartPIDirect(i, cur.startPI + delta);
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

    // 記錄 drag 開始前的文件快照，供 onGripDragEnd 推入 Undo
    m_beforeSnapshot = m_edit->parentDocument()
                       ? m_edit->parentDocument()->toJson()
                       : QJsonObject();

    qDebug() << "[AlignmentGripProvider] Drag begin:" << gripId;
}

// ── IGripProvider::onGripDrag ─────────────────────────────────────────────────

void AlignmentGripProvider::onGripDrag(const QString& /*gripId*/, const gp_Pnt& /*newPos*/)
{
    // 實際移動邏輯由 GripPoint::onDrag lambda 執行（movePIDirect + solve）
}

// ── IGripProvider::onGripDragEnd ─────────────────────────────────────────────

void AlignmentGripProvider::onGripDragEnd(const QString& gripId,
                                          const gp_Pnt& startPos,
                                          const gp_Pnt& endPos)
{
    if (startPos.Distance(endPos) < Precision::Confusion()) {
        qDebug() << "[AlignmentGripProvider] Cancelled (no movement):" << gripId;
        return;
    }

    // drag 結束：推入一筆 Undo（before = drag 前快照，after = 當前狀態）
    auto* doc = m_edit->parentDocument();
    if (doc) {
        QJsonObject after = doc->toJson();
        command::AlignmentEditCommand::push(doc, m_beforeSnapshot, after, "Grip Move PI");
    }

    qDebug() << "[AlignmentGripProvider] Grip committed:" << gripId;
}

// ── Private helpers ───────────────────────────────────────────────────────────

// static
AlignmentGripProvider::ParsedGrip
AlignmentGripProvider::parseGripId(const QString& id)
{
    ParsedGrip result;
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
