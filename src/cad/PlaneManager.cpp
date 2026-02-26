/**
 * @file PlaneManager.cpp
 * @brief 平面管理器實作
 * @author AICAD Team
 * @date 2025-01-08
 */

#include "PlaneManager.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include <QDebug>
#include <QtMath>
#include <QJsonArray>

namespace aicad {
namespace cad {

// ============================================================================
// PlaneManager 類別實作
// ============================================================================

PlaneManager* PlaneManager::s_instance = nullptr;

PlaneManager* PlaneManager::instance() {
    if (!s_instance) {
        s_instance = new PlaneManager();
    }
    return s_instance;
}

PlaneManager::PlaneManager(QObject* parent)
    : QObject(parent)
    , m_activePlane(nullptr)
    , m_xyCount(0)
    , m_yzCount(0)
    , m_zxCount(0)
    , m_customCount(0)
    , m_offsetCount(0)
{
    qDebug() << "[PlaneManager] Initialized";
}

PlaneManager::~PlaneManager() {
    clear();
}

Plane* PlaneManager::createPlane(Plane::Type type, const QString& name) {
    QVector3D origin(0, 0, 0);
    QVector3D normal, xAxis;

    switch (type) {
    case Plane::Type::XY:
        normal = QVector3D(0, 0, 1);
        xAxis = QVector3D(1, 0, 0);
        break;

    case Plane::Type::YZ:
        normal = QVector3D(1, 0, 0);
        xAxis = QVector3D(0, 1, 0);
        break;

    case Plane::Type::ZX:
        normal = QVector3D(0, 1, 0);
        xAxis = QVector3D(0, 0, 1);
        break;

    case Plane::Type::XZ:
        normal = QVector3D(0, -1, 0);
        xAxis = QVector3D(1, 0, 0);
        break;

    default:
        qWarning() << "[PlaneManager] Invalid standard plane type";
        return nullptr;
    }

    return createPlaneInternal(origin, normal, xAxis, type, name);
}

Plane* PlaneManager::createPlane(const QVector3D& origin,
                                 const QVector3D& normal,
                                 const QVector3D& xAxis,
                                 const QString& name) {
    return createPlaneInternal(origin, normal, xAxis, Plane::Type::Custom, name);
}

Plane* PlaneManager::createFromNormal(const QVector3D& origin,
                                      const QVector3D& normal,
                                      const QString& name) {
    QVector3D n = normal.normalized();

    // 選擇參考向量
    QVector3D reference;
    if (std::abs(n.z()) < 0.9) {
        reference = QVector3D(0, 0, 1);
    } else {
        reference = QVector3D(1, 0, 0);
    }

    // 生成 U 軸
    QVector3D xAxis = QVector3D::crossProduct(reference, n).normalized();

    return createPlaneInternal(origin, n, xAxis, Plane::Type::Custom, name);
}

Plane* PlaneManager::createFromThreePoints(const QVector3D& p1,
                                           const QVector3D& p2,
                                           const QVector3D& p3,
                                           const QString& name) {
    // p1 是原點
    QVector3D origin = p1;

    // 計算兩個向量
    QVector3D v1 = (p2 - p1).normalized();
    QVector3D v2 = (p3 - p1).normalized();

    // 計算法向量
    QVector3D normal = QVector3D::crossProduct(v1, v2).normalized();

    // U 軸使用 v1
    QVector3D xAxis = v1;

    return createPlaneInternal(origin, normal, xAxis, Plane::Type::ThreePoint, name);
}

Plane* PlaneManager::createOffsetPlane(const Plane* sourcePlane,
                                       double offset,
                                       const QString& name) {
    if (!sourcePlane) {
        qWarning() << "[PlaneManager] Source plane is null";
        return nullptr;
    }

    // 新原點 = 舊原點 + 偏移 * 法向量
    QVector3D newOrigin = sourcePlane->origin() + sourcePlane->normal() * offset;

    Plane* plane = createPlaneInternal(
        newOrigin,
        sourcePlane->normal(),
        sourcePlane->xAxis(),
        Plane::Type::Offset,
        name
        );

    if (plane && name.isEmpty()) {
        plane->setName(QString("%1 (Offset %2)")
                           .arg(sourcePlane->displayName())
                           .arg(offset, 0, 'f', 2));
    }

    return plane;
}

Plane* PlaneManager::createParallelPlane(const Plane* sourcePlane,
                                         const QVector3D& throughPoint,
                                         const QString& name) {
    if (!sourcePlane) {
        qWarning() << "[PlaneManager] Source plane is null";
        return nullptr;
    }

    Plane* plane = createPlaneInternal(
        throughPoint,
        sourcePlane->normal(),
        sourcePlane->xAxis(),
        Plane::Type::Custom,
        name
        );

    if (plane && name.isEmpty()) {
        plane->setName(QString("%1 (Parallel)")
                           .arg(sourcePlane->displayName()));
    }

    return plane;
}

Plane* PlaneManager::createFromJson(const QJsonObject& json) {
    Plane* plane = new Plane(
        QVector3D(0, 0, 0),
        QVector3D(0, 0, 1),
        QVector3D(1, 0, 0),
        Plane::Type::Custom,
        this
        );

    if (plane->fromJson(json)) {
        registerPlane(plane);
        return plane;
    } else {
        delete plane;
        return nullptr;
    }
}

Plane* PlaneManager::clonePlane(const Plane* source, const QString& name) {
    if (!source) {
        qWarning() << "[PlaneManager] Source plane is null";
        return nullptr;
    }

    Plane* clone = source->clone(this);

    if (!name.isEmpty()) {
        clone->setName(name);
    } else {
        clone->setName(generateUniqueName(source->name()));
    }

    registerPlane(clone);
    return clone;
}

bool PlaneManager::deletePlane(const QString& id) {
    Plane* plane = getPlane(id);
    return deletePlane(plane);
}

bool PlaneManager::deletePlane(Plane* plane) {
    if (!plane) {
        return false;
    }

    QString planeId = plane->id();
    QString planeName = plane->name();

    // 如果是活動平面，先取消活動狀態
    if (m_activePlane == plane) {
        setActivePlane(nullptr);
    }

    // 發出即將刪除信號
    Q_EMIT plane->aboutToBeDeleted();

    // 從管理器移除
    m_planes.remove(planeId);

    // 刪除物件
    plane->deleteLater();

    qDebug() << "[PlaneManager] Plane deleted:" << planeId << planeName;

    // 發出信號
    Q_EMIT planeDeleted(planeId);
    Q_EMIT planesChanged();

    // EventBus 事件
    emitEvent("plane.deleted", nullptr);

    return true;
}

Plane* PlaneManager::getPlane(const QString& id) const {
    return m_planes.value(id, nullptr);
}

Plane* PlaneManager::getPlaneByName(const QString& name) const {
    for (Plane* plane : m_planes) {
        if (plane->name() == name) {
            return plane;
        }
    }
    return nullptr;
}

QList<Plane*> PlaneManager::planes() const {
    return m_planes.values();
}

void PlaneManager::setActivePlane(Plane* plane) {
    if (m_activePlane == plane) {
        return;
    }

    // 停用舊的
    if (m_activePlane) {
        m_activePlane->setActive(false);
    }

    m_activePlane = plane;

    // 啟用新的
    if (m_activePlane) {
        m_activePlane->setActive(true);
    }

    qDebug() << "[PlaneManager] Active plane changed to:"
             << (plane ? plane->displayName() : "None");

    Q_EMIT activePlaneChanged(m_activePlane);

    // EventBus 事件
    emitEvent("plane.activated", plane);
}

void PlaneManager::setActivePlane(const QString& id) {
    Plane* plane = getPlane(id);
    setActivePlane(plane);
}

bool PlaneManager::nameExists(const QString& name) const {
    return getPlaneByName(name) != nullptr;
}

QString PlaneManager::generateUniqueName(const QString& baseName) const {
    QString name = baseName;
    int counter = 1;

    while (nameExists(name)) {
        name = QString("%1 (%2)").arg(baseName).arg(counter++);
    }

    return name;
}

void PlaneManager::clear() {
    qDebug() << "[PlaneManager] Clearing all planes";

    setActivePlane(nullptr);

    qDeleteAll(m_planes);
    m_planes.clear();

    m_xyCount = 0;
    m_yzCount = 0;
    m_zxCount = 0;
    m_customCount = 0;
    m_offsetCount = 0;

    Q_EMIT planesChanged();
}

QJsonObject PlaneManager::toJson() const {
    QJsonObject json;

    QJsonArray planesArray;
    for (Plane* plane : m_planes) {
        planesArray.append(plane->toJson());
    }
    json["planes"] = planesArray;

    if (m_activePlane) {
        json["activePlaneId"] = m_activePlane->id();
    }

    return json;
}

bool PlaneManager::fromJson(const QJsonObject& json) {
    clear();

    if (!json.contains("planes")) {
        return false;
    }

    QJsonArray planesArray = json["planes"].toArray();
    for (const QJsonValue& val : planesArray) {
        QJsonObject planeJson = val.toObject();
        createFromJson(planeJson);
    }

    // 恢復活動平面
    if (json.contains("activePlaneId")) {
        QString activeId = json["activePlaneId"].toString();
        setActivePlane(activeId);
    }

    qDebug() << "[PlaneManager] Loaded" << m_planes.size() << "planes from JSON";
    return true;
}

Plane* PlaneManager::createPlaneInternal(const QVector3D& origin,
                                         const QVector3D& normal,
                                         const QVector3D& xAxis,
                                         Plane::Type type,
                                         const QString& name) {
    Plane* plane = new Plane(origin, normal, xAxis, type, this);

    // 設定名稱
    if (name.isEmpty()) {
        QString baseName;
        switch (type) {
        case Plane::Type::XY:
            baseName = QString("XY%1").arg(++m_xyCount);
            break;
        case Plane::Type::YZ:
            baseName = QString("YZ%1").arg(++m_yzCount);
            break;
        case Plane::Type::ZX:
            baseName = QString("ZX%1").arg(++m_zxCount);
            break;
        case Plane::Type::XZ:
            baseName = "XZ";
            break;
        case Plane::Type::Offset:
            baseName = QString("Offset%1").arg(++m_offsetCount);
            break;
        case Plane::Type::Custom:
        case Plane::Type::ThreePoint:
        default:
            baseName = QString("Custom%1").arg(++m_customCount);
            break;
        }
        plane->setName(generateUniqueName(baseName));
    } else {
        plane->setName(generateUniqueName(name));
    }

    registerPlane(plane);
    return plane;
}

void PlaneManager::registerPlane(Plane* plane) {
    m_planes.insert(plane->id(), plane);

    qDebug() << "[PlaneManager] Plane registered:" << plane->id() << plane->displayName();

    Q_EMIT planeCreated(plane);
    Q_EMIT planesChanged();

    // EventBus 事件
    emitEvent("plane.created", plane);
}

void PlaneManager::emitEvent(const QString& eventName, Plane* plane) {
    if (!core::Application::instance() || !core::Application::instance()->eventBus()) {
        return;
    }

    QVariantMap data;
    if (plane) {
        data["planeId"] = plane->id();
        data["planeName"] = plane->name();
        data["planeType"] = static_cast<int>(plane->type());
    }

    core::Application::instance()->eventBus()->publish(eventName, data);
}

} // namespace cad
} // namespace aicad
