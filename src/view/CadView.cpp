// src/view/CadView.cpp

#include "CadView.h"
#include "RubberBand.h"
#include "GridOverlay.h"
#include "../cad/Document.h"
#include "../cad/features/Feature.h"
#include "../core/Application.h"
#include "../core/EventBus.h"

// OCCT
#include <OpenGl_GraphicDriver.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Quantity_Color.hxx>
#include <AIS_Shape.hxx>
#include <IntAna_IntConicQuad.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>

#ifdef _WIN32
#include <WNT_Window.hxx>
#else
#include <Xw_Window.hxx>
#endif

#include <QDebug>
#include <QTimer>

namespace aicad {
namespace view {

class CadView::Private {
public:
    // OCCT
    Handle(V3d_Viewer) viewer;
    Handle(V3d_View) view;
    Handle(AIS_InteractiveContext) context;
    Handle(AIS_ViewCube) viewCube;
    
    // 文件
    cad::Document* document;
    
    // 狀態
    ViewOrientation orientation;
    InteractionMode interactionMode;
    
    // 輔助工具
    RubberBand* rubberBand;
    GridOverlay* gridOverlay;
    
    // 互動狀態
    bool mousePressed;
    Qt::MouseButton pressedButton;
    QPoint lastMousePos;
    
    // 初始化標記
    bool viewInitialized;
    
    Private()
        : document(nullptr)
        , orientation(ViewOrientation::Isometric)
        , interactionMode(InteractionMode::None)
        , rubberBand(nullptr)
        , gridOverlay(nullptr)
        , mousePressed(false)
        , pressedButton(Qt::NoButton)
        , viewInitialized(false)
    {
    }
};

CadView::CadView(QWidget* parent)
    : QWidget(parent)
    , d(new Private())
{
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setBackgroundRole(QPalette::NoRole);
    
    initializeViewer();
    
    qDebug() << "[CadView] Created";
}

CadView::~CadView() {
    qDebug() << "[CadView] Destroying";
    
    delete d->rubberBand;
    delete d->gridOverlay;
    delete d;
}

void CadView::initializeViewer() {
    qDebug() << "[CadView] Initializing viewer";
    
    // 建立圖形驅動
#ifdef _WIN32
    Handle(Aspect_DisplayConnection) displayConnection = new Aspect_DisplayConnection();
#else
    Handle(Aspect_DisplayConnection) displayConnection = new Aspect_DisplayConnection("");
#endif
    
    Handle(OpenGl_GraphicDriver) graphicDriver = new OpenGl_GraphicDriver(displayConnection);
    
    // 建立 Viewer
    d->viewer = new V3d_Viewer(graphicDriver);
    d->viewer->SetDefaultLights();
    d->viewer->SetLightOn();
    
    // 建立 View
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
    
    // 建立 AIS Context
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
            Graphic3d_Vec2i(85, 85)));
    d->context->Display(d->viewCube, Standard_False);
    
    // 建立輔助工具
    d->rubberBand = new RubberBand(d->view, this);
    d->gridOverlay = new GridOverlay(d->view, this);
    
    // 設定初始視角
    setOrientation(ViewOrientation::Isometric);
    
    // 延遲更新
    QTimer::singleShot(0, this, [this]() {
        if (!d->view.IsNull()) {
            d->view->MustBeResized();
            d->view->Redraw();
        }
    });
    
    qDebug() << "[CadView] Viewer initialized";
}

void CadView::setDocument(cad::Document* document) {
    if (d->document == document) {
        return;
    }
    
    // 清除舊文件的顯示
    if (d->document) {
        eraseAll();
    }
    
    d->document = document;
    
    // 顯示新文件
    if (d->document) {
        displayAll();
    }
    
    qDebug() << "[CadView] Document set";
}

cad::Document* CadView::document() const {
    return d->document;
}

void CadView::displayFeature(cad::Feature* feature) {
    if (!feature || d->context.IsNull()) {
        return;
    }
    
    TopoDS_Shape shape = feature->shape();
    if (shape.IsNull()) {
        qWarning() << "[CadView] Feature has no shape";
        return;
    }
    
    Handle(AIS_Shape) aisShape = new AIS_Shape(shape);
    
    // 根據特徵類型設定顯示樣式
    if (feature->type() == cad::FeatureType::Sketch) {
        aisShape->SetColor(Quantity_NOC_WHITE);
        aisShape->SetWidth(2.0);
        d->context->SetDisplayMode(aisShape, AIS_WireFrame, Standard_False);
    } else {
        aisShape->SetColor(Quantity_NOC_LIGHTSTEELBLUE);
        d->context->SetDisplayMode(aisShape, AIS_Shaded, Standard_False);
    }
    
    d->context->Display(aisShape, Standard_False);
    d->context->UpdateCurrentViewer();
    
    qDebug() << "[CadView] Feature displayed:" << feature->name();
}

