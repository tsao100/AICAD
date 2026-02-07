/**
 * @file ViewManager.cpp
 * @brief ViewManager 類別實作
 * @author Felicia
 * @date 2024-12-04
 */

#include "ViewManager.h"
#include "CadView.h"
#include "RubberBand.h"
#include "ViewGrid.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "cad/Plane.h"
#include "cad/Sketch.h"
#include <QDebug>
#include <QVector>
#include <QPointer>

namespace aicad {
namespace view {

class ViewManager::Private {
public:
    QVector<QPointer<CadView>> views;
    QPointer<CadView> activeView;
};

ViewManager::ViewManager(QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    qDebug() << "[ViewManager] Created";
    
    // 訂閱相關事件
    using namespace core;
    EventBus* bus = Application::instance()->eventBus();
    
    if (bus) {
        // 訂閱文件事件以同步視圖
        bus->subscribe(Events::DOCUMENT_MODIFIED, this, 
            [this](const QVariant& data) {
                Q_UNUSED(data);
                refreshAllViews();
            });
            
        bus->subscribe(Events::FEATURE_CREATED, this,
            [this](const QVariant& data) {
                Q_UNUSED(data);
                refreshAllViews();
            });
            
        bus->subscribe(Events::FEATURE_UPDATED, this,
            [this](const QVariant& data) {
                Q_UNUSED(data);
                refreshAllViews();
                // ✅ ADD: Force immediate display update
                CadView* view = activeView();
                if (view && view->context()) {
                    view->context()->UpdateCurrentViewer();
                }
            });
        // ✅ 訂閱平面選取請求
        bus->subscribe("command.request-plane-selection", this,
                       [this](const QVariant& data) {
                           onPlaneSelectionRequested(data);
                       });

        // ✅ 監聽 sketch 建立事件並切換視圖
        bus->subscribe("sketch.created", this,
                       [this](const QVariant& data) {
                           onSketchCreated(data);
                       });

        bus->subscribe("command.enter-sketch-mode", this, [this](const QVariant& data) {
            CadView* view = activeView();
            if (view) {
                view->setMode(InteractionMode::Sketching);
            }
        });

        bus->subscribe("command.cancelled", this, [this](const QVariant& data) {
            CadView* view = activeView();
            if (view) {
                view->setMode(InteractionMode::Idle);
            }
        });

        // ✅ 監聽可見性變更
        bus->subscribe("feature.visibility-changed", this,
                       [this](const QVariant& data) {
                           onFeatureVisibilityChanged(data);
                       });

        bus->subscribe("command.request-view-setup", this,
            [this](const QVariant& data) {
                QVariantMap setup = data.toMap();
                CadView* view = activeView();
                if (!view) return;

                // Set interaction mode
                QString mode = setup["mode"].toString();
                if (mode == "sketching") {
                    view->setMode(InteractionMode::Sketching);
                } else if (mode == "selecting") {
                    view->setMode(InteractionMode::Selecting);
                } else if (mode == "idle") {
                    view->setMode(InteractionMode::Idle);
                }

                // Setup rubber band if requested
                if (setup.contains("rubberBandMode")) {
                    QString rbMode = setup["rubberBandMode"].toString();
                    view::RubberBand* rubber = view->rubberBand();

                    if (rbMode == "line") {
                        rubber->setMode(view::RubberBandMode::Line);
                    } else if (rbMode == "rectangle") {
                        rubber->setMode(view::RubberBandMode::Rectangle);
                    } else if (rbMode == "circle") {
                        rubber->setMode(view::RubberBandMode::Circle);
                    } else if (rbMode == "arc") {
                        rubber->setMode(view::RubberBandMode::Arc);
                    } else if (rbMode == "polyline") {
                        rubber->setMode(view::RubberBandMode::Polyline);
                    }

                    // Set rubber band plane to match active sketch
                    Application* app = Application::instance();
                    cad::Sketch* sketch = app->activeSketch();
                    if (sketch) {
                        cad::Plane sketchPlane = sketch->plane();
                        view::CustomPlane customPlane;
                        customPlane.origin = sketchPlane.origin();
                        customPlane.normal = sketchPlane.normal();
                        customPlane.uAxis = sketchPlane.xAxis();
                        customPlane.vAxis = sketchPlane.yAxis();
                        rubber->setPlane(customPlane);
                    }
                }

                qDebug() << "[ViewManager] View setup completed:" << mode;
            });

        // ✅ Handle rubber band updates
        bus->subscribe("command.update-rubber-band", this,
            [this](const QVariant& data) {
                QVariantMap update = data.toMap();
                CadView* view = activeView();
                if (!view) return;

                view::RubberBand* rubber = view->rubberBand();
                if (!rubber) return;

                QString action = update["action"].toString();

                if (action == "clearAndAdd") {
                    rubber->clearPoints();
                    rubber->clear();

                    if (update.contains("point")) {
                        QVector2D point = update["point"].value<QVector2D>();
                        rubber->addPoint(point);
                    }
                } else if (action == "addPoint") {
                    if (update.contains("point")) {
                        QVector2D point = update["point"].value<QVector2D>();
                        rubber->addPoint(point);
                    }
                } else if (action == "clear") {
                    rubber->clearPoints();
                    rubber->clear();
                } else if (action == "update") {
                    rubber->update();
                }

                qDebug() << "[ViewManager] Rubber band updated:" << action;
            });

        // ✅ Handle cleanup requests
        bus->subscribe("command.request-cleanup", this,
                       [this](const QVariant& data) {
                           QVariantMap cleanup = data.toMap();
                           CadView* view = activeView();
                           if (!view) return;

                           if (cleanup["clearRubberBand"].toBool()) {
                               view::RubberBand* rubber = view->rubberBand();
                               if (rubber) {
                                   rubber->clearPoints();
                                   rubber->clear();
                               }
                           }

                           // Reset to idle mode unless specified otherwise
                           if (!cleanup.contains("keepMode") || !cleanup["keepMode"].toBool()) {
                               view->setMode(InteractionMode::Idle);
                           }

                           // Hide grid if requested
                           if (cleanup.contains("hideGrid") && cleanup["hideGrid"].toBool()) {
                               view->setGridEnabled(false);
                           }

                           qDebug() << "[ViewManager] Cleanup completed";
                       });

    }
}

void ViewManager::onFeatureVisibilityChanged(const QVariant& data) {
    QVariantMap visData = data.toMap();
    QString itemId = visData["itemId"].toString();
    bool visible = visData["visible"].toBool();

    qDebug() << "[ViewManager] Feature visibility changed:" << itemId << visible;

    // ✅ 在視圖中顯示/隱藏對應的物件
    // TODO: 根據 itemId 找到對應的 AIS_Shape 並設定可見性

    CadView* view = activeView();
    if (view) {
        view->refreshView();
    }
}

void ViewManager::onPlaneSelectionRequested(const QVariant& data) {
    QVariantMap requestData = data.toMap();

    qDebug() << "[ViewManager] Plane selection requested";

    CadView* view = activeView();
    if (!view) {
        qWarning() << "[ViewManager] No active view";
        return;
    }

    // ✅ 啟用平面選取模式
    view->setMode(InteractionMode::Selecting);

    // ✅ 設定選取過濾器（只能選取平面）
    view->setSelectionFilter("plane");

    // ✅ 高亮顯示可選取的平面
    view->highlightSelectablePlanes(true);

    qDebug() << "[ViewManager] Plane selection mode enabled";
}

void ViewManager::onSketchCreated(const QVariant& data) {
    QVariantMap sketchData = data.toMap();
    QString planeName = sketchData["plane"].toString();

    qDebug() << "[ViewManager] Sketch created on" << planeName << "plane";

    CadView* view = activeView();
    if (!view) return;

    // ✅ 關閉平面高亮
    view->highlightSelectablePlanes(false);

    // ✅ Update grid plane to match sketch plane
    CustomPlane gridPlane;
    if (planeName == "XY") {
        gridPlane = CustomPlane::XY();
        view->setTopView();
    } else if (planeName == "XZ") {
        gridPlane = CustomPlane::XZ();
        view->setFrontView();
    } else if (planeName == "YZ") {
        gridPlane = CustomPlane::YZ();
        view->setRightView();
    }

    ViewGrid* grid = view->grid();  // Need to add getter method
    if (grid) {
        grid->setPlane(gridPlane);
    }

    // ✅ 進入草圖模式
    view->setMode(InteractionMode::Sketching);

    // ✅ 啟用網格
    view->setGridEnabled(true);

    view->fitAll();
}

ViewManager::~ViewManager() {
    qDebug() << "[ViewManager] Destroying...";
    closeAll();
    delete d;
}

CadView* ViewManager::createView(cad::Document* document, QWidget* parent) {
    qDebug() << "[ViewManager] Creating view for document";
    
    // 建立新視圖
    CadView* view = new CadView(parent);
    
    if (document) {
        // TODO: 設定視圖的文件
        // view->setDocument(document);
    }
    
    // 加入管理列表
    d->views.append(view);
    
    // 設為活動視圖
    setActiveView(view);
    
    // 監聽視圖銷毀事件
    connect(view, &QObject::destroyed, this, [this, view]() {
        d->views.removeOne(view);
        if (d->activeView == view) {
            if (!d->views.isEmpty()) {
                setActiveView(d->views.first());
            } else {
                setActiveView(nullptr);
            }
        }
        Q_EMIT viewCountChanged(d->views.size());
    });
    
    qDebug() << "[ViewManager] View created. Total views:" << d->views.size();
    
    Q_EMIT viewCreated(view);
    Q_EMIT viewCountChanged(d->views.size());
    
    // 發布事件
    using namespace core;
    EventBus* bus = Application::instance()->eventBus();
    if (bus) {
        bus->publish(Events::VIEW_CHANGED, QVariant());
    }
    
    return view;
}

bool ViewManager::closeView(CadView* view) {
    if (!view) {
        return false;
    }
    
    qDebug() << "[ViewManager] Closing view";
    
    // 從列表中移除
    d->views.removeOne(view);
    
    // 如果是活動視圖，切換到其他視圖
    if (d->activeView == view) {
        if (!d->views.isEmpty()) {
            setActiveView(d->views.first());
        } else {
            setActiveView(nullptr);
        }
    }
    
    // 發出信號
    Q_EMIT viewClosed(view);
    Q_EMIT viewCountChanged(d->views.size());
    
    // 刪除視圖
    view->deleteLater();
    
    qDebug() << "[ViewManager] View closed. Remaining:" << d->views.size();
    
    return true;
}

bool ViewManager::closeAll() {
    qDebug() << "[ViewManager] Closing all views";
    
    // 複製列表，因為 closeView 會修改它
    QVector<CadView*> viewsToClose;
    for (const QPointer<CadView>& viewPtr : d->views) {
        if (!viewPtr.isNull()) {
            viewsToClose.append(viewPtr.data());
        }
    }
    
    // 關閉所有視圖
    for (CadView* view : viewsToClose) {
        closeView(view);
    }
    
    qDebug() << "[ViewManager] All views closed";
    return true;
}

QVector<CadView*> ViewManager::views() const {
    QVector<CadView*> result;
    for (const QPointer<CadView>& viewPtr : d->views) {
        if (!viewPtr.isNull()) {
            result.append(viewPtr.data());
        }
    }
    return result;
}

int ViewManager::viewCount() const {
    // 只計算有效的視圖
    int count = 0;
    for (const QPointer<CadView>& viewPtr : d->views) {
        if (!viewPtr.isNull()) {
            count++;
        }
    }
    return count;
}

void ViewManager::setActiveView(CadView* view) {
    if (d->activeView == view) {
        return;
    }
    
    d->activeView = view;
    
    QString viewName = view ? "Valid View" : "None";
    qDebug() << "[ViewManager] Active view changed to:" << viewName;
    
    Q_EMIT activeViewChanged(view);
    
    // 發布事件
    using namespace core;
    EventBus* bus = Application::instance()->eventBus();
    if (bus) {
        bus->publish(Events::VIEW_CHANGED, QVariant());
    }
}

CadView* ViewManager::activeView() const {
    return d->activeView.data();
}

QVector<CadView*> ViewManager::findViewsByDocument(cad::Document* document) const {
    QVector<CadView*> result;
    
    if (!document) {
        return result;
    }
    
    for (const QPointer<CadView>& viewPtr : d->views) {
        if (!viewPtr.isNull()) {
            // TODO: 實作文件比對
            // if (viewPtr->document() == document) {
            //     result.append(viewPtr.data());
            // }
        }
    }
    
    return result;
}

void ViewManager::refreshAllViews() {
    qDebug() << "[ViewManager] Refreshing all views";
    
    for (const QPointer<CadView>& viewPtr : d->views) {
        if (!viewPtr.isNull()) {
            viewPtr->refreshView();
        }
    }
    
    // 發布事件
    using namespace core;
    EventBus* bus = Application::instance()->eventBus();
    if (bus) {
        bus->publish(Events::VIEW_REFRESHED, QVariant());
    }
}

void ViewManager::syncViewsToDocument(cad::Document* document) {
    if (!document) {
        return;
    }
    
    qDebug() << "[ViewManager] Syncing views to document";
    
    QVector<CadView*> docViews = findViewsByDocument(document);
    
    for (CadView* view : docViews) {
        // TODO: 實作視圖同步
        // view->displayAllFeatures();
        view->refreshView();
    }
}

} // namespace view
} // namespace aicad
