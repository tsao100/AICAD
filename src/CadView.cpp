#include "CadView.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GC_MakeSegment.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <IntAna_IntConicQuad.hxx>

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

namespace {
    // Helper to get proper OCCT coordinates from Qt event
    void QtToOCCT(const QWidget* widget, const QPoint& qtPos,
                  Standard_Integer& occX, Standard_Integer& occY) {
#ifdef _WIN32
        // On Windows, account for device pixel ratio (High DPI)
        qreal dpr = widget->devicePixelRatio();
        occX = static_cast<Standard_Integer>(qtPos.x() * dpr);
        occY = static_cast<Standard_Integer>(qtPos.y() * dpr);
#else
        // On Linux/X11, Qt coordinates match OCCT coordinates directly
        occX = qtPos.x();
        occY = qtPos.y();
#endif
    }
}

CadView::CadView(QWidget* parent)
    : QWidget(parent)
    , m_rubberBandObject(nullptr)
    , m_document(nullptr)
    , m_currentView(SketchView::Isometric)
    , m_mode(CadMode::Idle)
    , m_rubberBandMode(RubberBandMode::None)
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

// #ifdef _WIN32
//     // IMPORTANT: Set proper window size accounting for device pixel ratio
//     qreal dpr = devicePixelRatio();
//     Standard_Integer w = static_cast<Standard_Integer>(width() * dpr);
//     Standard_Integer h = static_cast<Standard_Integer>(height() * dpr);
//     window->SetVirtualSize(w, h);
// #endif

    m_view->SetBackgroundColor(Quantity_NOC_GRAY80);
    m_view->MustBeResized();
    //m_view->TriedronDisplay(Aspect_TOTP_RIGHT_UPPER, Quantity_NOC_GOLD, 0.08);

    m_context = new AIS_InteractiveContext(m_viewer);
    m_context->SetDisplayMode(AIS_Shaded, Standard_True);

    m_viewCube = new AIS_ViewCube();
    m_viewCube->SetBoxColor(Quantity_NOC_GRAY75);
    m_viewCube->SetSize(55);
    m_viewCube->SetFontHeight(12);
    m_viewCube->SetAxesLabels("X", "Y", "Z");
    m_viewCube->SetTransformPersistence(
        new Graphic3d_TransformPers(Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER, Graphic3d_Vec2i(85, 85)));
    m_context->Display(m_viewCube, Standard_False);

    setSketchView(SketchView::Isometric);

    // Force initial update - THIS IS THE QTimer::singleShot PART
    QTimer::singleShot(0, this, [this]() {
        if (!m_view.IsNull()) {
            m_view->MustBeResized();
            m_view->Redraw();
        }
    });
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

