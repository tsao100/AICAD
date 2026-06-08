/**
 * @file EventBus.cpp
 * @brief EventBus 類別實作
 * @author Jack
 * @date 2024-12-04
 */

#include "EventBus.h"

#include <QDebug>
#include <QHash>
#include <QVector>
#include <QPointer>

namespace aicad {
namespace core {

/**
 * @brief 訂閱資訊結構
 */
struct Subscription {
    QPointer<QObject> receiver;  // 使用 QPointer 自動追蹤物件生命週期
    std::function<void(const QVariant&)> callback;
    
    bool isValid() const {
        return !receiver.isNull();
    }
};

class EventBus::Private {
public:
    // 事件名稱 -> 訂閱列表
    QHash<QString, QVector<Subscription>> subscriptions;
    
    /**
     * @brief 清理無效的訂閱 (receiver 已被刪除)
     */
    void cleanupInvalidSubscriptions(const QString& eventName) {
        if (!subscriptions.contains(eventName)) {
            return;
        }
        
        QVector<Subscription>& subs = subscriptions[eventName];
        subs.erase(
            std::remove_if(subs.begin(), subs.end(),
                [](const Subscription& sub) { return !sub.isValid(); }),
            subs.end()
        );
        
        // 如果沒有訂閱者，移除此事件
        if (subs.isEmpty()) {
            subscriptions.remove(eventName);
        }
    }
};

EventBus::EventBus(QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    qDebug() << "[EventBus] Created";
}

EventBus::~EventBus() {
    qDebug() << "[EventBus] Destroyed";
    clear();
    delete d;
}

void EventBus::subscribe(const QString& eventName,
                         QObject* receiver,
                         std::function<void(const QVariant&)> callback)
{
    if (!receiver) {
        qWarning() << "[EventBus] Cannot subscribe with null receiver";
        return;
    }
    
    if (!callback) {
        qWarning() << "[EventBus] Cannot subscribe with null callback";
        return;
    }
    
    // 建立訂閱
    Subscription sub;
    sub.receiver = receiver;
    sub.callback = callback;
    
    d->subscriptions[eventName].append(sub);
    
    qDebug() << "[EventBus] Subscribed:" << eventName 
             << "Receiver:" << receiver->objectName()
             << "Total subscribers:" << d->subscriptions[eventName].size();
}

void EventBus::publish(const QString& eventName, const QVariant& data) {
    if (!d->subscriptions.contains(eventName)) {
        qDebug() << "[EventBus] No subscribers for event:" << eventName;
        return;
    }
    
    // 先清理無效訂閱
    d->cleanupInvalidSubscriptions(eventName);
    
    if (!d->subscriptions.contains(eventName)) {
        return;
    }
    
    const QVector<Subscription>& subs = d->subscriptions[eventName];
    
    qDebug() << "[EventBus] Publishing:" << eventName 
             << "Subscribers:" << subs.size();
    
    // 呼叫所有訂閱者的回呼
    for (const Subscription& sub : subs) {
        if (sub.isValid() && sub.callback) {
            try {
                sub.callback(data);
            } catch (const std::exception& e) {
                qWarning() << "[EventBus] Exception in callback for" << eventName
                          << ":" << e.what();
            } catch (...) {
                qWarning() << "[EventBus] Unknown exception in callback for" << eventName;
            }
        }
    }
    
    Q_EMIT eventPublished(eventName);
}

void EventBus::unsubscribe(const QString& eventName, QObject* receiver) {
    if (!receiver) {
        return;
    }
    
    if (!d->subscriptions.contains(eventName)) {
        return;
    }
    
    QVector<Subscription>& subs = d->subscriptions[eventName];
    
    // 移除此 receiver 的所有訂閱
    int removed = 0;
    subs.erase(
        std::remove_if(subs.begin(), subs.end(),
            [receiver, &removed](const Subscription& sub) {
                if (sub.receiver == receiver) {
                    removed++;
                    return true;
                }
                return false;
            }),
        subs.end()
    );
    
    if (removed > 0) {
        qDebug() << "[EventBus] Unsubscribed:" << eventName 
                 << "Receiver:" << receiver->objectName()
                 << "Removed:" << removed;
    }
    
    // 如果沒有訂閱者，移除此事件
    if (subs.isEmpty()) {
        d->subscriptions.remove(eventName);
    }
}

void EventBus::unsubscribeAll(QObject* receiver) {
    if (!receiver) {
        return;
    }
    
    int totalRemoved = 0;
    
    // 遍歷所有事件
    // ✅ Make a copy of event names first
    QStringList eventNames = d->subscriptions.keys();

    for (const QString& eventName : eventNames) {
        if (!d->subscriptions.contains(eventName)) {
            continue;
        }

        QVector<Subscription>& subs = d->subscriptions[eventName];

        int beforeSize = subs.size();
        subs.erase(
            std::remove_if(subs.begin(), subs.end(),
                           [receiver](const Subscription& sub) {
                               return sub.receiver == receiver;
                           }),
            subs.end()
            );

        totalRemoved += (beforeSize - subs.size());

        if (subs.isEmpty()) {
            d->subscriptions.remove(eventName);
        }
    }

    if (totalRemoved > 0) {
        qDebug() << "[EventBus] Unsubscribed all for receiver:"
                 << receiver->objectName()
                 << "Total removed:" << totalRemoved;
    }
}

int EventBus::subscriberCount(const QString& eventName) const {
    if (!d->subscriptions.contains(eventName)) {
        return 0;
    }
    
    // 只計算有效的訂閱
    int count = 0;
    for (const Subscription& sub : d->subscriptions[eventName]) {
        if (sub.isValid()) {
            count++;
        }
    }
    
    return count;
}

void EventBus::clear() {
    int totalEvents = d->subscriptions.size();
    d->subscriptions.clear();
    qDebug() << "[EventBus] Cleared all subscriptions. Events:" << totalEvents;
}

} // namespace core
} // namespace aicad
