/**
 * @file CadView.cpp
 * @brief CadView 類別實作 (重構版)
 * @author Felicia
 * @date 2024-12-04
 */

#include "CadView.h"
#include "DimPreviewOverlay.h"
#include "RubberBand.h"
#include "ViewGrid.h"
#include "cad/Feature.h"
#include "cad/Sketch.h"
#include "cad/Document.h"
#include "cad/PlaneManager.h"
#include "cad/grips/GripManager.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "core/CommandLineManager.h"
#include "core/DocumentManager.h"
#include "osnap/OSnapManager.h"
#include "ui/GripEventFilter.h"
#include "ui/UIManager.h"
#include "cad/grips/GripManager.h"
#include "command/CommandManager.h"
#include "geometry/GeometryBuilder.h"
#include "core/geometry/ProjectOrigin.h"

#include "cad/sketch/DimensionLineAIS.h"
#include "cad/sketch/SketchConstraint.h"
#include "cad/sketch/SketchAxisAIS.h"

#include <QDebug>
#include <QTimer>
#include <QMouseEvent>
#include <QCursor>
#include <QKeyEvent>
#include <cmath>
#include <QToolTip>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QFont>
#include <QFontMetrics>

#include <Aspect_DisplayConnection.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <AIS_ViewCube.hxx>
#include <AIS_AnimationCamera.hxx>
#include <Quantity_Color.hxx>
#include <gp_Pln.hxx>
#include <gp_Lin.hxx>
#include <gp_Dir.hxx>
#include <IntAna_IntConicQuad.hxx>
#include <Precision.hxx>
#include <AIS_SelectionScheme.hxx>
#include <SelectMgr_ViewerSelector.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_DisplayPriority.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <Geom_Surface.hxx>
#include <Geom_Plane.hxx>
#include <Graphic3d_ClipPlane.hxx>
#include <Graphic3d_SequenceOfHClipPlane.hxx>
#include <Prs3d_Drawer.hxx>
#include <Graphic3d_AspectLine3d.hxx>

#ifdef _WIN32
#  include <WNT_Window.hxx>
#elif defined(__APPLE__)
#  include <Cocoa_Window.hxx>
#else
#  include <Xw_Window.hxx>
#endif

using namespace aicad::core;
using namespace aicad::cad;

namespace aicad {
namespace view {

// 輔助函式：Qt 座標轉 OCCT 座標
#if defined(_WIN32) || defined(__APPLE__)
static void QtToOCCT(const QWidget* widget, const QPoint& qtPos,
                     Standard_Integer& occX, Standard_Integer& occY) {
    qreal dpr = widget->devicePixelRatio();
    occX = static_cast<Standard_Integer>(qtPos.x() * dpr);
    occY = static_cast<Standard_Integer>(qtPos.y() * dpr);
#else
static void QtToOCCT(const QWidget* /*widget*/, const QPoint& qtPos,
                     Standard_Integer& occX, Standard_Integer& occY) {
    occX = qtPos.x();
    occY = qtPos.y();
#endif
}

// 輔助函式：若給定的 AIS 物件是尺寸線（AIS_DimensionLine，例如點擊到尺寸文字），
// 回傳其對應的約束 UUID；否則回傳空字串。
// AIS_DimensionLine 由 ConstraintOverlayManager 自行管理顯示（m_dimLines），
// 不會註冊進 d->aisToGeomUuid，所以一般的幾何 uuid 反查在這裡會失敗，
// 需要另外用 DownCast 判斷型別後取 constraintUuid()。
static QString constraintUuidForAIS(const Handle(AIS_InteractiveObject)& obj) {
    if (obj.IsNull()) return QString();
    Handle(AIS_DimensionLine) dim = Handle(AIS_DimensionLine)::DownCast(obj);
    if (!dim.IsNull()) return dim->constraintUuid();
    return QString();
}

class CadView::Private {
public:
    // OCCT 核心物件
    Handle(V3d_Viewer) viewer;
    Handle(V3d_View) view;
    Handle(AIS_InteractiveContext) context;
    Handle(AIS_ViewCube) viewCube;    
    Handle(Graphic3d_ClipPlane) sectionClipPlane;
    QTimer* viewCubeTimer;

    QMap<QString, Handle(AIS_Shape)> regionAisMap;   ///< regionUuid → AIS face
    QString                          activeRegionUuid; ///< 目前高亮的 region

    // 關聯的文件
    cad::Document* document;

    // 輔助物件
    RubberBand* rubberBand;
    ViewGrid* grid;

    // 視圖狀態
    ViewType viewType;
    InteractionMode mode;
    bool viewInitialized;
    bool gridEnabled;

    // TM2 座標原點偏移（僅供狀態列顯示用，幾何管道已全程 double 不需補償）
    double coordinateOffsetE = 0.0;
    double coordinateOffsetN = 0.0;
    // 滑鼠狀態
    QPoint lastMousePos;
    bool mousePressed;
    Qt::MouseButton pressedButton;
    // ✅ FIX: 追蹤中間鍵是否正在按壓
    bool middleButtonPressed;
    QMap<AIS_InteractiveObject*, QString>  aisToFeatureId;
    QMap<AIS_InteractiveObject*, QString>  aisToGeomUuid;
    QMap<AIS_InteractiveObject*, int>     aisToGeomIndex;
    QHash<cad::Sketch*, QString>           sketchFeatureIds;

    QSet<int>                              selectedGeomIndices;
    ui::GripEventFilter* gripFilter;
    GripManager*         gripManager;
    bool commandInProgress = false;
    bool isDisplayingAllFeatures = false;
    bool constraintPickActive = false;  ///< pickSession 等待選取中（GetGeom 但 command 已 finished）

    // ── 草圖平面參考幾何 AIS（X 軸 / Y 軸 / 原點）────────────────────────
    // 用 AIS_Shape 基底型別儲存（SketchAxisAIS/SketchOriginAIS 繼承 AIS_Shape）
    Handle(AIS_Shape) sketchXAxisAIS;
    Handle(AIS_Shape) sketchYAxisAIS;
    Handle(AIS_Shape) sketchOriginAIS;

    QList<OverlayEntry> overlayObjects;
    QVector2D            dimLineAnchor2D;    // ✅ Task E: PlaceDimLine 錨點（草圖平面 2D）
    QVector2D            dimPreviewMousePt; // GDIM: 目前滑鼠草圖座標（overlay 更新用）

    // ── 尺寸線拖曳狀態 ──────────────────────────────────────────────────────
    QString              dragDimUuid;        ///< 正在拖曳的約束 UUID（空 = 無拖曳）
    QVector2D            dragDimStartMouse;  ///< 拖曳起始的草圖平面座標
    double               dragDimBaseOffsetX = 0.0;  ///< 拖曳前的舊偏移 X
    double               dragDimBaseOffsetY = 0.0;  ///< 拖曳前的舊偏移 Y

    // ── 窗選 / 穿越窗選（Window / Crossing box selection）────────────────────
    // Sketch edit 與 H-Alignment edit 共用同一套機制：
    //   由左至右 → 窗選（Window）：只選完全被框住的物件
    //   由右至左 → 穿越窗選（Crossing）：與框相交/接觸即選取
    // 操作方式：滑鼠左鍵點一下放開（記錄起點）→ 移動滑鼠（不必按住）→
    //          再點一下放開完成選取。若使用者改為按住拖曳也同樣支援。
    bool              boxSelectArmed      = false;  ///< 已記錄起點，等待完成（拖曳中或等待第二次點擊）
    bool              boxSelectActive     = false;  ///< 選取框正在顯示（拖曳超過門檻，或已進入等待第二次點擊）
    bool              boxSelectWaitingSecondClick = false;  ///< 第一次點擊已放開，選取框跟隨滑鼠自由移動，等待第二次點擊
    bool              boxSelectSketchMode = false;  ///< true=Sketch 幾何選取語意；false=一般（H-Alignment 等）
    bool              boxSelectAdditive   = false;  ///< Shift 按下＝疊加選取
    Standard_Integer  boxSelectStartX = 0, boxSelectStartY = 0;  ///< 起點（OCCT 物理像素，供 SelectRectangle 使用）
    QPoint            boxSelectStartQt;              ///< 起點（Qt 邏輯座標，供選取框視覺 unproject 使用）
    Handle(Prs3d_Presentation) boxSelectPresentation; ///< 選取框顯示物件（沿用 RubberBand 相同技術，非 AIS 物件）
    QVector<Handle(SelectMgr_EntityOwner)> boxSelectSavedOwners; ///< 框選開始前的選取狀態快照（預覽/取消時還原用）

    // ── 籬選(Fence) / 多邊形窗選(WPolygon) / 多邊形框選(CPolygon) ─────────────
    CadView::BoxSelectShape boxSelectShape = CadView::BoxSelectShape::Rectangle;
    QVector<QPoint>   boxSelectPolyQt;    ///< 已確定的多邊形/籬選頂點（Qt 邏輯座標），第一點＝框選起點
    QString           boxSelectShapeKeyBuffer;  ///< 累積鍵盤輸入緩衝，偵測 F / WP / CP（見 keyPressEvent）

    Private()
        : document(nullptr)
        , rubberBand(nullptr)
        , grid(nullptr)
        , viewType(ViewType::Isometric)
        , mode(InteractionMode::Idle)
        , viewInitialized(false)
        , gridEnabled(false)
        , mousePressed(false)
        , pressedButton(Qt::NoButton)
        , middleButtonPressed(false)   // ✅ FIX: 初始化中間鍵狀態
        , gripFilter(nullptr)
        , gripManager(nullptr)
    {
    }

    ~Private() {
        delete rubberBand;
        delete grid;
    }
};

CadView::CadView(QWidget* parent)
    : QWidget(parent)
    , d(new Private())
{
    // 設定 Widget 屬性
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NativeWindow);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setBackgroundRole(QPalette::NoRole);

    // ✅ Force native window creation NOW, before any OCCT calls
    winId();  // triggers WA_NativeWindow platform window creation

    // Create finish sketch button
    m_finishSketchButton = new QPushButton("Finish Sketch", this);
    m_finishSketchButton->setGeometry(width() - 120, 10, 110, 30);
    m_finishSketchButton->hide();

    m_finishSketchButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #4CAF50;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 5px 10px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #45a049;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #3d8b40;"
        "}"
        );

    connect(m_finishSketchButton, &QPushButton::clicked,
            this, &CadView::onFinishSketchClicked);

    // ── Return button（H-Alignment edit 模式，右上角圖示按鈕）──────────────
    m_returnAlignmentButton = new QPushButton(this);
    m_returnAlignmentButton->setIcon(QIcon(":/icons/return.png"));
    m_returnAlignmentButton->setIconSize(QSize(24, 24));
    m_returnAlignmentButton->setFixedSize(36, 36);
    m_returnAlignmentButton->setToolTip(tr("結束 Alignment 編輯模式"));
    m_returnAlignmentButton->setStyleSheet(
        "QPushButton {"
        "  background-color: rgba(60,60,60,200);"
        "  border: 1px solid #888;"
        "  border-radius: 4px;"
        "}"
        "QPushButton:hover {"
        "  background-color: rgba(80,80,80,230);"
        "}"
        "QPushButton:pressed {"
        "  background-color: rgba(40,40,40,255);"
        "}");
    m_returnAlignmentButton->hide();
    connect(m_returnAlignmentButton, &QPushButton::clicked,
            this, &CadView::returnAlignmentRequested);

    // GDIM: 尺寸預覽用 OCCT Presentation（仿 RubberBand），不使用 Qt widget overlay
    m_dimOverlay = new DimPreviewOverlay(this);
    // context 在 initializeViewer() 後才有效，在 showEvent 中呼叫 setContext

    // ✅ Do NOT call initializeViewer() here.
    // Defer to showEvent so NSView is fully realized.

    m_selectionFilter = "all";

