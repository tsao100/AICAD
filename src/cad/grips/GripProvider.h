// src/cad/grips/GripProvider.h
#pragma once
#include "GripPoint.h"
#include <QVector>

namespace aicad::cad {

/// 每個可被 grip 的 Feature 都要實作此介面
class IGripProvider {
public:
    virtual ~IGripProvider() = default;

    /// 回傳此物件當前所有 grip 點
    virtual QVector<GripPoint> computeGrips() const = 0;

    /// 當 grip 拖拉開始（供 Undo snapshot）
    virtual void onGripDragBegin(const QString& gripId) {}

    /// Grip 即時更新（拖拉中）
    virtual void onGripDrag(const QString& gripId, const gp_Pnt& newPos) = 0;

    /// Grip 拖拉結束（提交 Undo）
    virtual void onGripDragEnd(const QString& gripId,
                               const gp_Pnt& startPos,
                               const gp_Pnt& endPos) = 0;
};

} // namespace aicad::cad
