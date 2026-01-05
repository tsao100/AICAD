/**
 * @file CadView.cpp
 * @brief CadView 實作 (重構版)
 * @author Felicia
 * @date 2024-12-04
 */

#include "CadView.h"
#include "core/Application.h"
#include "core/EventBus.h"


// CAD 相關 (使用樁檔案)
#include "cad/geometry/CustomPlane.h"

// 視圖系統 (新模組)
#include "view/RubberBand.h"
#include "view/GridOverlay.h"
#include "view/CoordinateConverter.h"

// OCCT
#include <GC_MakeCircle.hxx>
#include <Geom_Circle.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge2d.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <Precision.hxx>
#include <Quantity_Color.hxx>

#ifdef _WIN32
#include <WNT_Window.hxx>
#else
#include <Xw_Window.hxx>
#endif

#include <QApplication>
#include <QDebug>

using namespace aicad::core;
using namespace aicad::view;
using namespace aicad::cad;

// ==================== 建構/解構 ====================

CadView::CadView(QWidget* parent)
    : QWidget(parent)
    , m_rubberBand(nullptr)
    , m_gridOverlay(nullptr)
    , m_document(nullptr)
    , m_currentView(SketchView::Isometric)
    , m_mode(CadMode::Idle)
    , m_mousePressed(false)
    , m_hasCurrentPoint(false)
    , m_viewInitialized(false)
{
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setBackgroundRole(QPalette::NoRole);

    initializeViewer();
    
    qDebug() << "[CadView] Created (Refactored)";
}

CadView::~CadView() {
    qDebug() << "[CadView] Destroying...";
    
    // 清理視圖系統物件
    delete m_rubberBand;
    delete m_gridOverlay;
}

// ==================== 初始化 ====================

void CadView::initializeViewer() {
    qDebug() << "[CadView] Initializing viewer...";
    
#ifdef _WIN32
    Handle(Aspect_DisplayConnection) displayConnection = new Aspect_DisplayConnection();
#else
    Handle(Aspect_DisplayConnection) displayConnection = new Aspect_DisplayConnection("");
#endif

    Handle(OpenGl_GraphicDriver) graphicDriver = new OpenGl_GraphicDriver(displayConnection);

    m_viewer = new V3d_Viewer(graphicDriver);
    m_viewer->SetDefaultLights();
    m_viewer->SetLightOn();

    m_view = m_viewer->CreateView();

#ifdef _WIN32
    Handle(WNT_Window) window = new WNT_Window((Aspect_Handle)winId());
#else
    Handle(Xw_Window) window = new Xw_Window(displayConnection, (Aspect_Drawable)winId());
#endif

    m_view->SetWindow(window);
    if (!window->IsMapped()) {
        window->Map();
    }

    m_view->SetBackgroundColor(Quantity_NOC_GRAY80);
    m_view->MustBeResized();

    m_context = new AIS_InteractiveContext(m_viewer);
    m_context->SetDisplayMode(AIS_Shaded, Standard_True);

    // ViewCube
    m_viewCube = new AIS_ViewCube();
    m_viewCube->SetBoxColor(Quantity_NOC_GRAY75);
    m_viewCube->SetSize(55);
    m_viewCube->SetFontHeight(12);
    m_viewCube->SetAxesLabels("X", "Y", "Z");
    m_viewCube->SetTransformPersistence(
        new Graphic3d_TransformPers(Graphic3d_TMF_TriedronPers, 
                                    Aspect_TOTP_RIGHT_UPPER, 
                                    Graphic3d_Vec2i(85, 85)));
    m_context->Display(m_viewCube, Standard_False);

    // 建立視圖系統物件
    m_rubberBand = new RubberBand(m_view, m_context, CustomPlane::XY(), this);
    m_gridOverlay = new GridOverlay(m_view, m_context, this);
    
    // 連接信號
    connect(m_rubberBand, &RubberBand::updated, this, [this]() {
        qDebug() << "[CadView] RubberBand updated";
    });
    
    connect(m_gridOverlay, &GridOverlay::visibilityChanged, this, 
            [this](bool visible) {
        qDebug() << "[CadView] Grid visibility changed:" << visible;
    });

    setSketchView(SketchView::Isometric);

    // 強制初始更新
    QTimer::singleShot(0, this, [this]() {
        if (!m_view.IsNull()) {
            m_view->MustBeResized();
            m_view->Redraw();
        }
    });

    Application* aicadApp = Application::instance();

    EventBus* bus = aicadApp->eventBus();
    bus->subscribe(Events::FEATURE_UPDATED, this,
                   [this](const QVariant&) {
                       updateDisplay();
                   });
    
    qDebug() << "[CadView] Viewer initialized";
}

// ==================== 文件與特徵 ====================