    qDebug() << "[CadView] Created";
}

CadView::~CadView() {
    qDebug() << "[CadView] Destroying...";
    delete d;
}

void CadView::initializeViewer() {
    qDebug() << "[CadView] Initializing OCCT viewer...";

    // 確認 native window handle 已存在（macOS 必要）
    WId wid = winId();
    if (wid == 0) {
        qCritical() << "[CadView] winId() == 0, native window not ready!";
        QTimer::singleShot(100, this, &CadView::initializeViewer);
        return;
    }

#ifdef __APPLE__
    NSView* nsView = reinterpret_cast<NSView*>(wid);
    if (!isVisible() || !internalWinId()) {
        qCritical() << "[CadView] NSView not yet attached to NSWindow, deferring...";
        QTimer::singleShot(100, this, &CadView::initializeViewer);
        return;
    }
#endif

// 建立顯示連接
#if defined(_WIN32) || defined(__APPLE__)
    Handle(Aspect_DisplayConnection) displayConnection = new Aspect_DisplayConnection();
#else
    Handle(Aspect_DisplayConnection) displayConnection = new Aspect_DisplayConnection("");
#endif

    // 建立圖形驅動
    Handle(OpenGl_GraphicDriver) graphicDriver = new OpenGl_GraphicDriver(displayConnection);

    // VMware / 軟體 OpenGL 容錯：關閉 OpenGL 3.2+ core profile 強制要求
#ifdef __APPLE__
    OpenGl_Caps& caps = graphicDriver->ChangeOptions();
    caps.contextCompatible = Standard_True;   // 允許 compatibility profile
    caps.buffersNoSwap     = Standard_False;
    caps.swapInterval      = 0;               // VMware 下關閉 vsync 同步等待
    // ✅ VMware: disable features that may not be supported
    caps.useSystemBuffer    = Standard_True;  // avoid FBO issues in VMware
#endif

    // 建立視圖器
    d->viewer = new V3d_Viewer(graphicDriver);
    d->viewer->SetDefaultLights();
    d->viewer->SetLightOn();

    // 建立視圖
    d->view = d->viewer->CreateView();

// 建立視窗
#ifdef _WIN32
    Handle(WNT_Window) window = new WNT_Window((Aspect_Handle)winId());
#elif defined(__APPLE__)
    Handle(Cocoa_Window) window = new Cocoa_Window((NSView*)winId());
#else
    Handle(Xw_Window) window = new Xw_Window(displayConnection, (Aspect_Drawable)winId());
#endif

    // ✅ Wrap SetWindow in a try-catch to get a real error message
    try {
        d->view->SetWindow(window);
    } catch (const Standard_Failure& e) {
        qCritical() << "[CadView] OCCT SetWindow failed:" << e.GetMessageString();
        return;
    } catch (...) {
        qCritical() << "[CadView] OCCT SetWindow failed: unknown exception";
        return;
    }

    if (!window->IsMapped()) {
        window->Map();
    }

    // 設定背景
    d->view->SetBackgroundColor(Quantity_NOC_GRAY80);
    d->view->MustBeResized();

    // 建立互動上下文
    d->context = new AIS_InteractiveContext(d->viewer);
    d->context->SetDisplayMode(AIS_Shaded, Standard_True);

    // ── 設定選中高亮色（避免與背景色混淆）──────────────────────
    {
        // 選中樣式：亮黃色，明顯區別於 GRAY80 背景
        Handle(Prs3d_Drawer) selStyle = new Prs3d_Drawer();
        selStyle->SetColor(Quantity_NOC_YELLOW);
        selStyle->SetupOwnShadingAspect();
        // selStyle->WireAspect()->SetWidth(3.0);
        // selStyle->WireAspect()->SetColor(Quantity_NOC_YELLOW);
        d->context->SetSelectionStyle(selStyle);

        // 預偵測（hover）高亮色：橘色
        Handle(Prs3d_Drawer) hilightStyle = new Prs3d_Drawer();
        hilightStyle->SetColor(Quantity_NOC_ORANGE);
        hilightStyle->SetupOwnShadingAspect();
        // hilightStyle->WireAspect()->SetWidth(2.0);
        // hilightStyle->WireAspect()->SetColor(Quantity_NOC_ORANGE);
        d->context->SetHighlightStyle(hilightStyle);
    }

    // 建立 ViewCube
    d->viewCube = new AIS_ViewCube();
    d->viewCube->SetBoxColor(Quantity_NOC_GRAY75);
    d->viewCube->SetSize(55);
    d->viewCube->SetFontHeight(12);
    d->viewCube->SetAxesLabels("X", "Y", "Z");
    d->viewCube->SetTransformPersistence(
        new Graphic3d_TransformPers(
            Graphic3d_TMF_TriedronPers,
            Aspect_TOTP_RIGHT_UPPER,
            Graphic3d_Vec2i(85, 85)
            )
        );
    d->context->Display(d->viewCube, Standard_False);

    // 建立輔助物件
    d->rubberBand = new RubberBand(d->context, this);
    // GDIM 預覽 overlay 使用 OCCT context
    if (m_dimOverlay)
        m_dimOverlay->setContext(d->context);
    d->grid = new ViewGrid(d->viewer, this);
    d->grid->setView(d->view);

    // 設定初始視角
    setViewType(ViewType::Isometric);

    // ── 初始化 OSnap ───────────────────────────────────────────────────────
    m_snapManager = new aicad::osnap::OSnapManager(this);
    m_snapManager->initialize(d->context, d->view);
    m_snapManager->detector().addExcludedObject(d->viewCube);

    // 預設設定（可根據需求調整）
    aicad::osnap::OSnapSettings settings;
    settings.enabledTypes   = aicad::osnap::SnapType::Standard;
    settings.pickPixelRadius = 12.0;
    settings.magnetRadius    = 8.0;
    settings.showTooltip     = true;
    m_snapManager->setSettings(settings);

    // 連接 OSnap 確認事件 → 通知 Command 系統
    connect(m_snapManager, &aicad::osnap::OSnapManager::snapConfirmed,
            this, [this](const gp_Pnt& /*pt*/, aicad::osnap::SnapType /*type*/) {
                auto* bus = aicad::core::Application::instance()->eventBus();
                if (!bus) return;

                // ✅ 改用 snapPoint2DF()（double 版），保持 TM2 大座標精度
                std::optional<QPointF> pt2d = m_snapManager->snapPoint2DF();
                QPointF planePt = pt2d.has_value()
                                ? pt2d.value()
                                : screenToPlaneD(mapFromGlobal(QCursor::pos()));

                QVariantMap data;
                data["point"] = QVariant::fromValue(planePt);   // QPointF（double）
                bus->publish(core::Events::POINT_ACQUIRED, data);
                Q_EMIT pointAcquired(planePt);                   // signal 已改為 QPointF
            });

    qDebug() << "[CadView] OSnapManager initialized";

    connect(m_snapManager, &osnap::OSnapManager::snapLocked,
            this, [this](const osnap::SnapCandidate& c) {
                if (!m_snapManager->settings().showTooltip) return;
                // 將 snap 世界座標轉成螢幕座標
                Standard_Integer sx, sy;
                d->view->Convert(c.worldPoint.X(), c.worldPoint.Y(), c.worldPoint.Z(),
                                 sx, sy);
                QPoint screenPt = mapToGlobal(QPoint(sx, sy - 20));
                QToolTip::showText(screenPt, osnap::snapTypeName(c.type), this);
            });

    connect(m_snapManager, &osnap::OSnapManager::snapCleared,
            this, []() {
                QToolTip::hideText();
            });

    // 延遲初始化
    QTimer::singleShot(0, this, [this]() {
        if (!d->view.IsNull()) {
            d->view->MustBeResized();
            d->view->Redraw();

            // ✅ 設定視圖已初始化並發出信號
            d->viewInitialized = true;
            qDebug() << "[CadView] View initialized, emitting signal";
            // 發出視圖就緒信號
            Q_EMIT viewInitialized();
        }
    });

    // 訂閱事件
    using namespace core;
    EventBus* bus = Application::instance()->eventBus();
    if (bus) {
        bus->subscribe(Events::FEATURE_CREATED, this, [this](const QVariant& data) {
            Q_UNUSED(data);
            displayAllFeatures();
        });

        // ── 窗選提示階段輸入 F / WP / CP：切換為籬選 / 多邊形窗選 / 多邊形框選 ──
        // 命令列文字輸入與選項按鈕點擊分別會傳回 label（例如「籬選」）或
        // shortcut（例如「F」），這裡兩種都比對，確保兩種輸入方式都能觸發。
        bus->subscribe(Events::OPTION_SELECTED, this, [this](const QVariant& data) {
            if (!d->boxSelectArmed) return;
            const QString opt = data.toString();

            if (d->boxSelectShape == BoxSelectShape::Rectangle) {
                // 矩形窗選階段：F/WP/CP 切換為籬選/多邊形窗選/多邊形框選
                if (opt.compare(tr("籬選"), Qt::CaseInsensitive) == 0 ||
                    opt.compare("F", Qt::CaseInsensitive) == 0) {
                    beginBoxSelectShapeMode(BoxSelectShape::Fence);
                } else if (opt.compare(tr("多邊形窗選"), Qt::CaseInsensitive) == 0 ||
                           opt.compare("WP", Qt::CaseInsensitive) == 0) {
                    beginBoxSelectShapeMode(BoxSelectShape::WPolygon);
                } else if (opt.compare(tr("多邊形框選"), Qt::CaseInsensitive) == 0 ||
                           opt.compare("CP", Qt::CaseInsensitive) == 0) {
                    beginBoxSelectShapeMode(BoxSelectShape::CPolygon);
                }
                return;
            }

            // ── 籬選 / 多邊形窗選 / 多邊形框選頂點收集中 ──────────────────────
            // 命令列在「等待輸入」狀態下，對空白輸入（純按 Enter 或 Space，
            // 沒有先輸入文字）一律會發布空字串的 OPTION_SELECTED；藉此判斷
            // 使用者是要完成目前的籬選/多邊形選取。
            if (opt.isEmpty()) {
                finishBoxSelectPolygon();
            }
        });

        bus->subscribe(Events::FEATURE_UPDATED, this, [this](const QVariant& data) {
            Q_UNUSED(data);
            // ✅ CHANGE: Force immediate viewer update
            if (!d->context.IsNull()) {
                d->context->UpdateCurrentViewer();
            }
            displayAllFeatures();
        });


        bus->subscribe("feature.visibility-changed", this,
                       [this](const QVariant& v) {
                           QVariantMap data = v.toMap();
                           QString itemId   = data["itemId"].toString();
                           bool    visible  = data["visible"].toBool();

                           cad::Feature* feature = d->document->findFeature(itemId);
                           if (!feature) return;

                           feature->setVisible(visible);

                           if (auto* sketch = qobject_cast<cad::Sketch*>(feature)) {

                               if (!visible) {
                                   // ✅ 順序非常重要：
                                   // ① 先 detach grips（釋放對 AIS handles 的參考）
                                   d->gripManager->detach();
                                   d->gripFilter->clearSketchPlane();

                                   // ② 再清除 map 條目
                                   for (const auto& obj : sketch->aisShapes()) {
                                       if (!obj.IsNull()) {
                                           d->aisToFeatureId.remove(obj.get());
                                       }
                                   }

                                   // ③ 最後才 erase from context
                                   sketch->eraseFromContext(d->context);

                               } else {
                                   // visible: 顯示並重新註冊
                                   QList<Handle(AIS_InteractiveObject)> shapes =
                                       sketch->displayInContext(d->context);

                                   // 清除舊條目
                                   for (auto it = d->aisToFeatureId.begin();
                                        it != d->aisToFeatureId.end(); ) {
                                       if (it.value() == itemId)
                                           it = d->aisToFeatureId.erase(it);
                                       else
                                           ++it;
                                   }

                                   d->sketchFeatureIds[sketch] = itemId;
                                   connect(sketch, &cad::Sketch::rebuilt,
                                           this, &CadView::onSketchRebuilt,
                                           Qt::UniqueConnection);
                               }

                           } else if (!feature->shape().IsNull()) {
                               // 非 Sketch feature
                               if (!visible) {
                                   // ① detach grips first
                                   d->gripManager->detach();

                                   // ② find and erase
                                   for (auto it = d->aisToFeatureId.begin();
                                        it != d->aisToFeatureId.end(); ++it) {
                                       if (it.value() == itemId) {
                                           Handle(AIS_InteractiveObject) obj =
                                               Handle(AIS_InteractiveObject)::DownCast(
                                                   static_cast<Standard_Transient*>(it.key()));
                                           if (!obj.IsNull())
                                               d->context->Erase(obj, Standard_False);
                                           d->aisToFeatureId.erase(it);
                                           break;
                                       }
                                   }
                               } else {
                                   Handle(AIS_Shape) aisShape = new AIS_Shape(feature->shape());
                                   aisShape->SetColor(Quantity_NOC_YELLOW);
                                   d->context->Display(aisShape, Standard_False);
                                   d->aisToFeatureId[aisShape.get()] = itemId;
                               }
                           }

                           d->context->UpdateCurrentViewer();
                       });    }

    qDebug() << "[CadView] OCCT viewer initialized";
}

// ✅ 新增：檢查視圖是否已初始化的方法
bool CadView::isViewInitialized() const {
    return d->viewInitialized;
}

void CadView::setSelectionFilter(const QString& filter) {
    m_selectionFilter = filter;
    qDebug() << "[CadView] Selection filter set to:" << filter;
}

void CadView::highlightSelectablePlanes(bool highlight) {
    if (!d->context) return;

    qDebug() << "[CadView] Highlight selectable planes:" << highlight;

    // 在 highlightSelectablePlanes 加入參考平面時，也同步排除它們：
    for (const Handle(AIS_Shape)& plane : m_referencePlanes) {
        m_snapManager->detector().addExcludedObject(plane);
    }

    AIS_ListOfInteractive allObjects;
    d->context->DisplayedObjects(allObjects);

    for (AIS_ListOfInteractive::Iterator it(allObjects); it.More(); it.Next()) {
        Handle(AIS_InteractiveObject) obj = it.Value();
        Handle(AIS_Shape) shape = Handle(AIS_Shape)::DownCast(obj);

        if (shape.IsNull()) continue;

        if (isReferencePlane(shape)) {
            if (highlight) {
                d->context->SetColor(shape, Quantity_NOC_YELLOW, Standard_False);
                d->context->SetTransparency(shape, 0.7, Standard_False);
            } else {
                d->context->SetColor(shape, Quantity_NOC_GRAY80, Standard_False);
                d->context->SetTransparency(shape, 0.9, Standard_False);
            }
        }
    }

    d->context->UpdateCurrentViewer();
}

bool CadView::isReferencePlane(const Handle(AIS_Shape)& shape) {
    for (const Handle(AIS_Shape)& plane : m_referencePlanes) {
        if (shape == plane) return true;
    }

    TopoDS_Shape topoShape = shape->Shape();
    if (topoShape.ShapeType() == TopAbs_FACE) {
        return true;
    }

    return false;
}

QString CadView::identifyPlane(const Handle(AIS_Shape)& shape) {
    TopoDS_Shape topoShape = shape->Shape();

    if (topoShape.ShapeType() != TopAbs_FACE) {
        return "UNKNOWN";
    }

    TopoDS_Face face = TopoDS::Face(topoShape);
    Handle(Geom_Surface) surface = BRep_Tool::Surface(face);
    Handle(Geom_Plane) plane = Handle(Geom_Plane)::DownCast(surface);

    if (!plane) {
        return "UNKNOWN";
    }

    gp_Pln gpPlane = plane->Pln();
    gp_Dir normal = gpPlane.Axis().Direction();

    const double tolerance = 0.1;

    if (std::abs(normal.Z() - 1.0) < tolerance ||
        std::abs(normal.Z() + 1.0) < tolerance) {
        return "XY";
    } else if (std::abs(normal.Y() - 1.0) < tolerance ||
               std::abs(normal.Y() + 1.0) < tolerance) {
        return "XZ";
    } else if (std::abs(normal.X() - 1.0) < tolerance ||
               std::abs(normal.X() + 1.0) < tolerance) {
        return "YZ";
    }

    return "UNKNOWN";
}

ViewGrid* CadView::grid() const {
    return d->grid;
}

osnap::OSnapManager*  CadView::snapManager() const
{
    return m_snapManager;
}

void CadView::setSectionPlane(const gp_Pln& plane) {
    if (d->view.IsNull()) return;

    // 若舊的還在，先移除
    if (!d->sectionClipPlane.IsNull())
        d->view->RemoveClipPlane(d->sectionClipPlane);

    d->sectionClipPlane = new Graphic3d_ClipPlane(plane);
    d->sectionClipPlane->SetOn(Standard_True);
    d->sectionClipPlane->SetCapping(Standard_True);      // 顯示剖切填充面

    Graphic3d_MaterialAspect mat;
    mat.SetColor(Quantity_Color(0.75, 0.75, 0.80, Quantity_TOC_RGB));
    mat.SetTransparency(0.2f);
    d->sectionClipPlane->SetCappingMaterial(mat);

    d->view->AddClipPlane(d->sectionClipPlane);
    d->view->Redraw();
}

void CadView::clearSectionPlane() {
    if (d->view.IsNull() || d->sectionClipPlane.IsNull()) return;
    d->view->RemoveClipPlane(d->sectionClipPlane);
    d->sectionClipPlane.Nullify();
    d->view->Redraw();
}

bool CadView::isSectionActive() const {
    return !d->sectionClipPlane.IsNull();
}

// CadView.cpp 實作（放在適當位置）：
QStringList CadView::selectedGeomUuids() const
{
    QStringList result;
    if (!d->context || !d->document) return result;

    for (d->context->InitSelected();
         d->context->MoreSelected();
         d->context->NextSelected())
    {
        // 使用 AIS_InteractiveObject（不限於 AIS_Shape），
        // 讓 SketchAxisAIS / SketchOriginAIS 也能被識別
        Handle(AIS_InteractiveObject) obj = d->context->SelectedInteractive();
        if (obj.IsNull()) continue;

        QString uuid = d->aisToGeomUuid.value(obj.get());
        if (uuid.isEmpty())
            uuid = constraintUuidForAIS(obj);   // 選到尺寸線/文字 → 約束 UUID
        if (!uuid.isEmpty())
            result << uuid;
    }
    return result;
}

QString CadView::detectedConstraintUuid() const
{
    if (d->context.IsNull() || !d->context->HasDetected())
        return QString();
    return constraintUuidForAIS(d->context->DetectedInteractive());
}

void CadView::clearSketchGeomSelection()
{
    if (!d->context.IsNull())
        d->context->ClearSelected(Standard_True);
    if (auto* bus = Application::instance()->eventBus())
        bus->publish(Events::SKETCH_GEOM_CLEARED, QVariant{});
}

void CadView::showSketchAxes(cad::Sketch* sketch)
{
    if (!sketch || !sketch->plane() || d->context.IsNull()) return;

    // 先移除舊的
    hideSketchAxes();

    auto* plane = sketch->plane();
    QVector3D o  = plane->origin();
    QVector3D xa = plane->xAxis();
    //QVector3D ya = plane->yAxis();
    QVector3D n  = plane->normal();

    gp_Pnt origin(o.x(), o.y(), o.z());
    gp_Dir normDir(n.x(), n.y(), n.z());
    gp_Dir xDir(xa.x(), xa.y(), xa.z());
    gp_Ax3 ax3(origin, normDir, xDir);

    // 軸長：視草圖大小而定（暫定 200mm；之後可改為自動適應）
    double halfLen = 200.0;
    double origSize = halfLen * 0.06;   // 原點十字大小

    QString skUuid = sketch->id();

    // 建立三個 AIS 物件
    d->sketchXAxisAIS   = new cad::SketchAxisAIS(ax3, halfLen,
                              cad::SketchAxisAIS::AxisType::X,
                              "sketch_xaxis:" + skUuid);
    d->sketchYAxisAIS   = new cad::SketchAxisAIS(ax3, halfLen,
                              cad::SketchAxisAIS::AxisType::Y,
                              "sketch_yaxis:" + skUuid);
    d->sketchOriginAIS  = new cad::SketchOriginAIS(ax3, origSize,
                              "sketch_origin:" + skUuid);

    // 登記到 aisToGeomUuid（供 selectedGeomUuids() 使用）
    d->aisToGeomUuid[static_cast<AIS_InteractiveObject*>(d->sketchXAxisAIS.get())]
        = "sketch_xaxis:"  + skUuid;
    d->aisToGeomUuid[static_cast<AIS_InteractiveObject*>(d->sketchYAxisAIS.get())]
        = "sketch_yaxis:"  + skUuid;
    d->aisToGeomUuid[static_cast<AIS_InteractiveObject*>(d->sketchOriginAIS.get())]
        = "sketch_origin:" + skUuid;

    // Display + Activate（與 SketchPointAIS 一致的標準模式：
    // 兩參數 Display，再單獨呼叫 Activate 啟用 selection mode 0）
    d->context->Display(d->sketchXAxisAIS,  Standard_False);
    d->context->Display(d->sketchYAxisAIS,  Standard_False);
    d->context->Display(d->sketchOriginAIS, Standard_False);

    d->context->Activate(d->sketchXAxisAIS,  0, Standard_False);
    d->context->Activate(d->sketchYAxisAIS,  0, Standard_False);
    d->context->Activate(d->sketchOriginAIS, 0, Standard_False);

    // 放寬選取容差（必須透過 context 呼叫，且在 Activate 之後，
    // 這時 selection mode 0 對應的 SelectMgr_Selection 已存在）：
    // 軸線稍寬方便點擊細線，原點更寬方便點擊單點。
    d->context->SetSelectionSensitivity(d->sketchXAxisAIS,  0, 4.0);
    d->context->SetSelectionSensitivity(d->sketchYAxisAIS,  0, 4.0);
    d->context->SetSelectionSensitivity(d->sketchOriginAIS, 0, 8.0);

    d->context->UpdateCurrentViewer();
}

void CadView::hideSketchAxes()
{
    if (d->context.IsNull()) return;

    auto remove = [&](auto& handle) {
        if (!handle.IsNull()) {
            d->aisToGeomUuid.remove(
                static_cast<AIS_InteractiveObject*>(handle.get()));
            d->context->Remove(handle, Standard_False);
            handle.Nullify();
        }
    };
    remove(d->sketchXAxisAIS);
    remove(d->sketchYAxisAIS);
    remove(d->sketchOriginAIS);

    d->context->UpdateCurrentViewer();
}

void CadView::setGripManager(GripManager* mgr, ui::GripEventFilter* filter) {
    d->gripManager = mgr;
    d->gripFilter  = filter;
    if (d->gripFilter) {
        d->gripFilter->setBoxSelectActiveQuery([this]() { return isBoxSelectArmed(); });
    }
}

bool CadView::hasActiveGrips() const {
    return d->gripManager && d->gripManager->hasActiveGrips();
}

bool CadView::turnOffActiveGrips() {
    const bool hadGrips = d->gripManager && d->gripManager->hasActiveGrips();

    if (hadGrips) {
        // 若正在拖曳 grip（click-to-place 中），先取消該次移動
        if (d->gripManager->isGripSelected()) {
            d->gripManager->cancelGrip();
        }
        // 不論是否正在拖曳，一律直接關閉所有 grips
        d->gripManager->detach();
        if (d->gripFilter)
            d->gripFilter->clearSketchPlane();
    }

    // ⚠️ 不論 grips 是否啟用，都要清除底層 AIS 選取狀態（例如窗選/穿越窗選
    // 選中但尚未觸發 grip 附加的物件、或 alignment overlay 這類不會產生
    // grip 的選取），確保 ESC 一律能可靠清除「所有」選取，而不是只清 grips。
    const bool hadSelection = !d->context.IsNull() && d->context->NbSelected() > 0;
    if (!d->context.IsNull()) {
        d->context->ClearSelected(Standard_False);
        d->context->UpdateCurrentViewer();
    }

    if (!hadGrips && !hadSelection) {
        return false;
    }

    auto* bus = core::Application::instance()->eventBus();
    bus->publish("selection.cleared", QVariant());

    qDebug() << "[CadView] All grips/selection turned off";
    return true;
}

void CadView::setDocument(cad::Document* document) {
    if (d->document == document) {
        return;
    }

    // ✅ 斷開舊 document 的連接
    if (d->document) {
        disconnect(d->document, nullptr, this, nullptr);
    }

    d->document = document;

    qDebug() << "[CadView] Document set";

    if (document) {
        // ✅ 在這裡連接，確保 document 非 null
        connect(document, &cad::Document::featureShapeUpdated,
                this, [this](cad::Feature*) {
                    if (d->isDisplayingAllFeatures) return;
                    displayAllFeatures();
                }, Qt::QueuedConnection);

        // ✅ 延遲一幀確保 OCCT context 穩定後再顯示
        QTimer::singleShot(0, this, &CadView::displayAllFeatures);
    }
}

cad::Document* CadView::document() const {
    return d->document;
}

void CadView::setViewType(ViewType type) {
    if (d->viewType == type) {
        return;
    }

    d->viewType = type;
    updateProjection();

    qDebug() << "[CadView] View type changed to:" << static_cast<int>(type);

    Q_EMIT viewTypeChanged(type);

    using namespace core;
    EventBus* bus = Application::instance()->eventBus();
    if (bus) {
        bus->publish(Events::VIEW_CHANGED, QVariant());
    }
}

ViewType CadView::viewType() const {
    return d->viewType;
}

void CadView::setConstraintPickActive(bool active)
{
    d->constraintPickActive = active;
}

bool CadView::isBoxSelectArmed() const
{
    return d->boxSelectArmed;
}

void CadView::cancelActiveBoxSelect()
{
    if (d->boxSelectArmed) {
        cancelBoxSelectCandidate();
        d->mousePressed = false;
    }
}

void CadView::setMode(InteractionMode mode) {
    if (d->mode == mode) {
        return;
    }

    // 模式切換時，若窗選/穿越窗選正在進行中則取消，避免殘留選取框。
    if (d->boxSelectArmed) {
        cancelBoxSelectCandidate();
    }

    // GetGeom 模式：關閉 OSnap，讓 OCCT DetectedInteractive 決定選取幾何
    // 避免 OSnap 攔截點擊（snapConfirmed 只發 POINT_ACQUIRED，無 geomUuid）
    if (m_snapManager) {
        const bool wasGetGeom = (d->mode == InteractionMode::GetGeom);
        const bool isGetGeom  = (mode    == InteractionMode::GetGeom);
        if (isGetGeom && !wasGetGeom)
            m_snapManager->setSnapEnabled(false);
        else if (wasGetGeom && !isGetGeom)
            m_snapManager->setSnapEnabled(true);
    }

    d->mode = mode;

    if (mode == InteractionMode::Sketching) {
        showFinishSketchButton();
    } else if (mode == InteractionMode::Idle || mode == InteractionMode::Selecting){
        hideFinishSketchButton();
    }

    qDebug() << "[CadView] Interaction mode changed to:" << static_cast<int>(mode);

    Q_EMIT modeChanged(mode);
}

InteractionMode CadView::mode() const {
    return d->mode;
}

// ✅ Task E: 啟動 PlaceDimLine 模式
void CadView::beginPlaceDimLine(const QVector2D& anchorPos2D)
{
    d->dimLineAnchor2D = anchorPos2D;
    setMode(InteractionMode::PlaceDimLine);
}

// ── GDIM helpers ──────────────────────────────────────────────────────────────

QPoint CadView::planeToScreen(const QVector2D& planePt) const
{
    if (d->view.IsNull()) return {};

    // 取得目前活躍草圖平面
    cad::Plane* plane = nullptr;
    auto* sk = core::Application::instance()
               ? core::Application::instance()->activeSketch()
               : nullptr;
    if (sk && sk->plane())
        plane = sk->plane();

    if (!plane) {
        // 後備：依 viewType 取標準平面
        cad::PlaneManager* mgr = cad::PlaneManager::instance();
        switch (d->viewType) {
        case ViewType::Front: case ViewType::Back: plane = mgr->xzPlane(); break;
        case ViewType::Right: case ViewType::Left: plane = mgr->yzPlane(); break;
        default:                                   plane = mgr->xyPlane(); break;
        }
    }
    if (!plane) return {};

    QVector3D world3 = plane->toWorld(planePt.x(), planePt.y());
    Standard_Integer sx, sy;
    d->view->Convert(static_cast<Standard_Real>(world3.x()),
                     static_cast<Standard_Real>(world3.y()),
                     static_cast<Standard_Real>(world3.z()), sx, sy);
    return QPoint(sx, sy);
}

void CadView::setDimPreview(const DimPreviewInfo& info)
{
    if (!m_dimOverlay) return;
    auto* sk = core::Application::instance()
               ? core::Application::instance()->activeSketch()
               : nullptr;
    m_dimOverlay->setSketch(sk);
    m_dimOverlay->setPreview(info);
}

void CadView::clearDimPreview()
{
    if (m_dimOverlay)
        m_dimOverlay->clearPreview();
}

Handle(AIS_InteractiveContext) CadView::context() const {
    return d->context;
}

Handle(V3d_View) CadView::view() const {
    return d->view;
}

RubberBand* CadView::rubberBand() const {
    return d->rubberBand;
}

void CadView::displayAllFeatures() {
    if (!d->document || d->context.IsNull()) return;
    if (d->isDisplayingAllFeatures) return;  // ✅ 防重入

    d->isDisplayingAllFeatures = true;

    d->context->RemoveAll(Standard_False);
    d->context->Display(d->viewCube, Standard_False);
    d->aisToFeatureId.clear();  // ✅ 全部重建

    // ── 立即重新顯示草圖平面參考幾何（X 軸 / Y 軸 / 原點）──────────────────
    // RemoveAll 會把它們整個移出 context，包括內部 SelectMgr_Selection 狀態，
    // 必須緊接著重建，避免後續流程中軸/原點處於「視覺可見但選取已失效」狀態。
    auto redisplayAxis = [&](auto& handle, double sensitivity) {
        if (!handle.IsNull()) {
            d->context->Display(handle, Standard_False);
            d->context->Activate(handle, 0, Standard_False);
            d->context->SetSelectionSensitivity(handle, 0, sensitivity);
        }
    };
    redisplayAxis(d->sketchXAxisAIS,  4.0);
    redisplayAxis(d->sketchYAxisAIS,  4.0);
    redisplayAxis(d->sketchOriginAIS, 8.0);

    for (Feature* feature : d->document->features()) {
        if (!feature) continue;

        if (Sketch* sketch = qobject_cast<Sketch*>(feature)) {

            if (feature->isVisible()) {
                // ✅ visible: 正常顯示並註冊
                QList<Handle(AIS_InteractiveObject)> shapes =
                    sketch->displayInContext(d->context);
                const QList<QString>& uuids = sketch->aisShapeUuids();
                for (int i = 0; i < shapes.size(); ++i) {
                    const auto& s = shapes[i];
                    if (!s.IsNull()) {
                        d->aisToFeatureId[s.get()] = feature->id();
                        d->aisToGeomUuid[s.get()] = (i < uuids.size()) ? uuids[i] : QString();
                        d->aisToGeomIndex[s.get()] = i;
                    }
                }
            } else {
                // ✅ 修正：invisible Sketch 只讀取已建立的 aisShapes，絕對不呼叫 rebuild()
                //    rebuild() 會 emit shapeChanged → featureShapeUpdated → displayAllFeatures 死循環
                // 不呼叫 displayInContext，shapes 存在但不顯示
                const QList<QString>& uuids = sketch->aisShapeUuids();
                const auto& allObjs = sketch->aisShapes();
                for (int i = 0; i < allObjs.size(); ++i) {
                    const auto& s = allObjs[i];
                    if (!s.IsNull()) {
                        d->aisToFeatureId[s.get()] = feature->id();
                        d->aisToGeomUuid[s.get()] = (i < uuids.size()) ? uuids[i] : QString();
                        d->aisToGeomIndex[s.get()] = i;
                    }
                }
            }

            d->sketchFeatureIds[sketch] = feature->id();
            connect(sketch, &Sketch::rebuilt,
                    this, &CadView::onSketchRebuilt,
                    Qt::UniqueConnection);

        } else if (!feature->shape().IsNull()) {
            Handle(AIS_Shape) aisShape = new AIS_Shape(feature->shape());
            // ✅ 修正：使用 Shaded 模式並設定合理的非選取色（淺藍灰）
            aisShape->SetDisplayMode(AIS_Shaded);
            aisShape->SetMaterial(Graphic3d_NameOfMaterial_Silver);
            aisShape->SetColor(Quantity_NOC_CADETBLUE);   // 不與選取黃色衝突
            if (feature->isVisible())
                d->context->Display(aisShape, Standard_False);
            d->aisToFeatureId[aisShape.get()] = feature->id();
        }
    }

    d->context->UpdateCurrentViewer();

    // ── 重新加回 overlay 物件（如 ExtrudeManipulator 箭頭），依 visible 旗標過濾 ──
    // eye-close 的 AlignmentRenderer overlay (visible==false) 不重新顯示，
    // 確保 Sketch draw commands 進入時不會意外顯示被 eye-close 的 Alignment。
    bool anyOverlayDisplayed = false;
    for (const auto& entry : d->overlayObjects) {
        if (!entry.obj.IsNull() && entry.visible) {
            d->context->Display(entry.obj, Standard_False);
            for (int m : entry.modes)
                d->context->Activate(entry.obj, m);
            anyOverlayDisplayed = true;
        }
    }
    if (anyOverlayDisplayed)
        d->context->UpdateCurrentViewer();

    d->context->UpdateCurrentViewer();
    d->isDisplayingAllFeatures = false;
}

// ── Reverse lookup ────────────────────────────────────────────────────
QString CadView::findFeatureIdByAIS(
    const Handle(AIS_Shape)& aisShape) const
{
    return d->aisToFeatureId.value(aisShape.get(), QString());
}

void CadView::registerSketchGeomAIS(const Handle(AIS_InteractiveObject)& obj,
                                    const QString& featureId,
                                    const QString& geomUuid,
                                    int geomIndex)
{
    if (obj.IsNull()) return;
    d->aisToFeatureId[obj.get()]  = featureId;
    d->aisToGeomUuid[obj.get()]   = geomUuid;
    d->aisToGeomIndex[obj.get()]  = geomIndex;
}

void CadView::unregisterSketchGeomAIS(const Handle(AIS_InteractiveObject)& obj)
{
    if (obj.IsNull()) return;
    d->aisToFeatureId.remove(obj.get());
    d->aisToGeomUuid.remove(obj.get());
    d->aisToGeomIndex.remove(obj.get());
}

void CadView::refreshView() {
    if (d->view.IsNull()) {
        return;
    }

    d->view->Redraw();
    update();
}

void CadView::fitAll() {
    if (d->view.IsNull()) {
        return;
    }

    d->view->FitAll();
    d->view->ZFitAll();

    // ✅ 視野改變，格線需重新計算涵蓋範圍
    if (d->grid && d->gridEnabled) {
        d->grid->update();
    }

    update();
}

void CadView::addOverlayAIS(const Handle(AIS_InteractiveObject)& obj,
                            const QList<int>& activationModes)
{
    if (obj.IsNull()) return;
    removeOverlayAIS(obj);  // 避免重複登錄
    d->overlayObjects.append({obj, activationModes});

    if (!d->context.IsNull()) {
        d->context->Display(obj, Standard_False);
        for (int m : activationModes)
            d->context->Activate(obj, m);
        d->context->UpdateCurrentViewer();
    }
}

void CadView::removeOverlayAIS(const Handle(AIS_InteractiveObject)& obj)
{
    // Qt 5 相容寫法（removeIf 是 Qt 6）
    for (int i = d->overlayObjects.size() - 1; i >= 0; --i) {
        if (d->overlayObjects[i].obj == obj) {
            d->overlayObjects.removeAt(i);
            break;
        }
    }

    if (!d->context.IsNull() && !obj.IsNull()
        && d->context->IsDisplayed(obj)) {
        d->context->Remove(obj, Standard_True);
    }
}

void CadView::setOverlayAISVisible(const Handle(AIS_InteractiveObject)& obj, bool visible)
{
    if (obj.IsNull()) return;

    // 更新登錄中的 visible 旗標
    for (auto& entry : d->overlayObjects) {
        if (entry.obj == obj) {
            entry.visible = visible;
            break;
        }
    }

    if (d->context.IsNull()) return;

    if (visible) {
        // 重新顯示
        if (!d->context->IsDisplayed(obj))
            d->context->Display(obj, Standard_False);
        d->context->UpdateCurrentViewer();
    } else {
        // 從 OCCT context 移除（但保留 overlayObjects 登錄，供下次 setVisible(true) 恢復）
        if (d->context->IsDisplayed(obj)) {
            d->context->Remove(obj, Standard_False);
            d->context->UpdateCurrentViewer();
        }
    }
}

QJsonObject CadView::saveViewState() const {
    QJsonObject state;
    if (d->view.IsNull()) return state;

    Standard_Real xe, ye, ze;
    d->view->Eye(xe, ye, ze);
    Standard_Real xa, ya, za;
    d->view->At(xa, ya, za);
    Standard_Real xu, yu, zu;
    d->view->Up(xu, yu, zu);
    Standard_Real scale = d->view->Scale();

    state["eyeX"] = xe; state["eyeY"] = ye; state["eyeZ"] = ze;
    state["atX"]  = xa; state["atY"]  = ya; state["atZ"]  = za;
    state["upX"]  = xu; state["upY"]  = yu; state["upZ"]  = zu;
    state["scale"] = scale;
    state["viewType"] = static_cast<int>(d->viewType);
    return state;
}

void CadView::restoreViewState(const QJsonObject& state) {
    if (d->view.IsNull() || state.isEmpty()) return;

    d->view->SetEye(state["eyeX"].toDouble(), state["eyeY"].toDouble(), state["eyeZ"].toDouble());
    d->view->SetAt (state["atX"].toDouble(),  state["atY"].toDouble(),  state["atZ"].toDouble());
    d->view->SetUp (state["upX"].toDouble(),  state["upY"].toDouble(),  state["upZ"].toDouble());
    d->view->SetScale(state["scale"].toDouble(1.0));
    d->view->Redraw();
    update();
}

QVector2D CadView::screenToPlane(const QPoint& screenPos) const {
    if (d->view.IsNull()) {
        return QVector2D(0, 0);
    }

    Standard_Integer xp, yp;
    qtToOCCT(screenPos, xp, yp);

    cad::Plane* plane;
    cad::PlaneManager* manager = cad::PlaneManager::instance();

    switch (d->viewType) {
    case ViewType::Top:
    case ViewType::Bottom:
        plane = manager->xyPlane();
        break;
    case ViewType::Front:
    case ViewType::Back:
        plane = manager->xzPlane();
        break;
    case ViewType::Right:
    case ViewType::Left:
        plane = manager->yzPlane();
        break;
    default:
        plane = manager->xyPlane();
        break;
    }

    gp_Pln gpPlane(
        gp_Pnt(plane->origin().x(), plane->origin().y(), plane->origin().z()),
        gp_Dir(plane->normal().x(), plane->normal().y(), plane->normal().z())
        );

    Standard_Real Xeye, Yeye, Zeye;
    Standard_Real Xproj, Yproj, Zproj;
    d->view->Eye(Xeye, Yeye, Zeye);
    d->view->Proj(Xproj, Yproj, Zproj);

    gp_Pnt eyePoint(Xeye, Yeye, Zeye);
    gp_Dir projDir(Xproj, Yproj, Zproj);

    Standard_Real Xv, Yv, Zv;
    d->view->Convert(xp, yp, Xv, Yv, Zv);
    gp_Pnt screenPoint3D(Xv, Yv, Zv);

    gp_Pnt rayStart;
    gp_Dir rayDir;

    if (d->view->Camera()->IsOrthographic()) {
        rayStart = screenPoint3D;
        rayDir = projDir;
    } else {
        rayStart = eyePoint;
        gp_Vec direction(eyePoint, screenPoint3D);
        if (direction.Magnitude() < Precision::Confusion()) {
            rayDir = projDir;
        } else {
            rayDir = gp_Dir(direction);
        }
    }

    gp_Lin pickLine(rayStart, rayDir);

    IntAna_IntConicQuad intersection(pickLine, gpPlane, Precision::Angular());

    if (intersection.IsDone() && intersection.NbPoints() > 0) {
        gp_Pnt intersectPnt = intersection.Point(1);

        QVector3D worldPt(intersectPnt.X(), intersectPnt.Y(), intersectPnt.Z());
        QVector3D localPt = worldPt - plane->origin();

        float u = QVector3D::dotProduct(localPt, plane->xAxis());
        float v = QVector3D::dotProduct(localPt, plane->yAxis());

        return QVector2D(u, v);
    }

    return QVector2D(0, 0);
}

QPointF CadView::screenToPlaneD(const QPoint& screenPos) const {
    if (d->view.IsNull()) return QPointF(0, 0);

    Standard_Integer xp, yp;
    qtToOCCT(screenPos, xp, yp);

    cad::Plane* plane;
    cad::PlaneManager* manager = cad::PlaneManager::instance();
    switch (d->viewType) {
    case ViewType::Top: case ViewType::Bottom: plane = manager->xyPlane(); break;
    case ViewType::Front: case ViewType::Back: plane = manager->xzPlane(); break;
    case ViewType::Right: case ViewType::Left: plane = manager->yzPlane(); break;
    default: plane = manager->xyPlane(); break;
    }

    gp_Pln gpPlane(
        gp_Pnt(plane->origin().x(), plane->origin().y(), plane->origin().z()),
        gp_Dir(plane->normal().x(), plane->normal().y(), plane->normal().z())
    );

    Standard_Real Xeye, Yeye, Zeye, Xproj, Yproj, Zproj;
    d->view->Eye(Xeye, Yeye, Zeye);
    d->view->Proj(Xproj, Yproj, Zproj);

    gp_Pnt eyePoint(Xeye, Yeye, Zeye);
    gp_Dir projDir(Xproj, Yproj, Zproj);

    Standard_Real Xv, Yv, Zv;
    d->view->Convert(xp, yp, Xv, Yv, Zv);
    gp_Pnt screenPoint3D(Xv, Yv, Zv);

    gp_Pnt rayStart;
    gp_Dir rayDir;
    if (d->view->Camera()->IsOrthographic()) {
        rayStart = screenPoint3D;
        rayDir = projDir;
    } else {
        rayStart = eyePoint;
        gp_Vec direction(eyePoint, screenPoint3D);
        rayDir = (direction.Magnitude() < Precision::Confusion())
                 ? projDir : gp_Dir(direction);
    }

    IntAna_IntConicQuad intersection(gp_Lin(rayStart, rayDir), gpPlane, Precision::Angular());
    if (intersection.IsDone() && intersection.NbPoints() > 0) {
        gp_Pnt ip = intersection.Point(1);
        // gp_Pnt 座標是 double，直接用 plane->toPlaneD() 保持精度
        return plane->toPlaneD(QVector3D(ip.X(), ip.Y(), ip.Z()));
    }
    return QPointF(0, 0);
}

void CadView::setGridEnabled(bool enabled) {
    d->gridEnabled = enabled;

    if (d->grid) {
        if (enabled) {
            d->grid->show();
        } else {
            d->grid->hide();
        }
    }
    // ✅ 同步 OSnap 網格吸附設定
    if (m_snapManager) {
        osnap::OSnapSettings s = m_snapManager->settings();
        s.gridSnapEnabled = enabled;
        // 從 ViewGrid 同步間距
        if (d->grid && enabled) {
            s.gridSpacing = d->grid->spacing();  // 需確認 ViewGrid 有 spacing() 方法
        }
        m_snapManager->setSettings(s);
    }
}

bool CadView::isGridEnabled() const {
    return d->gridEnabled;
}

void CadView::setCoordinateOffset(double easting, double northing) {
    d->coordinateOffsetE = easting;
    d->coordinateOffsetN = northing;
    qDebug() << "[CadView] Coordinate offset set: E=" << easting << "N=" << northing;
}

/// @deprecated 使用 ProjectOrigin::instance().originE() 取代
double CadView::coordinateOffsetE() const
{
    // Phase 2 deprecated compat: 若 ProjectOrigin 已設定，以它為準
    using namespace aicad::core::geometry;
    if (ProjectOrigin::instance().isSet())
        return ProjectOrigin::instance().originE();
    return d->coordinateOffsetE;
}
/// @deprecated 使用 ProjectOrigin::instance().originN() 取代
double CadView::coordinateOffsetN() const
{
    using namespace aicad::core::geometry;
    if (ProjectOrigin::instance().isSet())
        return ProjectOrigin::instance().originN();
    return d->coordinateOffsetN;
}

void CadView::onSketchRebuilt()
{
    auto* sketch = qobject_cast<cad::Sketch*>(sender());
    if (!sketch) return;

    const QString fid = d->sketchFeatureIds.value(sketch);
    if (fid.isEmpty()) return;

    for (auto it = d->aisToFeatureId.begin(); it != d->aisToFeatureId.end(); ) {
        if (it.value() == fid)
            it = d->aisToFeatureId.erase(it);
        else
            ++it;
    }
    for (auto it = d->aisToGeomUuid.begin(); it != d->aisToGeomUuid.end(); ) {
        if (!d->aisToFeatureId.contains(it.key()))
            it = d->aisToGeomUuid.erase(it);
        else
            ++it;
    }
    // 同步清除 aisToGeomIndex 中已失效的條目
    for (auto it = d->aisToGeomIndex.begin(); it != d->aisToGeomIndex.end(); ) {
        if (!d->aisToFeatureId.contains(it.key()))
            it = d->aisToGeomIndex.erase(it);
        else
            ++it;
    }

    const QList<QString>& uuids = sketch->aisShapeUuids();
    const auto& shapes = sketch->aisShapes();
    for (int i = 0; i < shapes.size(); ++i) {
        if (!shapes[i].IsNull()) {
            d->aisToFeatureId[shapes[i].get()] = fid;
            d->aisToGeomUuid[shapes[i].get()] = (i < uuids.size()) ? uuids[i] : QString();
            d->aisToGeomIndex[shapes[i].get()] = i;
        }
    }
}

void CadView::setTopView() {
    setViewType(ViewType::Top);
}

void CadView::setFrontView() {
    setViewType(ViewType::Front);
}

void CadView::setRightView() {
    setViewType(ViewType::Right);
}

void CadView::alignToPlane(const cad::Plane* plane)
{
    if (!plane || d->view.IsNull())
        return;

    gp_Pln pln = plane->toGpPln();
    gp_Ax3 ax  = pln.Position();

    gp_Dir normal = ax.Direction();
    gp_Dir xDir   = ax.XDirection();

    d->view->SetProj(normal.X(), normal.Y(), normal.Z());
    d->view->SetUp(xDir.X(), xDir.Y(), xDir.Z());

    d->viewer->SetPrivilegedPlane(ax);

    d->view->FitAll();

    // ✅ 視角/縮放改變，格線需重新計算
    if (d->grid && d->gridEnabled) {
        d->grid->update();
    }
}

void CadView::setIsometricView() {
    setViewType(ViewType::Isometric);
}

void CadView::showFinishSketchButton() {
    m_finishSketchButton->show();
    m_finishSketchButton->raise();
}

void CadView::hideFinishSketchButton() {
    m_finishSketchButton->hide();
}

void CadView::showReturnAlignmentButton() {
    if (!m_returnAlignmentButton) return;
    // 右上角：距右邊 10px，距上邊 10px
    m_returnAlignmentButton->move(width() - m_returnAlignmentButton->width() - 10, 10);
    m_returnAlignmentButton->show();
    m_returnAlignmentButton->raise();
}

void CadView::hideReturnAlignmentButton() {
    if (m_returnAlignmentButton)
        m_returnAlignmentButton->hide();
}

void CadView::setSuppressCoordDisplay(bool suppress)
{
    m_suppressCoordDisplay = suppress;
    // 立刻清除 status bar 顯示
    if (suppress)
        Q_EMIT statusMessageRequested(QString(), 0);
}

void CadView::onFinishSketchClicked() {
    if (d->rubberBand) {
        d->rubberBand->clearPoints();
        d->rubberBand->clear();
    }

    setMode(InteractionMode::Idle);
    hideFinishSketchButton();
    d->grid->hide();

    Application* app = Application::instance();
    cad::Sketch* sketch = app->activeSketch();
    core::EventBus* bus = app->eventBus();
    if (sketch) {
        QVariantMap data;
        data["itemId"] = sketch->id();
        data["visible"] = false;
        bus->publish("feature.visibility-changed", data);
    }

    Q_EMIT sketchFinished();
}

void CadView::displaySketchRegions(
    const QVector<cad::SketchRegion>& regions,
    const cad::Sketch* sketch)
{
    clearSketchRegions();

    for (const auto& region : regions) {
        TopoDS_Face face = geometry::faceFromSketchRegion(sketch, region);
        if (face.IsNull()) continue;

        Handle(AIS_Shape) aisRegion = new AIS_Shape(face);

        // 半透明青色填色（用於預覽）
        Graphic3d_MaterialAspect mat(Graphic3d_NOM_PLASTIC);
        mat.SetColor(Quantity_Color(0.0, 0.8, 0.8, Quantity_TOC_RGB));
        mat.SetTransparency(0.6f);
        aisRegion->SetMaterial(mat);
        aisRegion->SetTransparency(0.6);
        aisRegion->SetDisplayMode(AIS_Shaded);
        // 不加入 selection（region 的選取另外處理）
        d->context->Display(aisRegion, Standard_False);
        d->context->Deactivate(aisRegion);

        d->regionAisMap[region.uuid] = aisRegion;
    }
    d->context->UpdateCurrentViewer();
}

void CadView::clearSketchRegions()
{
    for (auto& ais : d->regionAisMap) {
        if (!ais.IsNull())
            d->context->Remove(ais, Standard_False);
    }
    d->regionAisMap.clear();
    d->activeRegionUuid.clear();
    d->context->UpdateCurrentViewer();
}

void CadView::highlightSketchRegion(const QString& uuid)
{
    // 取消前一個高亮
    if (!d->activeRegionUuid.isEmpty()) {
        auto it = d->regionAisMap.find(d->activeRegionUuid);
        if (it != d->regionAisMap.end() && !it.value().IsNull()) {
            d->context->SetColor(*it,
                                 Quantity_Color(0.0, 0.8, 0.8, Quantity_TOC_RGB),
                                 Standard_False);
            d->context->SetTransparency(*it, 0.6, Standard_False);
        }
    }
    // 高亮新選取的 region（不透明黃色）
    auto it = d->regionAisMap.find(uuid);
    if (it != d->regionAisMap.end() && !it.value().IsNull()) {
        d->context->SetColor(*it,
                             Quantity_Color(1.0, 0.9, 0.0, Quantity_TOC_RGB),
                             Standard_False);
        d->context->SetTransparency(*it, 0.2, Standard_False);
        d->activeRegionUuid = uuid;
    }
    d->context->UpdateCurrentViewer();
}

void CadView::showSketchContextMenu(const QPoint& screenPos)
{
    cad::Sketch* sketch = Application::instance()->activeSketch();
    if (!sketch) return;

    QMenu menu(this);

    // ── 偵測區域 ─────────────────────────────────────────────────
    QAction* actDetect = menu.addAction(tr("偵測區域"));
    connect(actDetect, &QAction::triggered, this, [this, sketch] {
        auto regions = sketch->detectRegions();
        if (regions.isEmpty()) {
            Q_EMIT statusMessageRequested(tr("未找到閉合迴路"), 2000);
            return;
        }
        displaySketchRegions(regions, sketch);
        Q_EMIT sketchRegionsDetected(regions);
        Q_EMIT statusMessageRequested(
            tr("偵測到 %1 個區域，左鍵點擊選取").arg(regions.size()), 0);
    });

    // ── 若 regionAisMap 非空，提供「清除區域顯示」 ──────────────
    if (!d->regionAisMap.isEmpty()) {
        menu.addSeparator();
        QAction* actClear = menu.addAction(tr("清除區域顯示"));
        connect(actClear, &QAction::triggered,
                this, &CadView::clearSketchRegions);
    }

    menu.addSeparator();

    // ── 完成草圖（原本右鍵功能移至此處） ────────────────────────
    QAction* actFinish = menu.addAction(tr("完成草圖"));
    connect(actFinish, &QAction::triggered,
            this, &CadView::onFinishSketchClicked);

    menu.exec(mapToGlobal(screenPos));
}

void CadView::updateProjection() {
    if (d->view.IsNull()) {
        return;
    }

    switch (d->viewType) {
    case ViewType::Top:
        d->view->SetProj(V3d_Zpos);
        break;
    case ViewType::Bottom:
        d->view->SetProj(V3d_Zneg);
        break;
    case ViewType::Front:
        d->view->SetProj(V3d_Yneg);
        break;
    case ViewType::Back:
        d->view->SetProj(V3d_Ypos);
        break;
    case ViewType::Right:
        d->view->SetProj(V3d_Xpos);
        break;
    case ViewType::Left:
        d->view->SetProj(V3d_Xneg);
        break;
    case ViewType::Isometric:
    case ViewType::Perspective:
    default:
        d->view->SetProj(V3d_XposYnegZpos);
        break;
    }

    fitAll();
}

void CadView::handlePointInput(const QPoint& screenPos) {
    QPointF planePt = screenToPlaneD(screenPos);   // ✅ 改用 double 版

    // 從 OSnapManager 取得目前鎖定的 snap 候選（含 geomUuid / geomHandle）
    QString geomUuid;
    int     geomHandle = -1;
    if (m_snapManager && m_snapManager->isSnapActive()) {
        auto snap = m_snapManager->currentSnap();
        if (snap.has_value()) {
            geomUuid   = snap->geomUuid;
            geomHandle = snap->geomHandle;
            // ✅ 改用 snapPoint2DF()，避免 SnapCandidate::planePoint（QVector2D float）截斷
            if (auto pt2d = m_snapManager->snapPoint2DF())
                planePt = pt2d.value();
        }
    }
    // Snap 未偵測到幾何時，回退到 OCC DetectedInteractive
    if (geomUuid.isEmpty() && !d->context.IsNull() && d->context->HasDetected()) {
        Handle(AIS_InteractiveObject) det = d->context->DetectedInteractive();
        if (!det.IsNull()) {
            QString detUuid = d->aisToGeomUuid.value(det.get());
            if (!detUuid.isEmpty()) {
                geomUuid   = detUuid;
                geomHandle = static_cast<int>(GeomHandle::WholeGeom);
            }
        }
    }

    // 同時發布到 EventBus
    auto* bus = core::Application::instance()->eventBus();
    if (bus) {
        QVariantMap data;
        data["point"]      = QVariant::fromValue(planePt);
        data["geomUuid"]   = geomUuid;
        data["geomHandle"] = geomHandle;
        bus->publish(core::Events::POINT_ACQUIRED, data);

        // GetGeom モード：GEOM_PICKED も発行して命令が幾何を受け取れるようにする
        if (d->mode == InteractionMode::GetGeom) {
            QVariantMap geomData;
            geomData["geomUuid"] = geomUuid;
            geomData["handle"]   = geomHandle;   // ← "handle" 與 onGeomPicked 一致
            geomData["point"]    = QVariant::fromValue(planePt);
            bus->publish(core::Events::GEOM_PICKED, geomData);
        }
    }

    qDebug() << "[CadView] Point acquired:" << planePt.x() << "," << planePt.y()
             << "geomUuid:" << geomUuid << "handle:" << geomHandle;

    // 舊 signal（向下相容）
    Q_EMIT pointAcquired(planePt);
    // 新 signal（帶 GeomRef 資訊）
    Q_EMIT geomRefPicked(planePt, geomUuid, geomHandle);
}

void CadView::handleObjectSelection(const QPoint& screenPos) {
    if (d->context.IsNull() || d->view.IsNull()) {
        return;
    }

    Standard_Integer xp, yp;
    qtToOCCT(screenPos, xp, yp);

    d->context->MoveTo(xp, yp, d->view, Standard_True);

    if (d->context->HasDetected()) {
        Handle(AIS_InteractiveObject) picked = d->context->DetectedInteractive();

        if (!picked.IsNull() && picked != d->viewCube) {
            d->context->SetSelected(picked, Standard_True);

            qDebug() << "[CadView] Selected individual wire/edge";

            EventBus* bus = Application::instance()->eventBus();
            QVariantMap selectionData;
            selectionData["aisObject"] = QVariant::fromValue((void*)picked.get());
            bus->publish("geometry.selected", selectionData);
        }
    }
}

void CadView::qtToOCCT(const QPoint& qtPos, Standard_Integer& occX, Standard_Integer& occY) const {
    QtToOCCT(this, qtPos, occX, occY);
}

// ── 窗選 / 穿越窗選（Window / Crossing box selection）────────────────────────
// 由左至右拖曳 = 窗選（Window，只選完全被框住的物件）
// 由右至左拖曳 = 穿越窗選（Crossing，與框相交/接觸即選取）
// 拖曳門檻（Qt 邏輯像素，manhattanLength）：小於此距離視為單純點擊，不啟動框選。
// 注意：選取框視覺沿用既有 RubberBand.cpp 的 Prs3d_Presentation 技術
// （非 AIS_InteractiveObject），因為 AIS_RubberBand::Compute() 在本專案的
// OCCT/顯示環境下會造成 segmentation fault，故不採用。
static constexpr int kBoxSelectDragThreshold = 4;

void CadView::beginBoxSelectCandidate(const QPoint& screenPos, bool additive, bool sketchMode)
{
    d->boxSelectArmed               = true;
    d->boxSelectActive              = false;
    d->boxSelectWaitingSecondClick  = false;
    d->boxSelectShape      = BoxSelectShape::Rectangle;
    d->boxSelectPolyQt.clear();
    d->boxSelectShapeKeyBuffer.clear();
    d->boxSelectAdditive   = additive;
    d->boxSelectSketchMode = sketchMode;
    d->boxSelectStartQt    = screenPos;
    qtToOCCT(screenPos, d->boxSelectStartX, d->boxSelectStartY);

    // 快照目前選取狀態：框選預覽期間會暫時改變 context 的選取內容，
    // 若使用者取消框選，或以疊加（Shift）方式完成框選時，需要以此還原/合併。
    d->boxSelectSavedOwners.clear();
    if (!d->context.IsNull()) {
        for (d->context->InitSelected(); d->context->MoreSelected(); d->context->NextSelected()) {
            Handle(SelectMgr_EntityOwner) owner = d->context->SelectedOwner();
            if (!owner.IsNull()) d->boxSelectSavedOwners.push_back(owner);
        }
    }

    // 命令列提示（比照 AutoCAD 窗選/穿越窗選慣例用語）。透過 CommandLineManager
    // 顯示（而非直接發布 COMMAND_PROMPT），讓使用者可在命令列輸入
    // F / WP / CP 切換為籬選 / 多邊形窗選 / 多邊形框選。
    auto* clm = core::CommandLineManager::instance();
    clm->showPrompt(tr("指定對角點或 [籬選(F)/多邊形窗選(WP)/多邊形框選(CP)]:"));
    clm->waitForInput(core::InputType::Option);
}

cad::Plane* CadView::boxSelectReferencePlane() const
{
    cad::PlaneManager* mgr = cad::PlaneManager::instance();
    switch (d->viewType) {
    case ViewType::Front: case ViewType::Back: return mgr->xzPlane();
    case ViewType::Right: case ViewType::Left: return mgr->yzPlane();
    default:                                   return mgr->xyPlane();
    }
}

void CadView::updateBoxSelectDrag(const QPoint& screenPos)
{
    if (!d->boxSelectArmed || d->context.IsNull() || d->view.IsNull()) return;

    if (!d->boxSelectActive) {
        const QPoint delta = screenPos - d->boxSelectStartQt;
        if (delta.manhattanLength() < kBoxSelectDragThreshold)
            return;   // 尚未超過拖曳門檻
        d->boxSelectActive = true;
    }

    // 依拖曳方向即時切換窗選（藍/實線）與穿越窗選（綠/虛線）樣式
    const bool isCrossing = (screenPos.x() < d->boxSelectStartQt.x());

    const int minX = qMin(d->boxSelectStartQt.x(), screenPos.x());
    const int maxX = qMax(d->boxSelectStartQt.x(), screenPos.x());
    const int minY = qMin(d->boxSelectStartQt.y(), screenPos.y());
    const int maxY = qMax(d->boxSelectStartQt.y(), screenPos.y());

    // 取得與 screenToPlane() 相同的參考平面（依 viewType 決定），
    // 將矩形四個角點各自 unproject 到該平面再轉回世界座標，
    // 如此無論目前視角為何都能組出對應螢幕上矩形的世界座標多邊形。
    cad::Plane* plane = boxSelectReferencePlane();
    if (!plane) return;

    const QPoint screenCorners[5] = {
        QPoint(minX, minY), QPoint(maxX, minY),
        QPoint(maxX, maxY), QPoint(minX, maxY),
        QPoint(minX, minY)
    };

    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(5);
    for (int i = 0; i < 5; ++i) {
        QVector2D planePt = screenToPlane(screenCorners[i]);
        QVector3D worldPt = plane->toWorld(planePt.x(), planePt.y());
        polyline->AddVertex(gp_Pnt(worldPt.x(), worldPt.y(), worldPt.z()));
    }

    if (!d->boxSelectPresentation.IsNull()) {
        d->boxSelectPresentation->Clear();
        d->boxSelectPresentation->Erase();
    }
    d->boxSelectPresentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());

    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        isCrossing ? Quantity_Color(0.25, 0.75, 0.30, Quantity_TOC_RGB)   // 綠 = 穿越窗選
                   : Quantity_Color(0.25, 0.55, 1.00, Quantity_TOC_RGB),  // 藍 = 窗選
        isCrossing ? Aspect_TOL_DASH : Aspect_TOL_SOLID,
        1.5);

    Handle(Graphic3d_Group) group = d->boxSelectPresentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);

    d->boxSelectPresentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->boxSelectPresentation->SetDisplayPriority(Graphic3d_DisplayPriority_Topmost);
    d->boxSelectPresentation->Display();

    // ── 預覽高亮：即時反映目前矩形範圍（依窗選/穿越窗選規則）會選中哪些物件 ──
    // 與正式完成選取（performRectangleSelection）共用同一套邏輯，確保 Shift
    // （疊加）狀態在拖曳過程中的每一格畫面都與最終結果一致：已選取的物件
    // 不會在預覽過程中被誤判為未選取而消失。
    {
        Standard_Integer sx0, sy0, sx1, sy1;
        qtToOCCT(d->boxSelectStartQt, sx0, sy0);
        qtToOCCT(screenPos, sx1, sy1);
        applyBoxSelectionScheme(sx0, sy0, sx1, sy1, d->boxSelectAdditive);
    }

    d->context->UpdateCurrentViewer();
}

