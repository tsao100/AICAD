#ifndef AICAD_CORE_GEOMETRY_COORDINATETRANSFORM_H
#define AICAD_CORE_GEOMETRY_COORDINATETRANSFORM_H

/**
 * @file CoordinateTransform.h
 * @brief 2D Sketch Plane ↔ 3D OCCT World 座標轉換工具。
 *
 * ⚠️  與 ProjectOrigin（TM2 雙座標系統）**完全無關**，請勿混用：
 *
 * | 類別                | 職責                                  | 使用場景           |
 * |---------------------|---------------------------------------|--------------------|
 * | CoordinateTransform | Sketch Plane 2D ↔ OCCT 3D World       | Sketch 幾何繪製    |
 * | ProjectOrigin       | TM2 Global（大座標） ↔ CAD Local      | Alignment / Railway|
 *
 * CoordinateTransform 運算的座標全程為 OCCT Local 座標（已經過 ProjectOrigin
 * 轉換後的小數值），不涉及任何地理座標系（EPSG / TM2 / TWD97）。
 *
 * @see src/core/geometry/ProjectOrigin.h
 * @see src/core/geometry/TM2_README.md
 */

#include "WorkPlane.h"
#include <QVector2D>
#include <QVector3D>
#include <QMatrix4x4>

namespace aicad {
namespace core {
namespace geometry {

class CoordinateTransform {
public:
    // 2D 平面座標 → 3D 世界座標
    static QVector3D planeToWorld(
        const QVector2D& planeCoord,
        const WorkPlaneDefinition& plane
        );

    // 3D 世界座標 → 2D 平面座標
    static QVector2D worldToPlane(
        const QVector3D& worldCoord,
        const WorkPlaneDefinition& plane
        );

    // 取得平面的變換矩陣（用於 OpenGL 渲染）
    static QMatrix4x4 getPlaneTransformMatrix(
        const WorkPlaneDefinition& plane
        );

    // 取得平面的逆變換矩陣
    static QMatrix4x4 getInversePlaneTransformMatrix(
        const WorkPlaneDefinition& plane
        );
};

} // namespace geometry
} // namespace core
} // namespace aicad

#endif // AICAD_CORE_GEOMETRY_COORDINATETRANSFORM_H