void CadView::setDocument(aicad::cad::Document* doc) {
    m_document = doc;
    // displayAllFeatures();  // 等待 Document 類別完成
    qDebug() << "[CadView] Document set";
}

void CadView::displayAllFeatures() {
    if (!m_document) {
        qWarning() << "[CadView] No document set";
        return;
    }

    m_context->RemoveAll(Standard_False);
    m_context->Display(m_viewCube, Standard_False);

    // TODO: 等待 Document 類別完成
    // QVector<TDF_Label> features = m_document->getFeatures();
    // for (const TDF_Label& label : features) {
    //     displayFeature(label);
    // }

    fitAll();
}

void CadView::displayFeature(TDF_Label label) {
    // TODO: 等待 Ben 完成 Feature 系統
    qDebug() << "[CadView] displayFeature (stub)";
}

void CadView::highlightFeature(int featureId) {
    qDebug() << "[CadView] highlightFeature:" << featureId;
    update();
}

// ==================== 視圖控制 ====================

void CadView::setSketchView(SketchView view) {
    m_currentView = view;

    switch(view) {
    case SketchView::Top:
        m_view->SetProj(V3d_Zpos);
        break;
    case SketchView::Bottom:
        m_view->SetProj(V3d_Zneg);
        break;
    case SketchView::Front:
        m_view->SetProj(V3d_Yneg);
        break;
    case SketchView::Back:
        m_view->SetProj(V3d_Ypos);
        break;
    case SketchView::Right:
        m_view->SetProj(V3d_Xpos);
        break;
    case SketchView::Left:
        m_view->SetProj(V3d_Xneg);
        break;
    case SketchView::Isometric:
    default:
        m_view->SetProj(V3d_XposYnegZpos);
        break;
    }

    fitAll();
    qDebug() << "[CadView] View changed to:" << static_cast<int>(view);
}

void CadView::fitAll() {
    if (!m_view.IsNull()) {
        m_view->FitAll();
        m_view->ZFitAll();
        update();
    }
}

void CadView::refreshView() {
    if (!m_view.IsNull()) {
        m_view->Redraw();
        update();
    }
}

// ==================== 模式控制 ====================

void CadView::setMode(CadMode mode) {
    m_mode = mode;
    qDebug() << "[CadView] Mode changed to:" << static_cast<int>(mode);
}

void CadView::setRubberBandMode(RubberBandMode mode) {
    if (m_rubberBand) {
        m_rubberBand->setMode(mode);
        m_sketchPoints.clear();
        m_hasCurrentPoint = false;
    }
}

RubberBandMode CadView::getRubberBandMode() const {
    return m_rubberBand ? m_rubberBand->mode() : RubberBandMode::None;
}

void CadView::setPendingSketch(TDF_Label sketch) {
    m_pendingSketch = sketch;
    updateGrid();
}

// ==================== 座標轉換 ====================

QVector2D CadView::screenToPlane(const QPoint& screenPos) {
    if (m_view.IsNull()) {
        return QVector2D(0, 0);
    }

    // 取得當前平面
    CustomPlane plane;
    if (!m_pendingSketch.IsNull()) {
        // TODO: 等待 Document 類別
        // plane = m_document->getSketchPlane(m_pendingSketch);
        plane = CustomPlane::XY();  // 暫時使用預設
    } else {
        switch (m_currentView) {
        case SketchView::Top:
        case SketchView::Bottom:
            plane = CustomPlane::XY();
            break;
        case SketchView::Front:
        case SketchView::Back:
            plane = CustomPlane::XZ();
            break;
        case SketchView::Right:
        case SketchView::Left:
            plane = CustomPlane::YZ();
            break;
        default:
            plane = CustomPlane::XY();
            break;
        }
    }

    // 使用 CoordinateConverter
    return CoordinateConverter::screenToPlane(m_view, screenPos, plane, this);
}

// ==================== 橡皮筋與網格 ====================

void CadView::updateRubberBand() {
    if (!m_rubberBand) {
        return;
    }
    
    if (m_mode != CadMode::Sketching && m_mode != CadMode::GetPoint) {
        return;
    }
    
    if (!m_hasCurrentPoint) {
        return;
    }
    
    // 設定基準點 (第一個點)
    if (!m_sketchPoints.isEmpty() && m_rubberBand->points().isEmpty()) {
        m_rubberBand->setBasePoint(m_sketchPoints[0]);
    }
    
    // 更新當前點
    m_rubberBand->updateCurrentPoint(m_currentPoint);
}

void CadView::clearRubberBand() {
    if (m_rubberBand) {
        m_rubberBand->clear();
    }
}

void CadView::displayShape(const TopoDS_Shape& shape, bool update)
{
    if (m_context.IsNull())
        return;

    Handle(AIS_Shape) ais = new AIS_Shape(shape);
    m_context->Display(ais, update ? Standard_True : Standard_False);
}