void CadView::finishBoxSelect(const QPoint& screenPos)
{
    if (!d->boxSelectArmed) return;

    const bool wasActive = d->boxSelectActive;

    if (!d->boxSelectPresentation.IsNull()) {
        d->boxSelectPresentation->Clear();
        d->boxSelectPresentation->Erase();
        d->boxSelectPresentation.Nullify();
    }

    if (wasActive) {
        // 疊加選取（Shift）的還原＋Add 邏輯已統一收斂到 performRectangleSelection
        // 內部呼叫的 applyBoxSelectionScheme，這裡不需要重複處理。
        Standard_Integer endX, endY;
        qtToOCCT(screenPos, endX, endY);
        performRectangleSelection(d->boxSelectStartX, d->boxSelectStartY,
                                   endX, endY,
                                   d->boxSelectAdditive, d->boxSelectSketchMode);
    } else if (!d->boxSelectSketchMode) {
        // 單純點擊空白處（非草圖模式，例如 H-Alignment edit）：
        // 不清除既有選取，維持目前選取狀態；一律改由 ESC 清除所有選取。
    }
    // 草圖模式下單純點擊空白處：同樣維持既有規則，不清除選取（改由 ESC 處理）。

    if (!d->context.IsNull()) d->context->UpdateCurrentViewer();

    // 結束框選流程：清除命令列提示，並重設「等待輸入」狀態
    // （resetInputWait 不會像 cancelCommand 一樣廣播 COMMAND_CANCELLED，
    //  避免誤觸其他模組的副作用，例如強制切回 Idle 檢視模式）。
    auto* clm = core::CommandLineManager::instance();
    clm->clearPrompt();
    clm->resetInputWait();

    d->boxSelectArmed               = false;
    d->boxSelectActive              = false;
    d->boxSelectWaitingSecondClick  = false;
    d->boxSelectShape               = BoxSelectShape::Rectangle;
    d->boxSelectPolyQt.clear();
    d->boxSelectShapeKeyBuffer.clear();
}

