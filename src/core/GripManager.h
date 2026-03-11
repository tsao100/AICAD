// core/GripManager.h
#ifndef AICAD_CORE_GRIPMANAGER_H
#define AICAD_CORE_GRIPMANAGER_H

#include "GripTypes.h"
#include <QObject>
#include <QMap>
#include <QVector>

namespace aicad {
namespace core {

class EventBus;

class GripManager : public QObject {
    Q_OBJECT

public:
    explicit GripManager(EventBus* eventBus, QObject* parent = nullptr);
    ~GripManager() override;

    // 顯示實體的 grips
    void showGripsForEntity(const QString& entityId);

    // 隱藏所有 grips
    void hideAllGrips();

    // 取得指定位置附近的 grip
    Grip* getGripAtPosition(const QVector2D& pos, float tolerance = 5.0f);

    // 更新 grip 位置
    void updateGripPosition(const QString& entityId, GripType type,
                            int index, const QVector2D& newPos);

private:
    void setupEventSubscriptions();
    void handleEntitySelected(const QVariant& data);
    void handleEntityDeselected(const QVariant& data);
    void handleGripDragStarted(const QVariant& data);
    void handleGripDragging(const QVariant& data);
    void handleGripDragEnded(const QVariant& data);

    QVector<Grip> generateGripsForLine(const QString& entityId,
                                       const QVector2D& start,
                                       const QVector2D& end);

    EventBus* m_eventBus;
    QMap<QString, QVector<Grip>> m_grips;  // entityId -> grips
    Grip* m_activeGrip;  // 當前正在拖動的 grip
};

} // namespace core
} // namespace aicad

#endif
