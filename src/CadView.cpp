#include "CadView.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GC_MakeSegment.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <Quantity_Color.hxx>
#include <Aspect_Window.hxx>

#include <QDebug>

#ifdef _WIN32
#include <WNT_Window.hxx>
#else
#include <Xw_Window.hxx>
#endif

#include <QApplication>
#include <QPainter>

CadView::CadView(QWidget* parent)
    : QWidget(parent)
    , m_document(nullptr)
    , m_currentView(SketchView::Isometric)
    , m_mode(CadMode::Idle)
    , m_rubberBandMode(RubberBandMode::None)
    , m_mousePressed(false)
    , m_hasCurrentPoint(false)
{
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setBackgroundRole(QPalette::NoRole);

    initializeViewer();
}

CadView::~CadView() {
}

void CadView::initializeViewer() {
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
    m_view->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, Quantity_NOC_GOLD, 0.08);

    m_context = new AIS_InteractiveContext(m_viewer);
    m_context->SetDisplayMode(AIS_Shaded, Standard_True);

    m_viewCube = new AIS_ViewCube();
    m_viewCube->SetBoxColor(Quantity_NOC_GRAY75);
    m_viewCube->SetSize(55);
    m_viewCube->SetFontHeight(12);
    m_viewCube->SetAxesLabels("X", "Y", "Z");
    m_viewCube->SetTransformPersistence(
        new Graphic3d_TransformPers(Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_LOWER, Graphic3d_Vec2i(85, 85)));
    m_context->Display(m_viewCube, Standard_False);

    setSketchView(SketchView::Isometric);
}

void CadView::setDocument(OcafDocument* doc) {
    m_document = doc;
    displayAllFeatures();
}

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
    update();
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

void CadView::displayAllFeatures() {
    if (!m_document) return;

    m_context->RemoveAll(Standard_False);
    m_context->Display(m_viewCube, Standard_False);

    QVector<TDF_Label> features = m_document->getFeatures();
    for (const TDF_Label& label : features) {
        displayFeature(label);
    }

    fitAll();
}

void CadView::displayFeature(TDF_Label label) {
    if (label.IsNull() || !m_document) return;

    FeatureType type = m_document->getFeatureType(label);
    TopoDS_Shape shape;

    if (type == FeatureType::Sketch) {
        QVector<QVector<QVector2D>> polylines = m_document->getSketchPolylines(label);
        CustomPlane plane = m_document->getSketchPlane(label);

        for (const auto& points : polylines) {
            if (!points.isEmpty()) {
                shape = createPolylineShape(points, plane);

                Handle(AIS_Shape) aisShape = new AIS_Shape(shape);
                aisShape->SetColor(Quantity_NOC_WHITE);
                aisShape->SetWidth(2.0);
                m_context->Display(aisShape, Standard_False);
            }
        }
    } else if (type == FeatureType::Extrude) {
        TDF_Label sketchLabel = m_document->getExtrudeSketch(label);
        double height = m_document->getExtrudeHeight(label);

        if (sketchLabel.IsNull()) {
            qWarning() << "Extrude feature" << m_document->getFeatureId(label)
                       << "references invalid sketch - cannot display";
            return;
        }

        shape = createExtrudeShape(sketchLabel, height);

        if (!shape.IsNull()) {
            m_document->setShape(label, shape);

            Handle(AIS_Shape) aisShape = new AIS_Shape(shape);
            aisShape->SetColor(Quantity_NOC_LIGHTSTEELBLUE);
            m_context->Display(aisShape, Standard_False);
        } else {
            qWarning() << "Failed to create extrude shape for feature"
                       << m_document->getFeatureId(label);
        }
    }

    m_context->UpdateCurrentViewer();
}