void CadView::cancelBoxSelectCandidate()
{
    if (!d->boxSelectPresentation.IsNull()) {
        d->boxSelectPresentation->Clear();
        d->boxSelectPresentation->Erase();
        d->boxSelectPresentation.Nullify();
    }
    // 若預覽期間曾暫時改變 context 的選取內容，取消時還原成框選前的原始狀態。
    if (d->boxSelectActive) {
        restoreBoxSelectSavedSelection();
    }
    if (!d->context.IsNull()) d->context->UpdateCurrentViewer();

    // 取消框選流程：清除命令列提示，並重設「等待輸入」狀態。
    auto* clm = core::CommandLineManager::instance();
    clm->clearPrompt();
    clm->resetInputWait();

    d->boxSelectArmed               = false;
    d->boxSelectActive              = false;
    d->boxSelectWaitingSecondClick  = false;
    d->boxSelectShape               = BoxSelectShape::Rectangle;
    d->boxSelectPolyQt.clear();
    d->boxSelectShapeKeyBuffer.clear();
}

void CadView::restoreBoxSelectSavedSelection()
{
    if (d->context.IsNull()) return;

    d->context->ClearSelected(Standard_False);
    for (const Handle(SelectMgr_EntityOwner)& owner : d->boxSelectSavedOwners) {
        if (!owner.IsNull()) d->context->AddOrRemoveSelected(owner, Standard_False);
    }
}

