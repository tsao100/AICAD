/**
 * @file EventBus.h
 * @brief 事件總線系統，用於模組間解耦通訊
 * @author Jack
 * @date 2024-12-04
 */

#ifndef AICAD_CORE_EVENTBUS_H
#define AICAD_CORE_EVENTBUS_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <functional>

namespace aicad {
namespace core {

/**
 * @brief 事件總線，實現發布-訂閱模式
 * 
 * EventBus 允許模組之間進行鬆耦合的通訊:
 * - 模組可以訂閱感興趣的事件
 * - 模組可以發布事件通知其他訂閱者
 * - 不需要知道訂閱者的具體類型
 * 
 * 使用範例:
 * @code
 * EventBus* bus = app->eventBus();
 * 
 * // 訂閱事件
 * bus->subscribe(Events::FEATURE_CREATED, this, 
 *     [](const QVariant& data) {
 *         qDebug() << "Feature created:" << data.toString();
 *     });
 * 
 * // 發布事件
 * bus->publish(Events::FEATURE_CREATED, featureName);
 * @endcode
 */
class EventBus : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief 建構子
     * @param parent 父物件
     */
    explicit EventBus(QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~EventBus() override;
    
    /**
     * @brief 訂閱事件
     * @param eventName 事件名稱
     * @param receiver 接收者物件 (用於自動取消訂閱)
     * @param callback 回呼函式
     * 
     * @note 當 receiver 物件被銷毀時，會自動取消訂閱
     */
    void subscribe(const QString& eventName,
                   QObject* receiver,
                   std::function<void(const QVariant&)> callback);
    
    /**
     * @brief 發布事件
     * @param eventName 事件名稱
     * @param data 事件資料 (可選)
     * 
     * 所有訂閱此事件的回呼函式都會被呼叫
     */
    void publish(const QString& eventName, const QVariant& data = QVariant());
    
    /**
     * @brief 取消訂閱
     * @param eventName 事件名稱
     * @param receiver 接收者物件
     * 
     * 取消指定接收者對某事件的所有訂閱
     */
    void unsubscribe(const QString& eventName, QObject* receiver);
    
    /**
     * @brief 取消接收者的所有訂閱
     * @param receiver 接收者物件
     */
    void unsubscribeAll(QObject* receiver);
    
    /**
     * @brief 取得訂閱者數量
     * @param eventName 事件名稱
     * @return 訂閱者數量
     */
    int subscriberCount(const QString& eventName) const;
    
    /**
     * @brief 清除所有訂閱
     */
    void clear();
    
Q_SIGNALS:
    /**
     * @brief 事件被發布時發出 (用於除錯)
     * @param eventName 事件名稱
     */
    void eventPublished(const QString& eventName);
    
private:
    class Private;
    Private* d;
};

/**
 * @brief 標準事件名稱
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
constexpr const char* FEATURE_SELECTED = "feature.selected";

// 選取事件
constexpr const char* SELECTION_CHANGED = "selection.changed";

// 視圖事件
constexpr const char* VIEW_CHANGED = "view.changed";
constexpr const char* VIEW_REFRESHED = "view.refreshed";

// 命令事件
constexpr const char* COMMAND_STARTED = "command.started";
constexpr const char* COMMAND_EXECUTED = "command.executed";
constexpr const char* COMMAND_CANCELLED = "command.cancelled";
}

} // namespace core
} // namespace aicad

#endif // AICAD_CORE_EVENTBUS_H
