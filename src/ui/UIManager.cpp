/**
 * @file UIManager.cpp
 * @brief UIManager 類別實作
 * @author James
 * @date 2025-01-07
 */

#include "UIManager.h"
#include <cmath>
#include <QPointer>
#include <QJsonObject>
#include <QAction>
#include <QIcon>
#include <QKeySequence>
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
#include <QKeyEvent>
#include "CommandLineWidget.h"
#include "CommandInputEdit.h"
#include "TransientCommandHistory.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "core/DocumentManager.h"
#include "core/MenuParser.h"
#include "cad/Document.h"
#include "cad/Sketch.h"
#include "cad/sketch/AnnotationStandardsChecker.h"
#include "cad/sketch/SketchPointAIS.h"
#include "cad/Plane.h"
#include "cad/PlaneManager.h"
#include "cad/Extrude.h"
#include "cad/AlignedProfileArray.h"
#include "cad/ProfileLoftSolid.h"
#include "cad/grips/GripManager.h"
#include "cad/grips/SketchGripProvider.h"
#include "cad/grips/AlignmentGripProvider.h"
#include "ui/GripEventFilter.h"
#include "SketchPanel.h"
#include "ParameterPanel.h"  // Phase 7
#include "command/CommandTypes.h"  // 確保包含完整定義
#include "command/CommandManager.h"
#include "command/SketchEditCommand.h"
#include "command/GripMoveCommand.h"
#include "command/ConstraintCommands.h"
#include "view/AlignmentRenderer.h"
#include "view/Railway3DAlignmentRenderer.h"
#include "railway/AlignmentDocument.h"
#include "core/geometry/ProjectOrigin.h"
#include "railway/RailwayAlignment.h"
#include "ui/VAlignEditorDockWidget.h"   // Step 16