void CadView::drawLine(const gp_Pnt& p1, const gp_Pnt& p2)
{
    TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(p1, p2);
    displayShape(edge);
}

void CadView::drawArc(const gp_Pnt& p1, const gp_Pnt& p2, const gp_Pnt& p3)
{
    GC_MakeCircle circleMaker(p1, p2, p3);
    if (!circleMaker.IsDone())
        return;

    TopoDS_Edge arc =
        BRepBuilderAPI_MakeEdge(circleMaker.Value(), p1, p3);
    displayShape(arc);
}

void CadView::drawCube(const gp_Pnt& p)
{
    TopoDS_Shape box =
        BRepPrimAPI_MakeBox(p, 50.0, 50.0, 50.0).Shape();
    displayShape(box);
}

void CadView::displayPreview(const Handle(AIS_InteractiveObject)& obj)
{
    if (m_context.IsNull())
        return;

    m_context->Display(obj, Standard_False);
    m_context->SetDisplayMode(obj, AIS_Shaded, Standard_False);
    m_view->Redraw();
}

void CadView::removePreview(const Handle(AIS_InteractiveObject)& obj)
{
    if (m_context.IsNull() || obj.IsNull())
        return;

    m_context->Remove(obj, Standard_False);
    m_view->Redraw();
}

void CadView::updateDisplay()
{
    if (!m_view.IsNull())
        m_view->Redraw();
}

void CadView::updateGrid() {
    if (!m_gridOverlay) {
        return;
    }
    
    if (m_pendingSketch.IsNull()) {
        m_gridOverlay->hide();
        return;
    }

    // 取得草圖平面
    CustomPlane plane;
    // TODO: plane = m_document->getSketchPlane(m_pendingSketch);
    plane = CustomPlane::XY();  // 暫時使用預設

    m_gridOverlay->setPlane(plane);
    m_gridOverlay->setGridSize(20);
    m_gridOverlay->setGridSpacing(10.0f);
    m_gridOverlay->show();
}

void CadView::clearGrid() {
    if (m_gridOverlay) {
        m_gridOverlay->hide();
    }
}

// ==================== 形狀建立 (樁函式) ====================

TopoDS_Shape CadView::createPolylineShape(const QVector<QVector2D>& points, 
                                          const CustomPlane& plane) {
    if (points.size() < 2) return TopoDS_Shape();

    try {
        BRepBuilderAPI_MakeWire wireBuilder;

        for (int i = 0; i < points.size() - 1; ++i) {
            QVector3D p1_3d = plane.origin + plane.uAxis * points[i].x() + 
                             plane.vAxis * points[i].y();
            QVector3D p2_3d = plane.origin + plane.uAxis * points[i+1].x() + 
                             plane.vAxis * points[i+1].y();

            gp_Pnt gp1(p1_3d.x(), p1_3d.y(), p1_3d.z());
            gp_Pnt gp2(p2_3d.x(), p2_3d.y(), p2_3d.z());

            if (gp1.Distance(gp2) > Precision::Confusion()) {
                BRepBuilderAPI_MakeEdge edgeBuilder(gp1, gp2);
                if (edgeBuilder.IsDone()) {
                    wireBuilder.Add(edgeBuilder.Edge());
                }
            }
        }

        if (wireBuilder.IsDone()) {
            return wireBuilder.Wire();
        }
    } catch (...) {
        qWarning() << "[CadView] Exception in createPolylineShape";
    }

    return TopoDS_Shape();
}

TopoDS_Shape CadView::createExtrudeShape(TDF_Label sketchLabel, double height) {
    // TODO: 等待完整實作
    qDebug() << "[CadView] createExtrudeShape (stub)";
    return TopoDS_Shape();
}

// ==================== 事件處理 ====================

void CadView::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    if (!m_view.IsNull()) {
        m_view->InvalidateImmediate();
        m_view->Redraw();
    }
}

void CadView::resizeEvent(QResizeEvent* event) {
    Q_UNUSED(event);

    if (!m_viewInitialized) {
        m_viewInitialized = true;
    }

    if (!m_view.IsNull()) {
        m_view->MustBeResized();
        m_view->Redraw();
    }
}