TopoDS_Shape CadView::createPolylineShape(const QVector<QVector2D>& points, const CustomPlane& plane) {
    if (points.size() < 2) return TopoDS_Shape();

    try {
        BRepBuilderAPI_MakeWire wireBuilder;

        for (int i = 0; i < points.size() - 1; ++i) {
            QVector3D p1_3d = plane.origin + plane.uAxis * points[i].x() + plane.vAxis * points[i].y();
            QVector3D p2_3d = plane.origin + plane.uAxis * points[i+1].x() + plane.vAxis * points[i+1].y();

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
    }

    return TopoDS_Shape();
}

TopoDS_Shape CadView::createExtrudeShape(TDF_Label sketchLabel, double height) {
    if (sketchLabel.IsNull() || !m_document) return TopoDS_Shape();

    QVector<QVector<QVector2D>> polylines = m_document->getSketchPolylines(sketchLabel);
    if (polylines.isEmpty()) return TopoDS_Shape();

    CustomPlane plane = m_document->getSketchPlane(sketchLabel);

    const QVector<QVector2D>& points = polylines.first();
    if (points.size() < 3) return TopoDS_Shape();

    try {
        BRepBuilderAPI_MakeWire wireBuilder;

        for (int i = 0; i < points.size(); ++i) {
            int next = (i + 1) % points.size();

            QVector3D p1_3d = plane.origin + plane.uAxis * points[i].x() + plane.vAxis * points[i].y();
            QVector3D p2_3d = plane.origin + plane.uAxis * points[next].x() + plane.vAxis * points[next].y();

            gp_Pnt gp1(p1_3d.x(), p1_3d.y(), p1_3d.z());
            gp_Pnt gp2(p2_3d.x(), p2_3d.y(), p2_3d.z());

            if (gp1.Distance(gp2) > Precision::Confusion()) {
                BRepBuilderAPI_MakeEdge edgeBuilder(gp1, gp2);
                if (edgeBuilder.IsDone()) {
                    wireBuilder.Add(edgeBuilder.Edge());
                }
            }
        }

        if (!wireBuilder.IsDone()) return TopoDS_Shape();

        TopoDS_Wire wire = wireBuilder.Wire();

        gp_Pln gpPlane = plane.toGpPln();
        BRepBuilderAPI_MakeFace faceBuilder(gpPlane, wire);

        if (!faceBuilder.IsDone()) return TopoDS_Shape();

        TopoDS_Face face = faceBuilder.Face();

        gp_Vec extrudeVec(plane.normal.x() * height,
                          plane.normal.y() * height,
                          plane.normal.z() * height);

        BRepPrimAPI_MakePrism prismBuilder(face, extrudeVec);

        if (prismBuilder.IsDone()) {
            return prismBuilder.Shape();
        }
    } catch (...) {
    }

    return TopoDS_Shape();
}

void CadView::highlightFeature(int featureId) {
    update();
}

void CadView::setRubberBandMode(RubberBandMode mode) {
    m_rubberBandMode = mode;
    m_sketchPoints.clear();
    m_hasCurrentPoint = false;
}

void CadView::setPendingSketch(TDF_Label sketch) {
    m_pendingSketch = sketch;
}

QVector2D CadView::screenToPlane(const QPoint& screenPos) {
    if (m_view.IsNull()) return QVector2D(0, 0);

    Standard_Integer xp = screenPos.x();
    Standard_Integer yp = screenPos.y();

    Standard_Real xv, yv, zv;
    m_view->Convert(xp, yp, xv, yv, zv);

    gp_Pnt eyePnt;
    gp_Dir eyeDir;
    m_view->ConvertWithProj(xp, yp, xv, yv, zv, eyePnt.ChangeCoord(), eyeDir.ChangeCoord());

    CustomPlane plane;
    if (!m_pendingSketch.IsNull() && m_document) {
        plane = m_document->getSketchPlane(m_pendingSketch);
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

    gp_Pln gpPlane = plane.toGpPln();
    gp_Lin line(eyePnt, eyeDir);

    IntAna_IntConicQuad intersection(line, gpPlane, Precision::Angular());

    if (intersection.IsDone() && intersection.NbPoints() > 0) {
        gp_Pnt intersectPnt = intersection.Point(1);

        QVector3D worldPt(intersectPnt.X(), intersectPnt.Y(), intersectPnt.Z());
        QVector3D localPt = worldPt - plane.origin;

        float u = QVector3D::dotProduct(localPt, plane.uAxis);
        float v = QVector3D::dotProduct(localPt, plane.vAxis);

        return QVector2D(u, v);
    }

    return QVector2D(xv, yv);
}

void CadView::paintEvent(QPaintEvent* event) {
    if (!m_view.IsNull()) {
        m_view->InvalidateImmediate();
        FlushViewEvents(m_context, m_view, Standard_True);
    }
}

void CadView::resizeEvent(QResizeEvent* event) {
    if (!m_view.IsNull()) {
        m_view->MustBeResized();
    }
}

void CadView::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->pos();
    m_mousePressed = true;
    m_pressedButton = event->button();

    if (m_mode == CadMode::Sketching && event->button() == Qt::LeftButton) {
        QVector2D planePt = screenToPlane(event->pos());

        if (m_rubberBandMode == RubberBandMode::Rectangle) {
            if (m_sketchPoints.isEmpty()) {
                m_sketchPoints.append(planePt);
            } else {
                m_sketchPoints.append(planePt);
                Q_EMIT pointAcquired(planePt);
                m_sketchPoints.clear();
                m_hasCurrentPoint = false;
            }
        } else if (m_rubberBandMode == RubberBandMode::Polyline) {
            m_sketchPoints.append(planePt);
            Q_EMIT pointAcquired(planePt);
        }
    }
}

void CadView::mouseMoveEvent(QMouseEvent* event) {
    if (m_mode == CadMode::Sketching) {
        m_currentPoint = screenToPlane(event->pos());
        m_hasCurrentPoint = true;
        update();
        return;
    }

    if (m_mousePressed && !m_view.IsNull()) {
        int dx = event->pos().x() - m_lastMousePos.x();
        int dy = event->pos().y() - m_lastMousePos.y();

        if (m_pressedButton == Qt::MiddleButton) {
            m_view->Pan(dx, -dy);
        } else if (m_pressedButton == Qt::RightButton) {
            m_view->Rotation(event->pos().x(), event->pos().y());
        }

        update();
    }

    m_lastMousePos = event->pos();
}

void CadView::mouseReleaseEvent(QMouseEvent* event) {
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
            update();
        } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (!m_sketchPoints.isEmpty() && m_rubberBandMode == RubberBandMode::Polyline) {
                Q_EMIT getPointKeyPressed("ENTER");
            }
        }
    }

    QWidget::keyPressEvent(event);
}
