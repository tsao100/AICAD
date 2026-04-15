/**
 * @file CadView.cpp
 * @brief CadView 類別實作 (重構版)
 * @author Felicia
 * @date 2024-12-04
 */

#include "CadView.h"
#include "RubberBand.h"
#include "ViewGrid.h"
#include "cad/Feature.h"
#include "cad/Sketch.h"
#include "cad/Document.h"
#include "cad/PlaneManager.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "core/DocumentManager.h"
#include "osnap/OSnapManager.h"
#include "ui/GripEventFilter.h"
#include "cad/grips/GripManager.h"

#include <QDebug>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QToolTip>

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
    QMap<AIS_InteractiveObject*, int>      aisToGeomIndex;
    QHash<cad::Sketch*, QString>           sketchFeatureIds;

    QSet<int>                              selectedGeomIndices;
    ui::GripEventFilter* gripFilter;
    GripManager*         gripManager;

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

    // 初始化視圖器
    initializeViewer();

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
        return;
    }

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

    d->view->SetWindow(window);
    if (!window->IsMapped()) {
        window->Map();
    }

    // 設定背景
    d->view->SetBackgroundColor(Quantity_NOC_GRAY80);
    d->view->MustBeResized();

    // 建立互動上下文
    d->context = new AIS_InteractiveContext(d->viewer);
    d->context->SetDisplayMode(AIS_Shaded, Standard_True);

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
                                   for (const Handle(AIS_Shape)& s : sketch->aisShapes()) {
                                       if (!s.IsNull()) {
                                           d->aisToFeatureId.remove(s.get());
                                           d->aisToGeomIndex.remove(s.get());
                                       }
                                   }

                                   // ③ 最後才 erase from context
                                   sketch->eraseFromContext(d->context);

                               } else {
                                   // visible: 顯示並重新註冊
                                   QList<Handle(AIS_Shape)> shapes =
                                       sketch->displayInContext(d->context);

                                   // 清除舊條目
                                   for (auto it = d->aisToFeatureId.begin();
                                        it != d->aisToFeatureId.end(); ) {
                                       if (it.value() == itemId)
                                           it = d->aisToFeatureId.erase(it);
                                       else
                                           ++it;
                                   }
                                   for (auto it = d->aisToGeomIndex.begin();
                                        it != d->aisToGeomIndex.end(); ) {
                                       if (!d->aisToFeatureId.contains(it.key()))
                                           it = d->aisToGeomIndex.erase(it);
                                       else
                                           ++it;
                                   }

                                   // 重新註冊
                                   int idx = 0;
                                   for (const Handle(AIS_Shape)& s : shapes) {
                                       if (!s.IsNull()) {
                                           d->aisToFeatureId[s.get()] = itemId;
                                           d->aisToGeomIndex[s.get()] = idx++;
                                       }
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

void CadView::setGripManager(GripManager* mgr, ui::GripEventFilter* filter) {
    d->gripManager = mgr;
    d->gripFilter  = filter;
}

void CadView::setDocument(cad::Document* document) {
    if (d->document == document) {
        return;
    }

    d->document = document;

    qDebug() << "[CadView] Document set";

    if (document) {
        displayAllFeatures();
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

    d->context->RemoveAll(Standard_False);
    d->context->Display(d->viewCube, Standard_False);
    d->aisToFeatureId.clear();  // ✅ 全部重建
    d->aisToGeomIndex.clear();

    for (Feature* feature : d->document->features()) {
        if (!feature) continue;

        if (Sketch* sketch = qobject_cast<Sketch*>(feature)) {

            if (feature->isVisible()) {
                // ✅ visible: 正常顯示並註冊
                QList<Handle(AIS_Shape)> shapes =
                    sketch->displayInContext(d->context);
                int idx = 0;
                for (const Handle(AIS_Shape)& s : shapes) {
                    if (!s.IsNull()) {
                        d->aisToFeatureId[s.get()] = feature->id();
                        d->aisToGeomIndex[s.get()] = idx++;
                    }
                }
            } else {
                // ✅ invisible: rebuild 但不 display，只建立 aisShapes
                //    讓 map 有條目，之後 setVisible(true) 時 display 即生效
                sketch->rebuild();
                // 不呼叫 displayInContext，shapes 存在但不顯示
                int idx = 0;
                for (const Handle(AIS_Shape)& s : sketch->aisShapes()) {
                    if (!s.IsNull()) {
                        d->aisToFeatureId[s.get()] = feature->id();
                        d->aisToGeomIndex[s.get()] = idx++;
                    }
                }
            }

            d->sketchFeatureIds[sketch] = feature->id();
            connect(sketch, &Sketch::rebuilt,
                    this, &CadView::onSketchRebuilt,
                    Qt::UniqueConnection);

        } else if (!feature->shape().IsNull()) {
            Handle(AIS_Shape) aisShape = new AIS_Shape(feature->shape());
            aisShape->SetColor(Quantity_NOC_YELLOW);
            if (feature->isVisible())
                d->context->Display(aisShape, Standard_False);
            d->aisToFeatureId[aisShape.get()] = feature->id();
        }
    }

    d->context->UpdateCurrentViewer();
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
    for (auto it = d->aisToGeomIndex.begin(); it != d->aisToGeomIndex.end(); ) {
        if (!d->aisToFeatureId.contains(it.key()))
            it = d->aisToGeomIndex.erase(it);
        else
            ++it;
    }
    int idx = 0;
    for (const Handle(AIS_Shape)& s : sketch->aisShapes()) {
        if (!s.IsNull()) {
            d->aisToFeatureId[s.get()] = fid;
            d->aisToGeomIndex[s.get()] = idx++;
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

    // ✅ 同時發布到 EventBus，確保 Command 系統能收到
    auto* bus = core::Application::instance()->eventBus();
    if (bus) {
        QVariantMap data;
        data["point"] = QVariant::fromValue(planePt);
        bus->publish(core::Events::POINT_ACQUIRED, data);
    }

    qDebug() << "[CadView] Point acquired:" << planePt.x() << "," << planePt.y();

    Q_EMIT pointAcquired(planePt);
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
        qDebug() << "[CadView] RMB clicked - finishing command";
        EventBus* bus = Application::instance()->eventBus();
        bus->publish(Events::POINT_CANCELLED, QVariant());
        return;
    }

    int x = event->x();
    int y = event->y();

    if (event->button() == Qt::LeftButton) {
        if (d->mode == InteractionMode::Sketching) {
            // ── OSnap 優先：有鎖定點則使用 snap 座標，直接 return ──────────────
            // snapConfirmed signal → CadView lambda 會發布正確格式的 POINT_ACQUIRED
            if (m_snapManager && m_snapManager->onMousePress(x, y)) {
                return;
            }

            // ── 無 snap：使用原始滑鼠座標 ─────────────────────────────────────
            QVector2D planePt = screenToPlane(event->pos());

            EventBus* bus = Application::instance()->eventBus();

            QVariantMap data;
            data["point"] = QVariant::fromValue(planePt);
            data["screenPos"] = event->pos();

            bus->publish(Events::POINT_ACQUIRED, data);
            Q_EMIT pointAcquired(planePt);
            return;
        }
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
                handlePointInput(event->pos());
                break;
            case InteractionMode::Selecting:
                d->context->SelectDetected(AIS_SelectionScheme_Replace);
                handleObjectSelection(event->pos());
                break;
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
        if (d->gripFilter && d->gripFilter->isCapturing()) {
            return;  // grip is dragging, skip AIS selection
        }

        // ✅ Shift = add to selection, otherwise replace
        bool additive = (!d->aisToGeomIndex.empty());

        // ② Tell OCCT to perform selection at this pixel
        d->context->SelectDetected(
            additive ? AIS_SelectionScheme_Add
                     : AIS_SelectionScheme_Replace);

        // ✅ Collect ALL currently selected shapes
        QMap<QString, QSet<int>> selectionMap;  // featureId → set of geomIndices

        for (d->context->InitSelected();
             d->context->MoreSelected();
             d->context->NextSelected())
        {
            Handle(AIS_Shape) s = Handle(AIS_Shape)::DownCast(
                d->context->SelectedInteractive());
            if (s.IsNull()) continue;

            QString featureId = d->aisToFeatureId.value(s.get());
            int     geomIdx   = d->aisToGeomIndex.value(s.get(), -1);

            if (!featureId.isEmpty() && geomIdx >= 0) {
                selectionMap[featureId].insert(geomIdx);
            }
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
            bus->publish("selection.cleared", QVariant());
        }
    }

    Q_EMIT viewClicked(event->pos(), event->button());
}

void CadView::mouseMoveEvent(QMouseEvent* event) {
    int x = event->x();
    int y = event->y();

    Standard_Integer xp, yp;
    qtToOCCT(event->pos(), xp, yp);
    d->context->MoveTo(xp, yp, d->view, Standard_True);

    // ── OSnap 偵測（每次 mouse move）──────────────────────────────────────
    // 注意：Grip 系統已透過 EventBus 設定 m_snapManager 的 m_gripActive 旗標，
    //       所以這裡不需要額外判斷。
    if (m_snapManager && m_snapManager->isSnapEnabled()) {
        m_snapManager->onMouseMove(x, y);
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
    if (!d->context.IsNull() && !d->view.IsNull()) {
        //d->context->MoveTo(xp, yp, d->view, Standard_True);

        if (d->context->HasDetected()) {
            Handle(AIS_InteractiveObject) detected = d->context->DetectedInteractive();
            if (!detected.IsNull() && detected == d->viewCube) {
                setCursor(Qt::PointingHandCursor);
            } else {
                unsetCursor();
            }
        } else {
            unsetCursor();
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
        if (d->gripManager && d->gripManager->hasActiveGrips()) {

            d->gripManager->detach();

            if (d->gripFilter)
                d->gripFilter->clearSketchPlane();

            // ② 清除 OCCT selection 高亮
            if (!d->context.IsNull()) {
                d->context->ClearSelected(Standard_False);
                d->context->UpdateCurrentViewer();
            }

            // ③ 通知其他元件選取已清除
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
    if (!d->viewer.IsNull() == false) {   // 尚未初始化
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