void CadView::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->pos();
    m_mousePressed = true;
    m_pressedButton = event->button();

    // 轉換座標
    Standard_Integer xp, yp;
    CoordinateConverter::qtToOcct(this, event->pos(), xp, yp);

    // 更新 OCCT 選取
    if (!m_context.IsNull() && !m_view.IsNull()) {
        m_context->MoveTo(xp, yp, m_view, Standard_True);

        if (event->button() == Qt::LeftButton) {
            m_context->Select(Standard_True);

            // 檢查是否點擊 ViewCube
            if (m_context->HasDetected()) {
                Handle(AIS_InteractiveObject) picked = m_context->DetectedInteractive();
                if (!picked.IsNull() && picked == m_viewCube) {
                    return;
                }
            }
        }
    }

    // 處理草圖模式
    if (m_mode == CadMode::Sketching && event->button() == Qt::LeftButton) {
        QVector2D planePt = screenToPlane(event->pos());

        if (getRubberBandMode() == RubberBandMode::Rectangle) {
            if (m_sketchPoints.isEmpty()) {
                // 第一個點
                m_sketchPoints.append(planePt);
                m_hasCurrentPoint = true;
                if (m_rubberBand) {
                    m_rubberBand->setBasePoint(planePt);
                }
            } else {
                // 第二個點 - 完成矩形
                Q_EMIT pointAcquired(planePt);
                m_sketchPoints.clear();
                m_hasCurrentPoint = false;
                clearRubberBand();
            }
        } else if (getRubberBandMode() == RubberBandMode::Polyline) {
            // 折線模式 - 每次點擊新增一個點
            m_sketchPoints.append(planePt);
            if (m_rubberBand) {
                m_rubberBand->addPoint(planePt);
            }
            Q_EMIT pointAcquired(planePt);
        }
    }

    // 處理 GetPoint 模式
    if (m_mode == CadMode::GetPoint && event->button() == Qt::LeftButton) {
        QVector2D planePt = screenToPlane(event->pos());
        Q_EMIT pointAcquired(planePt);
        clearRubberBand();
    }

    // 右鍵旋轉
    if (event->button() == Qt::RightButton && !m_view.IsNull()) {
        m_view->StartRotation(xp, yp);
    }
}

void CadView::mouseMoveEvent(QMouseEvent* event) {
    // 轉換座標
    Standard_Integer xp, yp;
    CoordinateConverter::qtToOcct(this, event->pos(), xp, yp);

    if (m_view.IsNull())
        return;

    emit mouseMoved(xp, yp);

    // 更新 OCCT 懸停檢測
    if (!m_context.IsNull() && !m_view.IsNull()) {
        m_context->MoveTo(xp, yp, m_view, Standard_True);

        if (m_context->HasDetected()) {
            Handle(AIS_InteractiveObject) detected = m_context->DetectedInteractive();
            if (!detected.IsNull() && detected == m_viewCube) {
                setCursor(Qt::PointingHandCursor);
            } else {
                unsetCursor();
            }
        } else {
            unsetCursor();
        }
    }

    // 草圖模式或 GetPoint 模式 - 更新橡皮筋
    if (m_mode == CadMode::Sketching || m_mode == CadMode::GetPoint) {
        m_currentPoint = screenToPlane(event->pos());
        m_hasCurrentPoint = true;
        updateRubberBand();
        return;
    }

    // 視圖操作
    if (m_mousePressed && !m_view.IsNull()) {
        int dx = event->pos().x() - m_lastMousePos.x();
        int dy = event->pos().y() - m_lastMousePos.y();

        if (m_pressedButton == Qt::MiddleButton) {
            m_view->Pan(dx, -dy);
        } else if (m_pressedButton == Qt::RightButton) {
            m_view->Rotation(xp, yp);
        }

        update();
    }

    m_lastMousePos = event->pos();
}

void CadView::mouseReleaseEvent(QMouseEvent* event) {
    Q_UNUSED(event);
    m_mousePressed = false;
}

void CadView::wheelEvent(QWheelEvent* event) {
    if (!m_view.IsNull()) {
        Standard_Real currentScale = m_view->Scale();
        Standard_Real delta = event->angleDelta().y() / 120.0;
        Standard_Real newScale = currentScale * (1.0 + delta * 0.1);
        m_view->SetScale(newScale);
        update();
    }
}

void CadView::keyPressEvent(QKeyEvent* event) {
    if (m_mode == CadMode::Sketching) {
        if (event->key() == Qt::Key_Escape) {
            Q_EMIT getPointCancelled();
            m_sketchPoints.clear();
            m_hasCurrentPoint = false;
            clearRubberBand();
            update();
            return;
        } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (!m_sketchPoints.isEmpty() && getRubberBandMode() == RubberBandMode::Polyline) {
                Q_EMIT getPointKeyPressed("ENTER");
                clearRubberBand();
            }
            return;
        }
    }

    // Up/Down 箭頭 - 焦點到命令輸入並轉發事件
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        Q_EMIT requestCommandInputFocus();
        Q_EMIT forwardKeyToCommandInput(static_cast<Qt::Key>(event->key()), 
                                       event->modifiers());
        return;
    }

    // 一般文字輸入 - 啟動命令輸入
    if (!event->text().isEmpty()) {
        Q_EMIT requestCommandInputFocus();
        Q_EMIT getPointActivateInput(event->text());
        return;
    }

    QWidget::keyPressEvent(event);
}
