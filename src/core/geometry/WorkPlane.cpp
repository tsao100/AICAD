#include "WorkPlane.h"
#include <QtMath>
#include <QDebug>

namespace aicad {
namespace core {
namespace geometry {

bool WorkPlaneManager::validateRightHandRule(const WorkPlaneDefinition& plane) {
    QVector3D cross = QVector3D::crossProduct(plane.uAxis, plane.vAxis);
    float dot = QVector3D::dotProduct(cross, plane.normal);

    bool isValid = qFuzzyCompare(dot + 1.0f, 1.0f + 1.0f); // 接近 1.0

    if (!isValid) {
        qWarning() << "[WorkPlane] Invalid right-hand rule for plane:" << plane.name
                   << "U×V·N =" << dot << "(expected 1.0)";
    }

    return isValid;
}

WorkPlaneDefinition WorkPlaneManager::createFromNormal(
    const QVector3D& normal,
    const QVector3D& origin,
    const QString& name
    ) {
    QVector3D n = normal.normalized();

    // 選擇一個不平行於法向量的參考向量
    QVector3D reference = qAbs(n.z()) < 0.9f
                              ? QVector3D(0, 0, 1)
                              : QVector3D(1, 0, 0);

    // Gram-Schmidt 正交化
    QVector3D uAxis = QVector3D::crossProduct(reference, n).normalized();
    QVector3D vAxis = QVector3D::crossProduct(n, uAxis).normalized();

    WorkPlaneDefinition plane(name, uAxis, vAxis, n, origin);

    Q_ASSERT(validateRightHandRule(plane));

    return plane;
}

WorkPlaneDefinition WorkPlaneManager::createFromThreePoints(
    const QVector3D& origin,
    const QVector3D& pointU,
    const QVector3D& pointV,
    const QString& name
    ) {
    QVector3D u = (pointU - origin).normalized();
    QVector3D v = (pointV - origin).normalized();
    QVector3D n = QVector3D::crossProduct(u, v).normalized();

    // 確保 V 軸垂直於 U 軸
    v = QVector3D::crossProduct(n, u).normalized();

    WorkPlaneDefinition plane(name, u, v, n, origin);

    Q_ASSERT(validateRightHandRule(plane));

    return plane;
}

} // namespace geometry
} // namespace core
} // namespace aicad
