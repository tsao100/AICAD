/**
 * @file UIManager.cpp
 * @brief UIManager 類別實作
 * @author James
 * @date 2025-01-07
 */

#include "UIManager.h"
#include "MainWindow.h"
#include "ToolManager.h"
#include "FeatureBrowser.h"
#include "PropertyPanel.h"
#include "CommandOverlayWidget.h"        // ✅ 新增
#include "core/CommandLineManager.h"     // ✅ 新增
#include "AutoCompleteModel.h"           // ✅ 新增
#include "command/CommandAlias.h"        // ✅ 新增
#include "view/ViewManager.h"  // ✅ 添加
#include "view/CadView.h"      // ✅ 添加
#include <QShortcut>
#include "ui/CommandOverlayWidget.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "core/DocumentManager.h"
#include "core/MenuParser.h"
#include "cad/Document.h"
#include "cad/Sketch.h"
#include "cad/grips/GripManager.h"
#include "cad/grips/SketchGripProvider.h"
#include "ui/GripEventFilter.h"
#include "command/CommandTypes.h"  // 確保包含完整定義
#include "command/CommandManager.h"

#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QTimer>
#include <QtMath>
#include <QDebug>

using namespace aicad::core;
using namespace aicad::cad;

namespace aicad {
namespace ui {

class UIManager::Private {
public:
    Private()
        : mainWindow(nullptr)
        , featureBrowser(nullptr)
        , propertyPanel(nullptr)
        , toolManager(nullptr)
        , cadView(nullptr)           // ✅ 添加
        , menuParser(nullptr)  // 新增
        , gripManager(nullptr)
        , gripFilter(nullptr)
        , initialized(false)
        , commandOverlay(nullptr)           // ✅ 新增
        , commandLineManager(nullptr)       // ✅ 新增
        , commandAlias(nullptr)             // ✅ 新增
        , autoCompleteModel(nullptr)        // ✅ 新增
    {
    }
    
    ~Private() {
        // MainWindow 會自動刪除子元件
        delete mainWindow;
    }
    
    MainWindow* mainWindow;
    FeatureBrowser* featureBrowser;
    PropertyPanel* propertyPanel;
    ToolManager* toolManager;
    view::CadView* cadView;          // ✅ 添加
    core::MenuParser* menuParser;  // 新增
    GripManager* gripManager;
    GripEventFilter* gripFilter;
    bool initialized;

