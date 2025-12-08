// src/core/EventBus.h

#ifndef AICAD_CORE_EVENTBUS_H
#define AICAD_CORE_EVENTBUS_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <functional>

namespace aicad {
namespace core {

/**
 * @brief 事件總線，用於模組間解耦通訊
 * 
 * EventBus 實作發布-訂閱模式，允許模組之間進行鬆散耦合的通訊。
 * 任何模組都可以發布事件，其他模組可以訂閱感興趣的事件。
 * 
 * 使用範例:
 * @code
 * // 訂閱事件
 * eventBus->subscribe("feature.created", this, [](const QVariant& data) {
 *     Feature* feature = data.value<Feature*>();
 *     qDebug() << "Feature created:" << feature->name();
 * });
 * 
 * // 發布事件
 * eventBus->publish("feature.created", QVariant::fromValue(feature));
 * @endcode
 */
class EventBus : public QObject {
    Q_OBJECT
    
public:
    explicit EventBus(QObject* parent = nullptr);
    ~EventBus() override;
    
    /**
     * @brief 訂閱事件
     * @param eventName 事件名稱
     * @param receiver 接收者物件 (用於自動清理訂閱)
     * @param callback 回呼函式
     */
    void subscribe(const QString& eventName,
                   QObject* receiver,
                   std::function<void(const QVariant&)> callback);
    
    /**
     * @brief 發布事件
     * @param eventName 事件名稱
     * @param data 事件資料
     */
    void publish(const QString& eventName, const QVariant& data = QVariant());
    
    /**
     * @brief 取消訂閱
     * @param eventName 事件名稱
     * @param receiver 接收者物件
     */
    void unsubscribe(const QString& eventName, QObject* receiver);
    
    /**
     * @brief 取消接收者的所有訂閱
     * @param receiver 接收者物件
     */
    void unsubscribeAll(QObject* receiver);
    
private:
    class Private;
    Private* d;
};

/**
 * @brief 標準事件名稱常數
 */
namespace Events {
    // 文件事件
    constexpr const char* DOCUMENT_CREATED = "document.created";
    constexpr const char* DOCUMENT_OPENED = "document.opened";
    constexpr const char* DOCUMENT_CLOSED = "document.closed";
    constexpr const char* DOCUMENT_SAVED = "document.saved";
    constexpr const char* DOCUMENT_MODIFIED = "document.modified";
    
    // 特徵事件
    constexpr const char* FEATURE_CREATED = "feature.created";
    constexpr const char* FEATURE_UPDATED = "feature.updated";
    constexpr const char* FEATURE_DELETED = "feature.deleted";
    
    // 選取事件
    constexpr const char* SELECTION_CHANGED = "selection.changed";
    constexpr const char* SELECTION_CLEARED = "selection.cleared";
    
    // 視圖事件
    constexpr const char* VIEW_CREATED = "view.created";
    constexpr const char* VIEW_CHANGED = "view.changed";
    constexpr const char* VIEW_CLOSED = "view.closed";
    
    // 命令事件
    constexpr const char* COMMAND_STARTED = "command.started";
    constexpr const char* COMMAND_EXECUTED = "command.executed";
    constexpr const char* COMMAND_CANCELLED = "command.cancelled";
    constexpr const char* COMMAND_UNDONE = "command.undone";
    constexpr const char* COMMAND_REDONE = "command.redone";
}

} // namespace core
} // namespace aicad

#endif // AICAD_CORE_EVENTBUS_H