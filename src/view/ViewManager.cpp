// src/view/ViewManager.cpp

#include "ViewManager.h"
#include "../core/Application.h"
#include "../core/EventBus.h"
#include <QDebug>

namespace aicad {
namespace view {

class ViewManager::Private {
public:
    QVector<CadView*> views;
    CadView* activeView;
    cad::Document* activeDocument;
    
    Private()
        : activeView(nullptr)
        , activeDocument(nullptr)
    {
    }
};

ViewManager::ViewManager(QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    qDebug() << "[ViewManager] Created";
}

ViewManager::~ViewManager() {
    qDebug() << "[ViewManager] Destroying";
    closeAllViews();
    delete d;
}

CadView* ViewManager::createView(QWidget* parent) {
    qDebug() << "[ViewManager] Creating new view";
    
    CadView* view = new CadView(parent);
    
    // 設定當前文件
    if (d->activeDocument) {
        view->setDocument(d->activeDocument);
    }
    
    // 連接視圖信號
    connect(view, &QObject::destroyed, this, &ViewManager::onViewDestroyed);
    
    d->views.append(view);
    
    // 如果是第一個視圖，設為活動視圖
    if (d->views.size() == 1) {
        setActiveView(view);
    }
    
    Q_EMIT viewCreated(view);
    
    // 發布事件
    if (auto app = core::Application::instance()) {
        if (auto bus = app->eventBus()) {
            bus->publish(core::Events::VIEW_CREATED, QVariant::fromValue(view));
        }
    }
    
    qDebug() << "[ViewManager] View created, total views:" << d->views.size();
    
    return view;
}

void ViewManager::closeView(CadView* view) {
    if (!view || !d->views.contains(view)) {
        return;
    }
    
    qDebug() << "[ViewManager] Closing view";
    
    // 如果是活動視圖，切換到其他視圖
    if (view == d->activeView) {
        int index = d->views.indexOf(view);
        d->views.removeOne(view);
        
        if (!d->views.isEmpty()) {
            int newIndex = (index > 0) ? index - 1 : 0;
            setActiveView(d->views[newIndex]);
        } else {
            d->activeView = nullptr;
            Q_EMIT activeViewChanged(nullptr);
        }
    } else {
        d->views.removeOne(view);
    }
    
    Q_EMIT viewClosed(view);
    
    // 發布事件
    if (auto app = core::Application::instance()) {
        if (auto bus = app->eventBus()) {
            bus->publish(core::Events::VIEW_CLOSED, QVariant::fromValue(view));
        }
    }
    
    view->deleteLater();
    
    qDebug() << "[ViewManager] View closed, remaining views:" << d->views.size();
}

void ViewManager::closeAllViews() {
    qDebug() << "[ViewManager] Closing all views";
    
    while (!d->views.isEmpty()) {
        CadView* view = d->views.first();
        closeView(view);
    }
}

QVector<CadView*> ViewManager::views() const {
    return d->views;
}

CadView* ViewManager::activeView() const {
    return d->activeView;
}

void ViewManager::setActiveView(CadView* view) {
    if (view == d->activeView) {
        return;
    }
    
    if (view && !d->views.contains(view)) {
        qWarning() << "[ViewManager] View not managed by this manager";
        return;
    }
    
    d->activeView = view;
    
    qDebug() << "[ViewManager] Active view changed";
    Q_EMIT activeViewChanged(view);
}

void ViewManager::setActiveDocument(cad::Document* document) {
    if (document == d->activeDocument) {
        return;
    }
    
    d->activeDocument = document;
    
    // 更新所有視圖顯示此文件
    for (CadView* view : d->views) {
        view->setDocument(document);
    }
    
    qDebug() << "[ViewManager] Active document changed";
    Q_EMIT activeDocumentChanged(document);
}

cad::Document* ViewManager::activeDocument() const {
    return d->activeDocument;
}

void ViewManager::updateAllViews() {
    for (CadView* view : d->views) {
        view->redraw();
    }
}

void ViewManager::fitAllViews() {
    for (CadView* view : d->views) {
        view->fitAll();
    }
}

void ViewManager::onViewDestroyed(QObject* obj) {
    CadView* view = static_cast<CadView*>(obj);
    d->views.removeOne(view);
    
    if (view == d->activeView) {
        d->activeView = nullptr;
        if (!d->views.isEmpty()) {
            setActiveView(d->views.first());
        }
    }
}

} // namespace view
} // namespace aicad