// src/core/EventBus.cpp

#include "EventBus.h"
#include <QHash>
#include <QVector>
#include <QDebug>

namespace aicad {
namespace core {

/**
 * @brief 事件訂閱資訊
 */
struct Subscription {
    QObject* receiver;
    std::function<void(const QVariant&)> callback;
};

/**
 * @brief EventBus 私有實作
 */
class EventBus::Private {
public:
    // 事件名稱 -> 訂閱列表
    QHash<QString, QVector<Subscription>> subscriptions;
};

EventBus::EventBus(QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    qDebug() << "[EventBus] Created";
}

EventBus::~EventBus() {
    qDebug() << "[EventBus] Destroyed";
    delete d;
}

void EventBus::subscribe(const QString& eventName,
                         QObject* receiver,
                         std::function<void(const QVariant&)> callback)
{
    if (!receiver || !callback) {
        qWarning() << "[EventBus] Invalid subscription parameters";
        return;
    }
    
    Subscription sub;
    sub.receiver = receiver;
    sub.callback = callback;
    
    d->subscriptions[eventName].append(sub);
    
    // 當接收者被刪除時，自動取消訂閱
    connect(receiver, &QObject::destroyed, this, [this, eventName, receiver]() {
        unsubscribe(eventName, receiver);
    });
    
    qDebug() << "[EventBus] Subscribed:" << eventName
             << "by" << receiver->metaObject()->className();
}

void EventBus::publish(const QString& eventName, const QVariant& data) {
    if (!d->subscriptions.contains(eventName)) {
        return;
    }
    
    qDebug() << "[EventBus] Publishing:" << eventName;
    
    const QVector<Subscription>& subs = d->subscriptions[eventName];
    
    for (const Subscription& sub : subs) {
        if (sub.receiver && sub.callback) {
            sub.callback(data);
        }
    }
}

void EventBus::unsubscribe(const QString& eventName, QObject* receiver) {
    if (!d->subscriptions.contains(eventName)) {
        return;
    }
    
    QVector<Subscription>& subs = d->subscriptions[eventName];
    
    // 移除該接收者的所有訂閱
    subs.erase(
        std::remove_if(subs.begin(), subs.end(),
                      [receiver](const Subscription& sub) {
                          return sub.receiver == receiver;
                      }),
        subs.end());
    
    qDebug() << "[EventBus] Unsubscribed:" << eventName
             << "by" << receiver->metaObject()->className();
}

void EventBus::unsubscribeAll(QObject* receiver) {
    for (auto it = d->subscriptions.begin(); it != d->subscriptions.end(); ++it) {
        QVector<Subscription>& subs = it.value();
        
        subs.erase(
            std::remove_if(subs.begin(), subs.end(),
                          [receiver](const Subscription& sub) {
                              return sub.receiver == receiver;
                          }),
            subs.end());
    }
    
    qDebug() << "[EventBus] Unsubscribed all for"
             << receiver->metaObject()->className();
}

} // namespace core
} // namespace aicad