    // ✅ 新增：命令列組件
    CommandOverlayWidget* commandOverlay;
    core::CommandLineManager* commandLineManager;
    command::CommandAlias* commandAlias;
    AutoCompleteModel* autoCompleteModel;
};

UIManager::UIManager(QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    qDebug() << "[UIManager] Created";
}

UIManager::~UIManager() {
    qDebug() << "[UIManager] Destroyed";
    delete d;
}

void UIManager::initGripSystem()
{
    // 取得核心系統
    core::Application* app = core::Application::instance();
    core::EventBus* bus = app->eventBus();
    core::DocumentManager* docMgr = app->documentManager();

    // 關閉 GripManager 自己的舊式 snap（由 OSnapManager 統一管理）
    d->gripManager->setGridSnap(false);
    d->gripManager->setEndpointSnap(false);
    d->gripManager->setMidpointSnap(false);

    // ── A) Feature selected → attach grip provider ────────────────────
    bus->subscribe("selection.featureSelected", this,
                   [this, docMgr](const QVariant& v) {
                    QString featureId = v.toMap()["featureId"].toString();
                    // ✅ Reconstruct QSet<int> from QVariantList
                    QSet<int> geomIndices;
                    for (const QVariant& idx : v.toMap()["geomIndices"].toList())
                        geomIndices.insert(idx.toInt());
                    qDebug() << "[UIManager]" << featureId;

        cad::Feature* feature = docMgr->currentDocument()->findFeature(featureId);
                       if (!feature) return;

                       // Detach old grips first
                       d->gripManager->detach();

                       // Attach appropriate provider
                       if (auto* sketch = qobject_cast<cad::Sketch*>(feature)) {
                           cad::Plane* plane = sketch->plane();

                           // ✅ 把 sketch plane 的真實軸向傳給 GripManager
                           QVector3D qx = plane->xAxis();
                           QVector3D qy = plane->yAxis();

                           d->gripManager->setPlaneAxes(
                               gp_Dir(qx.x(), qx.y(), qx.z()),
                               gp_Dir(qy.x(), qy.y(), qy.z())
                               );

                           // ✅ 同樣傳給 GripEventFilter 做 ray-plane 投影
                           d->gripFilter->setSketchPlane(plane);
                           d->gripManager->attachProvider(
                               new cad::SketchGripProvider(sketch, geomIndices));
                           qDebug() << "[UIManager] Grips attached for sketch:"
                                    << sketch->id() << " -> " << geomIndices;
                       }
                       // future: else if Extrude → ExtrudeGripProvider ...
                   });

    // ── B) Selection cleared → detach grips ──────────────────────────
    bus->subscribe("selection.cleared", this,
                   [this](const QVariant&) {
                       d->gripManager->detach();
                       qDebug() << "[UIManager] Grips detached (selection cleared)";
                   });

    // ── C) Document closed / new document → detach grips ─────────────
    connect(docMgr->currentDocument(), &cad::Document::aboutToClose,
            this, [this]() {
                d->gripManager->detach();
            });
}

bool UIManager::initialize(core::MenuParser* menuParser) {
    if (d->initialized) {
        qWarning() << "[UIManager] Already initialized";
        return true;
    }

    d->menuParser = menuParser;  // 儲存 MenuParser

    qDebug() << "[UIManager] Initializing...";
    
    try {
        // 取得核心系統
        core::Application* app = core::Application::instance();
        core::EventBus* bus = app->eventBus();
        core::DocumentManager* docMgr = app->documentManager();
        
        if (!bus || !docMgr) {
            qCritical() << "[UIManager] Core systems not available";
            return false;
        }
        
        // 1. 建立主視窗
        qDebug() << "[UIManager] Creating MainWindow...";
        d->mainWindow = new MainWindow();

        // 2. 使用 MenuParser 建立選單和工具列
        if (d->menuParser && d->menuParser->isLoaded()) {
            setupMenusFromParser();
            setupToolbarsFromParser();
        } else {
            qWarning() << "[UIManager] MenuParser not available, using default UI";
            setupDefaultUI();
        }
        
        // 2. 建立特徵瀏覽器
        qDebug() << "[UIManager] Creating FeatureBrowser...";
        d->featureBrowser = new FeatureBrowser(d->mainWindow);
        d->mainWindow->addDockWidget(Qt::LeftDockWidgetArea, d->featureBrowser);
        
        // 3. 建立屬性面板
        qDebug() << "[UIManager] Creating PropertyPanel...";
        d->propertyPanel = new PropertyPanel(d->mainWindow);
        d->mainWindow->addDockWidget(Qt::RightDockWidgetArea, d->propertyPanel);

        // 4. 建立工具管理器
        qDebug() << "[UIManager] Creating ToolManager...";
        d->toolManager = new ToolManager(d->mainWindow);

        // 5. 建立 CAD 視圖並設為中央 Widget  // ✅ 添加
        qDebug() << "[UIManager] Creating CadView...";
        d->cadView = new view::CadView(d->mainWindow);
        d->mainWindow->setCentralWidget(d->cadView);
        app->viewManager()->setActiveView(d->cadView);
        // 設置命令列系統（在 CadView 創建後）
        setupCommandLine();
        d->commandOverlay->setLayoutMode(LayoutMode::Standard);
        d->commandOverlay->setLayoutMode(LayoutMode::Compact);

        // 建立並加入 OSnap 工具列
        if (d->cadView && d->cadView->snapManager()) {
            m_snapToolbar = new osnap::OSnapToolbar(
                d->cadView->snapManager(), d->mainWindow);
            d->mainWindow->addToolBar(Qt::BottomToolBarArea, m_snapToolbar);
        }

        // auto* cmdOverlay =
        //     new aicad::ui::CommandOverlayWidget(d->cadView);
        // // 初始位置
        // cmdOverlay->raise();
        // cmdOverlay->show();
        // cmdOverlay->setLayoutMode(aicad::ui::LayoutMode::Standard);

        // auto* historyDock = new aicad::ui::CommandHistoryDockWidget(d->mainWindow);
        // d->mainWindow->addDockWidget(Qt::BottomDockWidgetArea, historyDock);
        // historyDock->hide();

        // // F2 toggle
        // QWidget* owner = d->mainWindow;  // 一定是 QWidget*

        // auto* shortcut = new QShortcut(QKeySequence(Qt::Key_F2), owner);
        // connect(shortcut, &QShortcut::activated, owner, [historyDock]() {
        //     historyDock->setVisible(!historyDock->isVisible());
        //     if (historyDock->isVisible())
        //         historyDock->raise();
        // });

        // ✅ 6. 連接視圖就緒信號，延遲初始化參考幾何
        connect(d->cadView, &view::CadView::viewInitialized,
                this, &UIManager::onViewReady);
        // 連接命令列事件
        connectCommandLineEvents();

        // 連接事件總線
        qDebug() << "[UIManager] Connecting to EventBus...";
        
        // 監聽文件事件
        bus->subscribe(core::Events::DOCUMENT_CREATED, this,
            [this](const QVariant& data) {
                qDebug() << "[UIManager] Document created:" << data.toString();
                updateFeatureTree();
                // ✅ 新文件建立時也初始化參考幾何
                onDocumentCreated();
            });

        // ✅ Add this to set current document
        bus->subscribe(core::Events::DOCUMENT_CREATED, this,
                       [this](const QVariant& data) {
                           cad::Document* doc = qvariant_cast<cad::Document*>(data);
                           if (doc && d->cadView) {
                               d->cadView->setDocument(doc);
                           }
                       });

        bus->subscribe(core::Events::DOCUMENT_OPENED, this,
                       [this](const QVariant& data) {
                           cad::Document* doc = qvariant_cast<cad::Document*>(data);
                           if (doc && d->cadView) {
                               d->cadView->setDocument(doc);
                               onDocumentCreated();
                           }
                       });

        bus->subscribe(core::Events::DOCUMENT_CLOSED, this,
            [this](const QVariant& data) {
                qDebug() << "[UIManager] Document closed:" << data.toString();
                updateFeatureTree();
            });
        
        // 監聽特徵事件
        bus->subscribe(core::Events::FEATURE_CREATED, this,
            [this](const QVariant& data) {
                qDebug() << "[UIManager] Feature created:" << data.toString();
                updateFeatureTree();

                // Keep sketching mode if it's a sketch
                QString featureName = data.toString();
                if (featureName.contains("Sketch", Qt::CaseInsensitive)) {
                    if (d->cadView) {
                        d->cadView->setMode(view::InteractionMode::Sketching);
                    }
                }
            });
        
        // 7. 連接 DocumentManager 信號
        connect(docMgr, &core::DocumentManager::documentCreated,
                this, [this](cad::Document* doc) {
            if (doc) {
                QString name = doc->fileName();
                qDebug() << "[UIManager] DocumentManager created document:" << name;
                setStatusMessage(QString("Document created: %1").arg(name), 3000);
            }});
        
        connect(docMgr, &core::DocumentManager::currentDocumentChanged,
                d->featureBrowser, &FeatureBrowser::setCurrentDocument);

        // 7. 連接主視窗關閉信號
        connect(d->mainWindow, &MainWindow::aboutToClose,
                this, &UIManager::mainWindowClosed);

        // ✅ 監聽平面選取請求（顯示提示）
        bus->subscribe("command.request-plane-selection", this,
                       [this](const QVariant& data) {
                           setStatusMessage("Click on a plane (XY, XZ, or YZ) to select...", 0);
                       });

        // ✅ 監聽平面選取結果
        bus->subscribe("plane.selected", this,
                       [this](const QVariant& data) {
                           QVariantMap planeData = data.toMap();

                           if (planeData["cancelled"].toBool()) {
                               setStatusMessage("Plane selection cancelled", 2000);
                           } else {
                               QString planeName = planeData["plane"].toString();
                               setStatusMessage(
                                   QString("Plane %1 selected").arg(planeName), 2000);
                           }
                       });

        // ✅ 監聽命令事件並更新 UI
        bus->subscribe("command.message", this, [this](const QVariant& data) {
            setStatusMessage(data.toString(), 3000);
        });

        bus->subscribe("command.error", this, [this](const QVariant& data) {
            setStatusMessage("Error: " + data.toString(), 5000);
        });

        bus->subscribe(core::Events::FEATURE_CREATED, this, [this](const QVariant& data) {
            updateFeatureTree();
        });

        // ✅ Monitor user interactions for debugging/logging
        bus->subscribe(core::Events::POINT_ACQUIRED, this,
                       [](const QVariant& data) {
                           QVariantMap map = data.toMap();
                           QVector2D point = map["point"].value<QVector2D>();
                           qDebug() << "[UIManager] User clicked point:" << point.x() << point.y();
                           // Could update coordinate display here
                       });

        // ✅ Handle sketch line creation requests
        bus->subscribe("command.create-sketch-line", this,
                       [this, bus](const QVariant& data) {
                           QVariantMap lineData = data.toMap();

                           Application* app = Application::instance();
                           cad::Sketch* sketch = app->activeSketch();

                           if (!sketch) {
                               qWarning() << "[UIManager] No active sketch for line creation";
                               bus->publish(Events::COMMAND_FAILED, "No active sketch");
                               return;
                           }

                           QVector2D startPoint = lineData["startPoint"].value<QVector2D>();
                           QVector2D endPoint = lineData["endPoint"].value<QVector2D>();

                           // Create the line in the sketch
                           sketch->addLine(startPoint, endPoint);
                           sketch->rebuild();

                           // Notify feature update
                           bus->publish(Events::FEATURE_UPDATED, sketch->name());

                           QString msg = QString("Line created from (%1,%2) to (%3,%4)")
                                             .arg(startPoint.x()).arg(startPoint.y())
                                             .arg(endPoint.x()).arg(endPoint.y());

                           setStatusMessage(msg, 3000);

                           qDebug() << "[UIManager]" << msg;
                       });

        // ✅ Handle sketch polyline creation requests
        bus->subscribe("command.create-sketch-polyline", this,
                       [this, bus](const QVariant& data) {
                           QVariantMap polylineData = data.toMap();
                           Application* app = Application::instance();
                           cad::Sketch* sketch = app->activeSketch();
                           if (!sketch) {
                               qWarning() << "[UIManager] No active sketch for polyline creation";
                               bus->publish(Events::COMMAND_FAILED, "No active sketch");
                               return;
                           }

                           // Unpack ordered vertex list
                           QVector<QVector2D> vertices = polylineData["points"].value<QVector<QVector2D>>();
                           if (vertices.size() < 2) {
                               qWarning() << "[UIManager] Polyline requires at least 2 points";
                               bus->publish(Events::COMMAND_FAILED, "Polyline requires at least 2 points");
                               return;
                           }

                           // Create each segment as a line in the sketch
                           // int segmentCount = 0;
                           // for (int i = 0; i < vertices.size() - 1; ++i) {
                           //     QVector2D startPoint = vertices[i].value<QVector2D>();
                           //     QVector2D endPoint   = vertices[i + 1].value<QVector2D>();
                           //     sketch->addLine(startPoint, endPoint);
                           //     ++segmentCount;
                           // }

                           sketch->addPolyline(vertices, false);
                           sketch->rebuild();

                           // Notify feature update
                           bus->publish(Events::FEATURE_UPDATED, sketch->name());

                           QString msg = QString("Polyline created with %1 points (%2 segments)")
                                             .arg(vertices.size())
                                             .arg(vertices.size()-1);
                           setStatusMessage(msg, 3000);
                           qDebug() << "[UIManager]" << msg;
                       });

        // ✅ Handle non-interactive sketch line requests (with coordinates)
        bus->subscribe("command.request-sketch-line", this,
                       [this, bus](const QVariant& data) {
                           QVariantMap request = data.toMap();
                           QStringList args = request["args"].toStringList();

                           if (args.size() < 4) {
                               bus->publish(Events::COMMAND_FAILED, "Need 4 coordinates");
                               return;
                           }

                           Application* app = Application::instance();
                           cad::Sketch* sketch = app->activeSketch();

                           if (!sketch) {
                               bus->publish(Events::COMMAND_FAILED, "No active sketch");
                               return;
                           }

                           bool ok;
                           double x1 = args[0].toDouble(&ok);
                           if (!ok) {
                               bus->publish(Events::COMMAND_FAILED, "Invalid x1");
                               return;
                           }

                           double y1 = args[1].toDouble(&ok);
                           if (!ok) {
                               bus->publish(Events::COMMAND_FAILED, "Invalid y1");
                               return;
                           }

                           double x2 = args[2].toDouble(&ok);
                           if (!ok) {
                               bus->publish(Events::COMMAND_FAILED, "Invalid x2");
                               return;
                           }

                           double y2 = args[3].toDouble(&ok);
                           if (!ok) {
                               bus->publish(Events::COMMAND_FAILED, "Invalid y2");
                               return;
                           }

                           // Create the line
                           sketch->addLine(QVector2D(x1, y1), QVector2D(x2, y2));
                           sketch->rebuild();

                           bus->publish(Events::FEATURE_UPDATED, sketch->name());
                           bus->publish(Events::COMMAND_EXECUTED, "Line created");

                           setStatusMessage("Line created", 3000);
                           qDebug() << "[UIManager] Line created from coordinates";
                       });

        // ✅ Handle rectangle creation (similar pattern)
        bus->subscribe("command.create-sketch-rect", this,
                       [this, bus](const QVariant& data) {
                           QVariantMap rectData = data.toMap();

                           Application* app = Application::instance();
                           cad::Sketch* sketch = app->activeSketch();

                           if (!sketch) {
                               bus->publish(Events::COMMAND_FAILED, "No active sketch");
                               return;
                           }

                           QVector2D corner1 = rectData["Corner1"].value<QVector2D>();
                           QVector2D corner2 = rectData["Corner2"].value<QVector2D>();

                           sketch->addRectangle(corner1, corner2);
                           sketch->rebuild();

                           bus->publish(Events::FEATURE_UPDATED, sketch->name());
                           bus->publish(Events::COMMAND_EXECUTED, "Rectangle created");
                           setStatusMessage("Rectangle created", 3000);

                           qDebug() << "[UIManager] Rectangle created";
                       });

        // ✅ Handle circle creation
        bus->subscribe("command.create-sketch-circle", this,
                       [this, bus](const QVariant& data) {
                           QVariantMap circleData = data.toMap();

                           Application* app = Application::instance();
                           cad::Sketch* sketch = app->activeSketch();

                           if (!sketch) {
                               bus->publish(Events::COMMAND_FAILED, "No active sketch");
                               return;
                           }

                           QVector2D center = circleData["centerPoint"].value<QVector2D>();
                           double radius = circleData["radius"].toDouble();

                           if (radius <= 0) {
                               bus->publish(Events::COMMAND_FAILED, "Invalid radius");
                               return;
                           }

                           sketch->addCircle(center, radius);
                           sketch->rebuild();

                           bus->publish(Events::FEATURE_UPDATED, sketch->name());
                           bus->publish(Events::COMMAND_EXECUTED,
                                        QString("Circle created (r=%1)").arg(radius));
                           setStatusMessage(QString("Circle created (r=%1)").arg(radius), 3000);

                           qDebug() << "[UIManager] Circle created";
                       });

        // ✅ Handle sketch ellipse creation requests
        bus->subscribe("command.create-sketch-ellipse", this,
                       [this, bus](const QVariant& data) {
                           QVariantMap ellipseData = data.toMap();
                           Application* app = Application::instance();
                           cad::Sketch* sketch = app->activeSketch();

                           if (!sketch) {
                               qWarning() << "[UIManager] No active sketch for ellipse creation";
                               bus->publish(Events::COMMAND_FAILED, "No active sketch");
                               return;
                           }

                           QVector2D center = ellipseData["center"].value<QVector2D>();
                           QVector2D majorAxisEnd = ellipseData["majorAxisEnd"].value<QVector2D>();
                           float minorRadius = ellipseData["minorRadius"].toFloat();
                           float majorRadius = ellipseData["majorRadius"].toFloat();

                           // Calculate major axis angle
                           QVector2D majorVector = majorAxisEnd - center;
                           float angle = qAtan2(majorVector.y(), majorVector.x());

                           // Create the ellipse in the sketch
                           sketch->addEllipse(center, majorRadius, minorRadius, angle);
                           sketch->rebuild();

                           // Notify feature update
                           bus->publish(Events::FEATURE_UPDATED, sketch->name());

                           QString msg = QString("Ellipse created: center (%1,%2), major radius %3, minor radius %4, angle %5°")
                                             .arg(center.x()).arg(center.y())
                                             .arg(majorRadius).arg(minorRadius)
                                             .arg(qRadiansToDegrees(angle), 0, 'f', 1);

                           setStatusMessage(msg, 3000);
                           qDebug() << "[UIManager]" << msg;
                       });

        // ── 訂閱建立弧線請求 ─────────────────────────────────────────────────
        // ✅ Handle arc creation
        bus->subscribe("command.create-sketch-arc", this,
                       [this, bus](const QVariant& data) {
                           QVariantMap arcData = data.toMap();

                           Application* app    = Application::instance();
                           cad::Sketch* sketch = app->activeSketch();

                           if (!sketch) {
                               bus->publish(Events::COMMAND_FAILED, "No active sketch");
                               return;
                           }

                           // ── 取出三個原始點（供 Sketch 記錄幾何意圖）──────────────
                           QVector2D startPoint = arcData["startPoint"].value<QVector2D>();
                           QVector2D midPoint   = arcData["midPoint"].value<QVector2D>();
                           QVector2D endPoint   = arcData["endPoint"].value<QVector2D>();

                           // ── 取出預算好的圓心 / 半徑 / 角度 ───────────────────────
                           QVector2D center     = arcData["center"].value<QVector2D>();
                           double    radius     = arcData["radius"].toDouble();
                           double    startAngle = arcData["startAngle"].toDouble();
                           double    endAngle   = arcData["endAngle"].toDouble();

                           if (radius <= 0) {
                               bus->publish(Events::COMMAND_FAILED, "Invalid arc radius");
                               return;
                           }

                           // ── 寫入 Sketch ──────────────────────────────────────────
                            sketch->addArc(startPoint, midPoint, endPoint);
                            sketch->rebuild();

                           // ── 廣播更新 ─────────────────────────────────────────────
                           bus->publish(Events::FEATURE_UPDATED, sketch->name());

                           QString msg = QString("Arc created (r=%1, %2°→%3°)")
                                             .arg(radius, 0, 'f', 3)
                                             .arg(startAngle, 0, 'f', 1)
                                             .arg(endAngle,   0, 'f', 1);

                           bus->publish(Events::COMMAND_EXECUTED, msg);
                           setStatusMessage(msg, 3000);

                           qDebug() << "[UIManager] Arc created:"
                                    << "center(" << center.x() << "," << center.y() << ")"
                                    << "r=" << radius
                                    << "angles:" << startAngle << "->" << endAngle;
                       });



        // ✅ Handle sketch polygon creation requests
        bus->subscribe("command.create-sketch-polygon", this,
                       [this, bus](const QVariant& data) {
                           QVariantMap polygonData = data.toMap();
                           Application* app = Application::instance();
                           cad::Sketch* sketch = app->activeSketch();

                           if (!sketch) {
                               qWarning() << "[UIManager] No active sketch for polygon creation";
                               bus->publish(Events::COMMAND_FAILED, "No active sketch");
                               return;
                           }

                           // Extract polygon parameters
                           QVector2D center = polygonData["center"].value<QVector2D>();
                           double radius = polygonData["radius"].toDouble();
                           int sides = polygonData["sides"].toInt();

                           // Extract vertices
                           QVariantList verticesList = polygonData["vertices"].toList();
                           QVector<QVector2D> vertices;
                           for (const QVariant& v : verticesList) {
                               vertices.append(v.value<QVector2D>());
                           }

                           // Validate parameters
                           if (sides < 3 || radius <= 0 || vertices.isEmpty()) {
                               qWarning() << "[UIManager] Invalid polygon parameters";
                               bus->publish(Events::COMMAND_FAILED, "Invalid polygon parameters");
                               return;
                           }

                           // Create the polygon in the sketch
                           // Option 1: If sketch has addPolygon method
                           sketch->addPolyline(vertices, true);

                           // Option 2: If sketch needs individual lines for polygon
                           // for (int i = 0; i < vertices.size(); ++i) {
                           //     int nextIndex = (i + 1) % vertices.size();
                           //     sketch->addLine(vertices[i], vertices[nextIndex]);
                           // }

                           sketch->rebuild();

                           // Notify feature update
                           bus->publish(Events::FEATURE_UPDATED, sketch->name());

                           QString msg = QString("Polygon created: %1 sides, center (%2,%3), radius %4")
                                             .arg(sides)
                                             .arg(center.x(), 0, 'f', 2)
                                             .arg(center.y(), 0, 'f', 2)
                                             .arg(radius, 0, 'f', 2);

                           setStatusMessage(msg, 3000);
                           qDebug() << "[UIManager]" << msg;
                       });

        // ✅ Handle sketch spline creation requests (interactive mode)
        bus->subscribe("command.create-sketch-spline", this,
                       [this, bus](const QVariant& data) {
                           QVariantMap splineData = data.toMap();
                           Application* app = Application::instance();
                           cad::Sketch* sketch = app->activeSketch();

                           if (!sketch) {
                               qWarning() << "[UIManager] No active sketch for spline creation";
                               bus->publish(Events::COMMAND_FAILED, "No active sketch");
                               return;
                           }

                           QVector<QVector2D> controlPoints =
                               splineData["controlPoints"].value<QVector<QVector2D>>();

                           // Validate minimum points
                           if (controlPoints.size() < 3) {
                               qWarning() << "[UIManager] Not enough control points for spline:"
                                          << controlPoints.size();
                               bus->publish(Events::COMMAND_FAILED,
                                            "Need at least 3 points for spline");
                               return;
                           }

                           // Create the spline in the sketch
                           sketch->addSpline(controlPoints);
                           sketch->rebuild();

                           // Notify feature update
                           bus->publish(Events::FEATURE_UPDATED, sketch->name());

                           QString msg = QString("Spline created with %1 control points")
                                             .arg(controlPoints.size());
                           setStatusMessage(msg, 3000);
                           qDebug() << "[UIManager]" << msg;
                       });

        // ✅ Handle sketch spline creation requests (non-interactive mode)
        bus->subscribe("command.request-sketch-spline", this,
                       [this, bus](const QVariant& data) {
                           QVariantMap request = data.toMap();
                           Application* app = Application::instance();
                           cad::Sketch* sketch = app->activeSketch();

                           if (!sketch) {
                               qWarning() << "[UIManager] No active sketch for spline creation";
                               bus->publish(Events::COMMAND_FAILED, "No active sketch");
                               return;
                           }

                           QStringList args = request["args"].toStringList();

                           // Parse coordinates: x1 y1 x2 y2 x3 y3 ...
                           // Need at least 6 values (3 points)
                           if (args.size() < 6 || args.size() % 2 != 0) {
                               qWarning() << "[UIManager] Invalid spline arguments:"
                                          << "Need at least 6 coordinates (x1 y1 x2 y2 x3 y3 ...)";
                               bus->publish(Events::COMMAND_FAILED,
                                            "Invalid arguments: Need at least 6 coordinates");
                               return;
                           }

                           // Convert string coordinates to QVector2D points
                           QVector<QVector2D> controlPoints;
                           for (int i = 0; i < args.size(); i += 2) {
                               bool okX, okY;
                               float x = args[i].toFloat(&okX);
                               float y = args[i + 1].toFloat(&okY);

                               if (!okX || !okY) {
                                   qWarning() << "[UIManager] Invalid coordinate values at index" << i;
                                   bus->publish(Events::COMMAND_FAILED,
                                                QString("Invalid coordinate at index %1").arg(i));
                                   return;
                               }

                               controlPoints.append(QVector2D(x, y));
                           }

                           // Create the spline in the sketch
                           sketch->addSpline(controlPoints);
                           sketch->rebuild();

                           // Notify feature update
                           bus->publish(Events::FEATURE_UPDATED, sketch->name());

                           QString msg = QString("Spline created with %1 control points from command line")
                                             .arg(controlPoints.size());
                           setStatusMessage(msg, 3000);
                           qDebug() << "[UIManager]" << msg;
                       });

        // ✅ Handle view refresh after geometry changes
        bus->subscribe(Events::FEATURE_UPDATED, this,
                       [this](const QVariant& data) {
                           Q_UNUSED(data);

                           // Refresh the active view
                           if (d->cadView) {
                               d->cadView->refreshView();
                           }
                       });



        // ✅ Connect view refresh when features update
        connect(bus, &core::EventBus::eventPublished, this,
                [this](const QString& eventName) {
                    if (eventName == core::Events::COMMAND_EXECUTED) {
                        if (d->cadView) {
                            d->cadView->refreshView();
                        }
                    }
                });

        // 初始化 GripManager
        d->gripManager = new GripManager(this);
        d->gripManager->setContext(d->cadView->context());
        d->gripManager->setGridSnap(true, 5.0);

        // 安裝事件攔截器到 CadView widget
        d->gripFilter = new GripEventFilter(d->gripManager, d->cadView->view(), this);
        d->cadView->installEventFilter(d->gripFilter);
        d->cadView->setGripManager(d->gripManager, d->gripFilter);

        // ── 當使用者選取 Feature 時，掛載對應 provider ───────────────────────
        connect(d->featureBrowser, &FeatureBrowser::featureSelectedById,
                this, [this, docMgr](const QString& featureId) {

                    d->gripManager->detach();   // 先清除舊 grips

            cad::Feature* f = docMgr->currentDocument()->findFeature(featureId);
                    if (!f) return;

                    if (auto* sketch = qobject_cast<cad::Sketch*>(f)) {
                        // Sketch 使用 SketchGripProvider
                        auto* provider = new SketchGripProvider(sketch);
                        d->gripManager->attachProvider(provider);
                    }
                    // 其他 Feature 類型可在此擴展（ExtrudeGripProvider 等）
                });

        // ── 取消選取時清除 grips ──────────────────────────────────────────────
        // connect(d->cadView->context().get(), &SomeSelectionSignal, this, [this]() {
        //     d->gripManager->detach();
        // });

        // ── 監聽 Grip 拖拉完成（Log / Status bar）────────────────────────────
        connect(d->gripManager, &GripManager::gripDragFinished,
                this, [bus](const QString& id, const gp_Pnt& from, const gp_Pnt& to) {
                    QString msg = QString("Grip '%1' moved Δ(%.2f, %.2f, %.2f)")
                                      .arg(id)
                                      .arg(to.X() - from.X())
                                      .arg(to.Y() - from.Y())
                                      .arg(to.Z() - from.Z());
                    bus->publish(Events::COMMAND_LOG, msg);
                });

        // ── Snap 指示（顯示在 status bar）──────────────────────────────────────
        connect(d->gripManager, &GripManager::snapOccurred,
                this, [bus](const SnapResult& s) {
                    if (s.snapped)
                        bus->publish(Events::COMMAND_LOG, "Snap: " + s.description);
                });

        initGripSystem();

        d->initialized = true;
        qDebug() << "[UIManager] Initialization completed";
        
        Q_EMIT initialized();
        return true;
        
    } catch (const std::exception& e) {
        qCritical() << "[UIManager] Initialization failed:" << e.what();
        return false;
    } catch (...) {
        qCritical() << "[UIManager] Initialization failed: Unknown exception";
        return false;
    }
}

// ✅ 新增：設置命令列系統
void UIManager::setupCommandLine() {
    qDebug() << "[UIManager] Setting up command line...";

    if (!d->cadView) {
        qWarning() << "[UIManager] CadView not available, cannot setup command line";
        return;
    }

    // 1. 獲取單例
    d->commandLineManager = core::CommandLineManager::instance();
    d->commandAlias = command::CommandAlias::instance();

    // 2. 創建自動完成模型
    d->autoCompleteModel = new AutoCompleteModel(this);
    d->autoCompleteModel->updateFromAlias();

    // 3. 創建命令列覆蓋層（附加到 CadView）
    d->commandOverlay = new CommandOverlayWidget(d->cadView);

    // 4. 設置自動完成
    d->commandOverlay->inputWidget()->setAutoCompleteModel(d->autoCompleteModel);

    // 5. 設置命令歷史
    d->commandOverlay->inputWidget()->setCommandHistory(
        d->commandLineManager->commandHistory()
        );

    // 6. 預設為緊湊模式
    d->commandOverlay->setLayoutMode(LayoutMode::Compact);
    qDebug() << "[UIManager] Command line setup complete";
}

// ✅ 新增：連接命令列事件
void UIManager::connectCommandLineEvents() {
    if (!d->commandOverlay) {
        return;
    }

    auto* bus = core::Application::instance()->eventBus();

    // 命令輸入
    connect(d->commandOverlay, &CommandOverlayWidget::commandEntered,
            this, [this](const QString& command) {
                qDebug() << "[UIManager] Command entered:" << command;

                // 解析別名
                QString resolved = d->commandAlias->resolveAlias(command);

                // 更新使用統計
                d->autoCompleteModel->incrementUsage(resolved);

                // 執行命令
                d->commandLineManager->executeCommand(resolved);
            });

    // 命令提示
    bus->subscribe(core::Events::COMMAND_PROMPT, this,
                   [this](const QVariant& v) {
                       QString prompt = v.toString();
                       d->commandOverlay->setPrompt(prompt);
                   });

    // 命令完成
    bus->subscribe(core::Events::COMMAND_EXECUTED, this,
                   [this](const QVariant& v) {
                       QString result = v.toString();
                       if (!result.isEmpty()) {
                           d->commandOverlay->appendHistory(result, "#00FF00");
                       }
                   });

    // 命令錯誤
    bus->subscribe(core::Events::COMMAND_ERROR, this,
                   [this](const QVariant& v) {
                       QString error = v.toString();
                       d->commandOverlay->appendHistory("Error: " + error, "#FF0000");
                       d->commandOverlay->setState(CommandLineState::Error);
                   });

    // 命令警告
    bus->subscribe(core::Events::COMMAND_WARNING, this,
                   [this](const QVariant& v) {
                       QString warning = v.toString();
                       d->commandOverlay->appendHistory("Warning: " + warning, "#FFA500");
                       d->commandOverlay->setState(CommandLineState::Warning);
                   });

    // 選項可用
    bus->subscribe(core::Events::OPTIONS_AVAILABLE, this,
                   [this](const QVariant& v) {
                       QList<CommandOption> options = v.value<QList<CommandOption>>();
                       d->commandOverlay->showOptions(options);
                   });

    qDebug() << "[UIManager] Command line events connected";
}

// ===== 新增的公開方法 =====

CommandOverlayWidget* UIManager::commandLine() const {
    return d->commandOverlay;
}

core::CommandLineManager* UIManager::commandLineManager() const {
    return d->commandLineManager;
}

command::CommandAlias* UIManager::commandAlias() const {
    return d->commandAlias;
}

void UIManager::showCommandMessage(const QString& message, const QString& color) {
    if (d->commandOverlay) {
        d->commandOverlay->appendHistory(message, color);
    }
}

void UIManager::showCommandError(const QString& error) {
    showCommandMessage("Error: " + error, "#FF0000");

    if (d->commandOverlay) {
        d->commandOverlay->setState(CommandLineState::Error);
    }
}

void UIManager::showCommandWarning(const QString& warning) {
    showCommandMessage("Warning: " + warning, "#FFA500");

    if (d->commandOverlay) {
        d->commandOverlay->setState(CommandLineState::Warning);
    }
}

void UIManager::showMainWindow() {
    if (!d->mainWindow) {
        qWarning() << "[UIManager] MainWindow not created";
        return;
    }
    
    qDebug() << "[UIManager] Showing MainWindow";
    d->mainWindow->show();
}

void UIManager::onSketchEditStarted(Sketch* sketch)
{
    if (!sketch) return;

    // 1️⃣ 設定 OSnap 平面
    if (d->cadView && d->cadView->snapManager()) {
        d->cadView->snapManager()->setActivePlane(sketch->plane());
        d->cadView->snapManager()->setActiveSketch(sketch);   // ✅ 新增
        d->cadView->snapManager()->setSnapEnabled(true);
    }

    // 2️⃣ 發事件（讓其他系統同步）
    auto* bus = Application::instance()->eventBus();
    bus->publish("sketch.editStarted", QVariant::fromValue(sketch));
}

void UIManager::onSketchEditEnded()
{
    // 1️⃣ 清掉 snap 狀態
    if (d->cadView && d->cadView->snapManager()) {
        d->cadView->snapManager()->setActivePlane(nullptr);
        d->cadView->snapManager()->setActiveSketch(nullptr);  // ✅ 新增
        d->cadView->snapManager()->clearLastInputPoint();
    }

    // 2️⃣ 發事件
    auto* bus = Application::instance()->eventBus();
    bus->publish("sketch.editEnded", QVariant{});
}

MainWindow* UIManager::mainWindow() const {
    return d->mainWindow;
}

FeatureBrowser* UIManager::featureBrowser() const {
    return d->featureBrowser;
}

PropertyPanel* UIManager::propertyPanel() const {
    return d->propertyPanel;
}

ToolManager* UIManager::toolManager() const {
    return d->toolManager;
}

void UIManager::updateFeatureTree() {
    if (!d->featureBrowser) {
        return;
    }
    
    qDebug() << "[UIManager] Updating feature tree";
    d->featureBrowser->refresh();
}

void UIManager::setStatusMessage(const QString& message, int timeout) {
    if (!d->mainWindow) {
        return;
    }
    
    d->mainWindow->statusBar()->showMessage(message, timeout);
    // ✅ 同時在命令列顯示
    showCommandMessage(message, "#AAAAAA");
}

// 添加 getter
view::CadView* UIManager::cadView() const {
    return d->cadView;
}

void UIManager::setupMenusFromParser() {
    if (!d->menuParser || !d->mainWindow) {
        return;
    }

    qDebug() << "[UIManager] Setting up menus from menu.txt...";

    QMenuBar* menuBar = d->mainWindow->menuBar();
    QStringList menuNames = d->menuParser->getAllMenuNames();

    for (const QString& menuName : menuNames) {
        QMenu* menu = menuBar->addMenu(menuName);

        auto items = d->menuParser->getMenuItems(menuName);

        for (const core::MenuItem& item : items) {
            if (item.type == core::MenuItemType::Separator) {
                menu->addSeparator();
            } else {
                QAction* action = menu->addAction(item.label);

                // 設定圖示
                if (!item.icon.isEmpty()) {
                    action->setIcon(QIcon(item.icon));
                }

                // 設定快捷鍵
                if (!item.shortcut.isEmpty()) {
                    action->setShortcut(QKeySequence(item.shortcut));
                }

                // 連接到命令系統
                connect(action, &QAction::triggered, this, [this, item]() {
                    executeCommand(item.id);
                });
            }
        }
    }

    qDebug() << "[UIManager] Created" << menuNames.size() << "menus";
}

void UIManager::setupToolbarsFromParser() {
    if (!d->menuParser || !d->mainWindow) {
        return;
    }

    qDebug() << "[UIManager] Setting up toolbars from menu.txt...";

    QStringList toolbarNames = d->menuParser->getAllToolbarNames();

    for (const QString& toolbarName : toolbarNames) {
        QToolBar* toolbar = d->mainWindow->addToolBar(toolbarName);
        toolbar->setObjectName(toolbarName);

        auto items = d->menuParser->getToolbarItems(toolbarName);

        for (const core::MenuItem& item : items) {
            if (item.type == core::MenuItemType::Separator) {
                toolbar->addSeparator();
            } else {
                QAction* action = toolbar->addAction(item.label);

                // 設定圖示
                if (!item.icon.isEmpty()) {
                    action->setIcon(QIcon(item.icon));
                }

                // 設定快捷鍵
                if (!item.shortcut.isEmpty()) {
                    action->setShortcut(QKeySequence(item.shortcut));
                }

                // 設定工具提示
                QString tooltip = item.label;
                if (!item.shortcut.isEmpty()) {
                    tooltip += QString(" (%1)").arg(item.shortcut);
                }
                action->setToolTip(tooltip);

                // 連接到命令系統
                connect(action, &QAction::triggered, this, [this, item]() {
                    executeCommand(item.id);
                });
            }
        }
    }

    qDebug() << "[UIManager] Created" << toolbarNames.size() << "toolbars";
}

void UIManager::executeCommand(const QString& commandId) {
    core::Application* app = core::Application::instance();
    command::CommandManager* cmdMgr = app->commandManager();

    if (!cmdMgr) {
        qWarning() << "[UIManager] CommandManager not available";
        return;
    }

    qDebug() << "[UIManager] Executing command:" << commandId;

    // 執行命令
    command::CommandResult result = cmdMgr->executeCommand(commandId);

    // 顯示結果
    if (result.success) {
        setStatusMessage(result.message, 3000);
    } else {
        setStatusMessage("Error: " + result.message, 5000);
    }
}

void UIManager::setupDefaultUI() {
    // 回退到預設 UI (如果沒有 menu.txt)
    qDebug() << "[UIManager] Setting up default UI...";

    QMenuBar* menuBar = d->mainWindow->menuBar();

    // File 選單
    QMenu* fileMenu = menuBar->addMenu("&File");
    fileMenu->addAction("&New", this, &UIManager::onNewDocument, QKeySequence::New);
    fileMenu->addAction("&Open", this, &UIManager::onOpenDocument, QKeySequence::Open);
    fileMenu->addAction("&Save", this, &UIManager::onSaveDocument, QKeySequence::Save);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", d->mainWindow, &QMainWindow::close, QKeySequence::Quit);

    // View 選單
    QMenu* viewMenu = menuBar->addMenu("&View");
    viewMenu->addAction("&Feature Browser");
    viewMenu->addAction("&Properties");
}

// 新增輔助方法:
void UIManager::onNewDocument() {
    executeCommand("new");
}

void UIManager::onOpenDocument() {
    executeCommand("load");
}

void UIManager::onSaveDocument() {
    executeCommand("save");
}

// ✅ 新增：視圖就緒時的處理
void UIManager::onViewReady() {
    qDebug() << "[UIManager] CadView is ready, initializing reference geometry...";

    // 視圖已經就緒，現在可以初始化參考幾何了
    core::Application* app = core::Application::instance();
    core::DocumentManager* docMgr = app->documentManager();
    cad::Document* doc = docMgr->currentDocument();

    if (doc) {
        initializeReferenceGeometry();

        // 設定初始視圖
        if (d->cadView) {
            d->cadView->setViewType(view::ViewType::Isometric);

            // 延遲一下再執行 fitAll，確保幾何已經顯示
            QTimer::singleShot(100, [this]() {
                if (d->cadView) {
                    d->cadView->fitAll();
                    qDebug() << "[UIManager] Initial view set to Isometric and fitted";
                }
            });
        }
    } else {
        qWarning() << "[UIManager] No document available for reference geometry initialization";
    }
}

// ✅ 新增：初始化參考幾何的方法
void UIManager::initializeReferenceGeometry() {
    if (!d->cadView) {
        qWarning() << "[UIManager] Cannot initialize reference geometry: no CadView";
        return;
    }

    // 取得 AIS 上下文
    Handle(AIS_InteractiveContext) context = d->cadView->context();
    if (context.IsNull()) {
        qWarning() << "[UIManager] Cannot initialize reference geometry: no AIS context";
        return;
    }

    // 取得當前文件
    core::Application* app = core::Application::instance();
    core::DocumentManager* docMgr = app->documentManager();
    cad::Document* doc = docMgr->currentDocument();

    if (!doc) {
        qDebug() << "[UIManager] No current document, skipping reference geometry";
        return;
    }

    // 初始化文件的參考幾何
    doc->initializeOrigin(context);

    // 刷新視圖
    d->cadView->refreshView();

    qDebug() << "[UIManager] Reference geometry initialized for document:"
             << doc->fileName();
}

// ✅ 新增：文件建立時的處理
void UIManager::onDocumentCreated() {
    qDebug() << "[UIManager] Handling document creation...";

    // 如果視圖已經就緒，立即初始化參考幾何
    if (d->cadView && d->cadView->isViewInitialized()) {
        initializeReferenceGeometry();

        // 設定視圖
        if (d->cadView) {
            d->cadView->setViewType(view::ViewType::Isometric);
            QTimer::singleShot(100, [this]() {
                if (d->cadView) {
                    d->cadView->fitAll();
                }
            });
        }
    }
    // 否則等待 viewInitialized 信號
}

// ✅ 當前文件改變時的處理
void UIManager::onCurrentDocumentChanged(cad::Document* doc) {
    qDebug() << "[UIManager] Current document changed:"
             << (doc ? doc->fileName() : "null");

    if (doc && d->cadView && d->cadView->isViewInitialized()) {
        // 為新的當前文件初始化參考幾何（如果還沒有）
        if (doc->referenceGeometries().isEmpty()) {
            initializeReferenceGeometry();
        }

        // 刷新視圖
        d->cadView->refreshView();
        d->cadView->fitAll();
    }
}

} // namespace ui
} // namespace aicad
