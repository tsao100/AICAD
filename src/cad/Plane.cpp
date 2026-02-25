/**
 * @file Plane.cpp
 * @brief 平面定義實作
 * @author AICAD Team
 * @date 2025-01-08
 */

#include "Plane.h"
#include <QVector2D>
#include <cmath>

namespace aicad {
namespace cad {

Plane::Plane()
    : m_origin(0, 0, 0)
    , m_normal(0, 0, 1)
    , m_xAxis(1, 0, 0)
    , m_yAxis(0, 1, 0)
{
}

Plane::Plane(const QVector3D& origin, 
             const QVector3D& normal, 
             const QVector3D& xAxis)
    : m_origin(origin)
    , m_normal(normal)
    , m_xAxis(xAxis)
{
    normalize();
}

Plane Plane::fromGpPln(const gp_Pln& gpPlane) {
    const gp_Ax3& ax3 = gpPlane.Position();
    gp_Ax2 ax2 = ax3.Ax2();   // ✅ 正確
    return fromGpAx2(ax2);
}

Plane Plane::fromGpAx2(const gp_Ax2& ax2) {
    gp_Pnt origin = ax2.Location();
    gp_Dir normal = ax2.Direction();
    gp_Dir xdir = ax2.XDirection();
    
    return Plane(
        QVector3D(origin.X(), origin.Y(), origin.Z()),
        QVector3D(normal.X(), normal.Y(), normal.Z()),
        QVector3D(xdir.X(), xdir.Y(), xdir.Z())
    );
}

Plane Plane::xy() {
    return Plane(
        QVector3D(0, 0, 0),
        QVector3D(0, 0, 1),
        QVector3D(1, 0, 0)
    );
}

Plane Plane::xz() {
    return Plane(
        QVector3D(0, 0, 0),
        QVector3D(0, -1, 0),
        QVector3D(1, 0, 0)
    );
}

Plane Plane::yz() {
    return Plane(
        QVector3D(0, 0, 0),
        QVector3D(1, 0, 0),
        QVector3D(0, 1, 0)
    );
}

void Plane::setOrigin(const QVector3D& origin) {
    m_origin = origin;
}

void Plane::setNormal(const QVector3D& normal) {
    m_normal = normal;
    normalize();
}

void Plane::setXAxis(const QVector3D& xAxis) {
    m_xAxis = xAxis;
    normalize();
}

QString Plane::displayName() const {
    // 檢查是否為標準平面
    if (isXY()) return "XY";
    if (isXZ()) return "XZ";
    if (isYZ()) return "YZ";
    
    // 自訂平面，顯示法向量
    return QString("Custom (%1, %2, %3)")
        .arg(m_normal.x(), 0, 'f', 2)
        .arg(m_normal.y(), 0, 'f', 2)
        .arg(m_normal.z(), 0, 'f', 2);
}

gp_Pln Plane::toGpPln() const {
    gp_Pnt origin(m_origin.x(), m_origin.y(), m_origin.z());
    gp_Dir normal(m_normal.x(), m_normal.y(), m_normal.z());
    return gp_Pln(origin, normal);
}

gp_Ax2 Plane::toGpAx2() const {
    gp_Pnt origin(m_origin.x(), m_origin.y(), m_origin.z());
    gp_Dir normal(m_normal.x(), m_normal.y(), m_normal.z());
    gp_Dir xdir(m_xAxis.x(), m_xAxis.y(), m_xAxis.z());
    return gp_Ax2(origin, normal, xdir);
}

QVector3D Plane::toWorld(double u, double v) const {
    return m_origin + m_xAxis * u + m_yAxis * v;
}

QVector2D Plane::toPlane(const QVector3D& worldPoint) const {
    QVector3D localVec = worldPoint - m_origin;
    double u = QVector3D::dotProduct(localVec, m_xAxis);
    double v = QVector3D::dotProduct(localVec, m_yAxis);
    return QVector2D(u, v);
}

double Plane::distanceTo(const QVector3D& point) const {
    QVector3D vec = point - m_origin;
    return QVector3D::dotProduct(vec, m_normal);
}

bool Plane::isXY() const {
    const double tolerance = 1e-6;
    return m_origin.length() < tolerance &&
           std::abs(m_normal.x()) < tolerance &&
           std::abs(m_normal.y()) < tolerance &&
           std::abs(m_normal.z() - 1.0) < tolerance;
}

bool Plane::isXZ() const {
    const double tolerance = 1e-6;
    return m_origin.length() < tolerance &&
           std::abs(m_normal.x()) < tolerance &&
           std::abs(m_normal.y() + 1.0) < tolerance &&
           std::abs(m_normal.z()) < tolerance;
}

bool Plane::isYZ() const {
    const double tolerance = 1e-6;
    return m_origin.length() < tolerance &&
           std::abs(m_normal.x() - 1.0) < tolerance &&
           std::abs(m_normal.y()) < tolerance &&
           std::abs(m_normal.z()) < tolerance;
}

bool Plane::isParallelTo(const Plane& other, double tolerance) const {
    // 兩個平面平行，如果法向量平行（點積接近 ±1）
    double dot = std::abs(QVector3D::dotProduct(m_normal, other.m_normal));
    return std::abs(dot - 1.0) < tolerance;
}

bool Plane::equals(const Plane& other, double tolerance) const {
    // 檢查原點距離
    if ((m_origin - other.m_origin).length() > tolerance) {
        return false;
    }
    
    // 檢查法向量是否平行且同向
    double normalDot = QVector3D::dotProduct(m_normal, other.m_normal);
    if (std::abs(normalDot - 1.0) > tolerance) {
        return false;
    }
    
    // 檢查 X 軸是否平行且同向
    double xAxisDot = QVector3D::dotProduct(m_xAxis, other.m_xAxis);
    if (std::abs(xAxisDot - 1.0) > tolerance) {
        return false;
    }
    
    return true;
}

void Plane::normalize() {
    // 正規化法向量
    if (m_normal.length() > 1e-10) {
        m_normal.normalize();
    } else {
        // 無效的法向量，使用預設 Z 軸
        m_normal = QVector3D(0, 0, 1);
    }
    
    // 正規化 X 軸並確保垂直於法向量
    if (m_xAxis.length() > 1e-10) {
        m_xAxis.normalize();
        
        // 使用 Gram-Schmidt 正交化
        // xAxis = xAxis - (xAxis · normal) * normal
        double dot = QVector3D::dotProduct(m_xAxis, m_normal);
        m_xAxis = m_xAxis - m_normal * dot;
        
        if (m_xAxis.length() > 1e-10) {
            m_xAxis.normalize();
        } else {
            // xAxis 與 normal 平行，選擇一個垂直向量
            if (std::abs(m_normal.x()) < 0.9) {
                m_xAxis = QVector3D(1, 0, 0);
            } else {
                m_xAxis = QVector3D(0, 1, 0);
            }
            // 再次正交化
            dot = QVector3D::dotProduct(m_xAxis, m_normal);
            m_xAxis = m_xAxis - m_normal * dot;
            m_xAxis.normalize();
        }
    } else {
        // 無效的 X 軸，選擇垂直於法向量的向量
        if (std::abs(m_normal.x()) < 0.9) {
            m_xAxis = QVector3D(1, 0, 0);
        } else {
            m_xAxis = QVector3D(0, 1, 0);
        }
        double dot = QVector3D::dotProduct(m_xAxis, m_normal);
        m_xAxis = m_xAxis - m_normal * dot;
        m_xAxis.normalize();
    }
    
    // 計算 Y 軸（叉積）
    m_yAxis = QVector3D::crossProduct(m_normal, m_xAxis);
    m_yAxis.normalize();
}

} // namespace cad
} // namespace aicad
