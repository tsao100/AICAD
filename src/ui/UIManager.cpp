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
#include "core/CommandLineManager.h"     // ✅ 新增
#include "AutoCompleteModel.h"           // ✅ 新增
#include "command/CommandAlias.h"        // ✅ 新增
#include "command/InputParser.h"
#include "view/ViewManager.h"  // ✅ 添加
#include "view/CadView.h"      // ✅ 添加
#include "view/RubberBand.h"
#include "view/ViewGrid.h"      // ✅ 添加
#include <QShortcut>
#include "CommandLineWidget.h"
#include "CommandInputEdit.h"
#include "TransientCommandHistory.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "core/DocumentManager.h"
#include "core/MenuParser.h"
#include "cad/Document.h"
#include "cad/Sketch.h"
#include "cad/Plane.h"
#include "cad/PlaneManager.h"
#include "cad/Extrude.h"
#include "cad/grips/GripManager.h"
#include "cad/grips/SketchGripProvider.h"
#include "cad/grips/AlignmentGripProvider.h"
#include "ui/GripEventFilter.h"
#include "SketchPanel.h"
#include "ParameterPanel.h"  // Phase 7
#include "command/CommandTypes.h"  // 確保包含完整定義
#include "command/CommandManager.h"
#include "command/LineCommand.h"
#include "command/GripMoveCommand.h"
#include "view/AlignmentRenderer.h"
#include "railway/AlignmentDocument.h"
#include "ui/VAlignEditorDockWidget.h"   // Step 16