void CadView::eraseFeature(cad::Feature* feature) {
    // TODO: 實作特徵與 AIS 物件的映射
    qDebug() << "[CadView] Feature erased:" << feature->name();
}

void CadView::updateFeature(cad::Feature* feature) {
    eraseFeature(feature);
    displayFeature(feature);
}

void CadView::displayAll() {
    if (!d->document) {
        return;
    }
    
    eraseAll();
    
    QVector<cad::Feature*> features = d->document->features();
    for (cad::Feature* feature : features) {
        if (feature->isVisible()) {
            displayFeature(feature);
        }
    }
    
    fitAll();
    
    qDebug() << "[CadView] Displayed all features:" << features.size();
}

void CadView::eraseAll() {
    if (d->context.IsNull()) {
        return;
    }
    
    d->context->RemoveAll(Standard_False);
    
    // 保留 ViewCube
    d->context->Display(d->viewCube, Standard_False);
    d->context->UpdateCurrentViewer();
    
    qDebug() << "[CadView] Erased all";
}

void CadView::fitAll() {
    if (!d->view.IsNull()) {
        d->view->FitAll();
        d->view->ZFitAll();
        redraw();
    }
}

void CadView::redraw() {
    if (!d->view.IsNull()) {
        d->view->Redraw();
        update();
    }
}

void CadView::setOrientation(ViewOrientation orientation) {
    if (d->orientation == orientation) {
        return;
    }
    
    d->orientation = orientation;
    
    switch (orientation) {
    case ViewOrientation::Top:
        d->view->SetProj(V3d_Zpos);
        break;
    case ViewOrientation::Bottom:
        d->view->SetProj(V3d_Zneg);
        break;
    case ViewOrientation::Front:
        d->view->SetProj(V3d_Yneg);
        break;
    case ViewOrientation::Back:
        d->view->SetProj(V3d_Ypos);
        break;
    case ViewOrientation::Right:
        d->view->SetProj(V3d_Xpos);
        break;
    case ViewOrientation::Left:
        d->view->SetProj(V3d_Xneg);
        break;
    case ViewOrientation::Isometric:
    default:
        d->view->SetProj(V3d_XposYnegZpos);
        break;
    }
    
    fitAll();
    
    Q_EMIT orientationChanged(orientation);
    Q_EMIT viewChanged();
    
    qDebug() << "[CadView] Orientation changed";
}

ViewOrientation CadView::orientation() const {
    return d->orientation;
}

void CadView::setInteractionMode(InteractionMode mode) {
    if (d->interactionMode == mode) {
        return;
    }
    
    d->interactionMode = mode;
    
    // 根據模式更新游標
    switch (mode) {
    case InteractionMode::Pan:
        setCursor(Qt::OpenHandCursor);
        break;
    case InteractionMode::Rotate:
        setCursor(Qt::ClosedHandCursor);
        break;
    case InteractionMode::SelectPoint:
        setCursor(Qt::CrossCursor);
        break;
    default:
        unsetCursor();
        break;
    }
    
    Q_EMIT interactionModeChanged(mode);
    
    qDebug() << "[CadView] Interaction mode changed";
}

InteractionMode CadView::interactionMode() const {
    return d->interactionMode;
}

QVector2D CadView::screenToPlane(const QPoint& screenPos) const {
    // 簡化實作 - 投影到 XY 平面
    Standard_Integer xp, yp;
    qtToOcct(screenPos, xp, yp);
    
    Standard_Real Xv, Yv, Zv;
    d->view->Convert(xp, yp, Xv, Yv, Zv);
    
    // 假設投影到 Z=0 平面
    return QVector2D(Xv, Yv);
}

QVector3D CadView::screenToWorld(const QPoint& screenPos, double depth) const {
    Standard_Integer xp, yp;
    qtToOcct(screenPos, xp, yp);
    
    Standard_Real Xv, Yv, Zv;
    d->view->Convert(xp, yp, Xv, Yv, Zv);
    
    return QVector3D(Xv, Yv, Zv * depth);
}

QPoint CadView::worldToScreen(const QVector3D& worldPos) const {
    Standard_Integer xp, yp;
    d->view->Convert(worldPos.x(), worldPos.y(), worldPos.z(), xp, yp);
    
#ifdef _WIN32
    qreal dpr = devicePixelRatio();
    return QPoint(xp / dpr, yp / dpr);
#else
    return QPoint(xp, yp);
#endif
}

Handle(V3d_View) CadView::occView() const {
    return d->view;
}

Handle(AIS_InteractiveContext) CadView::context() const {
    return d->context;
}

RubberBand* CadView::rubberBand() const {
    return d->rubberBand;
}

GridOverlay* CadView::gridOverlay() const {
    return d->gridOverlay;
}