void CadView::updateRubberBand() {
    if (m_context.IsNull()) return;

    // Clear previous rubber band
    clearRubberBand();

    if (m_mode != CadMode::Sketching) return;
    if (m_sketchPoints.isEmpty() || !m_hasCurrentPoint) return;

    // Get the sketch plane
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

    // Helper to convert plane to 3D
    auto planeToWorld = [&plane](const QVector2D& planePt) -> gp_Pnt {
        QVector3D worldPt = plane.origin + plane.uAxis * planePt.x() + plane.vAxis * planePt.y();
        return gp_Pnt(worldPt.x(), worldPt.y(), worldPt.z());
    };

    if (m_rubberBandMode == RubberBandMode::Line) {
        if (m_sketchPoints.isEmpty()) return;

        // Create line from base point to current point
        Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(2);
        polyline->AddVertex(planeToWorld(m_sketchPoints[0]));
        polyline->AddVertex(planeToWorld(m_currentPoint));

        // Create presentation
        Handle(Prs3d_Presentation) prs = new Prs3d_Presentation(m_context->MainPrsMgr()->StructureManager());
        Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
            Quantity_NOC_WHITE,
            Aspect_TOL_DASH,
            2.0
            );

        Handle(Graphic3d_Group) group = prs->NewGroup();
        group->SetGroupPrimitivesAspect(aspect->Aspect());
        group->AddPrimitiveArray(polyline);

        prs->SetZLayer(Graphic3d_ZLayerId_Top);
        prs->SetDisplayPriority(10);
        prs->Display();

        m_rubberBandObject = prs;
    }
    else if (m_rubberBandMode == RubberBandMode::Rectangle) {
        // Create rectangle points
        QVector2D p1 = m_sketchPoints[0];
        QVector2D p2 = m_currentPoint;

        gp_Pnt gp1 = planeToWorld(QVector2D(p1.x(), p1.y()));
        gp_Pnt gp2 = planeToWorld(QVector2D(p2.x(), p1.y()));
        gp_Pnt gp3 = planeToWorld(QVector2D(p2.x(), p2.y()));
        gp_Pnt gp4 = planeToWorld(QVector2D(p1.x(), p2.y()));

        // Create polyline array (5 points to close rectangle)
        Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(5);
        polyline->AddVertex(gp1);
        polyline->AddVertex(gp2);
        polyline->AddVertex(gp3);
        polyline->AddVertex(gp4);
        polyline->AddVertex(gp1); // Close the loop

        // Create presentation
        Handle(Prs3d_Presentation) prs = new Prs3d_Presentation(m_context->MainPrsMgr()->StructureManager());

        // Set line attributes (white dashed line)
        Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
            Quantity_NOC_WHITE,
            Aspect_TOL_DASH,
            2.0
            );

        // Create group and add polyline
        Handle(Graphic3d_Group) group = prs->NewGroup();
        group->SetGroupPrimitivesAspect(aspect->Aspect());
        group->AddPrimitiveArray(polyline);

        // Display presentation
        prs->SetZLayer(Graphic3d_ZLayerId_Top);
        prs->SetDisplayPriority(10);
        prs->Display();

        m_rubberBandObject = prs;

    } else if (m_rubberBandMode == RubberBandMode::Polyline) {
        if (m_sketchPoints.size() < 1) return;

        // Create polyline with all clicked points plus current point
        int numPoints = m_sketchPoints.size() + 1;
        Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(numPoints);

        for (const QVector2D& pt : m_sketchPoints) {
            polyline->AddVertex(planeToWorld(pt));
        }
        polyline->AddVertex(planeToWorld(m_currentPoint));

        // Create presentation
        Handle(Prs3d_Presentation) prs = new Prs3d_Presentation(m_context->MainPrsMgr()->StructureManager());

        Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
            Quantity_NOC_WHITE,
            Aspect_TOL_DASH,
            2.0
            );

        Handle(Graphic3d_Group) group = prs->NewGroup();
        group->SetGroupPrimitivesAspect(aspect->Aspect());
        group->AddPrimitiveArray(polyline);

        prs->SetZLayer(Graphic3d_ZLayerId_Top);
        prs->SetDisplayPriority(10);
        prs->Display();

        m_rubberBandObject = prs;
    }

    m_view->Redraw();
}

void CadView::clearRubberBand() {
    if (!m_rubberBandObject.IsNull()) {
        m_rubberBandObject->Clear();
        m_rubberBandObject->Erase();
        m_rubberBandObject.Nullify();
    }
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
    clearRubberBand();
}

void CadView::setPendingSketch(TDF_Label sketch) {
    m_pendingSketch = sketch;
}

// Updated screenToPlane function:

QVector2D CadView::screenToPlane(const QPoint& screenPos) {
    if (m_view.IsNull()) return QVector2D(0, 0);

    // Convert Qt coordinates to OCCT coordinates
    Standard_Integer xp, yp;
    QtToOCCT(this, screenPos, xp, yp);

    // Get the sketch plane
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

    // Get projection direction and eye point
    Standard_Real Xeye, Yeye, Zeye;
    Standard_Real Xproj, Yproj, Zproj;

    m_view->Eye(Xeye, Yeye, Zeye);
    m_view->Proj(Xproj, Yproj, Zproj);

    gp_Pnt eyePoint(Xeye, Yeye, Zeye);
    gp_Dir projDir(Xproj, Yproj, Zproj);

    // Convert screen point to 3D world coordinates
    Standard_Real Xv, Yv, Zv;
    m_view->Convert(xp, yp, Xv, Yv, Zv);
    gp_Pnt screenPoint3D(Xv, Yv, Zv);

    // Create picking ray
    gp_Pnt rayStart;
    gp_Dir rayDir;

    if (m_view->Camera()->IsOrthographic()) {
        // For orthographic projection, ray starts at the converted screen point
        // and direction is the projection direction
        rayStart = screenPoint3D;
        rayDir = projDir;
    } else {
        // For perspective projection, ray goes from eye through screen point
        rayStart = eyePoint;
        gp_Vec direction(eyePoint, screenPoint3D);
        if (direction.Magnitude() < Precision::Confusion()) {
            rayDir = projDir;
        } else {
            rayDir = gp_Dir(direction);
        }
    }

    gp_Lin pickLine(rayStart, rayDir);

    // Find intersection with plane
    IntAna_IntConicQuad intersection(pickLine, gpPlane, Precision::Angular());

    if (intersection.IsDone() && intersection.NbPoints() > 0) {
        gp_Pnt intersectPnt = intersection.Point(1);

        // Convert 3D world point to 2D plane coordinates
        QVector3D worldPt(intersectPnt.X(), intersectPnt.Y(), intersectPnt.Z());
        QVector3D localPt = worldPt - plane.origin;

        float u = QVector3D::dotProduct(localPt, plane.uAxis);
        float v = QVector3D::dotProduct(localPt, plane.vAxis);

        return QVector2D(u, v);
    }

    return QVector2D(0, 0);
}