void CadView::applyBoxSelectionScheme(Standard_Integer x0, Standard_Integer y0,
                                       Standard_Integer x1, Standard_Integer y1,
                                       bool additive)
{
    if (d->context.IsNull() || d->view.IsNull()) return;

    if (additive) {
        // 疊加選取（Shift）：每次套用前都先還原成框選開始前的原始選取狀態，
        // 再以 Add scheme 疊加目前矩形命中的物件。這樣不論是預覽中的每一格
        // 畫面、或是正式完成選取，原本已選取的物件都不會被誤判為未選取。
        restoreBoxSelectSavedSelection();
    }

    const Standard_Integer minX = qMin(x0, x1), maxX = qMax(x0, x1);
    const Standard_Integer minY = qMin(y0, y1), maxY = qMax(y0, y1);

    // 窗選（Window，由左至右）：只選完全被框住的物件
    // 穿越窗選（Crossing，由右至左）：只要與框相交/接觸即選取
    const bool isCrossing = (x1 < x0);
    Handle(SelectMgr_ViewerSelector) selector = d->context->MainSelector();
    if (!selector.IsNull())
        selector->AllowOverlapDetection(isCrossing);

    d->context->SelectRectangle(Graphic3d_Vec2i(minX, minY),
                                 Graphic3d_Vec2i(maxX, maxY),
                                 d->view,
                                 additive ? AIS_SelectionScheme_Add
                                          : AIS_SelectionScheme_Replace);

    if (!selector.IsNull())
        selector->AllowOverlapDetection(Standard_False);   // 還原預設，避免影響後續單擊 pick

    // 非疊加模式下，若這次矩形範圍沒有選中任何物件，不清除原本已有的選取
    // ——維持「點/框選空白處不清除選取，一律以 ESC 清除」的規則。
    if (!additive && d->context->NbSelected() == 0 && !d->boxSelectSavedOwners.isEmpty()) {
        restoreBoxSelectSavedSelection();
    }
}

void CadView::performRectangleSelection(Standard_Integer x0, Standard_Integer y0,
                                         Standard_Integer x1, Standard_Integer y1,
                                         bool additive, bool sketchMode)
{
    if (d->context.IsNull() || d->view.IsNull()) return;

    applyBoxSelectionScheme(x0, y0, x1, y1, additive);
    publishBoxSelectionResult(sketchMode);
}

