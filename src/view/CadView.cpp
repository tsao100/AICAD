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
#include "core/Application.h"
#include "core/EventBus.h"
#include "core/DocumentManager.h"

#include <QDebug>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>

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

#ifdef _WIN32
#include <WNT_Window.hxx>
#else
#include <Xw_Window.hxx>
#endif

using namespace aicad::core;
using namespace aicad::cad;

namespace aicad {
namespace view {

// 輔助函式：Qt 座標轉 OCCT 座標
static void QtToOCCT(const QWidget* widget, const QPoint& qtPos,
                     Standard_Integer& occX, Standard_Integer& occY) {
#ifdef _WIN32
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
    
    // 建立顯示連接
#ifdef _WIN32
    Handle(Aspect_DisplayConnection) displayConnection = new Aspect_DisplayConnection();
#else
    Handle(Aspect_DisplayConnection) displayConnection = new Aspect_DisplayConnection("");
#endif
    
    // 建立圖形驅動
    Handle(OpenGl_GraphicDriver) graphicDriver = new OpenGl_GraphicDriver(displayConnection);
    
    // 建立視圖器
    d->viewer = new V3d_Viewer(graphicDriver);
    d->viewer->SetDefaultLights();
    d->viewer->SetLightOn();
    
    // 建立視圖
    d->view = d->viewer->CreateView();
    
    // 建立視窗
#ifdef _WIN32
    Handle(WNT_Window) window = new WNT_Window((Aspect_Handle)winId());
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
    d->grid = new ViewGrid(d->context, this);
    
    // 設定初始視角
    setViewType(ViewType::Isometric);
    
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
    }
    
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

    // ✅ 遍歷場景中的所有物件，找出參考平面
    AIS_ListOfInteractive allObjects;
    d->context->DisplayedObjects(allObjects);

    for (AIS_ListOfInteractive::Iterator it(allObjects); it.More(); it.Next()) {
        Handle(AIS_InteractiveObject) obj = it.Value();
        Handle(AIS_Shape) shape = Handle(AIS_Shape)::DownCast(obj);

        if (shape.IsNull()) continue;

        // 檢查是否為參考平面（根據名稱或屬性判斷）
        if (isReferencePlane(shape)) {
            if (highlight) {
                // 高亮顯示
                d->context->SetColor(shape, Quantity_NOC_YELLOW, Standard_False);
                d->context->SetTransparency(shape, 0.7, Standard_False);
            } else {
                // 恢復原始顯示
                d->context->SetColor(shape, Quantity_NOC_GRAY80, Standard_False);
                d->context->SetTransparency(shape, 0.9, Standard_False);
            }
        }
    }