#include <QMenu>
#include <QMenuBar>
#include <QPointF>
#include <QRegularExpression>
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
#include "ui/AlignmentDataTableDialog.h"  // 線形資料表對話框
#include "ui/ProfileArrayStationTableDialog.h"  // Step 8：站位資料表對話框
#include "cad/AlignedProfileArray.h"

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
    // GDIM v2 Phase 4：PropertyPanel 目前正在顯示哪個標註（Mini Toolbar），
    // 供 propertyChanged 寫回時判斷目標；為空字串代表目前顯示的是
    // Feature 屬性（showFeatureProperties），而非標註屬性。
    cad::Sketch* propAnnotationSketch = nullptr;
    QString      propAnnotationUuid;
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
    railway::AlignmentDocument*  alignmentDoc      = nullptr;  ///< active edit session
    QString                      activeTclId;                   ///< H-Alignment edit 中的 TCL id
    ui::VAlignEditorDockWidget*  vAlignDock        = nullptr;  ///< Step 16

    /// Per-TCL AlignmentDocument instances (key = tcl->id())
    QHash<QString, railway::AlignmentDocument*> tclAlignmentDocs;

    /// Per-TCL renderers for 3D visibility (key = tcl->id())
    QHash<QString, view::AlignmentRenderer*> tclRenderers;

    /// Railway 資料夾節點 eyeOpen 觸發的「全部線路彙總 3D Alignment」顯示器
    view::Railway3DAlignmentRenderer* railway3DRenderer = nullptr;

    /// B.3 TM2 座標原點是否已由使用者設定（若否，進入 alignment edit 時自動套用預設值）
    bool originSet = false;

    /// Alignment 編輯模式下，目前於 CadView 中被點選的元素（供 Delete 鍵刪除用）。
    /// -1 = 尚未選取任何元素。索引對應 selectedAlignRenderer 所依附之
    /// HorizontalAlignmentEdit::elements()（即 m_elems）。
    int selectedAlignElemIdx = -1;
    view::AlignmentRenderer* selectedAlignRenderer = nullptr;

    // ── LineCommand 連續線段自動重合束制 ──────────────────────────────
    /// 目前是否處於一次連續畫線（LINE 指令）的過程中
    bool        lineChainActive = false;
    /// 本次連續畫線第一段的 SketchLine UUID（用於封閉迴路時的重合束制）
    QString     lineChainFirstLineUuid;
    /// 第一段的起點座標（用於判斷最後一點是否與起點「很接近」）
    QVector2D   lineChainFirstStartPos;
    /// 上一段的 SketchLine UUID（用於段與段之間的重合束制）
    QString     lineChainPrevLineUuid;
    /// 上一段的終點座標（用於判斷本段起點是否緊接上一段終點）
    QVector2D   lineChainPrevEndPos;

    // ── Sketch 通用快照式 undo/redo（掛在 CommandManager 訊號上）────────
    /// 目前執行中的 CommandManager 指令開始時，若有作用中的 Sketch，
    /// 是否已捕捉「操作前」快照（見 UIManager::initialize() 內
    /// 「Sketch 通用 undo/redo」區塊）
    bool                    sketchEditPending = false;
    /// 操作前快照所屬的 Sketch（用於操作後比對是否切換了作用中草圖）
    QPointer<cad::Sketch>   sketchEditTargetSketch;
    /// 操作前的 JSON 快照
    QJsonObject             sketchEditBeforeSnapshot;
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
                    QSet<int> geomIndices;
                    for (const QVariant& idx : v.toMap()["geomIndices"].toList())
                        geomIndices.insert(idx.toInt());

        cad::Feature* feature = docMgr->currentDocument()->findFeature(featureId);
                       if (!feature) return;

                       if (auto* sketch = qobject_cast<cad::Sketch*>(feature)) {
                           cad::Plane* plane = sketch->plane();
                           QVector3D qx = plane->xAxis();
                           QVector3D qy = plane->yAxis();

                           d->gripManager->setPlaneAxes(
                               gp_Dir(qx.x(), qx.y(), qx.z()),
                               gp_Dir(qy.x(), qy.y(), qy.z()));
                           d->gripFilter->setSketchPlane(plane);

                           // ── 累加模式：合併新舊 geomIndices，不 detach ─────────────
                           // 取得目前 provider 已有的 indices（若有）
                           QSet<int> merged = geomIndices;
                           if (auto* existing = dynamic_cast<cad::SketchGripProvider*>(
                                   d->gripManager->currentProvider())) {
                               merged |= existing->geomIndices();
                           }

                           // detach 舊的，attach 合併後的
                           d->gripManager->detach();
                           d->gripManager->attachProvider(
                               std::make_unique<cad::SketchGripProvider>(sketch, merged));
                           qDebug() << "[UIManager] Grips attached for sketch:"
                                    << sketch->id() << " -> " << merged;
                       }
                   });

    // ── B1) Selection cleared → detach grips ──────────────────────────
    bus->subscribe("selection.cleared", this,
                   [this](const QVariant&) {
                       // 若 grip 正在選取中，先取消以還原幾何，再 detach
                       if (d->gripManager->isGripSelected())
                           d->gripManager->cancelGrip();

                       d->gripManager->detach();
                       d->selectedAlignRenderer = nullptr;
                       d->selectedAlignElemIdx  = -1;
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
                       const QVariantMap data = payload.toMap();
                       auto* rawPtr = reinterpret_cast<AIS_InteractiveObject*>(
                           data.value("aisObject").value<void*>());
                       if (!rawPtr) return;
                       // Check all per-TCL renderers
                       for (auto* r : d->tclRenderers) {
                           if (r->containsObject(rawPtr)) {
                               // 記錄被點選到的具體元素（供 Delete 鍵刪除用）；
                               // editableIndexForObject() 找不到對應元素時
                               // （例如點到 PI grip 球體）回傳 -1，仍視為
                               // 「已點選到 Alignment 物件」但沒有可刪除的
                               // 具體元素。
                               d->selectedAlignRenderer = r;
                               d->selectedAlignElemIdx  = r->editableIndexForObject(rawPtr);

                               core::Application::instance()->eventBus()->publish(
                                   "alignment.elementSelected", QVariant{});
                               return;
                           }
                       }
                       // 點選到的不是任何 Alignment overlay → 清除殘留的選取記錄，
                       // 避免 Delete 鍵誤刪上一次點選到的 Alignment 元素。
                       d->selectedAlignRenderer = nullptr;
                       d->selectedAlignElemIdx  = -1;
                   });

    // ── C) Document closed / new document → detach grips ─────────────
    connect(docMgr->currentDocument(), &cad::Document::aboutToClose,
            this, [this]() {
                d->gripManager->detach();
            });
    // ── D) command.started → 切換至「繪圖模式」─────────────────────────────
    bus->subscribe(core::Events::COMMAND_STARTED, this,
                   [this](const QVariant& v) {
                       // OSnap ON（point 輸入需要 snap）
                       if (d->cadView && d->cadView->snapManager())
                           d->cadView->snapManager()->setSnapEnabled(true);
                       // Grips OFF（讓出滑鼠）
                       if (d->gripFilter) d->gripFilter->setEnabled(false);
                       if (d->gripManager) d->gripManager->setEnabled(false);
                       // 清除殘留的幾何選取高亮
                       if (d->cadView) d->cadView->clearSketchGeomSelection();

                       // 每次重新啟動 LINE 系列指令，重置「連續線段自動重合束制」的追蹤狀態，
                       // 避免將上一次畫線流程殘留的端點誤判為本次的連續段落。
                       const QString canonicalName = v.toString();
                       if (canonicalName == "line"
                           || canonicalName == "construction-line"
                           || canonicalName == "centerline") {
                           d->lineChainActive = false;
                           d->lineChainFirstLineUuid.clear();
                           d->lineChainPrevLineUuid.clear();
                       }
                   });

    // ── E) command 結束 → 自動回到「選取模式」────────────────────────────────
    auto onCommandEnd = [this](const QVariant&) {
        // LINE 系列指令結束（完成/取消/失敗）→ 連續線段追蹤狀態失效
        d->lineChainActive = false;
        d->lineChainFirstLineUuid.clear();
        d->lineChainPrevLineUuid.clear();

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
        } else if (d->alignmentDoc && d->gripManager->currentProvider()) {
            // Alignment 編輯模式：command 結束後也要恢復 grip
            if (d->gripFilter) d->gripFilter->setEnabled(true);
            if (d->gripManager) d->gripManager->setEnabled(true);
        }
    };
    bus->subscribe(core::Events::COMMAND_EXECUTED,  this, onCommandEnd);
    bus->subscribe(core::Events::COMMAND_CANCELLED, this, onCommandEnd);
    bus->subscribe(core::Events::COMMAND_FAILED,    this, onCommandEnd);

    // ── F) Sketch 進入 → 完整啟動（Grips / OSnap / Selection） ──
    bus->subscribe(core::Events::SKETCH_ENTERED, this,
                   [this](const QVariant& v) {
                       // Selection mode → Sketching（允許幾何選取）
                       if (d->cadView)
                           d->cadView->setMode(view::InteractionMode::Sketching);
                       // Grip Filter 啟動
                       if (d->gripFilter) d->gripFilter->setEnabled(true);
                       // displayAllFeatures 會 RemoveAll，因此之後必須重新顯示草圖軸
                       if (d->cadView) {
                           d->cadView->displayAllFeatures();
                           // 重新顯示 X 軸 / Y 軸 / 原點
                           // (displayAllFeatures 內的 RemoveAll 會清掉它們)
                           cad::Sketch* sk = v.value<cad::Sketch*>();
                           if (!sk) sk = m_currentActiveSketch;
                           if (sk) d->cadView->showSketchAxes(sk);
                       }
                       // ✅ 修正：displayAllFeatures() 的 RemoveAll 同樣會把
                       // SketchPanel::enterSketchMode() 剛剛才顯示出來的約束
                       // 符號/尺寸約束 AIS 物件整個移出 context —— 這正是
                       // 「進入 sketch 編輯畫面閃一下、約束符號隨即消失」的
                       // 成因。RemoveAll 之後必須跟草圖軸一樣重新顯示。
                       if (d->sketchPanel && d->sketchPanel->overlay()) {
                           d->sketchPanel->overlay()->rebuildAll();
                           d->sketchPanel->overlay()->setVisible(true);
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

        // GDIM v2 Phase 4：Mini Toolbar 寫回。propertyChanged 目前僅在
        // showAnnotationProperties() 顯示標註屬性時有意義（d->propAnnotationUuid
        // 非空）；showFeatureProperties() 顯示一般 Feature 屬性時
        // d->propAnnotationUuid 為空，這裡直接略過（維持既有行為不變）。
        connect(d->propertyPanel, &PropertyPanel::propertyChanged,
                this, [this](const QString& name, const QVariant& value) {
            if (d->propAnnotationUuid.isEmpty() || !d->propAnnotationSketch) return;
            cad::Sketch* sk = d->propAnnotationSketch;
            cad::SketchAnnotation* ann = sk->findAnnotation(d->propAnnotationUuid);
            if (!ann) return;

            if (name == tr("字首 Prefix")) {
                ann->prefix = value.toString();
            } else if (name == tr("字尾 Suffix")) {
                ann->suffix = value.toString();
            } else if (name == tr("精度 Precision")) {
                bool ok = false;
                int p = value.toInt(&ok);
                if (ok && p >= 0) ann->precision = p;
            } else if (name == tr("公差模式")) {
                QString v = value.toString();
                if (v == tr("無"))        ann->tolerance.mode = cad::ToleranceMode::None;
                else if (v == tr("對稱±")) ann->tolerance.mode = cad::ToleranceMode::Symmetric;
                else if (v == tr("偏差+/-")) ann->tolerance.mode = cad::ToleranceMode::Deviation;
                else if (v == tr("極限值"))  ann->tolerance.mode = cad::ToleranceMode::Limit;
                else if (v == tr("Basic")) ann->tolerance.mode = cad::ToleranceMode::Basic;
            } else if (name == tr("公差上限")) {
                bool ok = false; double v = value.toDouble(&ok);
                if (ok) ann->tolerance.upper = v;
            } else if (name == tr("公差下限")) {
                bool ok = false; double v = value.toDouble(&ok);
                if (ok) ann->tolerance.lower = v;
            } else if (name == tr("Basic Dimension")) {
                ann->isBasic = (value.toString() == tr("是"));
            } else if (name == tr("Inspection Dimension")) {
                ann->isInspection = (value.toString() == tr("是"));
            } else {
                return;  // 不認得的欄位（理論上不會發生）
            }

            // addAnnotation() 依 uuid 判斷為「更新」；prefix/suffix/tolerance/
            // precision/isBasic/isInspection 皆為純顯示屬性，不影響
            // refs/value/driving，因此隱含約束內容與 DOF 都不受影響。
            // 注意：Sketch::addAnnotation() 目前對所有 driving==true 的標註
            // 一律呼叫 solveConstraints()（沿用既有、統一的同步路徑），
            // 所以這裡實際上會觸發一次完整 rebuildAll()（其中
            // createSymbolFor() 已會重新掛載 annotation，見該函式）；
            // 隨後的 annotationAdded → refreshAnnotation() 屬於保險的
            // 輕量二次刷新（AIS 已是最新內容，此步驟為 no-op，僅在
            // rebuildAll 之外的路徑——例如未來非 driving 的顯示更新——
            // 才會是唯一真正生效的刷新）。以未變更數值重新求解一次，
            // 對現有已驗證過的求解器邏輯不構成風險（收斂於同一組數值）。
            sk->addAnnotation(*ann);

            // GDIM v2 Phase 9：規範檢查（只提醒，不阻擋寫回）
            for (const auto& w : cad::AnnotationStandardsChecker::checkAnnotation(*ann, ann->value)) {
                if (d->commandLineManager)
                    d->commandLineManager->printMessage(
                        tr("⚠ [%1] %2").arg(w.code, w.message),
                        core::MessageType::Warning);
            }
        });

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

        // ── Sketch 通用快照式 undo/redo ─────────────────────────────────
        // 設計：不逐指令手刻 undo/redo，而是掛在 CommandManager 的
        // commandStarted/commandFinished/commandCancelled 這三個既有訊號上，
        // 在「有作用中 Sketch」的前提下，指令開始時捕捉一次 Sketch::toJson()
        // 快照，指令結束（無論成功／取消）時再捕捉一次，兩者不同才推入
        // SketchEditCommand。對於 LineCommand 這類互動式多點指令（畫連續
        // 線），commandStarted/commandFinished 只在整個互動流程「開始」與
        // 「真正結束」（Enter/ESC/右鍵完成，而非每下一點）各觸發一次
        // （見 CommandManager::executeCommand 內 connect(cmd, &Command::finished, …)
        // 與 LineCommand::handlePointAcquired 逐段 publish 但不逐段 finish 的
        // 實作），因此一整段連續畫線會合併成一筆 Undo，行為與大多數 CAD
        // 軟體「一個指令一筆 Undo」一致。
        //
        // 與既有機制的分工：
        //   - grip 拖曳（GripMoveCommand）、AlignmentEditCommand（PI/VIP 拖曳、
        //     資料表編輯）都是直接操作、不經過 CommandManager::executeCommand，
        //     不會被這裡重複捕捉。
        //   - 排除 "undo"/"redo" 這兩個指令名稱本身，避免呼叫復原/重做時
        //     被本 hook 誤判成一次新的可復原操作，把 undo 動作自己也推入
        //     undo stack（那會讓 redo 歷史被錯誤截斷）。
        if (auto* cmdMgr = app->commandManager()) {
            auto isExcluded = [](const QString& name) {
                return name.compare("undo", Qt::CaseInsensitive) == 0
                    || name.compare("redo", Qt::CaseInsensitive) == 0;
            };

            connect(cmdMgr, &command::CommandManager::commandStarted, this,
                    [this, isExcluded](const QString& name) {
                        if (isExcluded(name)) { d->sketchEditPending = false; return; }

                        cad::Sketch* sketch = core::Application::instance()->activeSketch();
                        d->sketchEditPending      = (sketch != nullptr);
                        d->sketchEditTargetSketch = sketch;
                        d->sketchEditBeforeSnapshot = sketch ? sketch->toJson() : QJsonObject();
                    });

            auto finishHook = [this](const QString& name) {
                if (!d->sketchEditPending) return;
                d->sketchEditPending = false;

                cad::Sketch* sketch = d->sketchEditTargetSketch;
                if (!sketch) return;
                // 指令執行期間切換了作用中草圖（極少見）：保守起見不產生 undo 記錄，
                // 避免張冠李戴。
                if (core::Application::instance()->activeSketch() != sketch) return;

                const QJsonObject after = sketch->toJson();
                if (after == d->sketchEditBeforeSnapshot) return;   // no-op，無需記錄

                command::SketchEditCommand::push(sketch, d->sketchEditBeforeSnapshot,
                                                 after, name);
            };

            connect(cmdMgr, &command::CommandManager::commandFinished, this,
                    [finishHook](const QString& name, const command::CommandResult&) {
                        finishHook(name);
                    });
            connect(cmdMgr, &command::CommandManager::commandCancelled, this,
                    [finishHook](const QString& name) { finishHook(name); });
        }

        // ── 返回／重做工具列按鈕 ─────────────────────────────────────────
        // 這兩個按鈕刻意不走 menu.txt → CommandFactory 那條通用派送路徑：
        // 一般工具列按鈕觸發的是「一次性指令」，但 Undo/Redo 按鈕的
        // 可用狀態（enabled）、提示文字（"復原 xxx"／"重做 xxx"）需要
        // 跟著 QUndoStack::canUndoChanged/undoTextChanged 等訊號即時更新，
        // 用 QUndoStack 內建的 createUndoAction()/createRedoAction() 直接
        // 取得已經跟 stack 綁定好的 QAction 最單純可靠。
        {
            QToolBar* undoToolbar = d->mainWindow->addToolBar(tr("復原/重做"));
            undoToolbar->setObjectName(QStringLiteral("UndoRedoToolbar"));

            QAction* undoAction = d->undoStack->createUndoAction(d->mainWindow, tr("復原"));
            undoAction->setShortcut(QKeySequence::Undo);
            undoAction->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));

            QAction* redoAction = d->undoStack->createRedoAction(d->mainWindow, tr("重做"));
            redoAction->setShortcut(QKeySequence::Redo);
            redoAction->setIcon(QIcon::fromTheme(QStringLiteral("edit-redo")));

            undoToolbar->addAction(undoAction);
            undoToolbar->addAction(redoAction);

            // 讓快捷鍵在整個主視窗都有效，不侷限於工具列按鈕本身取得焦點時
            d->mainWindow->addAction(undoAction);
            d->mainWindow->addAction(redoAction);
        }

        setupCommandLine();

        // ✅ 在這裡呼叫，d->mainWindow 和 d->cadView 都已存在
        setupSketchPanel();

        // ── Step 16：建立縱斷面 Dock ──────────────────────────────────────────
        // Per-TCL AlignmentDocument 在 editAlignmentRequested 時建立。
        d->vAlignDock = new ui::VAlignEditorDockWidget(d->mainWindow);
        d->mainWindow->addDockWidget(Qt::BottomDockWidgetArea, d->vAlignDock);
        d->vAlignDock->hide();

        // ── 縱斷面編輯完成 → 寫回 TCL + 標記文件已修改 ───────────────────
        connect(d->vAlignDock, &ui::VAlignEditorDockWidget::alignmentChanged,
                this, [this, docMgr] {
                    d->vAlignDock->writeBackToTcl();
                    auto* doc = docMgr->currentDocument();
                    if (doc) doc->setModified(true);
                });

        // 建立並加入 OSnap 工具列
        // 改用信號，等 viewer 初始化完畢再建立 toolbar
        // ── 返回按鈕 → 結束 H-Alignment edit 模式 ─────────────────────────
    connect(d->cadView, &view::CadView::returnAlignmentRequested,
            this, [this, docMgr]() {
                auto* doc = docMgr->currentDocument();
                core::EventBus* bus = core::Application::instance()->eventBus();

                // 1. 隱藏返回按鈕
                d->cadView->hideReturnAlignmentButton();

                // 2. Status bar 停止顯示座標（清除並抑制後續游標座標 emit）
                d->cadView->setSuppressCoordDisplay(true);
                if (d->mainWindow)
                    d->mainWindow->statusBar()->clearMessage();

                // 3. 將該 HAlignment item 設為 eyeClose（setHAlignVisible(false)）
                //    並透過 halign-visibility-changed 讓 renderer 隱藏
                if (!d->activeTclId.isEmpty() && doc) {
                    auto* tcl = doc->findTrackCenterLine(d->activeTclId);
                    if (tcl) {
                        tcl->setHAlignVisible(false);
                        doc->setModified(true);
                    }
                    // renderer setVisible(false)
                    auto* r = d->tclRenderers.value(d->activeTclId, nullptr);
                    if (r) r->setVisible(false);
                    // 通知 FeatureBrowser 更新 eye icon 為 eyeClose
                    if (doc) Q_EMIT doc->treeStructureChanged();
                    // 發布 halign-visibility-changed 供其他模組訂閱
                    QVariantMap hv;
                    hv["tclId"]   = d->activeTclId;
                    hv["visible"] = false;
                    bus->publish("railway.halign-visibility-changed", hv);
                }

                // 4. 座標不再使用 TM2：清除 ProjectOrigin，重設 originSet flag
                using namespace aicad::core::geometry;
                ProjectOrigin::instance().clear();  // 發布 PROJECT_ORIGIN_CHANGED
                d->originSet = false;

                // 5. 關閉格線
                d->cadView->setGridEnabled(false);

                // 6. 清除 active alignment doc / tclId
                setActiveAlignmentDoc(nullptr);
                d->activeTclId.clear();

                // 7. 發布 alignment-edit-ended 讓其他模組知道
                bus->publish("railway.alignment-edit-ended", QVariant{});

                qDebug() << "[UIManager] H-Alignment edit ended via return button";
            });

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

                                // ✅ 還原 per-TCL AlignmentDocument + overlay + vAlign dock
                                for (auto* tcl : doc->trackCenterLines()) {
                                    const QString tid = tcl->id();

                                    // Build/restore per-TCL AlignmentDocument
                                    railway::AlignmentDocument* aDoc =
                                        d->tclAlignmentDocs.value(tid, nullptr);
                                    if (!aDoc) {
                                        aDoc = new railway::AlignmentDocument(d->mainWindow);
                                        d->tclAlignmentDocs.insert(tid, aDoc);
                                    }
                                    // Load edit session from per-TCL JSON (new format)
                                    QJsonObject editJson = doc->tclAlignmentData(tid);
                                    // Fallback: legacy global alignmentData
                                    if (editJson.isEmpty() && !doc->alignmentData().isEmpty())
                                        editJson = doc->alignmentData();
                                    if (!editJson.isEmpty())
                                        aDoc->fromJson(editJson);
                                    else
                                        aDoc->horizontal()->solve();  // solve empty → clears

                                    // Sync solved rawPoints back to TCL for PLAN DEV
                                    const railway::HorizontalAlignment* ha =
                                        aDoc->horizontal()->result();
                                    if (ha && !ha->isEmpty()) {
                                        QVector<railway::AlignmentPoint> merged = ha->rawPoints();
                                        railway::mergeAuxiliaryFields(merged, tcl->horizontal()->rawPoints());
                                        tcl->loadHorizontal(merged);
                                    }

                                    if (tcl->hAlignVisible()) {
                                        view::AlignmentRenderer* r =
                                            d->tclRenderers.value(tid, nullptr);
                                        if (!r) {
                                            r = new view::AlignmentRenderer(
                                                    d->cadView, d->mainWindow);
                                            connect(aDoc->horizontal(),
                                                    &railway::HorizontalAlignmentEdit::changed,
                                                    r, &view::AlignmentRenderer::refresh);
                                            d->tclRenderers.insert(tid, r);
                                        }
                                        r->setHorizontalAlignment(tcl->horizontal());
                                        r->setVisible(true);
                                        r->refresh();
                                        // ✅ 修正：只顯示水平線形（未開垂直斷面 dock）的 TCL，
                                        // 先前完全不會把它的 AlignmentDocument 設為 d->alignmentDoc
                                        // ——導致 AS/FC 等靠 d->alignmentDoc 找元素的命令，在剛載入
                                        // 檔案、且該 TCL 沒開垂直斷面 dock 時，永遠選不到任何弧／
                                        // 切線（因為 context.alignmentDoc 根本不是這條線的
                                        // AlignmentDocument）。比照下方 vAlignVisible 分支同樣
                                        // 「顯示中 → 設為 active」的邏輯。
                                        setActiveAlignmentDoc(aDoc);  // track active
                                    }
                                    if (tcl->vAlignVisible()) {
                                        d->vAlignDock->setAlignmentDocument(aDoc);
                                        d->vAlignDock->loadTrackCenterLine(tcl);
                                        d->vAlignDock->show();
                                        setActiveAlignmentDoc(aDoc);  // track active
                                    }
                                }
                           });

        bus->subscribe(core::Events::DOCUMENT_CLOSED, this,
            [this](const QVariant& data) {
                qDebug() << "[UIManager] Document closed:" << data.toString();
                updateFeatureTree();
                // ✅ 清除所有 per-TCL renderers
                for (auto* r : d->tclRenderers)
                    delete r;
                d->tclRenderers.clear();
                // ✅ 清除所有 per-TCL AlignmentDocuments
                for (auto* ad : d->tclAlignmentDocs)
                    delete ad;
                d->tclAlignmentDocs.clear();
                setActiveAlignmentDoc(nullptr);
                // ✅ 清除 Railway 彙總 3D Alignment 顯示
                if (d->railway3DRenderer)
                    d->railway3DRenderer->clear();
                // ✅ 隱藏 vAlign dock
                if (d->vAlignDock)
                    d->vAlignDock->hide();
                // ✅ 重設 OSnap 狀態，避免 dangling plane 指標
                if (d->cadView && d->cadView->snapManager()) {
                    d->cadView->snapManager()->setActivePlane(nullptr);
                    d->cadView->snapManager()->setActiveSketch(nullptr);
                    d->cadView->snapManager()->clearLastInputPoint();
                    d->cadView->snapManager()->setSnapEnabled(false);
                }
            });
        
        // ── TCL h-alignment 3D visibility ─────────────────────────────────────
        bus->subscribe("railway.halign-visibility-changed", this,
            [this, docMgr](const QVariant& data) {
                QVariantMap m = data.toMap();
                const QString tclId  = m["tclId"].toString();
                const bool    vis    = m["visible"].toBool();

                auto* doc = docMgr->currentDocument();
                if (!doc) return;
                auto* tcl = doc->findTrackCenterLine(tclId);
                if (!tcl) return;

                view::AlignmentRenderer* r = d->tclRenderers.value(tclId, nullptr);
                if (!r) {
                    r = new view::AlignmentRenderer(d->cadView, d->mainWindow);
                    d->tclRenderers.insert(tclId, r);
                }
                if (vis) {
                    r->setHorizontalAlignment(tcl->horizontal());
                    r->setVisible(true);
                    r->refresh();

                    // ✅ 修正：比照 valign-visibility-changed 分支，顯示水平線形時
                    // 也要 get-or-create 這條 TCL 的 AlignmentDocument 並設為 active
                    // ——先前這裡完全沒有碰 d->alignmentDoc，導致單靠「顯示水平線
                    // 形」（例如 feature tree 的眼睛圖示切換）無法讓 AS/FC 等命令
                    // 找到這條線的元素，必須額外去開垂直斷面 dock 才會意外生效。
                    //
                    // 完整比照 editAlignmentRequested 的建立流程（見上方該處）：
                    // 第一次建立時，先嘗試載入既有的 edit-session JSON；若仍無
                    // 元素資料，退回從 TCL 既有的稠密關鍵點序列反推一次，讓
                    // AS/FC/grip 一開始就有東西可以選取，而不是空的文件。
                    railway::AlignmentDocument* aDoc =
                        d->tclAlignmentDocs.value(tclId, nullptr);
                    if (!aDoc) {
                        aDoc = new railway::AlignmentDocument(d->mainWindow);
                        d->tclAlignmentDocs.insert(tclId, aDoc);
                        QJsonObject editJson = doc->tclAlignmentData(tclId);
                        if (!editJson.isEmpty())
                            aDoc->fromJson(editJson);
                        aDoc->horizontal()->seedFromRawPoints(tcl->horizontal()->rawPoints());
                        aDoc->horizontal()->solve();
                    }
                    setActiveAlignmentDoc(aDoc);  // track active
                } else {
                    r->setVisible(false);
                }
            });

        // ── TCL v-alignment profile dock visibility ────────────────────────────
        bus->subscribe("railway.valign-visibility-changed", this,
            [this, docMgr](const QVariant& data) {
                QVariantMap m = data.toMap();
                const QString tclId = m["tclId"].toString();
                const bool    vis   = m["visible"].toBool();

                auto* doc = docMgr->currentDocument();
                if (!doc) return;
                auto* tcl = doc->findTrackCenterLine(tclId);
                if (!tcl) return;

                if (vis) {
                    // Get or create per-TCL AlignmentDocument
                    railway::AlignmentDocument* aDoc =
                        d->tclAlignmentDocs.value(tclId, nullptr);
                    if (!aDoc) {
                        aDoc = new railway::AlignmentDocument(d->mainWindow);
                        d->tclAlignmentDocs.insert(tclId, aDoc);
                    }
                    setActiveAlignmentDoc(aDoc);  // set active
                    d->vAlignDock->setAlignmentDocument(aDoc);
                    d->vAlignDock->loadTrackCenterLine(tcl);
                    d->vAlignDock->show();
                    d->vAlignDock->raise();
                } else {
                    d->vAlignDock->hide();
                }
            });

        // ── Railway 資料夾：彙總 3D Alignment 顯示（eyeOpen/eyeClose）──────────
        bus->subscribe("railway.railway3d-visibility-changed", this,
            [this, docMgr](const QVariant& data) {
                QVariantMap m = data.toMap();
                const bool vis = m["visible"].toBool();

                auto* doc = docMgr->currentDocument();
                if (!doc) return;

                if (!d->railway3DRenderer) {
                    d->railway3DRenderer =
                        new view::Railway3DAlignmentRenderer(d->cadView, d->mainWindow);
                }

                if (vis)
                    d->railway3DRenderer->showAll(doc->trackCenterLines());
                else
                    d->railway3DRenderer->clear();
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

        // ── F.1 載入檔案後重建 tclRenderers ─────────────────────────────────
        // allFeaturesLoaded 是在 Document::load() 完成所有反序列化後 emit 的，
        // 此時 doc->trackCenterLines() 已有完整資料，可安全建立 renderers。
        connect(docMgr, &core::DocumentManager::currentDocumentChanged,
                this, [this](cad::Document* doc) {
            if (!doc) return;
            // Qt5 相容的 single-shot connect：用 QSharedPointer<QMetaObject::Connection>
            // 在 lambda 內部 disconnect 自身，取代 Qt6 的 Qt::SingleShotConnection。
            auto connPtr = QSharedPointer<QMetaObject::Connection>::create();
            *connPtr = connect(doc, &cad::Document::allFeaturesLoaded,
                    this, [this, doc, connPtr]() {
                // 只執行一次後立刻斷開
                QObject::disconnect(*connPtr);

                // 清除舊的 renderers（若有）
                for (auto* r : d->tclRenderers) delete r;
                d->tclRenderers.clear();

                if (!d->cadView) return;
                for (railway::TrackCenterLine* tcl : doc->trackCenterLines()) {
                    const QString tid = tcl->id();
                    // AlignmentDocument 可能已由 DOCUMENT_OPENED handler 建立
                    railway::AlignmentDocument* aDoc =
                        d->tclAlignmentDocs.value(tid, nullptr);
                    if (!aDoc) continue;  // 尚未載入資料，略過

                    auto* r = new view::AlignmentRenderer(d->cadView, d->mainWindow);
                    connect(aDoc->horizontal(),
                            &railway::HorizontalAlignmentEdit::changed,
                            r, &view::AlignmentRenderer::refresh);
                    d->tclRenderers.insert(tid, r);

                    const railway::HorizontalAlignment* ha = aDoc->horizontal()->result();
                    if (ha && !ha->isEmpty()) {
                        r->setHorizontalAlignment(tcl->horizontal());
                        r->setVisible(tcl->hAlignVisible());
                        r->refresh();
                    }
                }
                qDebug() << "[UIManager] rebuildTclRenderers (allFeaturesLoaded):"
                         << d->tclRenderers.size() << "tracks";
            });  // Qt5 compatible single-shot via self-disconnect
        });

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
        bus->subscribe("sketch.editEnded", this, [](const QVariant&) {
            // sketch.editEnded 由 onSketchEditEnded() 自己發布，避免遞迴
            // 此處僅作防禦性處理
        });

        // ✅ 監聽平面選取請求（顯示提示）
        bus->subscribe("command.request-plane-selection", this,
                       [this](const QVariant& /*data*/) {
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

        bus->subscribe(core::Events::FEATURE_CREATED, this, [this](const QVariant& /*data*/) {
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
                               QPointF pt = map["point"].value<QPointF>();
                               rb->addPoint(pt);
                               rb->update();
                           } else if (action == "addPoint") {
                               QPointF pt = map["point"].value<QPointF>();
                               rb->addPoint(pt);
                               rb->update();
                           } else if (action == "setCurrentPoint") {
                               QPointF pt = map["point"].value<QPointF>();
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

        // ── B.2 PROJECT_ORIGIN_CHANGED — 由 ProjectOrigin::publishChanged() 觸發 ─
        // Phase 0 fix: 統一訂閱 Events::PROJECT_ORIGIN_CHANGED，不再用舊字串事件。
        // Phase 2:     更新 CadView::coordinateOffset（相容層）+ originSet flag。
        bus->subscribe(core::Events::PROJECT_ORIGIN_CHANGED, this,
                       [this](const QVariant& data) {
                           QVariantMap map = data.toMap();
                           if (!map.value("isSet").toBool()) return;
                           double e = map["originE"].toDouble();
                           double n = map["originN"].toDouble();
                           // Phase 2: CadView deprecated 相容層（coordinateOffsetE/N getter 仍需此值）
                           if (d->cadView) {
                               d->cadView->setCoordinateOffset(e, n);
                               d->originSet = true;
                               qDebug() << "[UIManager] PROJECT_ORIGIN_CHANGED: E=" << e << "N=" << n;
                           }
                       });

        // ✅ Monitor user interactions for debugging/logging
        bus->subscribe(core::Events::POINT_ACQUIRED, this,
                       [this](const QVariant& data) {
                           QVariantMap map = data.toMap();
                           QPointF localPt = map["point"].value<QPointF>();
                           qDebug() << "[UIManager] User clicked point:" << localPt.x() << localPt.y();
                           // Phase 5: 將 Local 座標轉成 TM2 Global 後顯示於 status bar
                           using namespace aicad::core::geometry;
                           const QPointF globalPt = ProjectOrigin::instance().toGlobal(localPt);
                           const QString coordMsg =
                               QString("E: %1   N: %2")
                                   .arg(globalPt.x(), 0, 'f', 3)
                                   .arg(globalPt.y(), 0, 'f', 3);
                           if (d->mainWindow)
                               d->mainWindow->statusBar()->showMessage(coordMsg, 5000);
                       });

        // ── A.5 COORDINATE_INPUT → POINT_ACQUIRED 橋接 ──────────────────────
        // CommandLineManager 在 InputType::Point 狀態發布 COORDINATE_INPUT（字串），
        // 此橋接解析為 QPointF 並 re-publish POINT_ACQUIRED，使 alignment commands
        // 可透過鍵盤輸入座標。
        //
        // Phase 6 — TM2 邊界轉換規則：
        //   • 使用者輸入的座標若量級 > 10000，視為 TM2 Global（絕對值），
        //     透過 ProjectOrigin::toLocal() 轉成 Local 後再送入 POINT_ACQUIRED。
        //   • 量級 ≤ 10000 視為已是 Local 座標（相對偏移），直接使用。
        //   此規則支援直接貼上 TM2 坐標，也支援輸入相對距離。
        bus->subscribe(core::Events::COORDINATE_INPUT, this,
                       [bus](const QVariant& data) {
                           QString text = data.toString().trimmed();
                           auto parts = text.split(
                               QRegularExpression("[,\\s]+"),
                               Qt::SkipEmptyParts);
                           if (parts.size() < 2) return;
                           bool okX, okY;
                           double x = parts[0].toDouble(&okX);
                           double y = parts[1].toDouble(&okY);
                           if (!okX || !okY) return;

                           // Phase 6: TM2 大座標自動轉換
                           using namespace aicad::core::geometry;
                           auto& origin = ProjectOrigin::instance();
                           QPointF pt(x, y);
                           if (origin.isSet() && (qAbs(x) > 10000.0 || qAbs(y) > 10000.0)) {
                               // 輸入為 TM2 Global 座標 → 轉成 Local
                               pt = origin.toLocal(x, y);
                               qDebug() << "[UIManager] COORDINATE_INPUT TM2→Local:"
                                        << x << y << "->" << pt;
                           }

                           QVariantMap m;
                           m["point"] = QVariant::fromValue(pt);
                           bus->publish(core::Events::POINT_ACQUIRED, m);
                           qDebug() << "[UIManager] COORDINATE_INPUT → POINT_ACQUIRED:"
                                    << pt.x() << pt.y();
                       });

        // ── Extrude 建立 ─────────────────────────────────────────────
        bus->subscribe("command.create-extrude", this,
                       [](const QVariant& data) {
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

                           // Create the line in the sketch (addLineGeom creates SketchPoints)
                           QString lineUuid = sketch->addLineGeom(startPoint, endPoint);
                           // rebuildRequested signal auto-triggers Document::rebuildFeature
                           // → eraseFromContext + rebuild + displayInContext
                           // DO NOT call rebuild() again here: SketchPointAIS are managed by Sketch

                           // ── 連續線段自動加上「重合」束制 ──────────────────────────
                           // LineCommand 每次都以「上一段終點」作為下一段起點呼叫本事件，
                           // 因此本段起點理論上緊接著上一段終點；而若使用者將本段終點
                           // 移回本次連續畫線的最初起點附近，視為封閉迴路，同樣補上重合束制。
                           // 容許誤差：kLineJoinTol 用於判斷「段與段的銜接點」
                           //          （理論上為同一座標，取極小值即可）；
                           //          kLineCloseLoopTol 用於判斷「終點是否很接近起點」
                           //          （使用者手動點擊，容許稍大的誤差，可依圖面單位調整）。
                           constexpr float kLineJoinTol      = 1e-4f;
                           constexpr float kLineCloseLoopTol = 1e-3f;

                           if (!lineUuid.isEmpty()) {
                               // 段與段銜接：本段起點 ≈ 上一段終點 → 加上重合束制
                               if (d->lineChainActive
                                   && !d->lineChainPrevLineUuid.isEmpty()
                                   && (startPoint - d->lineChainPrevEndPos).lengthSquared()
                                          < kLineJoinTol * kLineJoinTol) {
                                   sketch->constrainCoincident(
                                       cad::GeomRef(d->lineChainPrevLineUuid, cad::GeomHandle::End),
                                       cad::GeomRef(lineUuid, cad::GeomHandle::Start));
                               }

                               // 本次連續畫線的第一段：記錄起點供之後判斷是否封閉
                               if (!d->lineChainActive) {
                                   d->lineChainActive          = true;
                                   d->lineChainFirstLineUuid   = lineUuid;
                                   d->lineChainFirstStartPos   = startPoint;
                               }

                               // 封閉迴路：本段終點 ≈ 最初起點（且不是同一條線自我重合）→ 加上重合束制
                               if (!d->lineChainFirstLineUuid.isEmpty()
                                   && d->lineChainFirstLineUuid != lineUuid
                                   && (endPoint - d->lineChainFirstStartPos).lengthSquared()
                                          < kLineCloseLoopTol * kLineCloseLoopTol) {
                                   sketch->constrainCoincident(
                                       cad::GeomRef(lineUuid, cad::GeomHandle::End),
                                       cad::GeomRef(d->lineChainFirstLineUuid, cad::GeomHandle::Start));
                               }

                               d->lineChainPrevLineUuid = lineUuid;
                               d->lineChainPrevEndPos   = endPoint;
                           }

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

                           // ✅ 尊重呼叫端（LineCommand::execute 非互動分支）夾帶的 role，
                           //    否則用打字輸入座標建立的建構線／中心線會被誤建成一般線。
                           auto role = static_cast<cad::GeomRole>(
                               request.value("role", static_cast<int>(cad::GeomRole::Normal)).toInt());

                           // Create the line in the sketch (addLineGeom creates SketchPoints)
                           sketch->addLineGeom(QVector2D(x1, y1), QVector2D(x2, y2),
                                               QString(), QString(), role);

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

                           sketch->addCircleGeom(center, radius);

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

                           // 建立多邊形：sketch->addPolyline() 現在會自動退化為
                           // N 條獨立 SketchLine + 相鄰角落的 Coincident 束制
                           // （與 Line / Rectangle 指令一致，見 Sketch::addLineChainGeom）
                           sketch->addPolyline(vertices, true);

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

        // ── 建構線（Construction Line）／中心線（Centerline） ────────────────
        // 共用邏輯：兩者都是「可被束制、可被 OSnap、有 UUID」的 SketchLine，
        // 只差在 role。互動模式下 LineCommand::handlePointAcquired() 發布的
        // payload 是 startPoint/endPoint（QVector2D），與一般線一致；
        // 舊版 handler 誤以為 payload 是 args 字串列表，導致互動畫線完全無效
        // （args 恆為空、提早 return，滑鼠點兩下畫面上什麼都不會出現）。
        // 這裡改用與 command.create-sketch-line 相同的處理方式與自動重合／
        // 封閉迴路邏輯（沿用同一組 d->lineChain* 追蹤狀態——COMMAND_STARTED /
        // 指令結束時已會重置，見上方 onCommandEnd 與 D) 區塊）。
        auto handleConstructionFamilyLine =
            [this, bus](const QVariant& v, cad::GeomRole role, const QString& createdMsg) {
                QVariantMap lineData = v.toMap();
                Application* app = Application::instance();
                cad::Sketch* sketch = app->activeSketch();
                if (!sketch) {
                    qWarning() << "[UIManager] No active sketch for construction/centerline creation";
                    bus->publish(Events::COMMAND_FAILED, "No active sketch");
                    return;
                }

                QVector2D startPoint = lineData["startPoint"].value<QVector2D>();
                QVector2D endPoint   = lineData["endPoint"].value<QVector2D>();

                QString lineUuid = sketch->addLineGeom(startPoint, endPoint,
                                                        QString(), QString(), role);

                constexpr float kLineJoinTol      = 1e-4f;
                constexpr float kLineCloseLoopTol = 1e-3f;

                if (!lineUuid.isEmpty()) {
                    if (d->lineChainActive
                        && !d->lineChainPrevLineUuid.isEmpty()
                        && (startPoint - d->lineChainPrevEndPos).lengthSquared()
                               < kLineJoinTol * kLineJoinTol) {
                        sketch->constrainCoincident(
                            cad::GeomRef(d->lineChainPrevLineUuid, cad::GeomHandle::End),
                            cad::GeomRef(lineUuid, cad::GeomHandle::Start));
                    }

                    if (!d->lineChainActive) {
                        d->lineChainActive        = true;
                        d->lineChainFirstLineUuid = lineUuid;
                        d->lineChainFirstStartPos = startPoint;
                    }

                    if (!d->lineChainFirstLineUuid.isEmpty()
                        && d->lineChainFirstLineUuid != lineUuid
                        && (endPoint - d->lineChainFirstStartPos).lengthSquared()
                               < kLineCloseLoopTol * kLineCloseLoopTol) {
                        sketch->constrainCoincident(
                            cad::GeomRef(lineUuid, cad::GeomHandle::End),
                            cad::GeomRef(d->lineChainFirstLineUuid, cad::GeomHandle::Start));
                    }

                    d->lineChainPrevLineUuid = lineUuid;
                    d->lineChainPrevEndPos   = endPoint;
                }

                bus->publish(Events::FEATURE_UPDATED, sketch->name());
                setStatusMessage(createdMsg, 2000);
            };

        bus->subscribe("command.create-sketch-construction-line", this,
                       [this, handleConstructionFamilyLine](const QVariant& v) {
                           handleConstructionFamilyLine(v, cad::GeomRole::Construction,
                                                         tr("建構線已加入"));
                       });

        bus->subscribe("command.create-sketch-centerline", this,
                       [this, handleConstructionFamilyLine](const QVariant& v) {
                           handleConstructionFamilyLine(v, cad::GeomRole::Centerline,
                                                         tr("中心線已加入"));
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

                    // ✅ 讓互動式指令（例如 PROFILEARRAY / PROFILELOFT 詢問要選哪個
                    //    草圖／線路／陣列時）也能收到這次點選，不用只能打字輸入名稱。
                    if (auto* bus = core::Application::instance()->eventBus())
                        bus->publish(core::Events::FEATURE_SELECTED, featureId);

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

                    // ── 啟用 H-alignment 3D 可見性 ────────────────────────────
                    if (!tcl->hAlignVisible()) {
                        tcl->setHAlignVisible(true);
                        doc->setModified(true);
                    }

                    // ── Get or create per-TCL AlignmentDocument ──────────────
                    railway::AlignmentDocument* aDoc =
                        d->tclAlignmentDocs.value(tclId, nullptr);
                    if (!aDoc) {
                        aDoc = new railway::AlignmentDocument(d->mainWindow);
                        d->tclAlignmentDocs.insert(tclId, aDoc);
                        // Load existing edit data if available
                        QJsonObject editJson = doc->tclAlignmentData(tclId);
                        if (!editJson.isEmpty())
                            aDoc->fromJson(editJson);

                        // 若載入後（或本來就）尚無元素資料，嘗試從 TCL 既有的
                        // 稠密 TS/SC/CS/CC/TC/ST 關鍵點序列反推一次（例如 ALD
                        // 匯入、尚未經過任何編輯器的情況），讓 grip 有東西可編。
                        // 已有資料時為 no-op。
                        aDoc->horizontal()->seedFromRawPoints(tcl->horizontal()->rawPoints());
                        aDoc->horizontal()->solve();
                    }
                    setActiveAlignmentDoc(aDoc);  // set active

                    // ── 建立/更新 per-TCL renderer ────────────────────────────
                    view::AlignmentRenderer* r = d->tclRenderers.value(tclId, nullptr);
                    if (!r) {
                        r = new view::AlignmentRenderer(d->cadView, d->mainWindow);
                        connect(aDoc->horizontal(),
                                &railway::HorizontalAlignmentEdit::changed,
                                r, &view::AlignmentRenderer::refresh);
                        d->tclRenderers.insert(tclId, r);
                    }
                    // Sync solved rawPoints to TCL for PLAN DEV
                    const railway::HorizontalAlignment* ha = aDoc->horizontal()->result();
                    if (ha && !ha->isEmpty()) {
                        QVector<railway::AlignmentPoint> merged = ha->rawPoints();
                        railway::mergeAuxiliaryFields(merged, tcl->horizontal()->rawPoints());
                        tcl->loadHorizontal(merged);
                    }
                    r->setAlignment(aDoc->horizontal());
                    r->setVisible(true);
                    r->refresh();

                    // ── 更新 FeatureBrowser eye icon ──────────────────────────
                    Q_EMIT doc->treeStructureChanged();

                    // ── B.3 TM2 格線：若尚未設定 origin，自動套用預設值 ───────
                    if (d->cadView) {
                        d->cadView->setGridEnabled(true);
                        if (!d->originSet) {
                            constexpr double kDefaultE = 250000.0;
                            constexpr double kDefaultN = 2650000.0;
                            // Phase 2 fix: 統一透過 ProjectOrigin::setOrigin() 驅動，
                            // publishChanged() 會觸發 PROJECT_ORIGIN_CHANGED，
                            // 由 B.2 handler 呼叫 CadView::setCoordinateOffset（相容層）。
                            // 不再直接呼叫 setCoordinateOffset()。
                            using namespace aicad::core::geometry;
                            if (!ProjectOrigin::instance().isSet()) {
                                ProjectOrigin::instance().setOrigin(kDefaultE, kDefaultN, 0.0);
                            }
                            d->originSet = true;
                            qDebug() << "[UIManager] Auto-set TM2 origin via ProjectOrigin: E="
                                     << kDefaultE << "N=" << kDefaultN;
                        }
                    }

                    // ── 記錄 active TCL id（return button 用）─────────────
                    d->activeTclId = tclId;

                    // ── 顯示返回按鈕（右上角）────────────────────────────────
                    // VAlignProfileView 由 VAlignment item 的 eyeOpen 控制，
                    // 啟動 H-Alignment edit 不自動開啟縱斷面 dock。
                    if (d->cadView) {
                        d->cadView->showReturnAlignmentButton();
                        // H-Alignment edit 期間啟用 TM2 座標顯示
                        d->cadView->setSuppressCoordDisplay(false);
                    }
                });

        // ── TrackCenterLine — showAlignmentDataTableRequested ─────────────────
        connect(d->featureBrowser, &FeatureBrowser::showAlignmentDataTableRequested,
                this, [this, docMgr](const QString& tclId) {
                    qDebug() << "[UIManager] showAlignmentDataTableRequested tclId=" << tclId;

                    auto* doc = docMgr->currentDocument();
                    if (!doc) {
                        qWarning() << "[UIManager] showAlignmentDataTableRequested: no currentDocument";
                        return;
                    }

                    auto* tcl = doc->findTrackCenterLine(tclId);
                    if (!tcl) {
                        qWarning() << "[UIManager] showAlignmentDataTableRequested: TCL not found id=" << tclId;
                        return;
                    }

                    // ── 取得（或建立）此 TCL 的 AlignmentDocument ────────────
                    railway::AlignmentDocument* aDoc =
                        d->tclAlignmentDocs.value(tclId, nullptr);
                    if (!aDoc) {
                        aDoc = new railway::AlignmentDocument(d->mainWindow);
                        d->tclAlignmentDocs.insert(tclId, aDoc);

                        // 優先從 per-TCL JSON（editSession）載入
                        QJsonObject editJson = doc->tclAlignmentData(tclId);
                        if (!editJson.isEmpty()) {
                            aDoc->fromJson(editJson);
                        }

                        // 若載入後（或本來就）尚無元素資料，嘗試從 TCL 既有的
                        // 稠密 TS/SC/CS/CC/TC/ST 關鍵點序列反推一次（例如 ALD
                        // 匯入、尚未經過任何編輯器的情況）。已有資料時為 no-op。
                        aDoc->horizontal()->seedFromRawPoints(tcl->horizontal()->rawPoints());

                        // 若載入後（或本來就）尚無 VIP 資料，嘗試從 TCL 既有的
                        // 稠密 VerticalAlignment 點位反推一次（例如 ALD 匯入、
                        // 尚未經過任何編輯器的情況）。已有資料時為 no-op。
                        aDoc->vertical()->seedFromDensePoints(tcl->vertical()->points());

                        // 確保 solver 已執行一次（空資料也要 solve，保持 m_result 有效）
                        aDoc->horizontal()->solve();
                        aDoc->vertical()->solve();
                    }

                    // ── 將 renderer 切換到 AlignmentDocument 即時模式 ────────
                    // 使得後續每次 solve() → changed() 都能自動刷新 3D 視圖，
                    // 無需等待 dataCommitted。
                    view::AlignmentRenderer* r = d->tclRenderers.value(tclId, nullptr);
                    if (!r) {
                        r = new view::AlignmentRenderer(d->cadView, d->mainWindow);
                        d->tclRenderers.insert(tclId, r);
                    }
                    r->setAlignment(aDoc->horizontal());
                    r->refresh();

                    qDebug() << "[UIManager] Opening AlignmentDataTableDialog:"
                             << "H elements=" << aDoc->horizontal()->elements().size()
                             << "V vipCount=" << aDoc->vertical()->vipCount();

                    auto* dlg = new AlignmentDataTableDialog(
                        aDoc, tcl, d->mainWindow);
                    dlg->setAttribute(Qt::WA_DeleteOnClose);
                    dlg->setWindowModality(Qt::NonModal);

                    // ── H 編輯：changed() 自動同步 TCL 並刷新 renderer ───────
                    // connKeeper 的生命週期綁定到 dlg，對話框關閉時自動斷開連線。
                    auto* connKeeper = new QObject(dlg);
                    connect(aDoc->horizontal(),
                            &railway::HorizontalAlignmentEdit::changed,
                            connKeeper,
                            [r, aDoc, tcl, doc]() {
                                // 同步 solver 結果到 TCL rawPoints（供其他消費者使用），
                                // 並保留既有輔助欄位（見 mergeAuxiliaryFields() 註解）。
                                const railway::HorizontalAlignment* ha =
                                    aDoc->horizontal()->result();
                                if (ha && !ha->isEmpty()) {
                                    QVector<railway::AlignmentPoint> merged = ha->rawPoints();
                                    railway::mergeAuxiliaryFields(merged, tcl->horizontal()->rawPoints());
                                    tcl->loadHorizontal(merged);
                                }
                                // renderer 已設為 AlignmentDocument 模式，直接刷新
                                if (r) r->refresh();
                                doc->setModified(true);
                            });

                    // ── V 編輯：dataCommitted 時同步縱斷面到 TCL ─────────────
                    connect(dlg, &AlignmentDataTableDialog::dataCommitted,
                            this, [this, tclId, aDoc, tcl, doc]() {
                                const railway::VerticalAlignment* va =
                                    aDoc->vertical()->result();
                                if (va && !va->isEmpty())
                                    tcl->loadVertical(va->points());

                                // 若縱斷面 dock 可見，刷新顯示
                                if (d->vAlignDock && d->vAlignDock->isVisible())
                                    d->vAlignDock->loadTrackCenterLine(tcl);

                                // 與 H 編輯對齊：標記文件已修改
                                doc->setModified(true);
                            });

                    dlg->show();
                    dlg->raise();
                    dlg->activateWindow();
                });

        // ── AlignedProfileArray — showProfileArrayTableRequested (Step 8) ──────
        connect(d->featureBrowser, &FeatureBrowser::showProfileArrayTableRequested,
                this, [this, docMgr](const QString& featureId) {
                    auto* doc = docMgr->currentDocument();
                    if (!doc) {
                        qWarning() << "[UIManager] showProfileArrayTableRequested: no currentDocument";
                        return;
                    }
                    auto* arr = qobject_cast<cad::AlignedProfileArray*>(doc->findFeature(featureId));
                    if (!arr) {
                        qWarning() << "[UIManager] showProfileArrayTableRequested: feature not found id=" << featureId;
                        return;
                    }
                    auto* dlg = new ProfileArrayStationTableDialog(arr, d->mainWindow);
                    dlg->setAttribute(Qt::WA_DeleteOnClose);
                    dlg->show();
                    dlg->raise();
                    dlg->activateWindow();
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
                    if (ret == QMessageBox::Yes) {
                        // ── F.2 先清除 renderer，再刪除 TCL ──────────────────
                        if (d->tclRenderers.contains(tclId)) {
                            delete d->tclRenderers.take(tclId);
                        }
                        // 清除對應的 AlignmentDocument
                        if (d->tclAlignmentDocs.contains(tclId)) {
                            delete d->tclAlignmentDocs.take(tclId);
                        }
                        doc->removeTrackCenterLine(tclId);
                    }
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

    // ①-a InputJig（距離/角度輸入 Jig）與 F8 正交鎖定：命令列全域按鍵攔截
    //     （qApp->installEventFilter）預設會把所有按鍵導向命令列輸入框，
    //     這裡開一個例外通道，讓 F8 與 InputJig 顯示中的按鍵改送回原本
    //     的目標 widget（CadView::keyPressEvent／InputJig 自己的 QLineEdit）。
    d->commandLine->setKeyCaptureBypassQuery([this](QKeyEvent* ke) -> bool {
        if (!d->cadView) return false;
        if (ke->key() == Qt::Key_F8) return true;
        if (d->cadView->isInputJigVisible()) return true;
        return false;
    });

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

    // ⑤ Delete 鍵 — 草圖編輯模式下，若有選取幾何則等同 ERASE 命令；
    //    Alignment 編輯模式下，若有選取元素則直接刪除，否則提示使用者
    //    先在視圖中點選要刪除的元素。
    //    使用 WidgetWithChildrenShortcut 綁定在 cadView 上，只在 cadView
    //    （或其子元件）持有焦點時才會觸發，避免吃掉命令列輸入框中
    //    Delete 鍵原本的「刪除字元」行為。
    auto* shortcutDelete = new QShortcut(QKeySequence(Qt::Key_Delete), d->cadView);
    shortcutDelete->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcutDelete, &QShortcut::activated, this, [this]() {
        auto* app = core::Application::instance();
        if (!app || !d->cadView) return;

        auto* cmdMgr = app->commandManager();          // executeCommand("ERASE", ...)
        auto* cmdLine = d->commandLineManager;          // printWarning/printSuccess/...

        // ── 依「實際選取到什麼」決定 Delete 鍵行為 ──────────────────────
        // 原本的判斷順序是「只要有 activeSketch() 就一律走 sketch-erase，
        // 且 sketch 幾何未選取時直接 return」，這會導致：即使使用者點選的
        // 是 Alignment overlay（B3 handler 已把 d->selectedAlignElemIdx
        // 設好），只要同時有 sketch 開著（例如剛用 Spline/Polyline 等指令
        // 畫過草圖、sketch 尚未關閉），sketch 分支會因為
        // selectedGeomUuids() 是空的而直接 return，永遠到不了下面的
        // Alignment erase 分支——看起來就像「Delete 鍵刪不掉 alignment
        // element」。
        //
        // 修正：優先看使用者「實際選取到的東西」而非「目前是否有 sketch
        // 開著」——若已經點選了一個具體的 Alignment 元素，就直接刪除它；
        // 否則才依 activeSketch() 是否存在，走 sketch-erase 或提示訊息。

        // ── Alignment 元素已被實際選取 → 優先刪除它 ─────────────────────
        if (d->alignmentDoc && d->selectedAlignElemIdx >= 0) {
            const bool ok = d->alignmentDoc->horizontal()
                                 ->eraseElementAt(d->selectedAlignElemIdx);

            d->selectedAlignElemIdx  = -1;
            d->selectedAlignRenderer = nullptr;

            // 已刪除的元素不再存在，先前掛載的 grip provider 可能持有
            // 過期的 index／幾何，直接 detach，等使用者下次點選再重新掛載。
            if (d->gripManager) d->gripManager->detach();

            if (cmdLine) {
                if (ok)
                    cmdLine->printSuccess("✅ Alignment element erased.");
                else
                    cmdLine->printWarning(
                        "⚠️  Could not erase — this element still anchors a "
                        "curve group; erase that curve first.");
            }
            return;
        }

        // ── 草圖編輯模式：Delete 鍵等同 ERASE 命令 ─────────────────────
        if (app->activeSketch()) {
            QStringList sel = d->cadView->selectedGeomUuids();
            if (sel.isEmpty()) return;  // 沒有選取任何幾何，不做任何事

            command::CommandContext ctx;
            ctx.args      = sel;
            ctx.uiManager = this;
            ctx.cadView   = d->cadView;

            if (cmdMgr) cmdMgr->executeCommand("ERASE", ctx);
            return;
        }

        // ── Alignment 編輯模式：沒有選取到具體元素 → 提示使用者先點選 ─────
        if (d->alignmentDoc) {
            if (cmdLine)
                cmdLine->printWarning(
                    "⚠️  No alignment element selected — click a tangent "
                    "or curve in the viewport first, then press Delete.");
            return;
        }
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
            this, [this]() {
                // ✅ 窗選 / 穿越窗選 / 籬選 / 多邊形窗選 / 多邊形框選進行中：
                //    ESC 優先取消框選本身，不做其他事。
                if (d->cadView && d->cadView->isBoxSelectArmed()) {
                    d->cadView->cancelActiveBoxSelect();
                    return;
                }
                // ✅ 命令列輸入框（CommandInputEdit）持有焦點時，ESC 事件不會
                //    落在 CadView::keyPressEvent()，過去只呼叫
                //    turnOffActiveGrips() 就提前 return，InputJig 顯示中、
                //    命令等待取點時完全沒有被取消（Jig 不會隱藏、橡皮筋不會
                //    清除、POINT_CANCELLED 也不會發佈）。改呼叫
                //    CadView::requestEscapeCancel()（＝ performEscapeCancel()，
                //    內部已包含 turnOffActiveGrips()），不再提前 return，並且
                //    一律接著呼叫 CommandLineManager::onEscapePressed() 取消
                //    目前執行中的命令本身，三者都要執行到。
                if (d->cadView) {
                    d->cadView->requestEscapeCancel();
                }
                d->commandLineManager->onEscapePressed();
            });

    // ── COMMAND_EXECUTE_REQUEST → CommandManager ──
    bus->subscribe(core::Events::COMMAND_EXECUTE_REQUEST, this,
                   [this](const QVariant& data) {
                       QString cmdName = data.toString();
                       auto* cmdMgr = core::Application::instance()->commandManager();
                       if (!cmdMgr) return;

                       command::CommandContext ctx;
                       ctx.alignmentDoc = d->alignmentDoc;
                       ctx.uiManager    = this;
                       ctx.cadView      = d->cadView;
                       if (d->vAlignDock)
                           ctx.profileView = d->vAlignDock->profileView();

                       // ── 若 CadView 有已選取的幾何，填入 ctx.args ──────────────
                       // 幾何約束命令（GeomConstraintCommand）在模式 A 時直接用
                       // ctx.args 作為 UUID 清單，無需進入互動選取（模式 B）。
                       // 這使得「先選取幾何 → 再按按鈕」的操作流程正確工作。
                       if (d->cadView) {
                           QStringList sel = d->cadView->selectedGeomUuids();
                           if (!sel.isEmpty())
                               ctx.args = sel;
                       }

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
                   [this](const QVariant& /*v*/) {
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

    // ── 命令一般訊息（Info / Success）──────────────────────────────
    bus->subscribe(core::Events::COMMAND_LOG, this,
                   [this](const QVariant& v) {
                       d->commandLine->appendHistory(v.toString(), /*isPrompt=*/false);
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
    bus->subscribe("command.start-construction-line", this, [](const QVariant&) {
        core::Application::instance()->commandManager()
            ->executeCommand("construction-line", QStringList{});
    });

    bus->subscribe("command.start-centerline", this, [](const QVariant&) {
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
            this, [] {
                auto* sketch = core::Application::instance()->activeSketch();
                if (!sketch) return;
                // 此處觸發互動式 command（與 LineCommand 相似但強制 Construction）
                auto* bus = core::Application::instance()->eventBus();
                bus->publish("command.start-construction-line", QVariant{});
            });

    connect(d->sketchPanel, &SketchPanel::requestAddCenterline,
            this, [] {
                auto* bus = core::Application::instance()->eventBus();
                bus->publish("command.start-centerline", QVariant{});
            });

    connect(d->sketchPanel, &SketchPanel::requestAddConstructionCircle,
            this, [] {
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
                        // 角度類型：參數表達式求值結果同樣視為「度」，需轉換為弧度
                        // （與 SketchPanel::onConstraintItemDoubleClicked 的literal number
                        //  路徑、以及 GeneralDimCommand 的一致慣例相同）。
                        const bool isAngleType =
                            (c.type == cad::ConstraintType::FixedAngleDim ||
                             c.type == cad::ConstraintType::FixedAngle);
                        c.paramExpr = newExpr;
                        c.value     = isAngleType ? (v * M_PI / 180.0) : v;
                        // 若 store 中尚未有此名稱，自動登記（登記原始度數值，維持表達式語意一致）
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

    // Phase 7：pickSession 收齊選取 → SketchPanel::onConstraintReadyFromSession
    // 已由 SketchPanel::enterSketchMode() 直接連接（Qt::UniqueConnection）。
    // 此處只負責收尾：清除 constraintPickActive flag、切回 Sketching 模式、更新狀態列。
    // 注意：不可在此重複呼叫 onConstraintReadyFromSession，否則約束會被處理兩次
    // （重複加入/重複報錯），這是先前 "COI: 找不到對應幾何元素" 重複出現兩次的原因。
    connect(d->pickSession, &cad::ConstraintPickSession::constraintReady,
            this, [this](QList<cad::GeomRef> refs,
                         double value, QString paramExpr,
                         bool driving, cad::ConstraintType type) {
        // 正常路徑：SketchPanel 已直接連接 constraintReady → onConstraintReadyFromSession，
        // 此處不重複呼叫。只有當 sketchPanel 不存在時才走 fallback。
        if (!d->sketchPanel) {
            auto* sketch = core::Application::instance()->activeSketch();
            if (sketch) {
                cad::SketchConstraint c;
                c.type = type; c.refs = refs; c.value = value;
                c.paramExpr = paramExpr; c.driving = driving;
                sketch->addConstraint(c);
                sketch->solveConstraints();
            }
        }
        // 清除 constraintPickActive flag，切回 Sketching 模式
        if (d->cadView) {
            d->cadView->setConstraintPickActive(false);
            d->cadView->setMode(view::InteractionMode::Sketching);
        }
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
        if (d->cadView) {
            d->cadView->setConstraintPickActive(false);
            d->cadView->setMode(view::InteractionMode::Sketching);
        }
    });
    if (d->cadView) {
        connect(d->cadView, &view::CadView::geomRefPicked,
                this, [this](QPointF planePt, QString geomUuid, int geomHandle) {
            // 舊路徑：pickSession（DimConstraintCommand 使用）
            if (d->pickSession->isActive()) {
                d->pickSession->feedPoint(planePt, geomUuid, geomHandle);
                // 更新 status bar 提示
                if (d->pickSession->isActive())
                    setStatusMessage(d->pickSession->promptText());
                return;
            }
            // 新路徑：GEOM_PICKED 已由 CadView::handlePointInput 在 GetGeom 模式下
            // 直接發出，不需要在此重複發佈，否則每次點擊會觸發兩次 onGeomPicked。
        });

        // ✅ Step 5/GAP 3: modeChanged — GetGeom 時啟用 SketchPointAIS 選取，離開時停用
        // SketchPointAIS 已統一在 sketch->aisShapes() 中，Activate/Deactivate 統一管理
        connect(d->cadView, &view::CadView::modeChanged,
                this, [this](view::InteractionMode mode) {
            auto ctx = d->cadView->context();
            if (ctx.IsNull()) return;

            const bool enterGetGeom = (mode == view::InteractionMode::GetGeom);

            // ✅ Step 16: GetGeom 模式下停用 GripEventFilter，避免 AIS_GripHandle
            // 搶先消費點擊事件，干擾約束選點流程。離開 GetGeom 時恢復。
            if (d->gripFilter) d->gripFilter->setEnabled(!enterGetGeom);
            if (d->gripManager) d->gripManager->setEnabled(!enterGetGeom);

            // 從 overlay pointAISMap（Constraint overlay 建立的點）
            auto* overlay = d->sketchPanel ? d->sketchPanel->overlay() : nullptr;
            if (overlay) {
                for (auto& ais : overlay->pointAISMap()) {
                    if (enterGetGeom)
                        ctx->Activate(ais, 0, Standard_False);
                    else
                        ctx->Deactivate(ais);
                }
            }

            // 從 sketch->aisShapes()（主 AIS 列表中的 SketchPointAIS）
            auto* app = aicad::core::Application::instance();
            if (auto* sketch = app ? app->activeSketch() : nullptr) {
                for (const auto& obj : sketch->aisShapes()) {
                    if (Handle(aicad::cad::SketchPointAIS)::DownCast(obj)) {
                        if (enterGetGeom)
                            ctx->Activate(obj, 0, Standard_False);
                        else
                            ctx->Deactivate(obj);
                    }
                }
            }

            ctx->UpdateCurrentViewer();
        });

        // ✅ Task E: PlaceDimLine 模式 — 預覽尺寸線位置
        connect(d->cadView, &view::CadView::dimLinePosPreview,
                this, [this](double ox, double oy) {
            // 舊路徑：pickSession（DimConstraintCommand）
            if (d->sketchPanel && d->sketchPanel->overlay()) {
                const QString uuid = d->pickSession
                                      ? d->pickSession->pendingConstraintUuid()
                                      : QString();
                if (!uuid.isEmpty()) {
                    d->sketchPanel->overlay()->updateDimLine(uuid, ox, oy);
                    return;
                }
            }
            // 新路徑：供 GeneralDimCommand 訂閱
            QVariantMap payload;
            payload["offsetX"] = ox;
            payload["offsetY"] = oy;
            auto* bus = core::Application::instance()->eventBus();
            if (bus) bus->publish(core::Events::DIM_LINE_PREVIEW, payload);
        });

        // ✅ Task E: PlaceDimLine 模式 — 確認尺寸線位置
        connect(d->cadView, &view::CadView::dimLinePosConfirmed,
                this, [this](double ox, double oy) {
            // 舊路徑
            if (d->pickSession)
                d->pickSession->confirmDimLineOffset(ox, oy);
            // 新路徑：供 GeneralDimCommand 訂閱
            QVariantMap payload;
            payload["offsetX"] = ox;
            payload["offsetY"] = oy;
            auto* bus = core::Application::instance()->eventBus();
            if (bus) bus->publish(core::Events::DIM_LINE_CONFIRMED, payload);
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

    // ── 拖曳現有尺寸線（即時更新偏移）─────────────────────────────────────────
    connect(d->cadView, &view::CadView::dimLineDragging,
            this, [this](const QString& uuid, double ox, double oy) {
        if (d->sketchPanel && d->sketchPanel->overlay())
            d->sketchPanel->overlay()->updateDimLine(uuid, ox, oy);
    });

    // ── 拖曳現有尺寸線（放開確認，寫回 Sketch）────────────────────────────────
    connect(d->cadView, &view::CadView::dimLineDragFinished,
            this, [this](const QString& uuid, double ox, double oy) {
        // 1. 先更新 overlay 顯示
        if (d->sketchPanel && d->sketchPanel->overlay())
            d->sketchPanel->overlay()->updateDimLine(uuid, ox, oy);
        // 2. 寫回 Sketch constraint，持久化偏移
        if (Sketch* sk = currentActiveSketch())
            sk->updateConstraintDimOffset(uuid, ox, oy);
    });

    // ── 雙擊尺寸線行內編輯（Enter/滑鼠右鍵確認）────────────────────────────────
    // 實際的數值/表達式解析、CoordinateDim「x,y」格式、角度度/弧度轉換、
    // solveConstraints()、命令列訊息回報，皆與 EDITCON 指令共用
    // command::applyDimensionEdit()。
    connect(d->cadView, &view::CadView::dimValueEditCommitted,
            this, [this](const QString& uuid, const QString& newExprOrValue) {
        Sketch* sk = currentActiveSketch();
        auto* cmdMgr = d->commandLineManager;
        command::applyDimensionEdit(sk, uuid, newExprOrValue, cmdMgr);
        // applyDimensionEdit 內部已呼叫 sk->solveConstraints()，
        // 其 constraintSolved 訊號會自動觸發 ConstraintOverlayManager::rebuildAll()。
    });

    // 尺寸線點擊（SketchPanel 的 slot 已處理，此處轉發給 ParameterPanel）
    connect(d->sketchPanel,
            &SketchPanel::dimensionConstraintClicked,
            this, [this](const QString& uuid,
                         cad::ConstraintOverlayManager::Mode mode,
                         const QString& instanceId) {
        if (mode == cad::ConstraintOverlayManager::Mode::Instance) {
            // 找到對應 instance 並切換 ParameterPanel 顯示
            // （Document 查找留給後續 command layer 實作）
            Q_UNUSED(instanceId)
            return;
        }

        // GDIM v2 Phase 4：Master 模式下，若這條尺寸線對應到一個
        // SketchAnnotation（implicitOf == uuid，見 Sketch::addImplicitConstraint），
        // 顯示 Mini Toolbar；否則維持舊行為（純 SketchConstraint，無 Mini Toolbar）。
        cad::Sketch* sk = currentActiveSketch();
        if (sk && sk->findAnnotation(uuid)) {
            showAnnotationProperties(sk, uuid);
            auto* bus = core::Application::instance()->eventBus();
            if (bus) bus->publish(core::Events::ANNOTATION_SELECTED, uuid);
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

void UIManager::showAnnotationProperties(cad::Sketch* sketch, const QString& annotationUuid) {
    auto* panel = d->propertyPanel;
    if (!panel || !sketch) return;

    cad::SketchAnnotation* ann = sketch->findAnnotation(annotationUuid);
    if (!ann) return;

    panel->clear();
    d->propAnnotationSketch = sketch;
    d->propAnnotationUuid   = annotationUuid;

    panel->addProperty(tr("字首 Prefix"), ann->prefix, /*editable*/true);
    panel->addProperty(tr("字尾 Suffix"), ann->suffix, /*editable*/true);
    panel->addProperty(tr("精度 Precision"), ann->precision, /*editable*/true);

    QString toleranceModeStr;
    switch (ann->tolerance.mode) {
    case cad::ToleranceMode::None:      toleranceModeStr = tr("無");      break;
    case cad::ToleranceMode::Symmetric: toleranceModeStr = tr("對稱±");   break;
    case cad::ToleranceMode::Deviation: toleranceModeStr = tr("偏差+/-"); break;
    case cad::ToleranceMode::Limit:     toleranceModeStr = tr("極限值");  break;
    case cad::ToleranceMode::Basic:     toleranceModeStr = tr("Basic");   break;
    }
    // 提示可輸入的合法值放在說明列，避免使用者不知道要打什麼
    panel->addProperty(tr("公差模式"), toleranceModeStr, /*editable*/true);
    panel->addProperty(tr("公差模式可選值"),
                        tr("無 / 對稱± / 偏差+/- / 極限值 / Basic"), false);
    panel->addProperty(tr("公差上限"), ann->tolerance.upper, /*editable*/true);
    panel->addProperty(tr("公差下限"), ann->tolerance.lower, /*editable*/true);

    panel->addProperty(tr("Basic Dimension"),      ann->isBasic ? tr("是") : tr("否"), true);
    panel->addProperty(tr("Inspection Dimension"), ann->isInspection ? tr("是") : tr("否"), true);
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

    // ── Loft (ProfileLoftSolid) 專屬 ─────────────────────────
    if (auto* loft = qobject_cast<cad::ProfileLoftSolid*>(feature)) {
        panel->addProperty(tr("來源陣列"),
                           loft->sourceArray() ? loft->sourceArray()->name() : tr("(無)"), false);
        // 體積為 shape() 的即時計算結果（m³，因為 loft 產生的座標沿用
        // document/TrackCenterLine 的公尺制），不是存檔欄位，所以每次開啟
        // 屬性面板都會反映 rebuild() 後的最新形狀。
        const double vol = loft->volume();
        panel->addProperty(tr("體積 (m³)"),
                           loft->hasValidShape() ? QString::number(vol, 'f', 4)
                                                 : tr("(尚未成功放樣)"),
                           false);
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

void UIManager::showCommandMessage(const QString& message, const QString& /*color*/) {
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

const QHash<QString, railway::AlignmentDocument*>& UIManager::tclAlignmentDocs() const
{
    return d->tclAlignmentDocs;
}

railway::AlignmentDocument* UIManager::ensureTclAlignmentDocument(const QString& tclId)
{
    // 與 showAlignmentDataTableRequested / railway.valign-visibility-changed
    // 兩處既有的「取得或建立 per-TCL AlignmentDocument」邏輯相同（見本檔案
    // 對應 lambda），抽出供命令層等其他呼叫者共用。
    auto* docMgr = core::Application::instance()->documentManager();
    auto* doc    = docMgr ? docMgr->currentDocument() : nullptr;
    if (!doc) return nullptr;

    auto* tcl = doc->findTrackCenterLine(tclId);
    if (!tcl) return nullptr;

    railway::AlignmentDocument* aDoc = d->tclAlignmentDocs.value(tclId, nullptr);
    if (aDoc) return aDoc;

    aDoc = new railway::AlignmentDocument(d->mainWindow);
    d->tclAlignmentDocs.insert(tclId, aDoc);

    // 優先從 per-TCL JSON（editSession）載入
    QJsonObject editJson = doc->tclAlignmentData(tclId);
    if (!editJson.isEmpty()) {
        aDoc->fromJson(editJson);
    }

    // 若載入後（或本來就）尚無元素資料/VIP資料，嘗試從 TCL 既有的稠密點位
    // 反推一次（例如 ALD 匯入、尚未經過任何編輯器的情況）。已有資料時為 no-op。
    aDoc->horizontal()->seedFromRawPoints(tcl->horizontal()->rawPoints());
    aDoc->vertical()->seedFromDensePoints(tcl->vertical()->points());

    // 確保 solver 已執行一次（空資料也要 solve，保持 m_result 有效）
    aDoc->horizontal()->solve();
    aDoc->vertical()->solve();

    return aDoc;
}

view::Railway3DAlignmentRenderer* UIManager::railway3DRenderer() const
{
    return d->railway3DRenderer;
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

    // ⚠️ 暫時性 workaround（尚未找到 OCCT 相機/視角切換的真正根因）：
    //    實測發現「直接從另一個平面的 Sketch 切換過來編輯」會導致繪圖
    //    平面錯誤並崩潰，但只要中間先強制切一次 isometric view、再切到
    //    這個 Sketch 對應的正視圖，就完全正常。目前判斷這與 OCCT
    //    V3d_View 的相機/投影矩陣在「兩個標準視圖之間直接切換」時，某些
    //    內部狀態（例如 Camera 的 Up/Direction 或 SetPrivilegedPlane）
    //    沒有被完整刷新有關；先經過 isometric 這個「非標準、三軸都不平行」
    //    的中繼視角，等於強制讓 OCCT 把相機狀態完整重算一輪。
    //    這裡先用這個方式讓功能可用，之後若能實際除錯/複現找到 OCCT 端
    //    的真正根因，應移除此 workaround，直接呼叫下面對應的
    //    setTopView()/setFrontView()/setRightView() + alignToPlane()。
    if (d->cadView) {
        d->cadView->setIsometricView();
    }

    // OSnap 平面設定
    if (d->cadView && d->cadView->snapManager()) {
        d->cadView->snapManager()->setActivePlane(sketch->plane());
        d->cadView->snapManager()->setActiveSketch(sketch);
        d->cadView->snapManager()->setSnapEnabled(false);
    }

    // ⚠️ 根因修正：RubberBand（互動畫圖時的橡皮筋／預覽線——直線、SCS、
    //    螺旋線等所有預覽圖形皆由它負責繪製）內部持有一份「自己的」
    //    cad::Plane* 快取（見 RubberBand::Private::plane），只有在 CadView
    //    建構當下用 PlaneManager::activePlane() 初始化一次；RubberBand::
    //    setPlane() 這個公開介面在全專案中從未被呼叫過。也就是說，不管
    //    使用者切換到哪個 Sketch、哪個平面編輯，RubberBand::planeToWorld()
    //    永遠是用「應用程式啟動當下那個平面」（通常剛好是 XY）去把 2D 預覽
    //    點轉成 3D 世界座標——這才是編輯 YZ 平面 Sketch 時，畫出來的圖形
    //    沒有落在 YZ 平面上的真正原因；純粹修正 2D 座標計算
    //    （screenToPlane()/screenToPlaneD()）並不會影響這個完全獨立、專責
    //    視覺呈現的平面參考。此處補上遺漏的同步呼叫。
    if (d->cadView && d->cadView->rubberBand() && sketch->plane()) {
        d->cadView->rubberBand()->setPlane(sketch->plane());
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

        // ✅ 正視於 Sketch Plane，並顯示格線（仿 ViewManager::onSketchCreated）
        //
        // 順序很重要：先把 ViewGrid 內部的平面（d->plane）更新為這個 Sketch
        // 的平面，再呼叫 setTopView()/setFrontView()/setRightView()／
        // alignToPlane()。這幾個函式內部都可能連帶觸發 fitAll()（而 fitAll()
        // 只要格線目前是啟用狀態就會呼叫 grid->update()）；如果格線的平面還
        // 沒更新，這些「順便」觸發的 update() 會拿舊平面（例如上一個正在編輯
        // 的 Sketch 的平面）計算格線範圍與基準面，等於多做一次錯誤結果，
        // 雖然最後 alignToPlane() 與 fitAll() 仍會用正確平面再刷新一次、
        // 表面上「最終結果正確」，但只要中間任何一步的假設改變（例如格線
        // 尚未 show() 時的隱含 early-return），就可能讓錯誤的中間結果變成
        // 最終顯示結果。把 grid->setPlane() 移到最前面，讓後續每一次
        // update() 用的都已經是正確平面，徹底消除這個時序風險。
        cad::Plane* gridPlane = sketch->plane();

        view::ViewGrid* grid = d->cadView->grid();
        if (grid)
            grid->setPlane(gridPlane);

        if (gridPlane->isXY()) {
            d->cadView->setTopView();
        } else if (gridPlane->isXZ()) {
            d->cadView->setFrontView();
        } else if (gridPlane->isYZ()) {
            d->cadView->setRightView();
        }
        // 自訂平面：alignToPlane 已對齊，不需額外設定標準視角

        d->cadView->alignToPlane(gridPlane);

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

    // 顯示草圖平面的 X 軸、Y 軸、原點（可供束制選取）
    if (d->cadView)
        d->cadView->showSketchAxes(sketch);

    bus->publish(core::Events::SKETCH_ENTERED, QVariant::fromValue(sketch));
    bus->publish("sketch.editStarted", QVariant::fromValue(sketch));
}

void UIManager::onSketchEditEnded()
{
    m_currentActiveSketch = nullptr;

    auto* bus = core::Application::instance()->eventBus();

    // 移除草圖平面參考幾何（X 軸 / Y 軸 / 原點）
    if (d->cadView)
        d->cadView->hideSketchAxes();

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

//    if (d->commandLine && !message.isEmpty())
//        d->commandLine->appendHistory(message);
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    fileMenu->addAction("&New",   QKeySequence::New,  this,          &UIManager::onNewDocument);
    fileMenu->addAction("&Open",  QKeySequence::Open, this,          &UIManager::onOpenDocument);
    fileMenu->addAction("&Save",  QKeySequence::Save, this,          &UIManager::onSaveDocument);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit",  QKeySequence::Quit, d->mainWindow, &QMainWindow::close);
#else
    fileMenu->addAction("&New", this, &UIManager::onNewDocument, QKeySequence::New);
    fileMenu->addAction("&Open", this, &UIManager::onOpenDocument, QKeySequence::Open);
    fileMenu->addAction("&Save", this, &UIManager::onSaveDocument, QKeySequence::Save);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", d->mainWindow, &QMainWindow::close, QKeySequence::Quit);
#endif


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
    // ✅ 修正：先前只補了 setContext()，漏掉重新呼叫 setView()。
    // GripManager::setView() 在此之前（view 尚未就緒時）就已經呼叫過一次，
    // 當時拿到的是尚未就緒的 handle，導致 m_view 之後一直是空/無效的，
    // hitTestGrip() 裡 `if (!m_view.IsNull())` 恆為 false，一律落到寫死的
    // 50.0 fallback 門檻值 —— 這正是 Grip hover/點擊範圍異常固定在
    // threshold=50 的成因。這裡補上正確、已就緒的 view。
    if (d->gripManager && d->cadView)
        d->gripManager->setView(d->cadView->view());
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

// ✅ 新增：統一設定「目前作用中」的 AlignmentDocument，並同步接到 OSnap
void UIManager::setActiveAlignmentDoc(railway::AlignmentDocument* doc) {
    d->alignmentDoc = doc;
    if (d->cadView && d->cadView->snapManager()) {
        // ✅ 修正：OSnap 的鎖點來源不應只有「目前正在編輯」的這一條
        // alignment（doc），而是場景中所有已知的 per-TCL AlignmentDocument
        // （d->tclAlignmentDocs），這樣 FC/AS 等命令取點時，才能吃到「其他」
        // alignment 上的 PI/TS/SC/CS/ST/中點/垂足，而不是只有 active 的那條。
        // 若 doc 本身還不在 d->tclAlignmentDocs 裡（例如尚未被寫入該 hash
        // 的過渡狀態），一併補上，確保它自己也不會被漏掉。
        QVector<railway::AlignmentDocument*> docs;
        docs.reserve(d->tclAlignmentDocs.size() + 1);
        for (railway::AlignmentDocument* ad : d->tclAlignmentDocs) {
            if (ad) docs.append(ad);
        }
        if (doc && !docs.contains(doc)) docs.append(doc);
        d->cadView->snapManager()->setAlignmentDocuments(docs);
    }
    // TC／中點／垂足的來源（每條 TCL 一定都有的持久化資料）不依賴 doc 本身，
    // 獨立刷新一次即可，見 refreshAlignmentOSnapSources() 說明。
    refreshAlignmentOSnapSources();
}

// ✅ 新增：見 UIManager.h 內對 refreshAlignmentOSnapSources() 的說明
void UIManager::refreshAlignmentOSnapSources() {
    if (!d->cadView || !d->cadView->snapManager()) return;

    // ✅ 修正：AlignmentDocument 清單（d->tclAlignmentDocs）只涵蓋「曾經打開
    // 編輯」過的 TCL，場景中其他只是單純顯示、從未被編輯過的 TCL 完全沒有
    // 對應的 AlignmentDocument，OSnap 因此永遠吸不到它們的鎖點。這裡改用
    // 「每條 TCL 一定都有」的持久化資料 tcl->horizontal()——正是
    // AlignmentRenderer 實際渲染畫面用的同一份資料——讓 TC／中點／垂足這
    // 三種 snap 對任何可視 alignment 都能生效，不受該 TCL 是否曾被打開編輯
    // 過的限制。
    QVector<railway::HorizontalAlignment*> haligns;
    auto* app = core::Application::instance();
    cad::Document* curDoc = app ? app->documentManager()->currentDocument() : nullptr;
    if (curDoc) {
        haligns.reserve(curDoc->trackCenterLines().size());
        for (railway::TrackCenterLine* tcl : curDoc->trackCenterLines()) {
            if (tcl && tcl->horizontal()) haligns.append(tcl->horizontal());
        }
    }
    d->cadView->snapManager()->setHorizontalAlignments(haligns);
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

    // ✅ 修正：不要等使用者去開垂直斷面 dock／切換某條 TCL 的可視性才把
    // alignment 接上 OSnap——文件一旦成為「目前文件」，就把它底下每條 TCL
    // 的 tcl->horizontal() 主動推給 OSnap 一次，確保剛開檔就直接下
    // FC/AS/FT 等取點指令時，alignment 的鎖點也立刻可用。
    refreshAlignmentOSnapSources();

    // ✅ 文件裡的 TCL 清單本身異動時（新增/刪除 TrackCenterLine），同步刷新
    // 一次，避免清單跟畫面上實際可視的 alignment 漸漸脫節。
    if (doc) {
        connect(doc, &cad::Document::trackCenterLinesChanged,
                this, &UIManager::refreshAlignmentOSnapSources,
                Qt::UniqueConnection);
    }

    if (d->propertyPanel)
        d->propertyPanel->clear();
}

cad::ConstraintPickSession* UIManager::constraintPickSession() const
{
    return d->pickSession;
}

void UIManager::beginGeomConstraintPick(cad::Sketch* sketch,
                                        cad::ConstraintType type,
                                        int /*requiredCount*/)
{
    if (!d->pickSession || !sketch) return;

    // 1. 啟動 session
    d->pickSession->begin(sketch, type, 0.0, QString(), true);

    // 2. 切換到 GetGeom 模式
    if (d->cadView) {
        d->cadView->setMode(view::InteractionMode::GetGeom);
        // 告知 CadView pickSession 正在等待（command 已 finished，hasCmd = false）
        d->cadView->setConstraintPickActive(true);
    }

    // 3. 更新 status bar 提示
    setStatusMessage(d->pickSession->promptText());
}

} // namespace ui
} // namespace aicad