#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QTimer>
#include <QtMath>
#include <QDebug>
#include <QUndoStack>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <gp_Ax3.hxx>
#include <gp_Trsf.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp.hxx>
#include "cad/SketchInstance.h"   // Phase 3
#include "cad/ConstraintPickSession.h"  // Point-pick for dimension constraints

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
        , commandLine(nullptr)           // ✅ 新增
        , commandLineManager(nullptr)       // ✅ 新增
        , commandAlias(nullptr)             // ✅ 新增
        , autoCompleteModel(nullptr)        // ✅ 新增
        , undoStack(nullptr)
    {
    }
    
    ~Private() {
        // MainWindow 會自動刪除子元件
        delete mainWindow;
        delete undoStack;
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
    CommandLineWidget* commandLine;
    core::CommandLineManager* commandLineManager;
    command::CommandAlias* commandAlias;
    AutoCompleteModel* autoCompleteModel;
    QUndoStack* undoStack;
    SketchPanel* sketchPanel = nullptr;
    ParameterPanel* parameterPanel = nullptr;  // Phase 7
    cad::ConstraintPickSession* pickSession = nullptr;  // Point-pick for dim constraints

    // ── Railway alignment ──────────────────────────────────────
    railway::AlignmentDocument*  alignmentDoc      = nullptr;
    view::AlignmentRenderer*     alignmentRenderer = nullptr;
    ui::VAlignEditorDockWidget*  vAlignDock        = nullptr;  ///< Step 16
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
                               std::make_unique<cad::SketchGripProvider>(sketch, geomIndices));
                           qDebug() << "[UIManager] Grips attached for sketch:"
                                    << sketch->id() << " -> " << geomIndices;
                       }
                       // future: else if Extrude → ExtrudeGripProvider ...
                   });

    // ── B1) Selection cleared → detach grips ──────────────────────────
    bus->subscribe("selection.cleared", this,
                   [this](const QVariant&) {
                       // 若 grip 正在選取中，先取消以還原幾何，再 detach
                       if (d->gripManager->isGripSelected())
                           d->gripManager->cancelGrip();

                       d->gripManager->detach();
                       qDebug() << "[UIManager] Grips detached (selection cleared)";
                   });

    // ── B2) Alignment element selected → attach AlignmentGripProvider ─────
    // 當使用者在 CadView 中點選任一 Alignment AIS 物件時，CadView 或
    // AlignmentRenderer 發布此事件，UIManager 負責掛載 Provider。
    bus->subscribe("alignment.elementSelected", this,
                   [this](const QVariant&) {
                       if (!d->alignmentDoc) return;

                       // 使 Grip 套用在 XY 平面（Alignment 永遠在 Z=0 平面）
                       d->gripManager->setPlaneAxes(gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
                       d->gripFilter->clearSketchPlane();   // 不使用草圖平面投影

                       d->gripManager->attachProvider(
                           std::make_unique<cad::AlignmentGripProvider>(
                               d->alignmentDoc->horizontal()));

                       // 啟用 Grip 互動
                       if (d->gripFilter) d->gripFilter->setEnabled(true);
                       if (d->gripManager) d->gripManager->setEnabled(true);

                       qDebug() << "[UIManager] AlignmentGripProvider attached";
                   });

    // ── B3) geometry.selected → 偵測是否點選到 Alignment 物件 ────────────
    bus->subscribe("geometry.selected", this,
                   [this](const QVariant& payload) {
                       if (!d->alignmentRenderer) return;
                       const QVariantMap data = payload.toMap();
                       auto* rawPtr = reinterpret_cast<AIS_InteractiveObject*>(
                           data.value("aisObject").value<void*>());
                       if (!rawPtr) return;

                       if (d->alignmentRenderer->containsObject(rawPtr)) {
                           // 重新發布為 alignment.elementSelected
                           core::Application::instance()->eventBus()->publish(
                               "alignment.elementSelected", QVariant{});
                       }
                   });

    // ── C) Document closed / new document → detach grips ─────────────
    connect(docMgr->currentDocument(), &cad::Document::aboutToClose,
            this, [this]() {
                d->gripManager->detach();
            });
    // ── D) command.started → 切換至「繪圖模式」─────────────────────────────
    bus->subscribe(core::Events::COMMAND_STARTED, this,
                   [this](const QVariant&) {
                       // OSnap ON（point 輸入需要 snap）
                       if (d->cadView && d->cadView->snapManager())
                           d->cadView->snapManager()->setSnapEnabled(true);
                       // Grips OFF（讓出滑鼠）
                       if (d->gripFilter) d->gripFilter->setEnabled(false);
                       if (d->gripManager) d->gripManager->setEnabled(false);
                       // 清除殘留的幾何選取高亮
                       if (d->cadView) d->cadView->clearSketchGeomSelection();
                   });

    // ── E) command 結束 → 自動回到「選取模式」────────────────────────────────
    auto onCommandEnd = [this](const QVariant&) {
        if (!d->cadView) return;
        const bool inSketch =
            (d->cadView->mode() == view::InteractionMode::Sketching);
        // OSnap OFF（無 point 需要 snap）
        if (d->cadView->snapManager())
            d->cadView->snapManager()->setSnapEnabled(false);
        // Grips ON（可選取幾何）
        if (inSketch) {
            if (d->gripFilter) d->gripFilter->setEnabled(true);
            if (d->gripManager) d->gripManager->setEnabled(true);
        }
    };
    bus->subscribe(core::Events::COMMAND_EXECUTED,  this, onCommandEnd);
    bus->subscribe(core::Events::COMMAND_CANCELLED, this, onCommandEnd);
    bus->subscribe(core::Events::COMMAND_FAILED,    this, onCommandEnd);

    // ── F) Sketch 進入 → 完整啟動（Grips / OSnap / Selection） ──
    bus->subscribe(core::Events::SKETCH_ENTERED, this,
                   [this](const QVariant&) {
                       // Selection mode → Sketching（允許幾何選取）
                       if (d->cadView)
                           d->cadView->setMode(view::InteractionMode::Sketching);
                       // Grip Filter 啟動
                       if (d->gripFilter) d->gripFilter->setEnabled(true);
                       // 此時 GripManager 的 provider 由 selection.featureSelected 設定
                       if (d->cadView){
                           d->cadView->displayAllFeatures();      // ← ADD
                       }
                   });

    // ESC 取消 PickSession（若正在選點中）
    bus->subscribe(core::Events::POINT_CANCELLED, this,
                   [this](const QVariant&) {
                       if (d->pickSession && d->pickSession->isActive()) {
                           d->pickSession->cancel();
                           if (d->cadView)
                               d->cadView->setMode(view::InteractionMode::Sketching);
                           setStatusMessage(tr("已取消選點"));
                       }
                   });

    // ── G) Sketch 退出 → 完整關閉（Grips / OSnap / Selection） ──
    bus->subscribe(core::Events::SKETCH_EXITED, this,
                   [this](const QVariant&) {
                       // Grip Filter & GripManager 完全關閉
                       if (d->gripFilter) d->gripFilter->setEnabled(false);
                       if (d->gripManager) d->gripManager->detach(); // 清除 provider
                       // OSnap 關閉
                       if (d->cadView && d->cadView->snapManager()) {
                           d->cadView->snapManager()->setSnapEnabled(false);
                           d->cadView->snapManager()->setActivePlane(nullptr);
                           d->cadView->snapManager()->setActiveSketch(nullptr);
                           d->cadView->snapManager()->clearLastInputPoint();
                       }
                       // Selection mode → Idle
                       if (d->cadView)
                           d->cadView->setMode(view::InteractionMode::Idle);
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

        // 在 Private 初始化完成、建立 cadView 之後加入（約 initialize() 函式內）：
        d->undoStack = new QUndoStack(d->mainWindow);   // parent 給 mainWindow 自動清理        // 設置命令列系統（在 CadView 創建後）

        setupCommandLine();

        // ✅ 在這裡呼叫，d->mainWindow 和 d->cadView 都已存在
        setupSketchPanel();

        d->alignmentDoc      = new railway::AlignmentDocument(d->mainWindow);
        d->alignmentRenderer = new view::AlignmentRenderer(d->cadView, d->mainWindow);
        d->alignmentRenderer->setAlignment(d->alignmentDoc->horizontal());
        connect(d->alignmentDoc->horizontal(),
                &railway::HorizontalAlignmentEdit::changed,
                d->alignmentRenderer,
                &view::AlignmentRenderer::refresh);

        // ── Step 16：建立縱斷面 Dock，連接水平↔縱斷面聯動 ─────────────────
        d->vAlignDock = new ui::VAlignEditorDockWidget(d->mainWindow);
        d->vAlignDock->setAlignmentDocument(d->alignmentDoc);
        d->mainWindow->addDockWidget(Qt::BottomDockWidgetArea, d->vAlignDock);
        d->vAlignDock->hide();   // 初始隱藏；PROFILEVIEW 命令顯示

        // ── 縱斷面編輯完成 → 寫回 TCL + 標記文件已修改 ───────────────────
        connect(d->vAlignDock, &ui::VAlignEditorDockWidget::alignmentChanged,
                this, [this, docMgr] {
                    d->vAlignDock->writeBackToTcl();
                    auto* doc = docMgr->currentDocument();
                    if (doc) doc->setModified(true);
                });

        // 建立並加入 OSnap 工具列
        // 改用信號，等 viewer 初始化完畢再建立 toolbar
        connect(d->cadView, &view::CadView::viewInitialized,
                this, [this]() {
                    if (m_snapToolbar) return;  // 避免重複建立
                    if (d->cadView && d->cadView->snapManager()) {
                        m_snapToolbar = new osnap::OSnapToolbar(
                            d->cadView->snapManager(), d->mainWindow);
                        d->mainWindow->addToolBar(Qt::BottomToolBarArea, m_snapToolbar);
                        qDebug() << "[UIManager] OSnapToolbar created after viewInitialized";
                    }
                });

        if (d->cadView) {
            connect(d->cadView, &view::CadView::sketchFinished,
                    this, &UIManager::onSketchEditEnded);
        }

        // ✅ 6. 連接視圖就緒信號，延遲初始化參考幾何
        connect(d->cadView, &view::CadView::viewInitialized,
                this, &UIManager::onViewReady);

        // 在 initialize() 中，ViewManager 初始化後加入：
        connect(app->viewManager(), &view::ViewManager::activeViewChanged,
                this, [this](view::CadView* newView) {
                    d->cadView = newView;
                    // 若正在編輯草圖，同步 snap 設定到新視圖
                    if (newView && newView->snapManager()) {
                        auto* app = core::Application::instance();
                        if (auto* sketch = app->activeSketch()) {
                            newView->snapManager()->setActivePlane(sketch->plane());
                            newView->snapManager()->setActiveSketch(sketch);
                        }
                    }
                });
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
                                if (!doc || !d->cadView) return;

                                d->cadView->setDocument(doc);

                                if (!doc->viewState().isEmpty()) {
                                    // ✅ 有儲存的 viewState：只初始化參考幾何，不 setViewType / fitAll
                                    initializeReferenceGeometry();
                                    // ✅ 等 displayAllFeatures 完成後再還原，用比所有 timer 更晚的時間點
                                    QTimer::singleShot(200, this, [this, doc]() {
                                        if (d->cadView)
                                            d->cadView->restoreViewState(doc->viewState());
                                    });
                                } else {
                                    // ✅ 新文件或無 viewState：走原本流程
                                    onDocumentCreated();
                                }

                                // ✅ 還原 alignment 資料
                                if (d->alignmentDoc && !doc->alignmentData().isEmpty())
                                    d->alignmentDoc->fromJson(doc->alignmentData());
                           });

        bus->subscribe(core::Events::DOCUMENT_CLOSED, this,
            [this](const QVariant& data) {
                qDebug() << "[UIManager] Document closed:" << data.toString();
                updateFeatureTree();
                // ✅ 重設 OSnap 狀態，避免 dangling plane 指標
                if (d->cadView && d->cadView->snapManager()) {
                    d->cadView->snapManager()->setActivePlane(nullptr);
                    d->cadView->snapManager()->setActiveSketch(nullptr);
                    d->cadView->snapManager()->clearLastInputPoint();
                    d->cadView->snapManager()->setSnapEnabled(false);
                }
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
                // ✅ 不在這裡呼叫 displayAllFeatures()，
                //    featureShapeUpdated 信號鏈已統一處理
            });
        
        // ✅ 新增：處理 extrude 完成後的視圖切換請求
        bus->subscribe("command.request-view-setup", this,
                       [this](const QVariant& data) {
                           if (!d->cadView) return;
                           QVariantMap map = data.toMap();
                           QString mode = map["mode"].toString();
                           if (mode == "iso" || mode == "3d") {
                               d->cadView->setMode(view::InteractionMode::Navigation);
                               d->cadView->setViewType(view::ViewType::Isometric);
                               QTimer::singleShot(50, [this]() {
                                   if (d->cadView) d->cadView->fitAll();
                               });
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

        // 在 UIManager::initialize() 的 EventBus 訂閱區段加入：
        bus->subscribe("sketch.created", this, [this](const QVariant& v) {
            QVariantMap data = v.toMap();
            QString sketchId = data["sketchId"].toString();

            cad::Sketch* sketch = data["sketch"].value<cad::Sketch*>();  // ✅ 直接取指標

            if (sketch) {
                onSketchEditStarted(sketch);  // ✅ 補上這個呼叫
            }
        });

        // 同時訂閱草圖結束事件（從 CadView::sketchFinished signal 或 command）
        bus->subscribe("sketch.editEnded", this, [this](const QVariant&) {
            // sketch.editEnded 由 onSketchEditEnded() 自己發布，避免遞迴
            // 此處僅作防禦性處理
        });

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

        // 處理 rubber band 更新請求（RectCommand 互動時）
        bus->subscribe("command.update-rubber-band", this,
                       [this](const QVariant& data) {
                           if (!d->cadView) return;
                           auto* rb = d->cadView->rubberBand();
                           if (!rb) return;
                           QVariantMap map = data.toMap();
                           QString action = map["action"].toString();
                           if (action == "setParams") {
                               // ── Spiral / SCS geometric parameters ────────────────
                               if (map.contains("radius"))
                                   rb->setRadius(map["radius"].toDouble());
                               if (map.contains("spiralLength"))
                                   rb->setSpiralLength(map["spiralLength"].toDouble());
                               if (map.contains("spiralLength1"))
                                   rb->setSpiralLength1(map["spiralLength1"].toDouble());
                               if (map.contains("spiralLength2"))
                                   rb->setSpiralLength2(map["spiralLength2"].toDouble());
                               // ── Spiral type (int encoding of SpiralType enum) ─────
                               if (map.contains("spiralType1"))
                                   rb->setSpiralType1(
                                       static_cast<railway::SpiralType>(map["spiralType1"].toInt()));
                               if (map.contains("spiralType2"))
                                   rb->setSpiralType2(
                                       static_cast<railway::SpiralType>(map["spiralType2"].toInt()));
                               if (map.contains("mode")) {
                                   // Convert string mode to enum
                                   QString modeStr = map["mode"].toString();
                                   if (modeStr == "scs")
                                       rb->setMode(view::RubberBandMode::SCS);
                                   else if (modeStr == "spiral")
                                       rb->setMode(view::RubberBandMode::Spiral);
                               }
                           } else if (action == "clearAndAdd") {
                               rb->clearPoints();
                               QVector2D pt = map["point"].value<QVector2D>();
                               rb->addPoint(pt);
                               rb->update();
                           } else if (action == "addPoint") {
                               QVector2D pt = map["point"].value<QVector2D>();
                               rb->addPoint(pt);
                               rb->update();
                           } else if (action == "setCurrentPoint") {
                               QVector2D pt = map["point"].value<QVector2D>();
                               rb->setCurrentPoint(pt);
                               rb->update();
                           } else if (action == "clear") {
                               rb->clearPoints();
                               rb->clear();
                           }
                       });

        bus->subscribe("command.request-cleanup", this,
                       [this](const QVariant& data) {
                           QVariantMap map = data.toMap();
                           if (map["clearRubberBand"].toBool() && d->cadView) {
                               if (auto* rb = d->cadView->rubberBand()) {
                                   rb->clearPoints();
                                   rb->clear();
                               }
                           }
                       });

        // ✅ Monitor user interactions for debugging/logging
        bus->subscribe(core::Events::POINT_ACQUIRED, this,
                       [](const QVariant& data) {
                           QVariantMap map = data.toMap();
                           QVector2D point = map["point"].value<QVector2D>();
                           qDebug() << "[UIManager] User clicked point:" << point.x() << point.y();
                           // Could update coordinate display here
                       });

        // ── Extrude 建立 ─────────────────────────────────────────────
        bus->subscribe("command.create-extrude", this,
                       [this](const QVariant& data) {
                           QVariantMap map = data.toMap();
                           QString sketchId = map["sketchId"].toString();
                           double height    = map["height"].toDouble();

                           core::Application* app = core::Application::instance();
                           cad::Document* doc = app->documentManager()->currentDocument();
                           if (!doc) return;

                           cad::Feature* feat = doc->findFeature(sketchId);
                           cad::Sketch* sketch = qobject_cast<cad::Sketch*>(feat);
                           if (!sketch) {
                               app->eventBus()->publish(core::Events::COMMAND_ERROR,
                                                        "Sketch not found for extrude");
                               return;
                           }

                           cad::Extrude* extrude = doc->createExtrude(sketch, height);
                           if (!extrude) {
                               app->eventBus()->publish(core::Events::COMMAND_ERROR,
                                                        "Failed to create extrude");
                               return;
                           }

                           // 切回 3D 視圖
                           QVariantMap viewData;
                           viewData["mode"] = "3d";
                           app->eventBus()->publish("command.request-view-setup", viewData);

                           // 通知 FeatureBrowser 刷新
                           QVariantMap featureData;
                           featureData["featureId"]   = extrude->id();
                           featureData["featureName"] = extrude->name();
                           app->eventBus()->publish(core::Events::FEATURE_CREATED, featureData);
                           app->eventBus()->publish(core::Events::COMMAND_PROMPT,
                                                    QString("Extrude '%1' created (h=%2)").arg(extrude->name()).arg(height));

                           app->setActiveSketch(nullptr);
                           QVariantMap viewData1;
                           viewData1["mode"] = "iso";
                           app->eventBus()->publish("command.request-view-setup", viewData1);
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

        bus->subscribe("scripting.osnap-enable", this, [this](const QVariant& v) {
            if (d->cadView && d->cadView->snapManager())
                d->cadView->snapManager()->setSnapEnabled(v.toBool());
        });

        // ── 建構線（Construction Line） ─────────────────────────────────
        bus->subscribe("command.create-sketch-construction-line", this,
                       [this, bus, app](const QVariant& v) {
                           auto data  = v.toMap();
                           auto args  = data["args"].toStringList();
                           auto* sketch = app->activeSketch();
                           if (!sketch || args.size() < 4) return;

                           bool ok[4]; float c[4];
                           for (int i = 0; i < 4; ++i) c[i] = args[i].toFloat(&ok[i]);
                           if (!ok[0]||!ok[1]||!ok[2]||!ok[3]) return;

                           sketch->addConstructionLine(QVector2D(c[0],c[1]), QVector2D(c[2],c[3]));
                           sketch->rebuild();
                           bus->publish(core::Events::FEATURE_UPDATED, sketch->name());
                           setStatusMessage(tr("建構線已加入"), 2000);
                       });

        // ── 中心線（Centerline） ─────────────────────────────────────────
        bus->subscribe("command.create-sketch-centerline", this,
                       [this, bus, app](const QVariant& v) {
                           auto data  = v.toMap();
                           auto args  = data["args"].toStringList();
                           auto* sketch = app->activeSketch();
                           if (!sketch || args.size() < 4) return;

                           bool ok[4]; float c[4];
                           for (int i = 0; i < 4; ++i) c[i] = args[i].toFloat(&ok[i]);
                           if (!ok[0]||!ok[1]||!ok[2]||!ok[3]) return;

                           sketch->addCenterline(QVector2D(c[0],c[1]), QVector2D(c[2],c[3]));
                           sketch->rebuild();
                           bus->publish(core::Events::FEATURE_UPDATED, sketch->name());
                           setStatusMessage(tr("中心線已加入"), 2000);
                       });

        // ── 建構圓（Construction Circle） ────────────────────────────────
        bus->subscribe("command.create-sketch-construction-circle", this,
                       [this, bus, app](const QVariant& v) {
                           auto data  = v.toMap();
                           auto args  = data["args"].toStringList();
                           auto* sketch = app->activeSketch();
                           if (!sketch || args.size() < 3) return;

                           bool ok1,ok2,ok3;
                           float cx = args[0].toFloat(&ok1);
                           float cy = args[1].toFloat(&ok2);
                           float r  = args[2].toFloat(&ok3);
                           if (!ok1||!ok2||!ok3) return;

                           sketch->addConstructionCircle(QVector2D(cx,cy), r);
                           sketch->rebuild();
                           bus->publish(core::Events::FEATURE_UPDATED, sketch->name());
                           setStatusMessage(tr("建構圓已加入"), 2000);
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

        d->gripManager->setView(d->cadView->view());

        // 安裝事件攔截器到 CadView widget
        d->gripFilter = new GripEventFilter(d->gripManager, d->cadView->view(), this);
        d->cadView->installEventFilter(d->gripFilter);
        d->cadView->setGripManager(d->gripManager, d->gripFilter);

        // ── 當使用者選取 Feature 時，掛載對應 provider ───────────────────────
        // 修改後：
        connect(d->featureBrowser, &FeatureBrowser::featureSelectedById,
                this, [this, docMgr](const QString& featureId) {

                    // Feature Browser 選取時只清除舊 grips、顯示屬性
                    // 不在此附加 provider，grips 只在 Sketching mode 幾何被選取時才顯示
                    d->gripManager->detach();

                    cad::Feature* f = docMgr->currentDocument()->findFeature(featureId);
                    if (!f){
                        if (d->propertyPanel) d->propertyPanel->clear();
                        return;
                    }

                    showFeatureProperties(f);
                });

        // ── 編輯草圖 ──────────────────────────────────────────────────────────────
        connect(d->featureBrowser, &FeatureBrowser::editSketchRequested,
                this, [this, docMgr](const QString& featureId) {
                    auto* doc    = docMgr->currentDocument();
                    if (!doc) return;
                    auto* sketch = qobject_cast<cad::Sketch*>(doc->findFeature(featureId));
                    if (!sketch) return;

                    // 1️⃣ 確保 sketch visible（Feature 層）+ AIS 顯示
                    if (!sketch->isVisible()) {
                        sketch->setVisible(true);
                        sketch->displayInContext(doc->aisContext());
                    }

                    // 2️⃣ 開啟格線
                    if (d->cadView)
                        d->cadView->setGridEnabled(true);

                    // 3️⃣ 設定 active sketch、切換模式、OSnap 等（原有邏輯）
                    core::Application::instance()->setActiveSketch(sketch);
                    if (d->cadView)
                        d->cadView->setMode(view::InteractionMode::Sketching);
                    onSketchEditStarted(sketch);
                });

        // ── 刪除特徵 ──────────────────────────────────────────────────────────────
        connect(d->featureBrowser, &FeatureBrowser::deleteFeatureRequested,
                this, [this, docMgr](const QString& featureId) {
                    auto* doc = docMgr->currentDocument();
                    if (!doc) return;
                    auto* feature = doc->findFeature(featureId);
                    if (!feature) return;

                    // 若正在編輯此草圖，先結束草圖模式
                    auto* sketch = qobject_cast<cad::Sketch*>(feature);
                    if (sketch &&
                        core::Application::instance()->activeSketch() == sketch) {
                        onSketchEditEnded();
                        if (d->cadView)
                            d->cadView->setMode(view::InteractionMode::Navigation);
                    }

                    doc->removeFeature(feature);
                    // featureRemoved → FeatureBrowser::onTreeStructureChanged → refresh() 自動執行
                });

        // ── 剖面視圖（toggle）──────────────────────────────────────────────────────
        connect(d->featureBrowser, &FeatureBrowser::sectionViewRequested,
                this, [this, docMgr](const QString& featureId) {
                    if (!d->cadView) return;

                    // 再按一次同一個草圖 → 取消剖面
                    if (d->cadView->isSectionActive()) {
                        d->cadView->clearSectionPlane();
                        setStatusMessage(tr("剖面已取消"), 2000);
                        return;
                    }

                    auto* doc = docMgr->currentDocument();
                    if (!doc) return;
                    auto* sketch = qobject_cast<cad::Sketch*>(
                        doc->findFeature(featureId));
                    if (!sketch || !sketch->plane()) return;

                    d->cadView->setSectionPlane(sketch->plane()->toGpPln());
                    setStatusMessage(
                        tr("剖面：%1（再次右鍵選「剖面」可取消）").arg(sketch->name()), 0);
                });

        // ── TrackCenterLine — editAlignmentRequested ──────────────────────────
        connect(d->featureBrowser, &FeatureBrowser::editAlignmentRequested,
                this, [this, docMgr](const QString& tclId) {
                    auto* doc = docMgr->currentDocument();
                    if (!doc) return;

                    if (tclId.isEmpty()) {
                        // 空 id = 新增線路
                        doc->addTrackCenterLine();
                        return;
                    }

                    auto* tcl = doc->findTrackCenterLine(tclId);
                    if (!tcl) return;

                    d->vAlignDock->loadTrackCenterLine(tcl);
                    d->vAlignDock->show();
                    d->vAlignDock->raise();
                });

        // ── TrackCenterLine — renameTrackRequested ────────────────────────────
        connect(d->featureBrowser, &FeatureBrowser::renameTrackRequested,
                this, [this, docMgr](const QString& tclId) {
                    auto* doc = docMgr->currentDocument();
                    if (!doc) return;
                    auto* tcl = doc->findTrackCenterLine(tclId);
                    if (!tcl) return;

                    bool ok = false;
                    QString newName = QInputDialog::getText(
                        d->mainWindow,
                        tr("重新命名線路"),
                        tr("線路名稱:"),
                        QLineEdit::Normal,
                        tcl->name(),
                        &ok);
                    if (ok && !newName.trimmed().isEmpty()) {
                        tcl->setName(newName.trimmed());
                        doc->setModified(true);
                        Q_EMIT doc->treeStructureChanged();
                    }
                });

        // ── TrackCenterLine — deleteTrackRequested ────────────────────────────
        connect(d->featureBrowser, &FeatureBrowser::deleteTrackRequested,
                this, [this, docMgr](const QString& tclId) {
                    auto* doc = docMgr->currentDocument();
                    if (!doc) return;
                    auto* tcl = doc->findTrackCenterLine(tclId);
                    if (!tcl) return;

                    auto ret = QMessageBox::question(
                        d->mainWindow,
                        tr("刪除線路"),
                        tr("確定要刪除「%1」嗎？").arg(tcl->name()),
                        QMessageBox::Yes | QMessageBox::No,
                        QMessageBox::No);
                    if (ret == QMessageBox::Yes)
                        doc->removeTrackCenterLine(tclId);
                });

        // ── 取消選取時清除 grips ──────────────────────────────────────────────
        // connect(d->cadView->context().get(), &SomeSelectionSignal, this, [this]() {
        //     d->gripManager->detach();
        // });

        // ── 監聽 Grip 拖拉完成（Log / Status bar）────────────────────────────
        // src/ui/UIManager.cpp — 替換 gripDragFinished 連接

        connect(d->gripManager, &GripManager::gripDragFinished,
                this, [this, bus](const QString& id,
                            const gp_Pnt& from, const gp_Pnt& to) {

                    if (from.Distance(to) < Precision::Confusion()) return;  // 無位移，不記錄

                    // 找到 active grip 的 onDrag lambda
                    auto* provider = d->gripManager->currentProvider();
                    if (!provider) return;

                    // 從 computeGrips 找對應 grip 的 onDrag（已套用在幾何上）
                    // 封裝成 GripMoveCommand 推入 undo stack
                    for (const auto& gp : d->gripManager->currentGrips()) {  // ★ 需新增 currentGrips()
                        if (gp.id == id && gp.onDrag) {
                            auto applyFn = gp.onDrag;  // copy lambda
                            auto* cmd = new cad::GripMoveCommand(
                                [applyFn](const gp_Pnt& p){ applyFn(p, false); },
                                id, from, to, provider);
                            d->undoStack->push(cmd);   // ★ 確認 d->undoStack 已初始化
                            break;
                        }
                    }

                    QString msg = QString("Grip '%1' moved Δ(%.2f, %.2f, %.2f)")
                                      .arg(id)
                                      .arg(to.X()-from.X())
                                      .arg(to.Y()-from.Y())
                                      .arg(to.Z()-from.Z());
                    bus->publish(Events::COMMAND_PROMPT, msg);
                });
        // ── Snap 指示（顯示在 status bar）──────────────────────────────────────
        connect(d->gripManager, &GripManager::snapOccurred,
                this, [bus](const SnapResult& s) {
                    if (s.snapped)
                        bus->publish(Events::COMMAND_PROMPT, "Snap: " + s.description);
                });

        initGripSystem();

        // 在 cadView 設定完成後：
        connect(d->cadView, &view::CadView::sketchFinished,
                this, &UIManager::onSketchEditEnded,
                Qt::UniqueConnection);

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
    if (!d->cadView) return;

    d->commandLineManager = core::CommandLineManager::instance();
    d->commandAlias       = command::CommandAlias::instance();

    d->autoCompleteModel  = new AutoCompleteModel(this);
    d->autoCompleteModel->updateFromAlias();

    // ① 建立新命令列 Widget（以 cadView 為 anchor）
    d->commandLine = new CommandLineWidget(d->cadView, d->mainWindow);

    // ② 注入歷程到 CommandInputEdit
    d->commandLine->inputEdit()->setHistory(
        d->commandLineManager->commandHistory());

    // ③ Ctrl+9 快捷鍵
    auto* shortcut9 = new QShortcut(
        QKeySequence(Qt::CTRL | Qt::Key_9), d->mainWindow);
    connect(shortcut9, &QShortcut::activated,
            d->commandLine, &CommandLineWidget::toggleVisible);

    // ④ F2 快捷鍵（等同按▲）
    auto* shortcutF2 = new QShortcut(
        QKeySequence(Qt::Key_F2), d->mainWindow);
    connect(shortcutF2, &QShortcut::activated,
            d->commandLine, &CommandLineWidget::onHistoryButtonClicked);

    connect(d->commandLineManager, &core::CommandLineManager::promptOptionsChanged,
            d->commandLine, &CommandLineWidget::onPromptOptionsChanged);

    // ── 訂閱 VIEW_READY，初次對齊延後到 CadView 真正就緒 ──
    auto* bus = core::Application::instance()->eventBus();
    bus->subscribe(core::Events::VIEW_READY, d->commandLine,
                   [this](const QVariant&) {
                       // VIEW_READY 只需處理一次，對齊後取消訂閱
                       d->commandLine->alignToCadView();
                       d->commandLine->show();
                       // CadView 有 Qt::StrongFocus，把焦點給它即可。
                       // 之後按任意鍵，CadView 的 keyPressEvent 會觸發，
                       // CommandLineWidget 的 eventFilter 也會攔截到，
                       // 命令列自然能接收鍵盤輸入，不需要先點擊 CadView。
                       if (d->cadView)
                           d->cadView->setFocus();
                       auto* bus = core::Application::instance()->eventBus();
                       bus->unsubscribe(core::Events::VIEW_READY, d->commandLine);
                   });

    // 注意：show() 移到 VIEW_READY callback 內，這裡不呼叫
    //d->commandLine->show();
}

// ✅ 新增：連接命令列事件
void UIManager::connectCommandLineEvents() {
    if (!d->commandLine) return;

    auto* bus = core::Application::instance()->eventBus();

    // ── 使用者輸入命令 ───────────────────────────────────────────────
    connect(d->commandLine, &CommandLineWidget::commandSubmitted,
            this, [this](const QString& cmd) {
                // 判斷目前是否正在等待資料輸入（數值、座標、選項等）
                // 若是，則此次 signal 攜帶的是「資料」而非「命令名稱」，
                // 不應更新命令歷程、自動完成計數、recentCommands 等。
                const bool isData = d->commandLineManager->isWaitingForInput();

                if (!isData) {
                    QString resolved = d->commandAlias->resolveAlias(cmd);
                    d->autoCompleteModel->incrementUsage(resolved);
                    // 以 resolved 名稱更新 widget 內的 recentCommands / inputEdit history
                    d->commandLine->recordResolvedCommand(resolved);
                    // CommandLineManager 內部的 addToHistory 也是 resolved
                    d->commandLineManager->executeCommand(resolved);
                } else {
                    // 資料輸入：直接送給 CommandLineManager，不碰任何歷程
                    d->commandLineManager->executeCommand(cmd);
                }
            });

    // ── 使用者選了選項按鈕 ──────────────────────────────────────────
    connect(d->commandLine, &CommandLineWidget::optionSelected,
            d->commandLineManager, &core::CommandLineManager::onOptionSelected);

    connect(d->commandLine->inputEdit(), &CommandInputEdit::escapePressed,
            d->commandLineManager, &core::CommandLineManager::onEscapePressed);

    // ── COMMAND_EXECUTE_REQUEST → CommandManager ──
    bus->subscribe(core::Events::COMMAND_EXECUTE_REQUEST, this,
                   [this](const QVariant& data) {
                       QString cmdName = data.toString();
                       auto* cmdMgr = core::Application::instance()->commandManager();
                       if (!cmdMgr) return;

                       // Build context — fill railway fields so alignment
                       // commands can access the document without a global.
                       command::CommandContext ctx;
                       ctx.alignmentDoc = d->alignmentDoc;
                       ctx.cadView      = d->cadView;
                       // Step 16: profileView now accessible via vAlignDock
                       if (d->vAlignDock)
                           ctx.profileView = d->vAlignDock->profileView();
                       cmdMgr->executeCommand(cmdName, ctx);
                   });

    // ── 命令發出提示（prompt）───────────────────────────────────────
    bus->subscribe(core::Events::COMMAND_PROMPT, this,
                   [this](const QVariant& v) {
                       const QString prompt = v.toString();
                       if (!prompt.isEmpty())
                           d->commandLine->appendHistory(prompt, /*isPrompt=*/true);

                       // ★ 解析 options 並更新 chips
                       auto parsed = command::InputParser::parsePrompt(prompt);
                       d->commandLine->setCommandOptions(parsed.options);

                       // ★ 用 setPromptText 取代 setPlaceholderText
                       d->commandLine->inputEdit()->setPromptText(prompt);
                       d->commandLine->setLastPrompt(prompt);
                   });

    // ── 命令完成 ────────────────────────────────────────────────────
    bus->subscribe(core::Events::COMMAND_EXECUTED, this,
                   [this](const QVariant& v) {
                       // 命令結束 → transient history 淡出
                       d->commandLine->transientHistory()->beginFadeOut();
                       d->commandLine->clearCommandOptions();
                       d->commandLine->inputEdit()->setPlaceholderText(
                           tr("輸入指令或 LISP..."));
                   });

    // ── 命令錯誤 ────────────────────────────────────────────────────
    bus->subscribe(core::Events::COMMAND_ERROR, this,
                   [this](const QVariant& v) {
                       d->commandLine->appendHistory("Error: " + v.toString());
                       d->commandLine->transientHistory()->beginFadeOut();
                   });

    // ── 命令警告 ────────────────────────────────────────────────────
    bus->subscribe(core::Events::COMMAND_WARNING, this,
                   [this](const QVariant& v) {
                       d->commandLine->appendHistory("Warning: " + v.toString());
                   });

    // ── 命令取消（Esc）──────────────────────────────────────────────
    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
                   [this](const QVariant&) {
                       auto* cmdMgr = core::Application::instance()->commandManager();
                       if (cmdMgr) cmdMgr->cancelCurrentCommand();
                       d->commandLine->clearCommandOptions();
                       d->commandLine->inputEdit()->setPlaceholderText(
                           tr("輸入指令或 LISP..."));
                   });

    // ── 選項可用（命令進行中）──────────────────────────────────────
    bus->subscribe(core::Events::OPTIONS_AVAILABLE, this,
                   [this](const QVariant& v) {
                       QStringList opts = v.toStringList();
                        QList<command::InputParser::ParsedOption> parsedOpts;
                       for (const QString& opt : opts) {
                            command::InputParser::ParsedOption po;
                           po.label    = opt;
                           po.shortcut = opt.isEmpty() ? QString() : QString(opt[0].toUpper());
                           parsedOpts.append(po);
                       }
                       d->commandLine->setCommandOptions(parsedOpts);
                   });

    // UIManager.cpp — connectCommandLineEvents() 或 setupSketchPanel() 加入：
    bus->subscribe("command.start-construction-line", this, [this](const QVariant&) {
        core::Application::instance()->commandManager()
            ->executeCommand("construction-line", QStringList{});
    });

    bus->subscribe("command.start-centerline", this, [this](const QVariant&) {
        core::Application::instance()->commandManager()
            ->executeCommand("centerline", QStringList{});
    });    

    bus->subscribe("command.show-sketch-regions", this,
                   [this](const QVariant& data) {
                       QVariantMap map = data.toMap();
                       QString sketchId = map["sketchId"].toString();
                       cad::Document* doc = core::Application::instance()
                                                ->documentManager()->currentDocument();
                       if (!doc) return;
                       cad::Feature* feat = doc->findFeature(sketchId);
                       cad::Sketch* sketch = qobject_cast<cad::Sketch*>(feat);
                       if (!sketch || !d->cadView) return;
                       auto regions = sketch->detectRegions();
                       d->cadView->displaySketchRegions(regions, sketch);
                   });

    bus->subscribe("command.clear-sketch-regions", this,
                   [this](const QVariant&) {
                       if (d->cadView)
                           d->cadView->clearSketchRegions();
                   });
}

// 在 setupDefaultUI() 或 initialize() 中，找到 addDockWidget 的位置後加入：
void UIManager::setupSketchPanel()
{
    d->sketchPanel = new SketchPanel(d->mainWindow);
    d->sketchPanel->setObjectName("SketchPanel");
    d->mainWindow->addDockWidget(Qt::RightDockWidgetArea, d->sketchPanel);
    d->sketchPanel->hide();   // 初始隱藏，進入草圖模式才顯示

    // ── 建構幾何信號 ─────────────────────────────────────────────
    connect(d->sketchPanel, &SketchPanel::requestAddConstructionLine,
            this, [this] {
                auto* sketch = core::Application::instance()->activeSketch();
                if (!sketch) return;
                // 此處觸發互動式 command（與 LineCommand 相似但強制 Construction）
                auto* bus = core::Application::instance()->eventBus();
                bus->publish("command.start-construction-line", QVariant{});
            });

    connect(d->sketchPanel, &SketchPanel::requestAddCenterline,
            this, [this] {
                auto* bus = core::Application::instance()->eventBus();
                bus->publish("command.start-centerline", QVariant{});
            });

    connect(d->sketchPanel, &SketchPanel::requestAddConstructionCircle,
            this, [this] {
                auto* bus = core::Application::instance()->eventBus();
                bus->publish("command.start-construction-circle", QVariant{});
            });

    // ── 約束信號 ─────────────────────────────────────────────────
    connect(d->sketchPanel, &SketchPanel::requestConstraint,
            this, [this](cad::ConstraintType type) {
                auto* sketch = core::Application::instance()->activeSketch();
                if (!sketch) return;
                QStringList selected = d->cadView
                                           ? d->cadView->selectedGeomUuids()
                                           : QStringList{};
                applyConstraintToSketch(sketch, type, selected, 0.0);

                // 清除選取高亮（已在選取模式，OSnap/Grip 狀態由 lifecycle 管理）
                if (d->cadView) d->cadView->clearSketchGeomSelection();
            });

    // requestConstraintWithValue 同樣處理
    connect(d->sketchPanel, &SketchPanel::requestConstraintWithValue,
            this, [this](cad::ConstraintType type, double value) {
                auto* sketch = core::Application::instance()->activeSketch();
                if (!sketch) return;
                QStringList selected = d->cadView
                                           ? d->cadView->selectedGeomUuids()
                                           : QStringList{};
                applyConstraintToSketch(sketch, type, selected, value);

                // 清除選取高亮（已在選取模式，OSnap/Grip 狀態由 lifecycle 管理）
                if (d->cadView) d->cadView->clearSketchGeomSelection();
            });

    // Phase 7：帶 paramExpr + driving 的完整尺寸約束
    connect(d->sketchPanel, &SketchPanel::requestConstraintWithValueAndExpr,
            this, [this](cad::ConstraintType type, double value,
                         const QString& paramExpr, bool driving) {
                auto* sketch = core::Application::instance()->activeSketch();
                if (!sketch) return;

                // 尺寸約束需要讓使用者點選幾何端點
                const bool needsPick =
                    type == cad::ConstraintType::FixedDistance  ||
                    type == cad::ConstraintType::FixedRadius     ||
                    type == cad::ConstraintType::FixedX          ||
                    type == cad::ConstraintType::FixedY          ||
                    type == cad::ConstraintType::FixedAngleDim;

                if (needsPick) {
                    // 若 paramExpr 非空，先在 store 登記
                    double evalVal = value;
                    if (!paramExpr.isEmpty()) {
                        auto [ok, v] = sketch->parameterStore()->evaluate(paramExpr);
                        if (ok) {
                            evalVal = v;
                            if (!sketch->parameterStore()->has(paramExpr))
                                sketch->parameterStore()->setLocal(paramExpr, value);
                        }
                    }

                    // 啟動 PickSession
                    d->pickSession->begin(sketch, type, evalVal, paramExpr, driving);
                    if (d->cadView) {
                        // ✅ GAP 4: 使用 GetGeom mode（可識別 SketchPointAIS）
                        d->cadView->setMode(view::InteractionMode::GetGeom);
                        // 顯示提示
                        setStatusMessage(d->pickSession->promptText());
                    }
                    return;
                }

                // 幾何約束（非尺寸）：沿用原有邏輯
                QStringList selected = d->cadView
                                         ? d->cadView->selectedGeomUuids()
                                         : QStringList{};
                applyConstraintToSketch(sketch, type, selected, value);
                if (d->cadView) d->cadView->clearSketchGeomSelection();
            });

    // Phase 7：清單雙擊 inline 編輯約束
    connect(d->sketchPanel, &SketchPanel::requestEditConstraint,
            this, [this](const QString& uuid, const QString& newExpr,
                         double newValue, bool isLiteralNumber) {
                auto* sketch = core::Application::instance()->activeSketch();
                if (!sketch) return;

                auto& constraints = sketch->constraintsMutable();
                for (auto& c : constraints) {
                    if (c.uuid != uuid) continue;

                    if (isLiteralNumber) {
                        c.value     = newValue;
                        c.paramExpr = QString();
                    } else {
                        // 表達式：向 store 求值
                        auto [ok, v] = sketch->parameterStore()->evaluate(newExpr);
                        if (!ok) {
                            qWarning() << "[UIManager] Cannot evaluate:" << newExpr;
                            return;
                        }
                        c.paramExpr = newExpr;
                        c.value     = v;
                        // 若 store 中尚未有此名稱，自動登記
                        if (!sketch->parameterStore()->has(newExpr))
                            sketch->parameterStore()->setLocal(newExpr, v);
                    }
                    break;
                }

                // 重新求解 + 重建
                sketch->solveConstraints();
                sketch->rebuild();

                // 刷新 ParameterPanel
                if (d->parameterPanel)
                    d->parameterPanel->showMaster(
                        sketch->parameterStore(), sketch->name(), {});

                if (d->cadView) d->cadView->refreshView();
            });

    connect(d->sketchPanel, &SketchPanel::requestRemoveConstraint,
            this, [this](const QString& uuid) {
                auto* sketch = core::Application::instance()->activeSketch();
                if (!sketch) return;
                // removeConstraint 內部已呼叫 solveConstraints() + emit rebuildRequested
                // → Document::rebuildFeature 會自動 erase+rebuild+display
                sketch->removeConstraint(uuid);
                if (d->cadView) d->cadView->refreshView();
            });

    connect(d->sketchPanel, &SketchPanel::requestSolve,
            this, [this] {
                auto* sketch = core::Application::instance()->activeSketch();
                if (!sketch) return;
                // solveConstraints 內部已 emit rebuildRequested → Document 自動 rebuild
                sketch->solveConstraints();
                if (d->cadView) d->cadView->refreshView();
            });

    connect(d->sketchPanel, &SketchPanel::regionDetectionRequested,
            this, [this] {
        Sketch* sketch = currentActiveSketch();  // 取得目前正在編輯的草圖
                if (!sketch) return;

                auto regions = sketch->detectRegions();
                if (regions.isEmpty()) {
                    setStatusMessage(tr("未找到閉合迴路"), 2000);
                    return;
                }

                // 在 3D 視圖中高亮顯示偵測到的區域（使用 TopoDS_Face）
                for (const auto& region : regions) {
                    TopoDS_Face face = buildFaceFromRegion(sketch, region); // 見下方
                    if (!face.IsNull())
                        highlightRegionFace(face, d->cadView->context());  // 自訂高亮函式
                }

                setStatusMessage(
                    tr("偵測到 %1 個區域，點擊選取").arg(regions.size()), 0);
            });

    // 在 UIManager 的 connectSignals 或 setupConnections 中加入：

    // ── region 偵測結果 → 狀態欄 ─────────────────────────────────────
    connect(d->cadView, &view::CadView::sketchRegionsDetected,
            this, [this](const QVector<cad::SketchRegion>& regions) {
                setStatusMessage(
                    tr("偵測到 %1 個區域").arg(regions.size()), 3000);
            });

    // ── region 被選取 → 狀態欄 + 觸發後續動作（如 Extrude profile 設定）
    connect(d->cadView, &view::CadView::sketchRegionPicked,
            this, [this](const cad::SketchRegion& region) {
                setStatusMessage(
                    tr("已選取區域（%1 個邊，%2 個洞）")
                        .arg(region.outerLoop.edgeUuids.size())
                        .arg(region.holes.size()), 0);

                // 將選取的 region 存入 Application，供後續 Extrude 使用
                Application::instance()->setSelectedRegion(region);  // 見下方
            });

    // ── status message from CadView ───────────────────────────────────
    connect(d->cadView, &view::CadView::statusMessageRequested,
            this, &UIManager::setStatusMessage);

    // ── Phase 7：ParameterPanel（透過 MainWindow 懶建立，避免重複 dock）──
    d->parameterPanel = d->mainWindow->parameterPanel();
    // tabify 在 SketchPanel 右側
    d->mainWindow->tabifyDockWidget(d->sketchPanel, d->parameterPanel);
    d->parameterPanel->hide();   // 初始隱藏，進入草圖模式才顯示

    // ── Point-Pick Session：尺寸約束選點狀態機 ────────────────────────
    d->pickSession = new cad::ConstraintPickSession(this);

    // Phase 7：注入 pickSession 到 SketchPanel 以便 Phase 6 slot 使用
    if (d->sketchPanel) {
        d->sketchPanel->setPickSession(d->pickSession);
    }

    // Phase 7：pickSession 收齊選取 → 委派給 SketchPanel::onConstraintReadyFromSession
    // SketchPanel 負責呼叫 Sketch API、求解、UI 更新 (Phase 6)
    connect(d->pickSession, &cad::ConstraintPickSession::constraintReady,
            this, [this](QList<cad::GeomRef> refs,
                         double value, QString paramExpr,
                         bool driving, cad::ConstraintType type) {
        if (d->sketchPanel) {
            // 委派 SketchPanel 處理（Phase 6 slot）
            QMetaObject::invokeMethod(d->sketchPanel,
                "onConstraintReadyFromSession",
                Qt::DirectConnection,
                Q_ARG(QList<cad::GeomRef>, refs),
                Q_ARG(double, value),
                Q_ARG(QString, paramExpr),
                Q_ARG(bool, driving),
                Q_ARG(cad::ConstraintType, type));
        } else {
            // Fallback：直接處理
            auto* sketch = core::Application::instance()->activeSketch();
            if (!sketch) return;
            cad::SketchConstraint c;
            c.type = type; c.refs = refs; c.value = value;
            c.paramExpr = paramExpr; c.driving = driving;
            sketch->addConstraint(c);
            sketch->solveConstraints();
        }
        // 切回 Sketching 模式
        if (d->cadView)
            d->cadView->setMode(view::InteractionMode::Sketching);
        setStatusMessage(tr("約束已施加"));
    });

    // pickSession 結束（含取消）→ 切回 Sketching 模式
    // pickSession 提示變更 → status bar + SketchPanel
    connect(d->pickSession, &cad::ConstraintPickSession::promptChanged,
            this, [this](const QString& text) {
        setStatusMessage(text);
        if (d->sketchPanel) d->sketchPanel->showPickPrompt(text);
    });

    connect(d->pickSession, &cad::ConstraintPickSession::sessionEnded,
            this, [this] {
        if (d->sketchPanel) d->sketchPanel->clearPickPrompt();
        if (!d->pickSession->isActive())
            if (d->cadView)
                d->cadView->setMode(view::InteractionMode::Sketching);
    });
    if (d->cadView) {
        connect(d->cadView, &view::CadView::geomRefPicked,
                this, [this](QVector2D planePt, QString geomUuid, int geomHandle) {
            if (!d->pickSession->isActive()) return;
            d->pickSession->feedPoint(planePt, geomUuid, geomHandle);
            // 更新 status bar 提示
            if (d->pickSession->isActive())
                setStatusMessage(d->pickSession->promptText());
        });

        // ✅ GAP 3: modeChanged — GetGeom 時啟用 SketchPointAIS 的 AIS 選取，離開時停用
        connect(d->cadView, &view::CadView::modeChanged,
                this, [this](view::InteractionMode mode) {
            auto* overlay = d->sketchPanel ? d->sketchPanel->overlay() : nullptr;
            if (!overlay) return;
            auto ctx = d->cadView->context();
            if (ctx.IsNull()) return;

            const bool enterGetGeom = (mode == view::InteractionMode::GetGeom);
            for (auto& ais : overlay->pointAISMap()) {
                if (enterGetGeom)
                    ctx->Activate(ais, 0, Standard_False);   // 啟用 Selection mode 0
                else
                    ctx->Deactivate(ais);
            }
            ctx->UpdateCurrentViewer();
        });

        // ✅ Task E: PlaceDimLine 模式 — 預覽尺寸線位置
        connect(d->cadView, &view::CadView::dimLinePosPreview,
                this, [this](double ox, double oy) {
            if (!d->sketchPanel || !d->sketchPanel->overlay()) return;
            const QString uuid = d->pickSession
                                  ? d->pickSession->pendingConstraintUuid()
                                  : QString();
            if (!uuid.isEmpty())
                d->sketchPanel->overlay()->updateDimLine(uuid, ox, oy);
        });

        // ✅ Task E: PlaceDimLine 模式 — 確認尺寸線位置
        connect(d->cadView, &view::CadView::dimLinePosConfirmed,
                this, [this](double ox, double oy) {
            if (d->pickSession)
                d->pickSession->confirmDimLineOffset(ox, oy);
        });
    }

    // 施加尺寸約束後刷新面板
    connect(d->sketchPanel,
            &SketchPanel::requestConstraintWithValue,
            this, [this](cad::ConstraintType, double) {
        if (Sketch* sk = currentActiveSketch())
            d->parameterPanel->showMaster(
                sk->parameterStore(), sk->name(), {});
    });

    // 尺寸線點擊（SketchPanel 的 slot 已處理，此處轉發給 ParameterPanel）
    connect(d->sketchPanel,
            &SketchPanel::dimensionConstraintClicked,
            this, [this](const QString& uuid,
                         cad::ConstraintOverlayManager::Mode mode,
                         const QString& instanceId) {
        Q_UNUSED(uuid)
        if (mode == cad::ConstraintOverlayManager::Mode::Instance) {
            // 找到對應 instance 並切換 ParameterPanel 顯示
            // （Document 查找留給後續 command layer 實作）
            Q_UNUSED(instanceId)
        }
    });

}

TopoDS_Face buildFaceFromRegion(
    const Sketch* sketch,
    const cad::SketchRegion& region)
{
    // 1. 用 region.outerLoop.edgeUuids 從 sketch 取出對應的 Wire
    BRepBuilderAPI_MakeWire outerWireMaker;
    for (const QString& uuid : region.outerLoop.edgeUuids) {
        // 找到對應 geometry 並轉為 Edge
        for (const SketchGeometry* g : sketch->geometries()) {
            if (g->uuid == uuid) {
                TopoDS_Edge edge = makeEdgeFromGeometry(sketch, g);
                if (!edge.IsNull())
                    outerWireMaker.Add(edge);
            }
        }
    }
    if (!outerWireMaker.IsDone()) return TopoDS_Face();

    BRepBuilderAPI_MakeFace faceMaker(outerWireMaker.Wire(),
                                      /*onlyplane=*/Standard_True);

    // 2. 逐一加入 hole
    for (const auto& hole : region.holes) {
        BRepBuilderAPI_MakeWire holeWireMaker;
        for (const QString& uuid : hole.edgeUuids) {
            for (const SketchGeometry* g : sketch->geometries()) {
                if (g->uuid == uuid) {
                    TopoDS_Edge edge = makeEdgeFromGeometry(sketch, g);
                    if (!edge.IsNull())
                        holeWireMaker.Add(edge);
                }
            }
        }
        if (holeWireMaker.IsDone())
            faceMaker.Add(holeWireMaker.Wire());
    }

    return faceMaker.IsDone() ? faceMaker.Face() : TopoDS_Face();
}

void highlightRegionFace(const TopoDS_Face& face,
                                const Handle(AIS_InteractiveContext)& context)
{
    Handle(AIS_Shape) aisFace = new AIS_Shape(face);

    aisFace->SetColor(Quantity_NOC_YELLOW);
    aisFace->SetTransparency(0.5);

    context->Display(aisFace, Standard_True);
    context->Deactivate(aisFace);
}

TopoDS_Edge makeEdgeFromGeometry(
    const aicad::cad::Sketch* sketch,
    const aicad::cad::SketchGeometry* g)
{
    using namespace aicad::cad;

    if (!g) return TopoDS_Edge();

    switch (g->type)
    {
    case SketchGeometryType::Line:
    {
        auto geom = static_cast<const SketchLine*>(g);
        QVector3D p1 = sketch->planeToWorld(geom->points[0]);
        QVector3D p2 = sketch->planeToWorld(geom->points[1]);


        return BRepBuilderAPI_MakeEdge(
            gp_Pnt(p1.x(), p1.y(), p1.z()),
            gp_Pnt(p2.x(), p2.y(), p2.z()));
    }

    case SketchGeometryType::Arc:
    {
        auto arc = static_cast<const SketchArc*>(g);
        QVector3D p1 = sketch->planeToWorld(arc->points[0]);
        QVector3D p2 = sketch->planeToWorld(arc->points[1]);
        QVector3D p3 = sketch->planeToWorld(arc->points[2]);

        gp_Pnt _p1 = gp_Pnt(p1.x(), p1.y(), p1.z());
        gp_Pnt _p2 = gp_Pnt(p2.x(), p2.y(), p2.z());
        gp_Pnt _p3 = gp_Pnt(p3.x(), p3.y(), p3.z());

        GC_MakeArcOfCircle arcMaker(_p1, _p2, _p3);
        if (!arcMaker.IsDone())
            return TopoDS_Edge();

        return BRepBuilderAPI_MakeEdge(arcMaker.Value());
    }

    default:
        return TopoDS_Edge();
    }
}

// 在 applyConstraintToSketch 前，新增輔助函式：

// 取得幾何元素的端點列表（2D 草圖座標）
static QVector<QPair<QVector2D, GeomHandle>>
geomEndpoints(const cad::SketchGeometry* g)
{
    using GH = cad::GeomHandle;
    QVector<QPair<QVector2D, GH>> pts;
    if (!g) return pts;
    switch (g->type) {
    case cad::SketchGeometryType::Line:
        pts << qMakePair(g->points[0], GH::Start)
            << qMakePair(g->points[1], GH::End);
        break;
    case cad::SketchGeometryType::Arc:
        if (g->points.size() >= 2) {
            pts << qMakePair(g->points[0], GH::Start)
                << qMakePair(g->points.last(), GH::End);
        }
        break;
    default:
        if (!g->points.isEmpty()) {
            pts << qMakePair(g->points.first(), GH::Start)
                << qMakePair(g->points.last(),  GH::End);
        }
        break;
    }
    return pts;
}

// 找兩幾何距離最近的端點對
static QPair<GeomHandle, GeomHandle>
closestEndpointPair(const cad::SketchGeometry* gA,
                    const cad::SketchGeometry* gB)
{
    using GH = cad::GeomHandle;
    auto ptsA = geomEndpoints(gA);
    auto ptsB = geomEndpoints(gB);

    GeomHandle bestA = GH::End, bestB = GH::Start;
    float bestDist = std::numeric_limits<float>::max();
    for (auto& [posA, hA] : ptsA) {
        for (auto& [posB, hB] : ptsB) {
            float d = (posA - posB).lengthSquared();
            if (d < bestDist) { bestDist = d; bestA = hA; bestB = hB; }
        }
    }
    return {bestA, bestB};
}

// 在 UIManager.cpp 加入此 private 方法（同時在 UIManager.h private 宣告）：
void UIManager::applyConstraintToSketch(cad::Sketch* sketch,
                                        cad::ConstraintType type,
                                        const QStringList& selected,
                                        double value)
{
    using CT = cad::ConstraintType;
    using GH = cad::GeomHandle;

    QString uuid;
    bool needsTwo = (type == CT::Parallel || type == CT::Perpendicular
                     || type == CT::Tangent  || type == CT::EqualLength
                     || type == CT::EqualRadius || type == CT::Concentric
                     || type == CT::Coincident  || type == CT::FixedDistance);

    if (needsTwo && selected.size() < 2) {
        showCommandMessage(tr("請先選取兩個幾何元素，再套用此約束"), "orange");
        return;
    }
    if (!needsTwo && selected.isEmpty()) {
        showCommandMessage(tr("請先選取一個幾何元素，再套用此約束"), "orange");
        return;
    }

    switch (type) {
    case CT::Coincident: {
        auto* gA = sketch->findGeometry(selected[0]);
        auto* gB = sketch->findGeometry(selected[1]);
        bool aCircular = gA && (gA->type == cad::SketchGeometryType::Circle
                                || gA->type == cad::SketchGeometryType::Arc);
        bool bCircular = gB && (gB->type == cad::SketchGeometryType::Circle
                                || gB->type == cad::SketchGeometryType::Arc);

        if (aCircular && bCircular) {
            // 兩個圓/弧 → 同心
            uuid = sketch->constrainConcentric(selected[0], selected[1]);
        } else if (aCircular || bCircular) {
            // 一個圓弧 + 一個線 → Center 對 Start/End
            auto* circGeom = aCircular ? gA : gB;
            auto* lineGeom = aCircular ? gB : gA;
            auto linePts = geomEndpoints(lineGeom);
            QVector2D circCenter = circGeom->points.isEmpty()
                                       ? QVector2D(0,0) : circGeom->points[0]; // circle stores center in points[0]? depends
            GH lineHandle = GH::Start;
            float best = std::numeric_limits<float>::max();
            for (auto& [pos, h] : linePts) {
                float d = (pos - circCenter).lengthSquared();
                if (d < best) { best = d; lineHandle = h; }
            }
            QString circUuid = aCircular ? selected[0] : selected[1];
            QString lineUuid = aCircular ? selected[1] : selected[0];
            GH lineH  = aCircular ? lineHandle : lineHandle;
            uuid = sketch->constrainCoincident(
                cad::GeomRef(circUuid, GH::Center),
                cad::GeomRef(lineUuid, lineH));
        } else {
            // 兩條線 → 找最近端點對
            auto [hA, hB] = closestEndpointPair(gA, gB);
            uuid = sketch->constrainCoincident(
                cad::GeomRef(selected[0], hA),
                cad::GeomRef(selected[1], hB));
        }
        break;
    }
    case CT::Horizontal:    uuid = sketch->constrainHorizontal(selected[0]);             break;
    case CT::Vertical:      uuid = sketch->constrainVertical(selected[0]);               break;
    case CT::Parallel:      uuid = sketch->constrainParallel(selected[0], selected[1]);  break;
    case CT::Perpendicular: uuid = sketch->constrainPerpendicular(selected[0], selected[1]); break;
    case CT::Tangent:       uuid = sketch->constrainTangent(selected[0], selected[1]);   break;
    case CT::EqualLength:   uuid = sketch->constrainEqualLength(selected[0], selected[1]); break;
    case CT::EqualRadius:   uuid = sketch->constrainEqualRadius(selected[0], selected[1]); break;
    case CT::Concentric:    uuid = sketch->constrainConcentric(selected[0], selected[1]); break;
    case CT::Fixed:         uuid = sketch->constrainFixed(selected[0]);                  break;
    case CT::FixedDistance:
        uuid = sketch->constrainDistance(
            cad::GeomRef(selected[0], GH::End),
            cad::GeomRef(selected[1], GH::Start), value);
        break;
    case CT::FixedRadius:   uuid = sketch->constrainRadius(selected[0], value);          break;
    case CT::FixedAngleDim:
        // ✅ 角度約束尚未實作方程式，顯示提示
        showCommandMessage(tr("角度約束尚未實作"), "orange");
        return;
    default:
        showCommandMessage(tr("此約束尚未支援"), "red");
        return;
    }

    if (uuid.isEmpty()) {
        showCommandMessage(tr("約束加入失敗（可能已過度約束）"), "red");
        return;
    }

    // ✅ addConstraint 內部已自動呼叫 solveConstraints() + emit rebuildRequested
    //    → Document::rebuildFeature → erase + rebuild + display + UpdateCurrentViewer
    //    不需要再呼叫 sketch->solveConstraints() 或 sketch->rebuild()
    if (d->cadView) d->cadView->refreshView();

    // 從最近一次 solve 結果取得 DOF（透過 SketchPanel 的 onConstraintSolved slot）
    int dof = sketch->degreesOfFreedom();
    QString msg = tr("約束已加入 [DOF: %1]").arg(dof);
    showCommandMessage(msg, dof == 0 ? "lime" : "cyan");
}

void UIManager::showFeatureProperties(cad::Feature* feature) {
    auto* panel = d->propertyPanel;
    if (!panel) return;

    panel->clear();
    if (!feature) return;

    // ── 共用屬性 ──────────────────────────────────────────
    panel->addProperty(tr("名稱"),     feature->name(),       /*editable*/true);
    panel->addProperty(tr("類型"),     feature->typeString(), false);
    panel->addProperty(tr("ID"),       feature->id(),         false);
    panel->addProperty(tr("可見"),     feature->isVisible() ? tr("是") : tr("否"), false);
    panel->addProperty(tr("抑制"),     feature->isSuppressed() ? tr("是") : tr("否"), false);
    if (feature->hasError())
        panel->addProperty(tr("錯誤"), feature->errorMessage(), false);

    // ── Sketch 專屬 ────────────────────────────────────────
    if (auto* sketch = qobject_cast<cad::Sketch*>(feature)) {
        panel->addProperty(tr("平面"),
                           sketch->plane() ? sketch->plane()->name() : tr("(無)"), false);
        panel->addProperty(tr("幾何數"),
                           QString::number(sketch->geometryCount()), false);
        panel->addProperty(tr("約束數"),
                           QString::number(sketch->constraints().size()), false);
        panel->addProperty(tr("自由度"),
                           QString::number(sketch->degreesOfFreedom()), false);
        return;
    }

    // ── Extrude 專屬 ───────────────────────────────────────
    if (auto* extrude = qobject_cast<cad::Extrude*>(feature)) {
        panel->addProperty(tr("草圖"),
                           extrude->sketch() ? extrude->sketch()->name() : tr("(無)"), false);
        // 顯示表達式（若有）否則顯示數值
        const QString heightExpr = extrude->heightExpression();
        panel->addProperty(tr("高度"),
                           heightExpr.isEmpty()
                               ? QString::number(extrude->height())
                               : heightExpr,
                           /*editable*/true);
        return;
    }

    // ── Plane 專屬 ─────────────────────────────────────────
    if (auto* plane = qobject_cast<cad::Plane*>(feature)) {
        const auto& o = plane->origin();
        const auto& n = plane->normal();
        panel->addProperty(tr("原點"),
                           QString("(%1, %2, %3)")
                               .arg(o.x(), 0, 'f', 3)
                               .arg(o.y(), 0, 'f', 3)
                               .arg(o.z(), 0, 'f', 3), false);
        panel->addProperty(tr("法向量"),
                           QString("(%1, %2, %3)")
                               .arg(n.x(), 0, 'f', 3)
                               .arg(n.y(), 0, 'f', 3)
                               .arg(n.z(), 0, 'f', 3), false);
        return;
    }
}

// ===== 新增的公開方法 =====

CommandLineWidget* UIManager::commandLine() const {
    return d->commandLine;
}

core::CommandLineManager* UIManager::commandLineManager() const {
    return d->commandLineManager;
}

command::CommandAlias* UIManager::commandAlias() const {
    return d->commandAlias;
}

void UIManager::showCommandMessage(const QString& message, const QString& color) {
    if (d->commandLine && !message.isEmpty())
        d->commandLine->appendHistory(message);
}

void UIManager::showCommandError(const QString& error) {
    if (d->commandLine)
        d->commandLine->appendHistory("Error: " + error);
}

void UIManager::showCommandWarning(const QString& warning) {
    if (d->commandLine)
        d->commandLine->appendHistory("Warning: " + warning);
}

cad::Sketch* UIManager::currentActiveSketch(){
    return m_currentActiveSketch;
}

QUndoStack* UIManager::undoStack() const { return d->undoStack; }

railway::AlignmentDocument* UIManager::alignmentDocument() const
{
    return d->alignmentDoc;
}

ui::VAlignEditorDockWidget* UIManager::vAlignDockWidget() const
{
    return d->vAlignDock;
}

void UIManager::showMainWindow() {
    if (!d->mainWindow) {
        qWarning() << "[UIManager] MainWindow not created";
        return;
    }
    
    qDebug() << "[UIManager] Showing MainWindow";
    d->mainWindow->show();
    //d->mainWindow->showFullScreen();
}

void UIManager::onSketchEditStarted(Sketch* sketch)
{
    if (!sketch) return;
    m_currentActiveSketch = sketch;

    auto* bus = core::Application::instance()->eventBus();

    // OSnap 平面設定
    if (d->cadView && d->cadView->snapManager()) {
        d->cadView->snapManager()->setActivePlane(sketch->plane());
        d->cadView->snapManager()->setActiveSketch(sketch);
        d->cadView->snapManager()->setSnapEnabled(false);
    }

    // GripFilter / GripManager 平面軸向
    if (sketch->plane()) {
        QVector3D qx = sketch->plane()->xAxis();
        QVector3D qy = sketch->plane()->yAxis();

        if (d->gripFilter)
            d->gripFilter->setSketchPlane(sketch->plane());

        if (d->gripManager)
            d->gripManager->setPlaneAxes(
                gp_Dir(qx.x(), qx.y(), qx.z()),
                gp_Dir(qy.x(), qy.y(), qy.z()));

        cad::PlaneManager::instance()->setActivePlane(sketch->plane());
        d->cadView->setViewType(d->cadView->viewType());

        // ✅ 正視於 Sketch Plane，並顯示格線（仿 ViewManager::onSketchCreated）
        cad::Plane* gridPlane = sketch->plane();
        d->cadView->alignToPlane(gridPlane);

        if (gridPlane->isXY()) {
            d->cadView->setTopView();
        } else if (gridPlane->isXZ()) {
            d->cadView->setFrontView();
        } else if (gridPlane->isYZ()) {
            d->cadView->setRightView();
        }
        // 自訂平面：alignToPlane 已對齊，不需額外設定標準視角

        view::ViewGrid* grid = d->cadView->grid();
        if (grid)
            grid->setPlane(gridPlane);

        d->cadView->setGridEnabled(true);
        d->cadView->fitAll();
    }

    // SketchPanel
    if (d->sketchPanel) {
        d->sketchPanel->setActiveSketch(sketch);
        d->sketchPanel->show();
        d->sketchPanel->raise();

        // Phase 6：約束覆蓋符號 — 建構 sketch→world Trsf
        if (d->cadView && sketch->plane()) {
            auto* plane = sketch->plane();
            QVector3D o  = plane->origin();
            QVector3D xa = plane->xAxis();
            QVector3D ya = plane->yAxis();
            QVector3D n  = plane->normal();

            gp_Ax3 ax3(
                gp_Pnt(o.x(),  o.y(),  o.z()),
                gp_Dir(n.x(),  n.y(),  n.z()),
                gp_Dir(xa.x(), xa.y(), xa.z()));
            gp_Trsf toWorld;
            toWorld.SetTransformation(ax3, gp::XOY());

            d->sketchPanel->enterSketchMode(sketch, d->cadView->context(), toWorld);
        }
    }

    // Phase 7：ParameterPanel 顯示 master 參數
    if (d->parameterPanel) {
        d->parameterPanel->showMaster(
            sketch->parameterStore(), sketch->name(), {});
        d->parameterPanel->show();
        d->parameterPanel->raise();
    } else if (d->mainWindow) {
        // 懶建立（若 UIManager 繞過 setupSketchPanel 路徑）
        ParameterPanel* pp = d->mainWindow->parameterPanel();
        pp->showMaster(sketch->parameterStore(), sketch->name(), {});
        pp->show();
        pp->raise();
    }

    bus->publish(core::Events::SKETCH_ENTERED, QVariant::fromValue(sketch));
    bus->publish("sketch.editStarted", QVariant::fromValue(sketch));
}

void UIManager::onSketchEditEnded()
{
    m_currentActiveSketch = nullptr;

    auto* bus = core::Application::instance()->eventBus();

    // SketchPanel + overlay 清除
    if (d->sketchPanel) {
        d->sketchPanel->clearSketch();
        d->sketchPanel->exitOverlayMode();   // Phase 6
        d->sketchPanel->hide();
    }

    // Phase 7：ParameterPanel 清空
    ParameterPanel* pp = d->parameterPanel
                       ? d->parameterPanel
                       : (d->mainWindow ? d->mainWindow->parameterPanel() : nullptr);
    if (pp) pp->clearPanel();

    // 統一發布 SKETCH_EXITED → 由 initGripSystem 的訂閱執行完整清理
    bus->publish(core::Events::SKETCH_EXITED, QVariant{});
    bus->publish("sketch.editEnded", QVariant{});  // 保留向下相容
}

MainWindow* UIManager::mainWindow() const {
    return d->mainWindow;
}

void UIManager::showInstanceInParameterPanel(cad::SketchInstance* instance) {
    ParameterPanel* pp = d->parameterPanel
                       ? d->parameterPanel
                       : (d->mainWindow ? d->mainWindow->parameterPanel() : nullptr);
    if (!pp || !instance) return;
    pp->showInstance(instance);
    pp->show();
    pp->raise();
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
    if (d->mainWindow)
        d->mainWindow->statusBar()->showMessage(message, timeout);

    if (d->commandLine && !message.isEmpty())
        d->commandLine->appendHistory(message);
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

                // 設定快捷鍵（純字母快捷鍵跳過，避免攔截命令列輸入）
                auto isSingleLetter = [](const QString& s) {
                    return s.length() == 1 && s[0].isLetter();
                };
                if (!item.shortcut.isEmpty() && !isSingleLetter(item.shortcut)) {
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

                // 設定快捷鍵（純字母快捷鍵跳過，避免攔截命令列輸入）
                auto isSingleLetter2 = [](const QString& s) {
                    return s.length() == 1 && s[0].isLetter();
                };
                if (!item.shortcut.isEmpty() && !isSingleLetter2(item.shortcut)) {
                    action->setShortcut(QKeySequence(item.shortcut));
                }

                // 設定工具提示
                QString tooltip = item.label;
                if (!item.shortcut.isEmpty() && !isSingleLetter2(item.shortcut)) {
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
    if (commandId.isEmpty()) return;

    QString resolved = d->commandAlias
                           ? d->commandAlias->resolveAlias(commandId)
                           : commandId;

    if (d->commandLine) {
        // 走 onInputSubmit 完整路徑：
        // → 更新 recentCommands / recentMenu
        // → m_inputEdit->addToHistory()   ← ↑/↓ 鍵有效
        // → appendHistory()               ← 歷程區顯示
        // → emit commandSubmitted()       ← 觸發 connectCommandLineEvents slot
        //       → alias resolve → commandLineManager->executeCommand()
        d->commandLine->submitCommand(resolved);
    } else {
        // fallback：CommandLine 尚未建立（極早期呼叫）
        if (d->commandLineManager)
            d->commandLineManager->executeCommand(resolved);
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

    // ★ 修正：在這裡才做 setSnapManager，因為 snapManager 是 initializeViewer() 裡建立的
    if (d->gripManager && d->cadView && d->cadView->snapManager()) {
        d->gripManager->setSnapManager(d->cadView->snapManager());
        qDebug() << "[UIManager] GripManager::setSnapManager connected";
    }

    // ✅ 現在 view/context 已就緒，補上 GripManager/Filter 初始化
    if (d->gripManager && d->cadView)
        d->gripManager->setContext(d->cadView->context());
    if (d->gripFilter && d->cadView)
        d->gripFilter->setView(d->cadView->view());

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
    if (doc && d->cadView && d->cadView->isViewInitialized()) {
        if (doc->referenceGeometries().isEmpty())
            initializeReferenceGeometry();

        d->cadView->refreshView();

        // ✅ 有 viewState 時不 fitAll，由 DOCUMENT_OPENED 的 timer 負責還原
        if (doc->viewState().isEmpty())
            d->cadView->fitAll();
    }

    if (d->propertyPanel)
        d->propertyPanel->clear();
}

cad::ConstraintPickSession* UIManager::constraintPickSession() const
{
    return d->pickSession;
}

} // namespace ui
} // namespace aicad