    d->context->UpdateCurrentViewer();
}

bool CadView::isReferencePlane(const Handle(AIS_Shape)& shape) {
    // ✅ 判斷是否為參考平面
    // 可以根據物件名稱、屬性或其他特徵判斷

    // 方法 1: 檢查是否在參考平面列表中
    for (const Handle(AIS_Shape)& plane : m_referencePlanes) {
        if (shape == plane) return true;
    }

    // 方法 2: 檢查 TopoDS_Shape 類型
    TopoDS_Shape topoShape = shape->Shape();
    if (topoShape.ShapeType() == TopAbs_FACE) {
        // 進一步檢查是否為平面
        // ...
        return true;
    }

    return false;
}

QString CadView::identifyPlane(const Handle(AIS_Shape)& shape) {
    // ✅ 識別平面類型
    TopoDS_Shape topoShape = shape->Shape();

    if (topoShape.ShapeType() != TopAbs_FACE) {
        return "UNKNOWN";
    }

    TopoDS_Face face = TopoDS::Face(topoShape);
    Handle(Geom_Surface) surface = BRep_Tool::Surface(face);
    Handle(Geom_Plane) plane = Handle(Geom_Plane)::DownCast(surface);

    if (plane.IsNull()) {
        return "UNKNOWN";
    }

    // 取得平面法向量
    gp_Pln gpPlane = plane->Pln();
    gp_Dir normal = gpPlane.Axis().Direction();

    const double tolerance = 0.1;

    // 判斷是哪個標準平面
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

void CadView::setDocument(cad::Document* document) {
    if (d->document == document) {
        return;
    }
    
    d->document = document;
    
    qDebug() << "[CadView] Document set";
    
    // 顯示文件內容
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
    
    // 發布事件
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

    // Show/hide finish button based on mode
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
    if (!d->document || d->context.IsNull()) {
        return;
    }

    qDebug() << "[CadView] Displaying all features";

    // 清除所有顯示 (保留 ViewCube)
    d->context->RemoveAll(Standard_False);
    d->context->Display(d->viewCube, Standard_False);

    // ✅ 顯示所有 Feature
    QList<Feature*> features = d->document->features();

    for (Feature* feature : features) {
        if (!feature || !feature->isVisible()) {
            continue;
        }

        // ✅ Sketch 使用特殊顯示方法
        if (Sketch* sketch = qobject_cast<Sketch*>(feature)) {
            sketch->displayInContext(d->context);
        }
        // ✅ 其他 Feature 使用傳統方法
        else if (!feature->shape().IsNull()) {
            Handle(AIS_Shape) aisShape = new AIS_Shape(feature->shape());
            aisShape->SetColor(Quantity_NOC_YELLOW);
            d->context->Display(aisShape, Standard_False);
        }
    }

    fitAll();
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
    
    // 轉換座標
    Standard_Integer xp, yp;
    qtToOCCT(screenPos, xp, yp);
    
    // 取得工作平面
    CustomPlane plane;
    switch (d->viewType) {
    case ViewType::Top:
    case ViewType::Bottom:
        plane = CustomPlane::XY();
        break;
    case ViewType::Front:
    case ViewType::Back:
        plane = CustomPlane::XZ();
        break;
    case ViewType::Right:
    case ViewType::Left:
        plane = CustomPlane::YZ();
        break;
    default:
        plane = CustomPlane::XY();
        break;
    }
    
    gp_Pln gpPlane(
        gp_Pnt(plane.origin.x(), plane.origin.y(), plane.origin.z()),
        gp_Dir(plane.normal.x(), plane.normal.y(), plane.normal.z())
    );
    
    // 取得投影方向和眼睛位置
    Standard_Real Xeye, Yeye, Zeye;
    Standard_Real Xproj, Yproj, Zproj;
    d->view->Eye(Xeye, Yeye, Zeye);
    d->view->Proj(Xproj, Yproj, Zproj);
    
    gp_Pnt eyePoint(Xeye, Yeye, Zeye);
    gp_Dir projDir(Xproj, Yproj, Zproj);
    
    // 轉換螢幕點到 3D
    Standard_Real Xv, Yv, Zv;
    d->view->Convert(xp, yp, Xv, Yv, Zv);
    gp_Pnt screenPoint3D(Xv, Yv, Zv);
    
    // 建立拾取射線
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
    
    // 找到與平面的交點
    IntAna_IntConicQuad intersection(pickLine, gpPlane, Precision::Angular());
    
    if (intersection.IsDone() && intersection.NbPoints() > 0) {
        gp_Pnt intersectPnt = intersection.Point(1);
        
        // 轉換 3D 世界座標到 2D 平面座標
        QVector3D worldPt(intersectPnt.X(), intersectPnt.Y(), intersectPnt.Z());
        QVector3D localPt = worldPt - plane.origin;
        
        float u = QVector3D::dotProduct(localPt, plane.uAxis);
        float v = QVector3D::dotProduct(localPt, plane.vAxis);
        
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
}

bool CadView::isGridEnabled() const {
    return d->gridEnabled;
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

    // ✅ 檢測是否有物件
    if (d->context->HasDetected()) {
        Handle(AIS_InteractiveObject) picked = d->context->DetectedInteractive();

        if (!picked.IsNull() && picked != d->viewCube) {
            // ✅ 選取單一物件
            d->context->SetSelected(picked, Standard_True);

            qDebug() << "[CadView] Selected individual wire/edge";

            // ✅ 發布選取事件
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

    // Reposition finish button
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

    // ✅ 如果是平面選取模式
    if (m_selectionFilter == "plane" && event->button() == Qt::LeftButton) {
        if (d->context->HasDetected()) {
            Handle(AIS_InteractiveObject) picked = d->context->DetectedInteractive();
            Handle(AIS_Shape) pickedShape = Handle(AIS_Shape)::DownCast(picked);

            if (!pickedShape.IsNull() && isReferencePlane(pickedShape)) {
                // ✅ 判斷選中的是哪個平面
                QString planeName = identifyPlane(pickedShape);

                qDebug() << "[CadView] Plane clicked:" << planeName;

                // ✅ 發布選取結果
                core::EventBus* bus = core::Application::instance()->eventBus();

                QVariantMap planeData;
                planeData["plane"] = planeName;
                planeData["cancelled"] = false;

                bus->publish("plane.selected", planeData);

                // 恢復正常模式
                //setMode(InteractionMode::Idle);
                highlightSelectablePlanes(false);

                return;
            }
        }
    }

    // ✅ Add this after plane selection handling, before sketching mode check:
    if (event->button() == Qt::RightButton && d->mode == InteractionMode::Sketching) {
        qDebug() << "[CadView] RMB clicked - finishing command";
        EventBus* bus = Application::instance()->eventBus();
        bus->publish(Events::POINT_CANCELLED, QVariant());
        return;
    }

    if (event->button() == Qt::LeftButton) {
        // ✅ In sketching mode, emit point
        if (d->mode == InteractionMode::Sketching) {
            QVector2D planePt = screenToPlane(event->pos());

            // ✅ Publish to EventBus instead of direct signal
            EventBus* bus = Application::instance()->eventBus();

            QVariantMap data;
            data["point"] = QVariant::fromValue(planePt);
            data["screenPos"] = event->pos();

            bus->publish(Events::POINT_ACQUIRED, data);

            // ✅ Still emit signal for backward compatibility
            Q_EMIT pointAcquired(planePt);  // ✅ LineCommand receives this
            return;
        }
    }

    // 更新 OCCT 選擇
    if (!d->context.IsNull() && !d->view.IsNull()) {
        
        if (event->button() == Qt::LeftButton) {
            // 檢查是否點擊 ViewCube
            if (d->context->HasDetected()) {
                Handle(AIS_InteractiveObject) picked = d->context->DetectedInteractive();
                if (!picked.IsNull() && picked == d->viewCube) {
                    return;
                }
            }
            
            // 根據模式處理
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
           
    // 啟動旋轉
    if (event->button() == Qt::RightButton && !d->view.IsNull()) {
        d->view->StartRotation(xp, yp);
    }
    
    Q_EMIT viewClicked(event->pos(), event->button());
}

void CadView::mouseMoveEvent(QMouseEvent* event) {
    Standard_Integer xp, yp;
    qtToOCCT(event->pos(), xp, yp);
    
    // 更新懸停偵測
    if (!d->context.IsNull() && !d->view.IsNull()) {
        d->context->MoveTo(xp, yp, d->view, Standard_True);
        
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
    
    // 草圖模式：更新橡皮筋
    if (d->mode == InteractionMode::Sketching) {
        if (d->rubberBand) {
            QVector2D planePt = screenToPlane(event->pos());
            d->rubberBand->setCurrentPoint(planePt);
            d->rubberBand->update();
        }
    }
}

void CadView::handleViewCubeClick(const QPoint& pos)
{
    d->context->MoveTo(pos.x(), pos.y(), d->view, Standard_True);

    if (d->context->HasDetected()) {
        Handle(AIS_InteractiveObject) detected = d->context->DetectedInteractive();

        if (detected == d->viewCube) {
            d->context->Select(Standard_True);

            if (!d->viewCube->HasAnimation()) return;

            // ✅ Correct method name
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
    if (event->button() == Qt::LeftButton && d->mousePressed) {
        const QPoint delta = event->pos() - d->lastMousePos;

        // Only treat as a click if mouse didn't move much (not a drag)
        if (delta.manhattanLength() < 4) {
            handleViewCubeClick(event->pos());
        }

        d->mousePressed = false;
    }
}

void CadView::wheelEvent(QWheelEvent* event) {
    if (d->view.IsNull()) {
        return;
    }
    
    Standard_Real currentScale = d->view->Scale();
    Standard_Real delta = event->angleDelta().y() / 120.0;
    Standard_Real newScale = currentScale * (1.0 + delta * 0.1);
    d->view->SetScale(newScale);
    update();
}

void CadView::keyPressEvent(QKeyEvent* event) {
    // ESC 取消操作
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
            // ✅ Publish to EventBus
            EventBus* bus = Application::instance()->eventBus();
            bus->publish(Events::POINT_CANCELLED, QVariant());

            Q_EMIT pointCancelled();
        }

        return;
    }
    
    // Enter 完成多段線
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (d->mode == InteractionMode::Sketching) {
            // TODO: 完成當前繪圖
        }
        return;
    }

    // ✅ Add spacebar handling:
    if (event->key() == Qt::Key_Space) {
        if (d->mode == InteractionMode::Sketching) {
            qDebug() << "[CadView] Spacebar pressed - finishing command";
            EventBus* bus = Application::instance()->eventBus();
            bus->publish(Events::POINT_CANCELLED, QVariant());
            return;
        }
    }

    // 字元輸入 - 用於座標輸入
    if (!event->text().isEmpty()) {
        Q_EMIT keyInputReceived(event->text());
        return;
    }
    
    QWidget::keyPressEvent(event);
}

} // namespace view
} // namespace aicad