void CadView::publishBoxSelectionResult(bool sketchMode)
{
    if (d->context.IsNull()) return;

    // ── 收集選取結果，比照既有單擊選取邏輯發布事件 ──────────────────────────
    QStringList uuids;
    QMap<QString, QSet<int>> selectionMap;   // featureId → set of geomIndices
    Handle(AIS_InteractiveObject) overlayHit;  // alignment overlay（不在 aisToFeatureId 內）

    for (d->context->InitSelected(); d->context->MoreSelected(); d->context->NextSelected()) {
        Handle(AIS_InteractiveObject) obj = d->context->SelectedInteractive();
        if (obj.IsNull()) continue;

        QString uuid = d->aisToGeomUuid.value(obj.get());
        if (!uuid.isEmpty()) uuids << uuid;

        QString featureId = d->aisToFeatureId.value(obj.get());
        if (featureId.isEmpty()) {
            if (overlayHit.IsNull()) overlayHit = obj;
            continue;
        }

        int geomIndex = d->aisToGeomIndex.value(obj.get(), -1);
        if (geomIndex >= 0)
            selectionMap[featureId].insert(geomIndex);
        else
            selectionMap[featureId];
    }

    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;

    if (!uuids.isEmpty()) {
        QVariantMap data;
        data["uuids"] = QVariant::fromValue(uuids);
        bus->publish(Events::SKETCH_GEOM_SELECTED, data);
    }

    if (!selectionMap.isEmpty()) {
        for (auto it = selectionMap.constBegin(); it != selectionMap.constEnd(); ++it) {
            QVariantList indexList;
            for (int idx : it.value()) indexList.append(idx);
            QVariantMap selData;
            selData["featureId"]   = it.key();
            selData["geomIndices"] = indexList;
            bus->publish("selection.featureSelected", selData);
        }
    } else if (!overlayHit.IsNull()) {
        QVariantMap selData;
        selData["aisObject"] = QVariant::fromValue((void*)overlayHit.get());
        bus->publish("geometry.selected", selData);
    } else if (!sketchMode) {
        // 非草圖模式（例如 H-Alignment edit）：若框選前就已有選取，
        // applyBoxSelectionScheme() 已經在矩形沒選中任何物件時還原回原本的選取，
        // 這裡只會在「框選前本來就沒有任何選取」時才會真的發布 cleared
        // （此時發布也無實質影響，因為本來就沒有東西可清）。
        bus->publish("selection.cleared", QVariant());
    }
    // 草圖模式下框選未選中任何物件：維持既有規則，不清除選取（改由 ESC 處理）。
}

// ── 籬選(Fence) / 多邊形窗選(WPolygon) / 多邊形框選(CPolygon) ────────────────
//
// 在矩形窗選提示「指定對角點或 [籬選(F)/多邊形窗選(WP)/多邊形框選(CP)]:」階段，
// 透過命令列輸入 F/WP/CP（見建構子中對 Events::OPTION_SELECTED 的訂閱）觸發，
// 切換為多點式選取：第一點沿用矩形窗選的起點，之後每次左鍵點一下新增一個頂點，
// Enter 或 Space 完成，ESC 取消（沿用既有的 cancelBoxSelectCandidate）。
//
// 窗選 / 穿越窗選的判定規則沿用：多邊形窗選＝完全包含（AllowOverlapDetection=false）、
// 多邊形框選＝相交即選（AllowOverlapDetection=true）。籬選（Fence）在 OCCT 沒有對應
// 的「開放路徑crossing」原生 API，這裡以 SelectPolygon + AllowOverlapDetection(true)
// 近似實作：多邊形會自動以最後一點連回第一點封閉，因此若某物件恰好只被這條「隱形
// 封閉邊」穿越、而未真正穿越使用者畫出的籬選路徑本身，仍可能被選中；對一般用途
// （籬選路徑不與自身首尾重疊）而言此近似已相當接近正確結果。

void CadView::beginBoxSelectShapeMode(BoxSelectShape shape)
{
    if (!d->boxSelectArmed || shape == BoxSelectShape::Rectangle) return;
    // 呼叫端（OPTION_SELECTED 訂閱／鍵盤攔截）已經以
    // d->boxSelectShape == Rectangle 判斷「尚未切換過」，這裡不需要也不應該
    // 再檢查 d->boxSelectActive —— 該旗標在使用者快速點一下放開後就已經是
    // true（代表「等待第二次點擊」），而這正是接受 F/WP/CP 切換的正常時機，
    // 用它當守衛反而會擋掉合法的切換。

    d->boxSelectShape = shape;
    d->boxSelectPolyQt.clear();
    d->boxSelectShapeKeyBuffer.clear();
    d->boxSelectPolyQt.push_back(d->boxSelectStartQt);   // 第一點沿用矩形窗選的起點
    d->boxSelectWaitingSecondClick = false;   // 矩形模式的「等待第二次點擊」已不適用
    d->boxSelectActive = true;   // 借用既有旗標讓 mouseMoveEvent/ESC 等邏輯知道框選「進行中」

    const QString label = shape == BoxSelectShape::Fence     ? tr("籬選")
                         : shape == BoxSelectShape::WPolygon  ? tr("多邊形窗選")
                                                                : tr("多邊形框選");
    auto* clm = core::CommandLineManager::instance();
    clm->showPrompt(tr("指定%1下一點（點擊左鍵新增頂點，Enter 或 Space 完成）:").arg(label));
    clm->waitForInput(core::InputType::Option);

    // 切換模式當下立即以目前滑鼠位置刷新一次預覽線＋高亮，不等待下一次
    // mouseMoveEvent 才顯示——確保按下 F/WP/CP 的當下（不必先移動滑鼠）
    // 就能立刻看到預覽效果。
    updateBoxSelectPolyPreview(mapFromGlobal(QCursor::pos()));
}

void CadView::addBoxSelectPolyVertex(const QPoint& screenPos)
{
    if (!d->boxSelectArmed || d->boxSelectShape == BoxSelectShape::Rectangle) return;
    if (d->boxSelectPolyQt.isEmpty()) return;

    // 忽略與最後一個已確認頂點幾乎重合的點擊，避免產生零長度線段
    const QPoint delta = screenPos - d->boxSelectPolyQt.last();
    if (delta.manhattanLength() < kBoxSelectDragThreshold) return;

    d->boxSelectPolyQt.push_back(screenPos);
    updateBoxSelectPolyPreview(screenPos);
}

void CadView::updateBoxSelectPolyPreview(const QPoint& screenPos)
{
    if (!d->boxSelectArmed || d->boxSelectShape == BoxSelectShape::Rectangle) return;
    if (d->context.IsNull() || d->view.IsNull() || d->boxSelectPolyQt.isEmpty()) return;

    cad::Plane* plane = boxSelectReferencePlane();
    if (!plane) return;

    // 已確定的頂點 + 目前滑鼠位置（尚未確認的橡皮筋段）
    // realPts：實際頂點，供 SelectPolygon 判斷選取範圍使用（SelectPolygon 本身
    //          就會自動以最後一點連回第一點封閉，不需要額外重複起點）。
    // drawPts：純粹供畫面繪製使用；多邊形窗選/多邊形框選額外加上封閉邊，
    //          讓畫面上看起來是完整封閉的多邊形（籬選維持開放路徑）。
    QVector<QPoint> realPts = d->boxSelectPolyQt;
    realPts.push_back(screenPos);

    QVector<QPoint> drawPts = realPts;
    if (d->boxSelectShape != BoxSelectShape::Fence) {
        drawPts.push_back(d->boxSelectPolyQt.first());
    }

    const bool isCrossingStyle = (d->boxSelectShape != BoxSelectShape::WPolygon);

    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(drawPts.size());
    for (const QPoint& p : drawPts) {
        QVector2D planePt = screenToPlane(p);
        QVector3D worldPt = plane->toWorld(planePt.x(), planePt.y());
        polyline->AddVertex(gp_Pnt(worldPt.x(), worldPt.y(), worldPt.z()));
    }

    if (!d->boxSelectPresentation.IsNull()) {
        d->boxSelectPresentation->Clear();
        d->boxSelectPresentation->Erase();
    }
    d->boxSelectPresentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());

    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        isCrossingStyle ? Quantity_Color(0.25, 0.75, 0.30, Quantity_TOC_RGB)   // 綠 = 穿越類（籬選/多邊形框選）
                        : Quantity_Color(0.25, 0.55, 1.00, Quantity_TOC_RGB),  // 藍 = 窗選（多邊形窗選）
        isCrossingStyle ? Aspect_TOL_DASH : Aspect_TOL_SOLID,
        1.5);

    Handle(Graphic3d_Group) group = d->boxSelectPresentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);

    d->boxSelectPresentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->boxSelectPresentation->SetDisplayPriority(Graphic3d_DisplayPriority_Topmost);
    d->boxSelectPresentation->Display();

    // ── 預覽高亮：頂點數足夠時即時反映目前範圍會選中哪些物件 ──────────────
    // 多邊形（WPolygon/CPolygon）至少需要 3 點才有面積；籬選至少需要 2 點
    // （即已有一段路徑）就能進行相交測試。
    const int minPtsForPreview = (d->boxSelectShape == BoxSelectShape::Fence) ? 2 : 3;
    if (realPts.size() >= minPtsForPreview) {
        applyPolygonSelectionScheme(realPts, isCrossingStyle, d->boxSelectAdditive);
    }

    d->context->UpdateCurrentViewer();
}

void CadView::applyPolygonSelectionScheme(const QVector<QPoint>& ptsQt, bool crossing, bool additive)
{
    if (d->context.IsNull() || d->view.IsNull() || ptsQt.size() < 2) return;

    if (additive) {
        // 疊加選取（Shift）：每次套用前都先還原成框選開始前的原始選取狀態，
        // 邏輯與 applyBoxSelectionScheme 的矩形版本一致。
        restoreBoxSelectSavedSelection();
    }

    // OCCT 的 SelectPolygon 需要至少 3 個頂點，且該多邊形不能是零面積的退化
    // 形狀。籬選路徑只有 2 點（單一線段）時，若直接重複最後一點補成 3 點，
    // 形成的是「面積剛好為零」的退化三角形，很可能被 OCCT 內部判斷為無效
    // 多邊形而整個忽略、完全不會選到任何東西（這正是先前「籬選要點到第 3
    // 點才有效果」的根本原因——2 點時的補點方式其實從未真正生效，只是恰好
    // 使用者之後點了真正的第 3 點才開始work）。
    // 修正做法：往垂直方向偏移極小距離（2 個螢幕像素）取代直接重複同一點，
    // 讓多邊形有一個非零但可忽略不計的面積，使 OCCT 能將其視為有效多邊形，
    // 同時因為偏移量極小，實際判斷效果幾乎等同純粹的線段相交測試。
    QVector<QPoint> effectivePts = ptsQt;
    if (effectivePts.size() == 2) {
        const QPoint a = effectivePts[0];
        const QPoint b = effectivePts[1];
        const QPoint dir = b - a;
        const double len = std::sqrt(double(dir.x()) * dir.x() + double(dir.y()) * dir.y());
        if (len > 0.5) {
            // 垂直於 a→b 的單位向量，偏移 2 像素
            const double ux = -dir.y() / len;
            const double uy =  dir.x() / len;
            const QPoint c(qRound((a.x() + b.x()) / 2.0 + ux * 2.0),
                           qRound((a.y() + b.y()) / 2.0 + uy * 2.0));
            effectivePts.push_back(c);
        } else {
            // a、b 幾乎重合（理論上不會發生，addBoxSelectPolyVertex 已濾除
            // 過近的點），保底仍重複最後一點避免陣列大小不符預期。
            effectivePts.push_back(b);
        }
    }

    TColgp_Array1OfPnt2d polygon(1, effectivePts.size());
    for (int i = 0; i < effectivePts.size(); ++i) {
        Standard_Integer px, py;
        qtToOCCT(effectivePts[i], px, py);
        polygon.SetValue(i + 1, gp_Pnt2d(px, py));
    }

    Handle(SelectMgr_ViewerSelector) selector = d->context->MainSelector();
    if (!selector.IsNull()) selector->AllowOverlapDetection(crossing);

    d->context->SelectPolygon(polygon, d->view,
                               additive ? AIS_SelectionScheme_Add
                                        : AIS_SelectionScheme_Replace);

    if (!selector.IsNull()) selector->AllowOverlapDetection(Standard_False);

    // 非疊加模式下，若目前範圍沒有選中任何物件，不清除原本已有的選取
    // ——與矩形窗選的規則一致，維持「選不到不清除，一律以 ESC 清除」。
    if (!additive && d->context->NbSelected() == 0 && !d->boxSelectSavedOwners.isEmpty()) {
        restoreBoxSelectSavedSelection();
    }
}

void CadView::finishBoxSelectPolygon()
{
    if (!d->boxSelectArmed || d->boxSelectShape == BoxSelectShape::Rectangle) return;

    const int minPts = (d->boxSelectShape == BoxSelectShape::Fence) ? 2 : 3;
    if (d->boxSelectPolyQt.size() < minPts) {
        // 點數不足以構成有效的籬選路徑／多邊形，比照 AutoCAD：直接取消整個框選。
        cancelBoxSelectCandidate();
        return;
    }

    if (!d->boxSelectPresentation.IsNull()) {
        d->boxSelectPresentation->Clear();
        d->boxSelectPresentation->Erase();
        d->boxSelectPresentation.Nullify();
    }

    const bool isCrossingStyle = (d->boxSelectShape != BoxSelectShape::WPolygon);
    if (!d->context.IsNull()) {
        applyPolygonSelectionScheme(d->boxSelectPolyQt, isCrossingStyle, d->boxSelectAdditive);
        publishBoxSelectionResult(d->boxSelectSketchMode);
        d->context->UpdateCurrentViewer();
    }

    auto* clm = core::CommandLineManager::instance();
    clm->clearPrompt();
    clm->resetInputWait();

    d->boxSelectArmed               = false;
    d->boxSelectActive              = false;
    d->boxSelectWaitingSecondClick  = false;
    d->boxSelectShape               = BoxSelectShape::Rectangle;
    d->boxSelectPolyQt.clear();
    d->boxSelectShapeKeyBuffer.clear();
}

void CadView::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    if (!d->view.IsNull()) {
        d->view->InvalidateImmediate();
        d->view->Redraw();
    }
    // 尺寸線預覽由 m_dimOverlay（child widget）負責，此處不需 QPainter
}

void CadView::resizeEvent(QResizeEvent* event) {
    Q_UNUSED(event);

    if (!d->view.IsNull()) {
        d->view->MustBeResized();
        d->view->Redraw();
    }

    // ✅ 視窗大小改變會影響格線涵蓋範圍，需重新計算
    if (d->grid && d->gridEnabled) {
        d->grid->update();
    }

    if (m_finishSketchButton) {
        m_finishSketchButton->setGeometry(width() - 120, 10, 110, 30);
    }
    if (m_returnAlignmentButton && m_returnAlignmentButton->isVisible()) {
        m_returnAlignmentButton->move(width() - m_returnAlignmentButton->width() - 10, 10);
    }

    // GDIM overlay 是 OCCT Presentation，resize 無需更新 widget geometry
}

