#ifndef AICAD_CORE_GEOMETRY_WORKPLANE_H
#define AICAD_CORE_GEOMETRY_WORKPLANE_H

#include <QVector3D>
#include <QVector2D>
#include <QString>

namespace aicad {
namespace core {
namespace geometry {

struct WorkPlaneDefinition {
    QString name;
    QVector3D uAxis;      // 橫向軸（螢幕水平向右）
    QVector3D vAxis;      // 縱向軸（螢幕垂直向上）
    QVector3D normal;     // 法向量（指向觀察者）
    QVector3D origin;     // 平面原點

    WorkPlaneDefinition()
        : name("XY")
        , uAxis(1, 0, 0)
        , vAxis(0, 1, 0)
        , normal(0, 0, 1)
        , origin(0, 0, 0)
    {}

    WorkPlaneDefinition(const QString& n,
                        const QVector3D& u,
                        const QVector3D& v,
                        const QVector3D& norm,
                        const QVector3D& orig = QVector3D(0, 0, 0))
        : name(n), uAxis(u), vAxis(v), normal(norm), origin(orig)
    {}
};

// 標準平面定義
namespace StandardPlanes {
// XY 平面（俯視圖）
inline WorkPlaneDefinition XY() {
    return WorkPlaneDefinition(
        "XY",
        QVector3D(1, 0, 0),   // U = +X
        QVector3D(0, 1, 0),   // V = +Y
        QVector3D(0, 0, 1)    // N = +Z
        );
}

// YZ 平面（右視圖）
inline WorkPlaneDefinition YZ() {
    return WorkPlaneDefinition(
        "YZ",
        QVector3D(0, 1, 0),   // U = +Y
        QVector3D(0, 0, 1),   // V = +Z
        QVector3D(1, 0, 0)    // N = +X
        );
}

// ZX 平面（前視圖）
inline WorkPlaneDefinition ZX() {
    return WorkPlaneDefinition(
        "ZX",
        QVector3D(0, 0, 1),   // U = +Z
        QVector3D(1, 0, 0),   // V = +X
        QVector3D(0, 1, 0)    // N = +Y
        );
}
}

class WorkPlaneManager {
public:
    // 驗證右手定則
    static bool validateRightHandRule(const WorkPlaneDefinition& plane);

    // 從法向量建立工作平面
    static WorkPlaneDefinition createFromNormal(
        const QVector3D& normal,
        const QVector3D& origin = QVector3D(0, 0, 0),
        const QString& name = "Custom"
        );

    // 從三點建立工作平面
    static WorkPlaneDefinition createFromThreePoints(
        const QVector3D& origin,
        const QVector3D& pointU,
        const QVector3D& pointV,
        const QString& name = "Custom"
        );
};

} // namespace geometry
} // namespace core
} // namespace aicad

#endif // AICAD_CORE_GEOMETRY_WORKPLANE_H
