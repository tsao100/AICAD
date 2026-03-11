// core/GripManager.cpp
#include "GripManager.h"
#include "EventBus.h"
#include <QDebug>

namespace aicad {
namespace core {

GripManager::GripManager(EventBus* eventBus, QObject* parent)
    : QObject(parent)
    , m_eventBus(eventBus)
    , m_activeGrip(nullptr)
{
    setupEventSubscriptions();
}

GripManager::~GripManager() {
    m_eventBus->unsubscribeAll(this);
}

void GripManager::setupEventSubscriptions() {
    // 訂閱實體選擇事件
    m_eventBus->subscribe("entity.selected", this,
                          [this](const QVariant& data) {
                              handleEntitySelected(data);
                          });

    m_eventBus->subscribe("entity.deselected", this,
                          [this](const QVariant& data) {
                              handleEntityDeselected(data);
                          });

    // 訂閱 grip 拖動事件
    m_eventBus->subscribe("grip.drag-started", this,
                          [this](const QVariant& data) {
                              handleGripDragStarted(data);
                          });

    m_eventBus->subscribe("grip.dragging", this,
                          [this](const QVariant& data) {
                              handleGripDragging(data);
                          });

    m_eventBus->subscribe("grip.drag-ended", this,
                          [this](const QVariant& data) {
                              handleGripDragEnded(data);
                          });
}

void GripManager::showGripsForEntity(const QString& entityId) {
    // 從資料層取得實體資訊
    QVariantMap request;
    request["entityId"] = entityId;
    m_eventBus->publish("data.request-entity", request);

    // 這裡應該透過回調處理，簡化示範直接生成
    // 實際應該等待 data.entity-response 事件
}

void GripManager::handleEntitySelected(const QVariant& data) {
    QVariantMap map = data.toMap();
    QString entityId = map["entityId"].toString();
    QString entityType = map["type"].toString();

    qDebug() << "[GripManager] Entity selected:" << entityId << entityType;

    if (entityType == "line") {
        QVector2D start = map["startPoint"].value<QVector2D>();
        QVector2D end = map["endPoint"].value<QVector2D>();

        QVector<Grip> grips = generateGripsForLine(entityId, start, end);
        m_grips[entityId] = grips;

        // 發布事件通知視圖顯示 grips
        QVariantMap gripsData;
        gripsData["entityId"] = entityId;
        QVariantList gripsList;
        for (const auto& grip : grips) {
            gripsList.append(grip.toVariant());
        }
        gripsData["grips"] = gripsList;

        m_eventBus->publish("grip.show", gripsData);
    }
}

void GripManager::handleEntityDeselected(const QVariant& data) {
    QVariantMap map = data.toMap();
    QString entityId = map["entityId"].toString();

    m_grips.remove(entityId);

    QVariantMap hideData;
    hideData["entityId"] = entityId;
    m_eventBus->publish("grip.hide", hideData);
}

void GripManager::handleGripDragStarted(const QVariant& data) {
    QVariantMap map = data.toMap();
    Grip grip = Grip::fromVariant(map);

    qDebug() << "[GripManager] Grip drag started:" << grip.entityId;

    // 儲存活動 grip
    if (m_grips.contains(grip.entityId)) {
        auto& grips = m_grips[grip.entityId];
        for (auto& g : grips) {
            if (g.type == grip.type && g.index == grip.index) {
                g.isActive = true;
                m_activeGrip = &g;
                break;
            }
        }
    }
}

void GripManager::handleGripDragging(const QVariant& data) {
    QVariantMap map = data.toMap();
    QVector2D newPos = map["position"].value<QVector2D>();

    if (!m_activeGrip) return;

    // 更新 grip 位置
    m_activeGrip->position = newPos;

    // 發布實體更新請求
    QVariantMap updateData;
    updateData["entityId"] = m_activeGrip->entityId;
    updateData["gripType"] = static_cast<int>(m_activeGrip->type);
    updateData["gripIndex"] = m_activeGrip->index;
    updateData["newPosition"] = QVariant::fromValue(newPos);

    m_eventBus->publish("entity.update-via-grip", updateData);
}

void GripManager::handleGripDragEnded(const QVariant& data) {
    if (m_activeGrip) {
        m_activeGrip->isActive = false;
        m_activeGrip = nullptr;
    }

    // 發布完成事件
    m_eventBus->publish("grip.edit-completed", data);
}

QVector<Grip> GripManager::generateGripsForLine(
    const QString& entityId,
    const QVector2D& start,
    const QVector2D& end)
{
    QVector<Grip> grips;

    // 起點 grip
    Grip startGrip;
    startGrip.entityId = entityId;
    startGrip.type = GripType::StartPoint;
    startGrip.position = start;
    startGrip.index = 0;
    startGrip.isActive = false;
    grips.append(startGrip);

    // 終點 grip
    Grip endGrip;
    endGrip.entityId = entityId;
    endGrip.type = GripType::EndPoint;
    endGrip.position = end;
    endGrip.index = 0;
    endGrip.isActive = false;
    grips.append(endGrip);

    // 中點 grip
    Grip midGrip;
    midGrip.entityId = entityId;
    midGrip.type = GripType::MidPoint;
    midGrip.position = (start + end) * 0.5f;
    midGrip.index = 0;
    midGrip.isActive = false;
    grips.append(midGrip);

    return grips;
}

void GripManager::hideAllGrips() {
    m_grips.clear();
    m_activeGrip = nullptr;
    m_eventBus->publish("grip.hide-all", QVariant());
}

Grip* GripManager::getGripAtPosition(const QVector2D& pos, float tolerance) {
    for (auto& grips : m_grips) {
        for (auto& grip : grips) {
            float dist = (grip.position - pos).length();
            if (dist <= tolerance) {
                return &grip;
            }
        }
    }
    return nullptr;
}

} // namespace core
} // namespace aicad