void CadView::mousePressEvent(QMouseEvent* event) {
    d->lastMousePos = event->pos();
    d->mousePressed = true;
    d->pressedButton = event->button();

    // ── 籬選 / 多邊形窗選 / 多邊形框選：每次左鍵點擊新增一個頂點 ──────────────
    if (event->button() == Qt::LeftButton && d->boxSelectArmed &&
        d->boxSelectShape != BoxSelectShape::Rectangle) {
        addBoxSelectPolyVertex(event->pos());
        event->accept();
        return;
    }

    // ── 窗選 / 穿越窗選：第二次點擊完成選取 ──────────────────────────────
    // 不論這次點擊點到什麼（空白處或幾何物件），只要正在等待第二次點擊，
    // 一律以起點～目前位置的矩形範圍完成窗選/穿越窗選（比照 AutoCAD 慣例）。
    if (event->button() == Qt::LeftButton && d->boxSelectWaitingSecondClick) {
        finishBoxSelect(event->pos());
        event->accept();
        return;
    }

    Standard_Integer xp, yp;
    qtToOCCT(event->pos(), xp, yp);
    d->context->MoveTo(xp, yp, d->view, Standard_True);

    // ✅ FIX: 中間鍵按下 → 記錄狀態，開始 pan
    if (event->button() == Qt::MiddleButton) {
        d->middleButtonPressed = true;
        setCursor(Qt::SizeAllCursor);
        event->accept();
        return;
    }

    // ✅ 如果是平面選取模式
    if (m_selectionFilter == "plane" && event->button() == Qt::LeftButton) {
        if (d->context->HasDetected()) {
            Handle(AIS_InteractiveObject) picked = d->context->DetectedInteractive();
            Handle(AIS_Shape) pickedShape = Handle(AIS_Shape)::DownCast(picked);

            if (!pickedShape.IsNull() && isReferencePlane(pickedShape)) {
                QString planeName = identifyPlane(pickedShape);

                qDebug() << "[CadView] Plane clicked:" << planeName;

                core::EventBus* bus = core::Application::instance()->eventBus();

                QVariantMap planeData;
                planeData["plane"] = planeName;
                planeData["cancelled"] = false;

                bus->publish("plane.selected", planeData);

                highlightSelectablePlanes(false);

                return;
            }
        }
    }

    if (event->button() == Qt::RightButton && d->mode == InteractionMode::Sketching) {
        // 若目前有指令進行中 → 取消指令（原本行為）
        // 若無指令進行中      → 顯示草圖 context menu
        Application* app = Application::instance();
        auto* bus=app->eventBus();
        command::CommandManager* cmdMgr = app->commandManager();
        bool commandActive = cmdMgr->hasActiveCommand() ; // 見下方說明

        if (commandActive) {
            bus->publish(Events::POINT_CANCELLED, QVariant());
        } else {
            showSketchContextMenu(event->pos());
        }
        return;
    }

    // ── 尺寸線拖曳偵測（Sketching 模式，無 active command，左鍵）─────────────
    // 必須在 region 選取和 Sketching 畫線之前先判斷
    if (event->button() == Qt::LeftButton &&
        (d->mode == InteractionMode::Sketching || d->mode == InteractionMode::Idle))
    {
        auto* cmdMgr2 = Application::instance()->commandManager();
        if (!cmdMgr2 || !cmdMgr2->hasActiveCommand()) {
            if (d->context->HasDetected()) {
                Handle(AIS_InteractiveObject) det = d->context->DetectedInteractive();
                Handle(aicad::cad::AIS_DimensionLine) dimAIS =
                    Handle(aicad::cad::AIS_DimensionLine)::DownCast(det);
                if (!dimAIS.IsNull()) {
                    // 點擊到尺寸線 → 開始拖曳
                    d->dragDimUuid       = dimAIS->constraintUuid();
                    d->dragDimStartMouse = screenToPlane(event->pos());
                    d->dragDimBaseOffsetX = dimAIS->dimOffsetX();
                    d->dragDimBaseOffsetY = dimAIS->dimOffsetY();
                    setMode(InteractionMode::DimLineDrag);
                    setCursor(Qt::SizeAllCursor);
                    Q_EMIT dimLineDragStarted(d->dragDimUuid);
                    event->accept();
                    return;
                }
            }
        }
    }

    // 在現有的左鍵 Sketching 處理之前插入（緊接在 snap 判斷之前）：

    if (event->button() == Qt::LeftButton &&
        d->mode == InteractionMode::Sketching &&
        !d->regionAisMap.isEmpty())
    {
        // 先嘗試 region 選取
        QVector2D planePt = screenToPlane(event->pos());
        cad::Sketch* sketch = Application::instance()->activeSketch();
        if (sketch) {
            auto regions = sketch->detectRegions();
            for (const auto& region : regions) {
                if (region.contains(planePt)) {
                    highlightSketchRegion(region.uuid);
                    Q_EMIT sketchRegionPicked(region);
                    return;   // 消耗事件，不進入畫線流程
                }
            }
        }
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    int x = static_cast<int>(event->position().x());
    int y = static_cast<int>(event->position().y());
#else
    int x = event->x();
    int y = event->y();
#endif

    if (event->button() == Qt::LeftButton &&
        (d->mode == InteractionMode::Sketching ||
         d->mode == InteractionMode::GetPoint  ||
         d->mode == InteractionMode::GetGeom))
    {
        auto* cmdMgr = Application::instance()->commandManager();
        const bool hasCmd = cmdMgr && cmdMgr->hasActiveCommand();

        if (!hasCmd) {
            auto* bus = Application::instance()->eventBus();

            // ── GetGeom 模式 + pickSession 等待中 ────────────────────────────
            // GeomConstraintCommand::execute() 回傳後 command state 不是 Running，
            // hasCmd = false，但 pickSession 仍在等待幾何選取。
            // → 走 handlePointInput 路徑，透過 geomRefPicked → feedPoint。
            if (d->mode == InteractionMode::GetGeom && d->constraintPickActive) {
                if (m_snapManager && m_snapManager->onMousePress(x, y))
                    return;
                handlePointInput(event->pos());
                return;
            }

            // ── Sketch Idle 選取規則 ─────────────────────────────────────────
            // ① 點到空白處 → 不做任何事（取消選取／關閉 grips 一律改由 ESC 處理）
            // ② 點到幾何   → 累加選取（不需按 Shift）
            // Shift 鍵 = XOR（可反選已選物件）
            if (!d->context->HasDetected()) {
                // ✅ 點擊空白處：先記錄起點，若接下來形成拖曳則啟動窗選/
                //    穿越窗選；若只是單純點擊則維持原行為（不清除選取）。
                bool shiftHeld = (event->modifiers() & Qt::ShiftModifier);
                beginBoxSelectCandidate(event->pos(), shiftHeld, /*sketchMode=*/true);
                return;
            }

            // 點到幾何：無 Shift = 累加；Shift = XOR（反選）
            bool shiftHeld = (event->modifiers() & Qt::ShiftModifier);
            d->context->SelectDetected(
                shiftHeld ? AIS_SelectionScheme_XOR
                          : AIS_SelectionScheme_Add);

            // ── 同一個迴圈同時收集 UUID（給 Sketch 高亮）和 geomIndex（給 Grips）──
            QStringList uuids;
            QMap<QString, QSet<int>> selectionMap;
            Handle(AIS_InteractiveObject) overlayHit;  // alignment overlay (not in aisToFeatureId)

            for (d->context->InitSelected();
                 d->context->MoreSelected();
                 d->context->NextSelected())
            {
                // 統一處理 AIS_Shape（幾何曲線）和 SketchPointAIS（點）
                Handle(AIS_InteractiveObject) obj = d->context->SelectedInteractive();
                if (obj.IsNull()) continue;

                QString uuid = d->aisToGeomUuid.value(obj.get());
                if (!uuid.isEmpty()) uuids << uuid;

                QString featureId = d->aisToFeatureId.value(obj.get());
                if (featureId.isEmpty()) {
                    // Not a sketch feature — could be an alignment overlay
                    if (overlayHit.IsNull()) overlayHit = obj;
                    continue;
                }

                int geomIndex = d->aisToGeomIndex.value(obj.get(), -1);
                if (geomIndex >= 0)
                    selectionMap[featureId].insert(geomIndex);
                else
                    selectionMap[featureId];  // 確保 key 存在
            }

            if (!uuids.isEmpty()) {
                // ① 通知 Sketch 高亮選取的幾何（傳送目前所有已選 UUID）
                QVariantMap data;
                data["uuids"] = QVariant::fromValue(uuids);
                bus->publish(Events::SKETCH_GEOM_SELECTED, data);

                // ② 觸發 GripManager：把所有已選幾何的 indices 合併後一次送出
                // 支援跨多個 feature 的選取（實務上 sketch 同一個）
                for (auto it = selectionMap.constBegin();
                     it != selectionMap.constEnd(); ++it)
                {
                    QVariantList indexList;
                    for (int idx : it.value()) indexList.append(idx);
                    QVariantMap selData;
                    selData["featureId"]   = it.key();
                    selData["geomIndices"] = indexList;
                    bus->publish("selection.featureSelected", selData);
                }
            } else if (!overlayHit.IsNull()) {
                // Alignment overlay clicked in Sketching mode
                QVariantMap selData;
                selData["aisObject"] = QVariant::fromValue((void*)overlayHit.get());
                bus->publish("geometry.selected", selData);
            } else {
                bus->publish(Events::SKETCH_GEOM_CLEARED, QVariant{});
                bus->publish("selection.cleared", QVariant());
            }
            return;
        }

        // ── 有 command：snap → POINT_ACQUIRED ────────────────────────
        if (m_snapManager && m_snapManager->onMousePress(x, y))
            return;

        handlePointInput(event->pos());
        return;   // ← 已處理，不進入下面第二個 if 區塊
    }

    if (!d->context.IsNull() && !d->view.IsNull()) {

        if (event->button() == Qt::LeftButton) {
            if (d->context->HasDetected()) {
                Handle(AIS_InteractiveObject) picked = d->context->DetectedInteractive();
                if (!picked.IsNull() && picked == d->viewCube) {
                    return;
                }
            }

            switch (d->mode) {
            case InteractionMode::Sketching:
            case InteractionMode::GetPoint:
            case InteractionMode::GetGeom:
                // ★ 注意：這個 switch 只有在上面第一個 if 沒有命中時才到達
                //   （即 !hasCmd 路徑已 return，或 mode 不在那個 if 的條件中）
                //   GetGeom 在 hasCmd==true 時已由上面 handlePointInput 處理並 return，
                //   不會再到這裡。此處保留供 hasCmd==false 但不走 SelectDetected 的情況。
                // → 已由第一個 if-block 的 hasCmd 路徑處理，此處不應再執行
                // （實際上因為上面已 return，這個 case 在 GetGeom+hasCmd 時不會被執行）
                break;   // ← 改為 break，移除重複的 handlePointInput 呼叫
            case InteractionMode::Selecting:
                // ✅ 點擊空白處不再清除選取／grips —— 一律改由 ESC 取代
                if (d->context->HasDetected()) {
                    d->context->SelectDetected(AIS_SelectionScheme_Replace);
                    handleObjectSelection(event->pos());
                }
                break;
            case InteractionMode::PlaceDimLine: {  // ✅ Task E: 確認尺寸線位置
                QVector2D planePt = screenToPlane(event->pos());
                QVector2D offset  = planePt - d->dimLineAnchor2D;
                Q_EMIT dimLinePosConfirmed(offset.x(), offset.y());
                setMode(InteractionMode::Sketching);
                break;
            }
            default:
                break;
            }
        }
    }

    // 右鍵啟動旋轉
    if (event->button() == Qt::RightButton && !d->view.IsNull()) {
        d->view->StartRotation(xp, yp);
    }

    if (!d->context.IsNull() && event->button() == Qt::LeftButton) {

        // ① Let GripEventFilter have first chance (already installed)
        //    If grip captured the event, it returns true and Qt won't
        //    propagate here — but if you handle manually, check:
        if (d->gripFilter && d->gripFilter->isGripSelected()) {
            return;  // grip is dragging, skip AIS selection
        }

        // ✅ Shift = add to selection, otherwise replace
        bool additive = (event->modifiers() & Qt::ShiftModifier);

        // ✅ 點擊空白處：可能是窗選/穿越窗選的起點，先記錄；
        //    若後續形成拖曳則啟動框選，否則於放開時比照原行為處理
        //    （非草圖模式，例如 H-Alignment edit：未選中任何物件則清除選取）。
        if (!d->context->HasDetected()) {
            beginBoxSelectCandidate(event->pos(), additive, /*sketchMode=*/false);
            return;
        }

        // ② Tell OCCT to perform selection at this pixel
        d->context->SelectDetected(
            additive ? AIS_SelectionScheme_Add
                     : AIS_SelectionScheme_Replace);

        // ✅ Collect ALL currently selected shapes
        QMap<QString, QSet<int>> selectionMap;  // featureId → set of geomIndices
        Handle(AIS_InteractiveObject) overlayHit;  // alignment overlay hit (not in aisToFeatureId)

        for (d->context->InitSelected();
             d->context->MoreSelected();
             d->context->NextSelected())
        {
            Handle(AIS_Shape) s = Handle(AIS_Shape)::DownCast(
                d->context->SelectedInteractive());
            if (s.IsNull()) continue;

            QString featureId = d->aisToFeatureId.value(s.get());
            if (featureId.isEmpty()) {
                // Not a registered feature — could be an alignment overlay
                if (overlayHit.IsNull()) overlayHit = s;
                continue;
            }

            int geomIndex = d->aisToGeomIndex.value(s.get(), -1);
            if (geomIndex >= 0)
                selectionMap[featureId].insert(geomIndex);   // ← 指定幾何的 grip
            else
                selectionMap[featureId];                     // ← 確保 key 存在（非 sketch feature）
        }

        if (!selectionMap.isEmpty()) {
            // For simplicity, take the first (or only) feature
            // Multi-feature selection can be extended here
            QString featureId   = selectionMap.firstKey();
            QSet<int> indices   = selectionMap[featureId];

            // ✅ Convert QSet<int> to QVariantList for EventBus
            QVariantList indexList;
            for (int idx : indices) indexList.append(idx);

            auto* bus = core::Application::instance()->eventBus();
            QVariantMap data;
            data["featureId"]   = featureId;
            data["geomIndices"] = indexList;  // ✅ replaces single geomIndex
            bus->publish("selection.featureSelected", data);

        } else {
            auto* bus = core::Application::instance()->eventBus();
            // 檢查是否點選到 alignment overlay（未登記在 aisToFeatureId 的 AIS 物件）
            if (!overlayHit.IsNull()) {
                QVariantMap selData;
                selData["aisObject"] = QVariant::fromValue((void*)overlayHit.get());
                bus->publish("geometry.selected", selData);
            } else {
                bus->publish("selection.cleared", QVariant());
            }
        }
    }

    Q_EMIT viewClicked(event->pos(), event->button());
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void CadView::enterEvent(QEnterEvent* event) {
#else
void CadView::enterEvent(QEvent* event) {
#endif
    QWidget::enterEvent(event);
    setFocus(Qt::MouseFocusReason);
}

void CadView::mouseMoveEvent(QMouseEvent* event) {
    //int x = event->x();
    //int y = event->y();

    Standard_Integer xp, yp;
    qtToOCCT(event->pos(), xp, yp);

    // ── 籬選 / 多邊形窗選 / 多邊形框選：更新橡皮筋預覽線＋即時高亮 ──────────
    if (d->boxSelectArmed && d->boxSelectShape != BoxSelectShape::Rectangle) {
        updateBoxSelectPolyPreview(event->pos());
        event->accept();
        return;
    }

    // ── 窗選 / 穿越窗選：更新選取框（拖曳中，或已放開等待第二次點擊皆適用）───
    if (d->boxSelectArmed) {
        updateBoxSelectDrag(event->pos());
        if (d->boxSelectActive) {
            event->accept();
            return;   // 框選進行中，不處理 hover / rubberband 等其他滑鼠移動邏輯
        }
    }

    bool gripActive = d->gripManager && d->gripManager->isGripSelected();
    if (!gripActive) {
        d->context->MoveTo(xp, yp, d->view, Standard_True);
    }
    // ── OSnap 偵測（每次 mouse move）──────────────────────────────────────
    // 注意：Grip 系統已透過 EventBus 設定 m_snapManager 的 m_gripActive 旗標，
    //       所以這裡不需要額外判斷。
    if (m_snapManager && m_snapManager->isSnapEnabled()) {
        m_snapManager->onMouseMove(xp, yp);
    }

    // ✅ FIX: 中間鍵拖曳 → 執行 Pan
    if (d->middleButtonPressed && !d->view.IsNull()) {
        const QPoint delta = event->pos() - d->lastMousePos;

        // OCCT Pan(dX, dY)：dX 向右為正，dY 向上為正（螢幕 Y 軸相反）
        d->view->Pan(delta.x(), -delta.y());

        // ✅ 平移會改變格線需涵蓋的世界座標範圍，需重新計算
        if (d->grid && d->gridEnabled) {
            d->grid->update();
        }

        d->lastMousePos = event->pos();
        update();
        event->accept();
        return;
    }

    // 更新懸停偵測
    if (!d->context.IsNull() && !d->view.IsNull() && !gripActive) {
        //d->context->MoveTo(xp, yp, d->view, Standard_True);\n
        if (d->context->HasDetected()) {
            Handle(AIS_InteractiveObject) detected = d->context->DetectedInteractive();
            if (!detected.IsNull() && detected == d->viewCube) {
                setCursor(Qt::PointingHandCursor);
            } else if (!detected.IsNull()) {
                // 懸停到尺寸線時顯示移動游標
                Handle(aicad::cad::AIS_DimensionLine) dimDet =
                    Handle(aicad::cad::AIS_DimensionLine)::DownCast(detected);
                auto* cmdMgr3 = Application::instance()->commandManager();
                bool noCmd = !cmdMgr3 || !cmdMgr3->hasActiveCommand();
                if (!dimDet.IsNull() && noCmd &&
                    (d->mode == InteractionMode::Sketching || d->mode == InteractionMode::Idle))
                    setCursor(Qt::SizeAllCursor);
                else
                    unsetCursor();
            } else {
                unsetCursor();
            }
        } else {
            unsetCursor();
        }
    }

    // ── 尺寸線拖曳：移動中即時發送偏移 ──────────────────────────────────────
    if (d->mode == InteractionMode::DimLineDrag && !d->dragDimUuid.isEmpty()) {
        QVector2D planePt = screenToPlane(event->pos());
        QVector2D delta   = planePt - d->dragDimStartMouse;
        double newOffX = d->dragDimBaseOffsetX + delta.x();
        double newOffY = d->dragDimBaseOffsetY + delta.y();
        Q_EMIT dimLineDragging(d->dragDimUuid, newOffX, newOffY);
        event->accept();
        return;
    }

    // ✅ Task E: PlaceDimLine 模式 — 滑鼠移動時發出偏移預覽 & 更新 QPainter overlay
    if (d->mode == InteractionMode::PlaceDimLine) {
        QVector2D planePt = screenToPlane(event->pos());
        d->dimPreviewMousePt = planePt;
        if (m_dimOverlay)
            m_dimOverlay->setMousePlanePt(planePt);
        QVector2D offset  = planePt - d->dimLineAnchor2D;
        Q_EMIT dimLinePosPreview(offset.x(), offset.y());
        auto* bus = core::Application::instance()->eventBus();
        if (bus) {
            QVariantMap m;
            m["offsetX"] = static_cast<double>(offset.x());
            m["offsetY"] = static_cast<double>(offset.y());
            bus->publish(core::Events::DIM_LINE_PREVIEW, m);
        }
    }

    // GDIM: GetGeom 模式 → 發 GEOM_HOVER，供命令 hover 時更新預覽
    if (d->mode == InteractionMode::GetGeom) {
        QVector2D planePt = screenToPlane(event->pos());
        d->dimPreviewMousePt = planePt;
        if (m_dimOverlay)
            m_dimOverlay->setMousePlanePt(planePt);
        auto* bus = core::Application::instance()->eventBus();
        if (bus) {
            QString hoverUuid;
            int     hoverHandle = -1;
            if (m_snapManager && m_snapManager->isSnapActive()) {
                auto snap = m_snapManager->currentSnap();
                if (snap.has_value()) {
                    hoverUuid   = snap->geomUuid;
                    hoverHandle = snap->geomHandle;
                }
            }
            if (hoverUuid.isEmpty() && !d->context.IsNull() && d->context->HasDetected()) {
                Handle(AIS_InteractiveObject) det = d->context->DetectedInteractive();
                if (!det.IsNull()) {
                    hoverUuid   = d->aisToGeomUuid.value(det.get());
                    hoverHandle = static_cast<int>(cad::GeomHandle::WholeGeom);
                }
            }
            QVariantMap m;
            m["geomUuid"] = hoverUuid;
            m["handle"]   = hoverHandle;
            m["point"]    = QVariant::fromValue(planePt);
            bus->publish(core::Events::GEOM_HOVER, m);
        }
    }

    // 右鍵拖曳旋轉
    if (d->mousePressed && d->pressedButton == Qt::RightButton && !d->view.IsNull()) {
        d->view->Rotation(xp, yp);
        d->lastMousePos = event->pos();
        update();
        return;
    }

    // Phase 3: Navigation 模式游標座標雙顯示（Local + TM2 Global）
    // m_suppressCoordDisplay == true 時（Alignment edit 結束後）不顯示任何座標
    if (!m_suppressCoordDisplay &&
        (d->mode == InteractionMode::Navigation || d->mode == InteractionMode::Idle)) {
        auto* bus = core::Application::instance() ? core::Application::instance()->eventBus() : nullptr;
        if (bus) {
            QPointF localPt = screenToPlaneD(event->pos());
            // 若 OSnap 鎖定，用 snap 座標（精度更高）
            if (m_snapManager && m_snapManager->isSnapActive()) {
                auto snapPt = m_snapManager->snapPoint2DF();
                if (snapPt.has_value()) localPt = snapPt.value();
            }
            using namespace aicad::core::geometry;
            const auto& origin = ProjectOrigin::instance();
            QString coordMsg;
            if (origin.isSet()) {
                const QPointF global = origin.toGlobal(localPt);
                coordMsg = QString("E: %1   N: %2   (Local: %3, %4)")
                    .arg(global.x(), 0, 'f', 3)
                    .arg(global.y(), 0, 'f', 3)
                    .arg(localPt.x(), 0, 'f', 3)
                    .arg(localPt.y(), 0, 'f', 3);
            } else {
                coordMsg = QString("X: %1   Y: %2")
                    .arg(localPt.x(), 0, 'f', 3)
                    .arg(localPt.y(), 0, 'f', 3);
            }
            Q_EMIT statusMessageRequested(coordMsg, 0); // timeout=0 → 持續顯示直到下一次更新
        }
    }

    // 草圖模式：更新橡皮筋
    if (d->mode == InteractionMode::Sketching) {
        if (d->rubberBand) {
            QPointF planePtF;

            // ✅ 優先使用 snap 鎖定座標（double 版）
            if (m_snapManager && m_snapManager->isSnapActive()) {
                auto pt2d = m_snapManager->snapPoint2DF();
                planePtF = pt2d.has_value() ? pt2d.value()
                                            : screenToPlaneD(event->pos());
            } else {
                planePtF = screenToPlaneD(event->pos());
            }

            d->rubberBand->setCurrentPoint(planePtF);  // Phase fix: QPointF直接傳入
            d->rubberBand->update();
        }
    }

    //if (!d->context.IsNull()) {
    //    // Update OCCT's internal detected object
    //     d->context->MoveTo(event->x(), event->y(), d->view, Standard_True);
    //}

}

void CadView::handleViewCubeClick(const QPoint& pos)
{
    d->context->MoveTo(pos.x(), pos.y(), d->view, Standard_True);

    if (d->context->HasDetected()) {
        Handle(AIS_InteractiveObject) detected = d->context->DetectedInteractive();

        if (detected == d->viewCube) {
            d->context->SelectDetected(AIS_SelectionScheme_Replace);

            if (!d->viewCube->HasAnimation()) return;

            Handle(AIS_AnimationCamera) anim = d->viewCube->ViewAnimation();
            anim->StartTimer(0.0, 1.0, Standard_True);

            startViewCubeAnimation();
        }
    }
}

void CadView::startViewCubeAnimation()
{
    if (!d->viewCubeTimer) {
        d->viewCubeTimer = new QTimer(this);
        connect(d->viewCubeTimer, &QTimer::timeout, this, [this]() {
            Handle(AIS_AnimationCamera) anim = d->viewCube->ViewAnimation();
            Standard_Real pts = 0.0;
            const Standard_Boolean isDone = anim->Update(pts);
            d->view->Invalidate();
            d->view->Redraw();
            if (isDone) {
                d->viewCubeTimer->stop();
            }
        });
    }
    d->viewCubeTimer->start(16);
}

void CadView::mouseReleaseEvent(QMouseEvent* event) {
    // ── 籬選 / 多邊形窗選 / 多邊形框選：頂點已在 mousePressEvent 新增，
    //    放開滑鼠不做任何事。──────────────────────────────────────────────
    if (event->button() == Qt::LeftButton && d->boxSelectArmed &&
        d->boxSelectShape != BoxSelectShape::Rectangle) {
        d->mousePressed = false;
        event->accept();
        return;
    }

    // ── 窗選 / 穿越窗選：放開滑鼠時的行為 ────────────────────────────────────
    if (event->button() == Qt::LeftButton && d->boxSelectArmed) {
        if (d->boxSelectActive) {
            // 按住拖曳超過門檻後放開：立即完成窗選/穿越窗選（傳統拖曳方式）。
            finishBoxSelect(event->pos());
        } else {
            // 快速點一下就放開（尚未形成拖曳）：轉為「等待第二次點擊」模式，
            // 選取框改為跟隨滑鼠自由移動，直到使用者再點一下為止。
            d->boxSelectActive             = true;
            d->boxSelectWaitingSecondClick = true;
        }
        d->mousePressed = false;
        event->accept();
        return;
    }

    // ── 尺寸線拖曳結束 ────────────────────────────────────────────────────────
    if (event->button() == Qt::LeftButton &&
        d->mode == InteractionMode::DimLineDrag &&
        !d->dragDimUuid.isEmpty())
    {
        QVector2D planePt = screenToPlane(event->pos());
        QVector2D delta   = planePt - d->dragDimStartMouse;
        double newOffX = d->dragDimBaseOffsetX + delta.x();
        double newOffY = d->dragDimBaseOffsetY + delta.y();
        Q_EMIT dimLineDragFinished(d->dragDimUuid, newOffX, newOffY);
        d->dragDimUuid.clear();
        unsetCursor();
        setMode(InteractionMode::Sketching);
        event->accept();
        return;
    }

    // ✅ FIX: 中間鍵放開 → 結束 pan
    if (event->button() == Qt::MiddleButton) {
        d->middleButtonPressed = false;
        unsetCursor();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && d->mousePressed) {
        const QPoint delta = event->pos() - d->lastMousePos;

        if (delta.manhattanLength() < 4) {
            handleViewCubeClick(event->pos());
        }

        d->mousePressed = false;
    }

    if (event->button() == Qt::RightButton) {
        d->mousePressed = false;
    }
}

void CadView::wheelEvent(QWheelEvent* event) {
    if (d->view.IsNull()) {
        return;
    }

    const int wheelDelta = event->angleDelta().y();
    if (wheelDelta == 0) {
        return;
    }

    // ✅ FIX: 以滑鼠游標位置為中心縮放
    //
    // 原理：
    //   1. 取得縮放前滑鼠下方的 3D 世界座標
    //   2. 執行縮放（SetScale 以視圖中心為基準）
    //   3. 取得縮放後同一 3D 點投影到螢幕的新位置
    //   4. Pan 補償位移差，使該 3D 點回到滑鼠位置下方

    // Step 1: 滑鼠位置 → OCCT 像素座標
    Standard_Integer xp, yp;
    qtToOCCT(event->position().toPoint(), xp, yp);

    // Step 2: 螢幕座標 → 3D 世界座標（縮放前）
    Standard_Real xv, yv, zv;
    d->view->Convert(xp, yp, xv, yv, zv);

    // Step 3: 計算縮放倍率（每格滾輪 ±10%）
    const Standard_Real zoomFactor = (wheelDelta > 0) ? 1.1 : (1.0 / 1.1);
    const Standard_Real newScale   = d->view->Scale() * zoomFactor;
    d->view->SetScale(newScale);

    // Step 4: 同一 3D 點在縮放後的新螢幕座標
    Standard_Integer newXp, newYp;
    d->view->Convert(xv, yv, zv, newXp, newYp);

    // Step 5: Pan 補償，讓 3D 點回到原始滑鼠位置
    // OCCT Pan(dX, dY)：dX 向右為正，dY 向上為正（螢幕 Y 軸相反）
    d->view->Pan(xp - newXp, -(yp - newYp));

    // ✅ 縮放比例改變，格線間距/範圍需依新的縮放比例重新計算
    if (d->grid && d->gridEnabled) {
        d->grid->update();
    }

    if (m_snapManager && m_snapManager->isSnapActive()) {
        // 重新算一次同位置的 snap，讓 Indicator 以新的 scale 重繪
        m_snapManager->refreshIndicator();
    }
    update();
}

void CadView::keyPressEvent(QKeyEvent* event) {
    // ── 窗選提示階段：直接攔截鍵盤輸入 F / WP / CP（備援路徑）──────────────
    // 主要路徑其實是 CommandLineWidget 對 qApp 安裝的全域事件過濾器，會把
    // 所有非修飾鍵的按鍵導向命令列輸入框（見 CommandLineWidget::eventFilter），
    // 所以正常情況下這裡通常收不到這些按鍵事件。保留這段作為備援，
    // 以防命令列隱藏或事件過濾器未生效等情況。
    if (d->boxSelectArmed && d->boxSelectShape == BoxSelectShape::Rectangle) {
        const QString text = event->text().toUpper();
        if (!text.isEmpty() && text.at(0).isLetter()) {
            d->boxSelectShapeKeyBuffer += text.at(0);
            if (d->boxSelectShapeKeyBuffer.size() > 2) {
                d->boxSelectShapeKeyBuffer = d->boxSelectShapeKeyBuffer.right(2);
            }

            if (d->boxSelectShapeKeyBuffer.endsWith("WP")) {
                beginBoxSelectShapeMode(BoxSelectShape::WPolygon);
                event->accept();
                return;
            }
            if (d->boxSelectShapeKeyBuffer.endsWith("CP")) {
                beginBoxSelectShapeMode(BoxSelectShape::CPolygon);
                event->accept();
                return;
            }
            if (text == "F") {
                beginBoxSelectShapeMode(BoxSelectShape::Fence);
                event->accept();
                return;
            }
            // 其餘字母（例如打到一半的 W/C）：先累積在緩衝區，事件本身不消費，
            // 讓其餘既有按鍵邏輯（如快捷鍵）仍可正常運作。
        }
    }

    // 在 keyPressEvent 的 ESC 判斷之前加入：
    if (event->key() == Qt::Key_F3) {
        if (m_snapManager) {
            if (event->modifiers() & Qt::ShiftModifier) {
                // Shift+F3：全部關閉
                osnap::OSnapSettings s = m_snapManager->settings();
                s.enabledTypes = osnap::SnapType::None;
                m_snapManager->setSettings(s);
            } else {
                // F3：切換 snap 全開/全關
                bool nowEnabled = m_snapManager->isSnapEnabled();
                m_snapManager->setSnapEnabled(!nowEnabled);
            }
        }
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        // 窗選 / 穿越窗選進行中：取消框選，不做其他事
        if (d->boxSelectArmed) {
            cancelBoxSelectCandidate();
            d->mousePressed = false;
            event->accept();
            return;
        }
        if (m_selectionFilter == "plane") {
            qDebug() << "[CadView] Plane selection cancelled";

            core::EventBus* bus = core::Application::instance()->eventBus();

            QVariantMap planeData;
            planeData["cancelled"] = true;

            bus->publish("plane.selected", planeData);

            highlightSelectablePlanes(false);

            return;
        }
        if (d->mode == InteractionMode::Sketching || d->mode == InteractionMode::GetPoint) {
            Q_EMIT pointCancelled();
            if (d->rubberBand) {
                d->rubberBand->clearPoints();
                d->rubberBand->clear();
            }
        }
        if (d->mode == InteractionMode::GetPoint) {
            EventBus* bus = Application::instance()->eventBus();
            bus->publish(Events::POINT_CANCELLED, QVariant());

            Q_EMIT pointCancelled();
        }
        // ESC：grips 開啟時，單次按下即關閉全部 grips（Sketch / HAlign edit 共用同一邏輯）
        if (turnOffActiveGrips()) {
            event->accept();
        }
        return;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (d->boxSelectArmed && d->boxSelectShape != BoxSelectShape::Rectangle) {
            finishBoxSelectPolygon();
            event->accept();
            return;
        }
        if (d->mode == InteractionMode::Sketching) {
            // TODO: 完成當前繪圖
        }
        return;
    }

    if (event->key() == Qt::Key_Space) {
        if (d->boxSelectArmed && d->boxSelectShape != BoxSelectShape::Rectangle) {
            finishBoxSelectPolygon();
            event->accept();
            return;
        }
        if (d->mode == InteractionMode::Sketching) {
            qDebug() << "[CadView] Spacebar pressed - finishing command";
            EventBus* bus = Application::instance()->eventBus();
            bus->publish(Events::POINT_CANCELLED, QVariant());
            return;
        }
    }

    if (!event->text().isEmpty()) {
        Q_EMIT keyInputReceived(event->text());
        return;
    }

    QWidget::keyPressEvent(event);
}

// CadView.cpp
void CadView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);

    // ← 新增：首次 show 時才真正初始化 OCCT viewer
    if (d->viewer.IsNull()) {   // 尚未初始化
        // 用 singleShot 確保 Native Window / NSView 已完全就緒
        QTimer::singleShot(0, this, [this]() {
            initializeViewer();

            if (!m_viewReadyPublished && isVisible() && width() > 0 && height() > 0) {
                m_viewReadyPublished = true;
                auto* bus = core::Application::instance()->eventBus();
                QTimer::singleShot(0, this, [bus]() {
                    bus->publish(core::Events::VIEW_READY);
                });
            }
        });
        return;
    }

    // 已初始化後再次 show（例如 dock 顯示）
    if (!m_viewReadyPublished && isVisible() && width() > 0 && height() > 0) {
        m_viewReadyPublished = true;
        auto* bus = core::Application::instance()->eventBus();
        // 延一個 event loop，確保 WM 完成初始定位
        QTimer::singleShot(0, this, [bus]() {
            bus->publish(core::Events::VIEW_READY);
        });
    }
}
} // namespace view
} // namespace aicad
