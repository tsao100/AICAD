#include "CoordinateTransform.h"

namespace aicad {
namespace core {
namespace geometry {

QVector3D CoordinateTransform::planeToWorld(
    const QVector2D& planeCoord,
    const WorkPlaneDefinition& plane
    ) {
    return plane.origin
           + planeCoord.x() * plane.uAxis
           + planeCoord.y() * plane.vAxis;
}

QVector2D CoordinateTransform::worldToPlane(
    const QVector3D& worldCoord,
    const WorkPlaneDefinition& plane
    ) {
    QVector3D local = worldCoord - plane.origin;
    return QVector2D(
        QVector3D::dotProduct(local, plane.uAxis),
        QVector3D::dotProduct(local, plane.vAxis)
        );
}

QMatrix4x4 CoordinateTransform::getPlaneTransformMatrix(
    const WorkPlaneDefinition& plane
    ) {
    QMatrix4x4 mat;

    // 設置旋轉部分（U, V, N 作為新的基底）
    mat(0, 0) = plane.uAxis.x();
    mat(1, 0) = plane.uAxis.y();
    mat(2, 0) = plane.uAxis.z();

    mat(0, 1) = plane.vAxis.x();
    mat(1, 1) = plane.vAxis.y();
    mat(2, 1) = plane.vAxis.z();

    mat(0, 2) = plane.normal.x();
    mat(1, 2) = plane.normal.y();
    mat(2, 2) = plane.normal.z();

    // 設置平移部分
    mat(0, 3) = plane.origin.x();
    mat(1, 3) = plane.origin.y();
    mat(2, 3) = plane.origin.z();

    return mat;
}

QMatrix4x4 CoordinateTransform::getInversePlaneTransformMatrix(
    const WorkPlaneDefinition& plane
    ) {
    QMatrix4x4 mat = getPlaneTransformMatrix(plane);
    return mat.inverted();
}

} // namespace geometry
} // namespace core
} // namespace aicad
