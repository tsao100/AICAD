/**
 * @file CustomPlane.cpp
 * @brief CustomPlane 實作
 * @author Ben
 * @date 2024-12-04
 */

#include "CustomPlane.h"
#include <QDebug>
#include <QtMath>

namespace aicad {
namespace cad {

CustomPlane::CustomPlane()
    : origin(0, 0, 0)
    , normal(0, 0, 1)
    , uAxis(1, 0, 0)
    , vAxis(0, 1, 0)
{
}

CustomPlane::CustomPlane(const QVector3D& origin,
                         const QVector3D& normal,
                         const QVector3D& uAxis)
    : origin(origin)
    , normal(normal.normalized())
    , uAxis(uAxis.normalized())
{
    // 計算 vAxis 為 normal 和 uAxis 的叉積
    vAxis = QVector3D::crossProduct(this->normal, this->uAxis).normalized();
    
    // 重新計算 uAxis 確保正交
    this->uAxis = QVector3D::crossProduct(vAxis, this->normal).normalized();
}

CustomPlane CustomPlane::XY() {
    CustomPlane p;
    p.origin = QVector3D(0, 0, 0);
    p.normal = QVector3D(0, 0, 1);
    p.uAxis = QVector3D(1, 0, 0);
    p.vAxis = QVector3D(0, 1, 0);
    return p;
}

CustomPlane CustomPlane::XZ() {
    CustomPlane p;
    p.origin = QVector3D(0, 0, 0);
    p.normal = QVector3D(0, 1, 0);
    p.uAxis = QVector3D(1, 0, 0);
    p.vAxis = QVector3D(0, 0, 1);
    return p;
}

CustomPlane CustomPlane::YZ() {
    CustomPlane p;
    p.origin = QVector3D(0, 0, 0);
    p.normal = QVector3D(1, 0, 0);
    p.uAxis = QVector3D(0, 1, 0);
    p.vAxis = QVector3D(0, 0, 1);
    return p;
}

CustomPlane CustomPlane::fromThreePoints(const QVector3D& p1,
                                         const QVector3D& p2,
                                         const QVector3D& p3)
{
    // 計算兩個向量
    QVector3D v1 = p2 - p1;
    QVector3D v2 = p3 - p1;
    
    // 法向量是兩個向量的叉積
    QVector3D normal = QVector3D::crossProduct(v1, v2).normalized();
    
    // U 軸沿著 v1 方向
    QVector3D uAxis = v1.normalized();
    
    return CustomPlane(p1, normal, uAxis);
}

CustomPlane CustomPlane::fromOriginNormal(const QVector3D& origin,
                                          const QVector3D& normal)
{
    QVector3D n = normal.normalized();
    
    // 選擇一個不與法向量平行的向量作為參考
    QVector3D reference;
    if (qAbs(n.x()) < 0.9) {
        reference = QVector3D(1, 0, 0);
    } else {
        reference = QVector3D(0, 1, 0);
    }
    
    // U 軸垂直於法向量
    QVector3D uAxis = QVector3D::crossProduct(n, reference).normalized();
    
    return CustomPlane(origin, n, uAxis);
}

void CustomPlane::setOrigin(const QVector3D& origin) {
    this->origin = origin;
}

void CustomPlane::setNormal(const QVector3D& normal) {
    this->normal = normal.normalized();
    
    // 重新計算正交軸
    QVector3D reference;
    if (qAbs(this->normal.x()) < 0.9) {
        reference = QVector3D(1, 0, 0);
    } else {
        reference = QVector3D(0, 1, 0);
    }
    
    uAxis = QVector3D::crossProduct(this->normal, reference).normalized();
    vAxis = QVector3D::crossProduct(this->normal, uAxis).normalized();
}

QString CustomPlane::getDisplayName() const {
    // 檢查是否為標準平面
    const double tolerance = 1e-6;
    
    if (origin.length() < tolerance) {
        // 原點在 (0,0,0)
        if ((normal - QVector3D(0, 0, 1)).length() < tolerance) {
            return "XY";
        } else if ((normal - QVector3D(0, 1, 0)).length() < tolerance) {
            return "XZ";
        } else if ((normal - QVector3D(1, 0, 0)).length() < tolerance) {
            return "YZ";
        } else if ((normal - QVector3D(0, 0, -1)).length() < tolerance) {
            return "XY (Bottom)";
        } else if ((normal - QVector3D(0, -1, 0)).length() < tolerance) {
            return "XZ (Back)";
        } else if ((normal - QVector3D(-1, 0, 0)).length() < tolerance) {
            return "YZ (Left)";
        }
    }
    
    return QString("Custom (%1, %2, %3)")
        .arg(normal.x(), 0, 'f', 2)
        .arg(normal.y(), 0, 'f', 2)
        .arg(normal.z(), 0, 'f', 2);
}

gp_Pln CustomPlane::toGpPln() const {
    gp_Pnt originPnt(origin.x(), origin.y(), origin.z());
    gp_Dir normalDir(normal.x(), normal.y(), normal.z());
    return gp_Pln(originPnt, normalDir);
}

gp_Ax2 CustomPlane::toGpAx2() const {
    gp_Pnt originPnt(origin.x(), origin.y(), origin.z());
    gp_Dir normalDir(normal.x(), normal.y(), normal.z());
    gp_Dir uAxisDir(uAxis.x(), uAxis.y(), uAxis.z());
    return gp_Ax2(originPnt, normalDir, uAxisDir);
}

bool CustomPlane::isValid() const {
    const double tolerance = 1e-6;
    
    // 檢查向量是否為單位向量
    if (qAbs(normal.length() - 1.0) > tolerance) {
        return false;
    }
    if (qAbs(uAxis.length() - 1.0) > tolerance) {
        return false;
    }
    if (qAbs(vAxis.length() - 1.0) > tolerance) {
        return false;
    }
    
    // 檢查軸是否正交
    if (qAbs(QVector3D::dotProduct(normal, uAxis)) > tolerance) {
        return false;
    }
    if (qAbs(QVector3D::dotProduct(normal, vAxis)) > tolerance) {
        return false;
    }
    if (qAbs(QVector3D::dotProduct(uAxis, vAxis)) > tolerance) {
        return false;
    }
    
    return true;
}

void CustomPlane::normalize() {
    normal.normalize();
    uAxis.normalize();
    vAxis.normalize();
}

double CustomPlane::distanceToPoint(const QVector3D& point) const {
    QVector3D vec = point - origin;
    return QVector3D::dotProduct(vec, normal);
}

QVector3D CustomPlane::projectPoint(const QVector3D& point) const {
    double distance = distanceToPoint(point);
    return point - normal * distance;
}

bool CustomPlane::isParallelTo(const CustomPlane& other, double tolerance) const {
    // 兩個平面平行若法向量平行
    double dot = qAbs(QVector3D::dotProduct(normal, other.normal));
    return qAbs(dot - 1.0) < tolerance;
}

bool CustomPlane::equals(const CustomPlane& other, double tolerance) const {
    // 檢查原點距離
    if ((origin - other.origin).length() > tolerance) {
        return false;
    }
    
    // 檢查法向量
    if ((normal - other.normal).length() > tolerance) {
        return false;
    }
    
    // 檢查 U 軸
    if ((uAxis - other.uAxis).length() > tolerance) {
        return false;
    }
    
    return true;
}

bool CustomPlane::operator==(const CustomPlane& other) const {
    return equals(other);
}

bool CustomPlane::operator!=(const CustomPlane& other) const {
    return !equals(other);
}

} // namespace cad
} // namespace aicad