void CadView::setViewCubeVisible(bool visible) {
    if (d->viewCube.IsNull()) {
        return;
    }
    
    if (visible) {
        d->context->Display(d->viewCube, Standard_False);
    } else {
        d->context->Erase(d->viewCube, Standard_False);
    }
    
    redraw();
}

void CadView::setBackgroundColor(const QColor& color) {
    if (d->view.IsNull()) {
        return;
    }
    
    Quantity_Color occColor(color.redF(), color.greenF(), color.blueF(), Quantity_TOC_RGB);
    d->view->SetBackgroundColor(occColor);
    redraw();
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
if (!d->viewInitialized) {
    d->viewInitialized = true;
}

if (!d->view.IsNull()) {
    d->view->MustBeResized();
    d->view->Redraw();
}
}

void CadView::mousePressEvent(QMouseEvent* event) {
d->lastMousePos = event->pos();
d->mousePressed = true;
d->pressedButton = event->button();
Standard_Integer xp, yp;
qtToOcct(event->pos(), xp, yp);

// 更新 OCCT 選取
if (!d->context.IsNull() && !d->view.IsNull()) {
    d->context->MoveTo(xp, yp, d->view, Standard_True);
    
    if (event->button() == Qt::LeftButton) {
        // 檢查是否點擊 ViewCube
        if (d->context->HasDetected()) {
            Handle(AIS_InteractiveObject) picked = d->context->DetectedInteractive();
            if (!picked.IsNull() && picked == d->viewCube) {
                // ViewCube 會自動處理
                return;
            }
        }
        
        // 根據互動模式處理
        if (d->interactionMode == InteractionMode::SelectPoint) {
            QVector2D point = screenToPlane(event->pos());
            Q_EMIT pointSelected(point);
            
            qDebug() << "[CadView] Point selected:" << point;
        } else {
            d->context->Select(Standard_True);
            Q_EMIT selectionChanged();
        }
    }
}

// 開始旋轉
if (event->button() == Qt::RightButton && !d->view.IsNull()) {
    d->view->StartRotation(xp, yp);
}
}
void CadView::mouseMoveEvent(QMouseEvent* event) {
Standard_Integer xp, yp;
qtToOcct(event->pos(), xp, yp);
// 更新懸停檢測
if (!d->context.IsNull() && !d->view.IsNull()) {
    d->context->MoveTo(xp, yp, d->view, Standard_True);
    
    if (d->context->HasDetected()) {
        Handle(AIS_InteractiveObject) detected = d->context->DetectedInteractive();
        if (!detected.IsNull() && detected == d->viewCube) {
            setCursor(Qt::PointingHandCursor);
        } else if (d->interactionMode == InteractionMode::None) {
            unsetCursor();
        }
    } else if (d->interactionMode == InteractionMode::None) {
        unsetCursor();
    }
}

// 處理橡皮筋
if (d->interactionMode == InteractionMode::Sketching) {
    updateRubberBand(event->pos());
}

// 視圖操作
if (d->mousePressed && !d->view.IsNull()) {
    int dx = event->pos().x() - d->lastMousePos.x();
    int dy = event->pos().y() - d->lastMousePos.y();
    
    if (d->pressedButton == Qt::MiddleButton) {
        d->view->Pan(dx, -dy);
        Q_EMIT viewChanged();
    } else if (d->pressedButton == Qt::RightButton) {
        d->view->Rotation(xp, yp);
        Q_EMIT viewChanged();
    }
    
    update();
}

d->lastMousePos = event->pos();
}
void CadView::mouseReleaseEvent(QMouseEvent* event) {
Q_UNUSED(event);
d->mousePressed = false;
}
void CadView::wheelEvent(QWheelEvent* event) {
if (!d->view.IsNull()) {
Standard_Real currentScale = d->view->Scale();
Standard_Real delta = event->angleDelta().y() / 120.0;
Standard_Real newScale = currentScale * (1.0 + delta * 0.1);
d->view->SetScale(newScale);
update();
    Q_EMIT viewChanged();
}
}
void CadView::keyPressEvent(QKeyEvent* event) {
// ESC 取消當前操作
if (event->key() == Qt::Key_Escape) {
setInteractionMode(InteractionMode::None);
d->rubberBand->clear();
}
QWidget::keyPressEvent(event);
}
void CadView::updateRubberBand(const QPoint& pos) {
if (d->rubberBand) {
QVector2D point = screenToPlane(pos);
d->rubberBand->updateCurrentPoint(point);
}
}
void CadView::qtToOcct(const QPoint& qtPos, Standard_Integer& occX, Standard_Integer& occY) const {
#ifdef _WIN32
qreal dpr = devicePixelRatio();
occX = static_cast<Standard_Integer>(qtPos.x() * dpr);
occY = static_cast<Standard_Integer>(qtPos.y() * dpr);
#else
occX = qtPos.x();
occY = qtPos.y();
#endif
}
} // namespace view
} // namespace aicad
