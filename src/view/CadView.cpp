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
#include "cad/grips/SketchGripProvider.h"
#include "cad/grips/GripManager.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "core/DocumentManager.h"
#include "osnap/OSnapManager.h"
#include "ui/GripEventFilter.h"
#include "ui/UIManager.h"
#include "cad/grips/GripManager.h"
#include "command/CommandManager.h"
#include "geometry/GeometryBuilder.h"

#include "cad/sketch/DimensionLineAIS.h"
#include "cad/sketch/SketchConstraint.h"

#include <QDebug>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>
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
static void QtToOCCT(const QWidget* widget, const QPoint& qtPos,
                     Standard_Integer& occX, Standard_Integer& occY) {
#if defined(_WIN32) || defined(__APPLE__)
    qreal dpr = widget->devicePixelRatio();
    occX = static_cast<Standard_Integer>(qtPos.x() * dpr);
    occY = static_cast<Standard_Integer>(qtPos.y() * dpr);
#else
    occX = qtPos.x();
    occY = qtPos.y();
#endif
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

    QList<OverlayEntry> overlayObjects;
    QVector2D            dimLineAnchor2D;    // ✅ Task E: PlaceDimLine 錨點（草圖平面 2D）
    QVector2D            dimPreviewMousePt; // GDIM: 目前滑鼠草圖座標（overlay 更新用）

    // ── 尺寸線拖曳狀態 ──────────────────────────────────────────────────────
    QString              dragDimUuid;        ///< 正在拖曳的約束 UUID（空 = 無拖曳）
    QVector2D            dragDimStartMouse;  ///< 拖曳起始的草圖平面座標
    double               dragDimBaseOffsetX = 0.0;  ///< 拖曳前的舊偏移 X
    double               dragDimBaseOffsetY = 0.0;  ///< 拖曳前的舊偏移 Y

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
            this, [this](const gp_Pnt& pt, aicad::osnap::SnapType /*type*/) {
                auto* bus = aicad::core::Application::instance()->eventBus();
                if (!bus) return;

                // ✅ 用 snapPoint2D() 取草圖平面座標，格式與 Command 期待一致
                std::optional<QVector2D> pt2d = m_snapManager->snapPoint2D();
                QVector2D planePt = pt2d.has_value()
                                        ? pt2d.value()
                                        : screenToPlane(mapFromGlobal(QCursor::pos()));

                QVariantMap data;
                data["point"] = QVariant::fromValue(planePt);
                bus->publish(core::Events::POINT_ACQUIRED, data);  // ✅ Command 系統能收到
                Q_EMIT pointAcquired(planePt);
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
            this, [this]() {
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
        Handle(AIS_Shape) s = Handle(AIS_Shape)::DownCast(
            d->context->SelectedInteractive());
        if (s.IsNull()) continue;

        QString uuid = d->aisToGeomUuid.value(s.get());  // ← 直接取 UUID
        if (!uuid.isEmpty())
            result << uuid;
    }
    return result;
}

void CadView::clearSketchGeomSelection()
{
    if (!d->context.IsNull())
        d->context->ClearSelected(Standard_True);
    if (auto* bus = Application::instance()->eventBus())
        bus->publish(Events::SKETCH_GEOM_CLEARED, QVariant{});
}

void CadView::setGripManager(GripManager* mgr, ui::GripEventFilter* filter) {
    d->gripManager = mgr;
    d->gripFilter  = filter;
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

void CadView::setMode(InteractionMode mode) {
    if (d->mode == mode) {
        return;
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

    // ── 重新加回 overlay 物件（如 ExtrudeManipulator 箭頭）──────────────
    for (const auto& entry : d->overlayObjects) {
        if (!entry.obj.IsNull()) {
            d->context->Display(entry.obj, Standard_False);
            for (int m : entry.modes)
                d->context->Activate(entry.obj, m);
        }
    }
    if (!d->overlayObjects.isEmpty())
        d->context->UpdateCurrentViewer();

    d->isDisplayingAllFeatures = false;
}

// ── Reverse lookup ────────────────────────────────────────────────────
QString CadView::findFeatureIdByAIS(
    const Handle(AIS_Shape)& aisShape) const
{
    return d->aisToFeatureId.value(aisShape.get(), QString());
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
    connect(actDetect, &QAction::triggered, this, [this, sketch, screenPos] {
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
    QVector2D planePt = screenToPlane(screenPos);

    // 從 OSnapManager 取得目前鎖定的 snap 候選（含 geomUuid / geomHandle）
    QString geomUuid;
    int     geomHandle = -1;
    if (m_snapManager && m_snapManager->isSnapActive()) {
        auto snap = m_snapManager->currentSnap();
        if (snap.has_value()) {
            geomUuid   = snap->geomUuid;
            geomHandle = snap->geomHandle;
            // 若 snap 鎖定點存在，以 snap 的 planePoint 取代原始螢幕投影
            if (!snap->planePoint.isNull())
                planePt = snap->planePoint;
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

    if (m_finishSketchButton) {
        m_finishSketchButton->setGeometry(width() - 120, 10, 110, 30);
    }

    // GDIM overlay 是 OCCT Presentation，resize 無需更新 widget geometry
}

void CadView::mousePressEvent(QMouseEvent* event) {
    d->lastMousePos = event->pos();
    d->mousePressed = true;
    d->pressedButton = event->button();

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

    int x = event->x();
    int y = event->y();

    if (event->button() == Qt::LeftButton &&
        (d->mode == InteractionMode::Sketching ||
         d->mode == InteractionMode::GetPoint  ||
         d->mode == InteractionMode::GetGeom))
    {
        auto* cmdMgr = Application::instance()->commandManager();
        const bool hasCmd = cmdMgr && cmdMgr->hasActiveCommand();

        if (!hasCmd) {
            bool additive = (event->modifiers() & Qt::ShiftModifier);
            d->context->SelectDetected(
                additive ? AIS_SelectionScheme_Add
                         : AIS_SelectionScheme_Replace);

            auto* bus = Application::instance()->eventBus();

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
                // ① 通知 Sketch 高亮選取的幾何
                QVariantMap data;
                data["uuids"] = QVariant::fromValue(uuids);
                bus->publish(Events::SKETCH_GEOM_SELECTED, data);

                // ② 觸發 GripManager 附加 Provider（這是之前完全缺漏的步驟）
                if (!selectionMap.isEmpty()) {
                    QString featureId = selectionMap.firstKey();
                    QVariantList indexList;
                    for (int idx : selectionMap[featureId]) indexList.append(idx);
                    QVariantMap selData;
                    selData["featureId"]   = featureId;
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
                bus->publish("selection.cleared", QVariant());  // 同步清除 Grips
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
                d->context->SelectDetected(AIS_SelectionScheme_Replace);
                handleObjectSelection(event->pos());
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
    int x = event->x();
    int y = event->y();

    Standard_Integer xp, yp;
    qtToOCCT(event->pos(), xp, yp);
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

    // 草圖模式：更新橡皮筋
    if (d->mode == InteractionMode::Sketching) {
        if (d->rubberBand) {
            QVector2D planePt;

            // ✅ 優先使用 snap 鎖定座標
            if (m_snapManager && m_snapManager->isSnapActive()) {
                auto pt2d = m_snapManager->snapPoint2D();
                planePt = pt2d.has_value() ? pt2d.value()
                                           : screenToPlane(event->pos());
            } else {
                planePt = screenToPlane(event->pos());
            }

            d->rubberBand->setCurrentPoint(planePt);
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

    if (m_snapManager && m_snapManager->isSnapActive()) {
        // 重新算一次同位置的 snap，讓 Indicator 以新的 scale 重繪
        m_snapManager->refreshIndicator();
    }
    update();
}

void CadView::keyPressEvent(QKeyEvent* event) {
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
        // ① 先 detach grips（安全順序同 visibility-changed）
        // src/view/CadView.cpp — keyPressEvent ESC 段落，替換原本的 grip detach 區塊

        if (d->gripManager && d->gripManager->hasActiveGrips()) {

            // ① 若 grip 正在選取中（click-to-place 模式），先取消，保留 grip 顯示
            if (d->gripManager->isGripSelected()) {
                d->gripManager->cancelGrip();
                event->accept();
                return;   // 第一次 ESC 只取消移動，不清除 grips
            }

            // ② 第二次 ESC：真正 detach
            d->gripManager->detach();
            if (d->gripFilter)
                d->gripFilter->clearSketchPlane();

            if (!d->context.IsNull()) {
                d->context->ClearSelected(Standard_False);
                d->context->UpdateCurrentViewer();
            }

            auto* bus = core::Application::instance()->eventBus();
            bus->publish("selection.cleared", QVariant());

            qDebug() << "[CadView] ESC: grips detached";
            event->accept();
        }
        return;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (d->mode == InteractionMode::Sketching) {
            // TODO: 完成當前繪圖
        }
        return;
    }

    if (event->key() == Qt::Key_Space) {
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
        QTimer::singleShot(0, this, [bus, this]() {
            bus->publish(core::Events::VIEW_READY);
        });
    }
}
} // namespace view
} // namespace aicad