void CadView::paintEvent(QPaintEvent* event) {
    if (!m_view.IsNull()) {
        m_view->InvalidateImmediate();
        m_view->Redraw();
    }
}

void CadView::resizeEvent(QResizeEvent* event) {
    Q_UNUSED(event);  // Suppress unused parameter warning

    if (!m_viewInitialized) {
        m_viewInitialized = true;
    }

     if (!m_view.IsNull()) {
// #ifdef _WIN32
//         // Update window size with device pixel ratio
//         Handle(Aspect_Window) window = m_view->Window();
//         if (!window.IsNull()) {
//             qreal dpr = devicePixelRatio();
//             Standard_Integer w = static_cast<Standard_Integer>(width() * dpr);
//             Standard_Integer h = static_cast<Standard_Integer>(height() * dpr);
//             window->SetVirtualSize(w, h);
//         }
// #endif

        m_view->MustBeResized();
        m_view->Redraw();
    }
}

void CadView::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->pos();
    m_mousePressed = true;
    m_pressedButton = event->button();

    // Convert to OCCT coordinates
    Standard_Integer xp, yp;
    QtToOCCT(this, event->pos(), xp, yp);

    // Update OCCT selection
    if (!m_context.IsNull() && !m_view.IsNull())
    {
        m_context->MoveTo(xp, yp, m_view, Standard_True);

        if (event->button() == Qt::LeftButton)
        {
            m_context->Select(Standard_True);

            if (m_context->HasDetected())
            {
                Handle(AIS_InteractiveObject) picked = m_context->DetectedInteractive();
                if (!picked.IsNull() && picked == m_viewCube)
                {
                    return;
                }
            }
        }
    }

    if (m_mode == CadMode::Sketching && event->button() == Qt::LeftButton) {
        QVector2D planePt = screenToPlane(event->pos());

        if (m_rubberBandMode == RubberBandMode::Rectangle) {
            if (m_sketchPoints.isEmpty()) {
                m_sketchPoints.append(planePt);
                m_hasCurrentPoint = true;
            } else {
                Q_EMIT pointAcquired(planePt);
                m_sketchPoints.clear();
                m_hasCurrentPoint = false;
                clearRubberBand();
            }
        } else if (m_rubberBandMode == RubberBandMode::Polyline) {
            m_sketchPoints.append(planePt);
            Q_EMIT pointAcquired(planePt);
        }
    }
    if ( m_mode == CadMode::GetPoint && event->button() == Qt::LeftButton) {
        QVector2D planePt = screenToPlane(event->pos());
        // For getpoint with rubber band line - emit immediately
        Q_EMIT pointAcquired(planePt);  // <-- ADDED FOR GETPOINT
        clearRubberBand();
    }


    // Store position for rotation
    if (event->button() == Qt::RightButton && !m_view.IsNull()) {
        m_view->StartRotation(xp, yp);
    }
}

void CadView::mouseMoveEvent(QMouseEvent* event) {
    // Convert to OCCT coordinates
    Standard_Integer xp, yp;
    QtToOCCT(this, event->pos(), xp, yp);

    // Update OCCT hover detection
    if (!m_context.IsNull() && !m_view.IsNull())
    {
        m_context->MoveTo(xp, yp, m_view, Standard_True);

        if (m_context->HasDetected())
        {
            Handle(AIS_InteractiveObject) detected = m_context->DetectedInteractive();
            if (!detected.IsNull() && detected == m_viewCube)
            {
                setCursor(Qt::PointingHandCursor);
            }
            else
            {
                unsetCursor();
            }
        }
        else
        {
            unsetCursor();
        }
    }

    // Sketching mode logic
    if (m_mode == CadMode::Sketching) {
        m_currentPoint = screenToPlane(event->pos());
        m_hasCurrentPoint = true;
        updateRubberBand();
        return;
    }

    // View manipulation
    if (m_mousePressed && !m_view.IsNull()) {
        int dx = event->pos().x() - m_lastMousePos.x();
        int dy = event->pos().y() - m_lastMousePos.y();

        if (m_pressedButton == Qt::MiddleButton) {
            m_view->Pan(dx, -dy);
        } else if (m_pressedButton == Qt::RightButton) {
            // For rotation, just call Rotation with current position
            m_view->Rotation(xp, yp);
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
            clearRubberBand();
            update();
        } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (!m_sketchPoints.isEmpty() && m_rubberBandMode == RubberBandMode::Polyline) {
                Q_EMIT getPointKeyPressed("ENTER");
                clearRubberBand();
            }
        }
    }

    QWidget::keyPressEvent(event);
}
