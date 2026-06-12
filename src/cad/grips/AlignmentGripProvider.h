// src/cad/grips/AlignmentGripProvider.h
#pragma once

#include "GripProvider.h"
#include "GripPoint.h"

#include <QVector>
#include <QString>
#include <QPointF>
#include <QJsonObject>

namespace aicad {
namespace railway { class HorizontalAlignmentEdit; }

namespace cad {

/**
 * @brief IGripProvider 實作：為 HorizontalAlignmentEdit 提供 PI 端點與中間點 Grip。
 *
 * 拖曳流程：
 *   onGripDragBegin() → 記錄 before 快照
 *   onDrag lambda     → movePIDirect + solve()（不 push Undo，保持即時刷新）
 *   onGripDragEnd()   → push 一筆 AlignmentEditCommand（before/after）
 */
class AlignmentGripProvider : public IGripProvider
{
public:
    explicit AlignmentGripProvider(railway::HorizontalAlignmentEdit* edit);

    // ── IGripProvider interface ──────────────────────────────────────────────

    QVector<GripPoint> computeGrips() const override;
    void onGripDragBegin(const QString& gripId) override;
    void onGripDrag(const QString& gripId, const gp_Pnt& newPos) override;
    void onGripDragEnd(const QString& gripId,
                       const gp_Pnt& startPos,
                       const gp_Pnt& endPos) override;

private:
    struct ParsedGrip {
        int  elemIdx = -1;
        bool isStart = true;
        bool isMid   = false;
    };
    static ParsedGrip parseGripId(const QString& gripId);
    static QPointF    toQPointF(const gp_Pnt& p);

    railway::HorizontalAlignmentEdit* m_edit = nullptr;

    /// drag 開始前的文件快照，供 onGripDragEnd 推入 Undo
    QJsonObject m_beforeSnapshot;
};

} // namespace cad
} // namespace aicad
