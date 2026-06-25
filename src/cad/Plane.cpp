/**
 * @file Plane.cpp
 * @brief 增強版平面類別實作
 * @author AICAD Team
 * @date 2025-01-08
 */

#include "Plane.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include <QVector2D>
#include <QJsonArray>
#include <QtMath>
#include <QDebug>

namespace aicad {
namespace cad {

// ============================================================================
// Plane 類別實作
// ============================================================================

Plane::Plane(const QVector3D& origin,
             const QVector3D& normal,
             const QVector3D& xAxis,
             Type type,
             QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
    , m_name()
    , m_type(type)
    , m_createdTime(QDateTime::currentDateTime())
    , m_modifiedTime(QDateTime::currentDateTime())
    , m_isActive(false)
    , m_isLocked(false)
    , m_origin(origin)
    , m_normal(normal)
    , m_xAxis(xAxis)
{
    normalize();
    qDebug() << "[Plane]" << id() << "created";
}

Plane::~Plane() {
    qDebug() << "[Plane]" << id() << name() << "destroyed";
}

void Plane::setName(const QString& name) {
    if (m_isLocked) {
        qWarning() << "[Plane]" << id() << "is locked, cannot rename";
        return;
    }

    if (m_name != name) {
        QString oldName = m_name;
        m_name = name;
        updateModifiedTime();

        qDebug() << "[Plane]" << id() << "renamed from" << oldName << "to" << name;
        Q_EMIT nameChanged(m_name);

        // 發送 EventBus 事件
        if (core::Application::instance() && core::Application::instance()->eventBus()) {
            QVariantMap data;
            data["planeId"] = id();
            data["oldName"] = oldName;
            data["newName"] = m_name;
            core::Application::instance()->eventBus()->publish("plane.renamed", data);
        }
    }
}

QString Plane::displayName() const {
    if (!m_name.isEmpty()) {
        return m_name;
    }

    // 根據類型返回預設名稱
    switch (m_type) {
    case Type::XY:
        return "XY";
    case Type::YZ:
        return "YZ";
    case Type::ZX:
        return "ZX";
    case Type::XZ:
        return "XZ";
    case Type::Offset:
        return QString("Offset (%1, %2, %3)")
            .arg(m_normal.x(), 0, 'f', 2)
            .arg(m_normal.y(), 0, 'f', 2)
            .arg(m_normal.z(), 0, 'f', 2);
    case Type::ThreePoint:
        return "ThreePoint";
    case Type::Custom:
    default:
        return QString("Custom (%1, %2, %3)")
            .arg(m_normal.x(), 0, 'f', 2)
            .arg(m_normal.y(), 0, 'f', 2)
            .arg(m_normal.z(), 0, 'f', 2);
    }
}

void Plane::setLocked(bool locked) {
    if (m_isLocked != locked) {
        m_isLocked = locked;
        qDebug() << "[Plane]" << id() << name() << "locked state:" << locked;
        Q_EMIT lockedStateChanged(m_isLocked);
    }
}

void Plane::setOrigin(const QVector3D& origin) {
    if (m_isLocked) {
        qWarning() << "[Plane]" << id() << "is locked, cannot modify";
        return;
    }

    m_origin = origin;
    updateModifiedTime();
    Q_EMIT geometryChanged();
}

void Plane::setNormal(const QVector3D& normal) {
    if (m_isLocked) {
        qWarning() << "[Plane]" << id() << "is locked, cannot modify";
        return;
    }

    m_normal = normal;
    normalize();
    updateModifiedTime();
    Q_EMIT geometryChanged();
}

void Plane::setXAxis(const QVector3D& xAxis) {
    if (m_isLocked) {
        qWarning() << "[Plane]" << id() << "is locked, cannot modify";
        return;
    }

    m_xAxis = xAxis;
    normalize();
    updateModifiedTime();
    Q_EMIT geometryChanged();
}

void Plane::setCoordinateSystem(const QVector3D& origin,
                                const QVector3D& normal,
                                const QVector3D& xAxis) {
    if (m_isLocked) {
        qWarning() << "[Plane]" << id() << "is locked, cannot modify";
        return;
    }

    m_origin = origin;
    m_normal = normal;
    m_xAxis = xAxis;
    normalize();
    updateModifiedTime();
    Q_EMIT geometryChanged();
}

bool Plane::validateRightHandRule() const {
    QVector3D cross = QVector3D::crossProduct(m_xAxis, m_yAxis);
    float dot = QVector3D::dotProduct(cross, m_normal);

    const float tolerance = 1e-5f;
    bool isValid = std::abs(dot - 1.0f) < tolerance;

    if (!isValid) {
        qWarning() << "[Plane]" << id() << displayName()
                   << "Invalid right-hand rule: X×Y·N =" << dot;
    }

    return isValid;
}

bool Plane::isXY() const {
    const double tolerance = 1e-6;
    return m_origin.length() < tolerance &&
           std::abs(m_normal.x()) < tolerance &&
           std::abs(m_normal.y()) < tolerance &&
           std::abs(m_normal.z() - 1.0) < tolerance;
}

bool Plane::isYZ() const {
    const double tolerance = 1e-6;
    return m_origin.length() < tolerance &&
           std::abs(m_normal.x() - 1.0) < tolerance &&
           std::abs(m_normal.y()) < tolerance &&
           std::abs(m_normal.z()) < tolerance;
}

bool Plane::isZX() const {
    const double tolerance = 1e-6;
    return m_origin.length() < tolerance &&
           std::abs(m_normal.x()) < tolerance &&
           std::abs(m_normal.y() - 1.0) < tolerance &&
           std::abs(m_normal.z()) < tolerance &&
           std::abs(m_xAxis.x()) < tolerance &&
           std::abs(m_xAxis.y()) < tolerance &&
           std::abs(m_xAxis.z() - 1.0) < tolerance;
}

bool Plane::isXZ() const {
    const double tolerance = 1e-6;
    return m_origin.length() < tolerance &&
           std::abs(m_normal.x()) < tolerance &&
           std::abs(m_normal.y() - 1.0) < tolerance &&
           std::abs(m_normal.z()) < tolerance;
}

bool Plane::isParallelTo(const Plane* other, double tolerance) const {
    if (!other) return false;

    double dot = std::abs(QVector3D::dotProduct(m_normal, other->m_normal));
    return std::abs(dot - 1.0) < tolerance;
}

bool Plane::equals(const Plane* other, double tolerance) const {
    if (!other) return false;

    if ((m_origin - other->m_origin).length() > tolerance) {
        return false;
    }

    double normalDot = QVector3D::dotProduct(m_normal, other->m_normal);
    if (std::abs(normalDot - 1.0) > tolerance) {
        return false;
    }

    double xAxisDot = QVector3D::dotProduct(m_xAxis, other->m_xAxis);
    if (std::abs(xAxisDot - 1.0) > tolerance) {
        return false;
    }

    return true;
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

Plane* Plane::fromGpPln(const gp_Pln& gpPlane, QObject* parent) {
    const gp_Ax3& ax3 = gpPlane.Position();
    gp_Ax2 ax2 = ax3.Ax2();
    return fromGpAx2(ax2, parent);
}

Plane* Plane::fromGpAx2(const gp_Ax2& ax2, QObject* parent) {
    gp_Pnt origin = ax2.Location();
    gp_Dir normal = ax2.Direction();
    gp_Dir xdir = ax2.XDirection();

    return new Plane(
        QVector3D(origin.X(), origin.Y(), origin.Z()),
        QVector3D(normal.X(), normal.Y(), normal.Z()),
        QVector3D(xdir.X(), xdir.Y(), xdir.Z()),
        Type::Custom,
        parent
        );
}

QVector3D Plane::toWorld(double u, double v) const {
    return m_origin + m_xAxis * u + m_yAxis * v;
}

QVector3D Plane::toWorld(const QVector2D& planeCoord) const {
    return toWorld(planeCoord.x(), planeCoord.y());
}

QVector2D Plane::toPlane(const QVector3D& worldPoint) const {
    QVector3D localVec = worldPoint - m_origin;
    double u = QVector3D::dotProduct(localVec, m_xAxis);
    double v = QVector3D::dotProduct(localVec, m_yAxis);
    return QVector2D(u, v);
}

QPointF Plane::toPlaneD(const QVector3D& worldPoint) const {
    // 以 double 計算各分量差值，避免 QVector3D float 中間值截斷
    // 對 XY Plane（origin=(0,0,0), xAxis=(1,0,0), yAxis=(0,1,0)）：
    //   u = worldPoint.x()，v = worldPoint.y()，完全無精度損失
    double lx = static_cast<double>(worldPoint.x()) - static_cast<double>(m_origin.x());
    double ly = static_cast<double>(worldPoint.y()) - static_cast<double>(m_origin.y());
    double lz = static_cast<double>(worldPoint.z()) - static_cast<double>(m_origin.z());
    double u = lx * static_cast<double>(m_xAxis.x())
             + ly * static_cast<double>(m_xAxis.y())
             + lz * static_cast<double>(m_xAxis.z());
    double v = lx * static_cast<double>(m_yAxis.x())
             + ly * static_cast<double>(m_yAxis.y())
             + lz * static_cast<double>(m_yAxis.z());
    return QPointF(u, v);
}

double Plane::distanceTo(const QVector3D& point) const {
    QVector3D vec = point - m_origin;
    return QVector3D::dotProduct(vec, m_normal);
}

QJsonObject Plane::toJson() const {
    QJsonObject json;

    // 基本資訊
    json["id"] = id();
    json["name"] = m_name;
    json["type"] = static_cast<int>(m_type);
    json["createdTime"] = m_createdTime.toString(Qt::ISODate);
    json["modifiedTime"] = m_modifiedTime.toString(Qt::ISODate);
    json["isLocked"] = m_isLocked;

    // 幾何資訊
    QJsonObject geometry;

    QJsonArray originArr;
    originArr.append(m_origin.x());
    originArr.append(m_origin.y());
    originArr.append(m_origin.z());
    geometry["origin"] = originArr;

    QJsonArray normalArr;
    normalArr.append(m_normal.x());
    normalArr.append(m_normal.y());
    normalArr.append(m_normal.z());
    geometry["normal"] = normalArr;

    QJsonArray xAxisArr;
    xAxisArr.append(m_xAxis.x());
    xAxisArr.append(m_xAxis.y());
    xAxisArr.append(m_xAxis.z());
    geometry["xAxis"] = xAxisArr;

    json["geometry"] = geometry;

    return json;
}

bool Plane::fromJson(const QJsonObject& json) {
    if (!json.contains("id") || !json.contains("geometry")) {
        qWarning() << "[Plane] Invalid JSON: missing required fields";
        return false;
    }

    // 基本資訊
    m_id = QUuid::fromString(json["id"].toString());
    m_name = json["name"].toString();
    m_type = static_cast<Type>(json["type"].toInt(static_cast<int>(Type::Custom)));
    m_createdTime = QDateTime::fromString(json["createdTime"].toString(), Qt::ISODate);
    m_modifiedTime = QDateTime::fromString(json["modifiedTime"].toString(), Qt::ISODate);
    m_isLocked = json["isLocked"].toBool(false);

    // 幾何資訊
    QJsonObject geometry = json["geometry"].toObject();

    QJsonArray originArr = geometry["origin"].toArray();
    m_origin = QVector3D(
        originArr[0].toDouble(),
        originArr[1].toDouble(),
        originArr[2].toDouble()
        );

    QJsonArray normalArr = geometry["normal"].toArray();
    m_normal = QVector3D(
        normalArr[0].toDouble(),
        normalArr[1].toDouble(),
        normalArr[2].toDouble()
        );

    QJsonArray xAxisArr = geometry["xAxis"].toArray();
    m_xAxis = QVector3D(
        xAxisArr[0].toDouble(),
        xAxisArr[1].toDouble(),
        xAxisArr[2].toDouble()
        );

    normalize();

    qDebug() << "[Plane]" << id() << "loaded from JSON:" << displayName();
    return true;
}

Plane* Plane::clone(QObject* parent) const {
    Plane* cloned = new Plane(m_origin, m_normal, m_xAxis, m_type, parent);
    cloned->m_name = m_name + " (Copy)";
    return cloned;
}

void Plane::markModified() {
    updateModifiedTime();
    Q_EMIT geometryChanged();
}

void Plane::setActive(bool active) {
    if (m_isActive != active) {
        m_isActive = active;
        qDebug() << "[Plane]" << id() << name() << "active state:" << active;
        Q_EMIT activeStateChanged(m_isActive);
    }
}

void Plane::normalize() {
    // 正規化法向量
    if (m_normal.length() > 1e-10) {
        m_normal.normalize();
    } else {
        m_normal = QVector3D(0, 0, 1);
    }

    // 正規化 X 軸並確保垂直於法向量
    if (m_xAxis.length() > 1e-10) {
        m_xAxis.normalize();

        // Gram-Schmidt 正交化
        double dot = QVector3D::dotProduct(m_xAxis, m_normal);
        m_xAxis = m_xAxis - m_normal * dot;

        if (m_xAxis.length() > 1e-10) {
            m_xAxis.normalize();
        } else {
            if (std::abs(m_normal.x()) < 0.9) {
                m_xAxis = QVector3D(1, 0, 0);
            } else {
                m_xAxis = QVector3D(0, 1, 0);
            }
            dot = QVector3D::dotProduct(m_xAxis, m_normal);
            m_xAxis = m_xAxis - m_normal * dot;
            m_xAxis.normalize();
        }
    } else {
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

void Plane::updateModifiedTime() {
    m_modifiedTime = QDateTime::currentDateTime();
}

} // namespace cad
} // namespace aicad
