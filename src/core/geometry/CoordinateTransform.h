#ifndef AICAD_CORE_GEOMETRY_COORDINATETRANSFORM_H
#define AICAD_CORE_GEOMETRY_COORDINATETRANSFORM_H

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
