// src/cad/grips/SketchGripProvider.h
#pragma once
#include "GripProvider.h"
#include "../Sketch.h"

namespace aicad::cad {

class ConstraintOverlayManager;

class SketchGripProvider : public IGripProvider {
public:
    // ✅ -1 = all geometries (old behaviour), >= 0 = specific geometry
    explicit SketchGripProvider(Sketch* sketch,
                                const QSet<int>& geomIndices = {});

    QVector<GripPoint> computeGrips() const override;
    void onGripDragBegin(const QString& gripId) override;
    void onGripDrag(const QString& gripId, const gp_Pnt& newPos) override;
    void onGripDragEnd(const QString& gripId,
                       const gp_Pnt& startPos,
                       const gp_Pnt& endPos) override;
    void restoreSnapshot();

    /// ✅ Task F: 為尺寸約束提供可拖曳 Grip（菱形），供 UIManager 呼叫
    QVector<GripPoint> gripsForConstraint(const QString& constraintUuid,
                                          ConstraintOverlayManager* overlay) const;

    /// 目前持有的幾何 indices（供累加選取時合併用）
    const QSet<int>& geomIndices() const { return m_geomIndices; }

private:
    Sketch*                     m_sketch;
    QSet<int>  m_geomIndices;  // ✅ empty = all geometries

    struct GeomSnapshot {
        int              geomIndex;
        QVector<QVector2D> points;
        Handle(Geom_TrimmedCurve)  arcCurve;   // ← 新增，僅 Arc 使用
    };
    QVector<GeomSnapshot> m_snapshots; ///< Undo snapshot
};

} // namespace aicad::cad
