/**
 * @file RubberBand.cpp
 * @brief RubberBand 類別實作
 * @author Felicia
 * @date 2024-12-04
 */

#include "RubberBand.h"

#include <QDebug>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Graphic3d_Group.hxx>
#include <Quantity_Color.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <gp_Pnt.hxx>
#include <gp_Circ.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>

namespace aicad {
namespace view {

// CustomPlane 靜態方法實作
CustomPlane CustomPlane::XY() {
    CustomPlane p;
    p.origin = QVector3D(0, 0, 0);
    p.normal = QVector3D(0, 0, 1);
    p.uAxis = QVector3D(1, 0, 0);
    p.vAxis = QVector3D(0, 1, 0);
    return p;
}

CustomPlane CustomPlane::XZ() {
    CustomPlane p;
    p.origin = QVector3D(0, 0, 0);
    p.normal = QVector3D(0, 1, 0);
    p.uAxis = QVector3D(1, 0, 0);
    p.vAxis = QVector3D(0, 0, 1);
    return p;
}

CustomPlane CustomPlane::YZ() {
    CustomPlane p;
    p.origin = QVector3D(0, 0, 0);
    p.normal = QVector3D(1, 0, 0);
    p.uAxis = QVector3D(0, 1, 0);
    p.vAxis = QVector3D(0, 0, 1);
    return p;
}

class RubberBand::Private {
public:
    Handle(AIS_InteractiveContext) context;
    Handle(Prs3d_Presentation) presentation;
    
    RubberBandMode mode;
    CustomPlane plane;
    QVector<QVector2D> points;
    QVector2D currentPoint;
    bool hasCurrentPoint;
};

RubberBand::RubberBand(const Handle(AIS_InteractiveContext)& context, QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    d->context = context;
    d->mode = RubberBandMode::None;
    d->plane = CustomPlane::XY();
    d->hasCurrentPoint = false;
    
    qDebug() << "[RubberBand] Created";
}

RubberBand::~RubberBand() {
    clear();
    delete d;
    qDebug() << "[RubberBand] Destroyed";
}

void RubberBand::setMode(RubberBandMode mode) {
    if (d->mode == mode) {
        return;
    }
    
    d->mode = mode;
    d->points.clear();
    d->hasCurrentPoint = false;
    clear();
    
    qDebug() << "[RubberBand] Mode changed to:" << static_cast<int>(mode);
}

RubberBandMode RubberBand::mode() const {
    return d->mode;
}

void RubberBand::setPlane(const CustomPlane& plane) {
    d->plane = plane;
    qDebug() << "[RubberBand] Plane set";
}

CustomPlane RubberBand::plane() const {
    return d->plane;
}

void RubberBand::addPoint(const QVector2D& point) {
    d->points.append(point);
    qDebug() << "[RubberBand] Point added:" << point.x() << "," << point.y()
             << "Total points:" << d->points.size();
}

void RubberBand::setCurrentPoint(const QVector2D& point) {
    d->currentPoint = point;
    d->hasCurrentPoint = true;
}

QVector<QVector2D> RubberBand::points() const {
    return d->points;
}

void RubberBand::clearPoints() {
    d->points.clear();
    d->hasCurrentPoint = false;
    clear();
}

void RubberBand::update() {
    if (d->context.IsNull()) {
        return;
    }
    
    // 清除舊的顯示
    clear();
    
    // 根據模式更新
    switch (d->mode) {
    case RubberBandMode::Line:
        updateLine();
        break;
    case RubberBandMode::Rectangle:
        updateRectangle();
        break;
    case RubberBandMode::Polyline:
        updatePolyline();
        break;
    case RubberBandMode::Circle:
        updateCircle();
        break;
    case RubberBandMode::Arc:
        updateArc();
        break;
    case RubberBandMode::None:
    default:
        break;
    }
    
    Q_EMIT updated();
}

void RubberBand::clear() {
    if (!d->presentation.IsNull()) {
        d->presentation->Clear();
        d->presentation->Erase();
        d->presentation.Nullify();
    }
    
    Q_EMIT cleared();
}

bool RubberBand::hasCurrentPoint() const {
    return d->hasCurrentPoint;
}

void RubberBand::updateLine() {
    if (d->points.isEmpty() || !d->hasCurrentPoint) {
        return;
    }
    
    // 創建線段
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(2);
    
    QVector3D p1 = planeToWorld(d->points[0]);
    QVector3D p2 = planeToWorld(d->currentPoint);
    
    polyline->AddVertex(gp_Pnt(p1.x(), p1.y(), p1.z()));
    polyline->AddVertex(gp_Pnt(p2.x(), p2.y(), p2.z()));
    
    // 創建呈現
    d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());
    
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_NOC_WHITE,
        Aspect_TOL_DASH,
        2.0
    );
    
    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);
    
    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(10);
    d->presentation->Display();
}

void RubberBand::updateRectangle() {
    if (d->points.isEmpty() || !d->hasCurrentPoint) {
        return;
    }
    
    // 創建矩形
    QVector2D p1 = d->points[0];
    QVector2D p2 = d->currentPoint;
    
    QVector3D gp1 = planeToWorld(QVector2D(p1.x(), p1.y()));
    QVector3D gp2 = planeToWorld(QVector2D(p2.x(), p1.y()));
    QVector3D gp3 = planeToWorld(QVector2D(p2.x(), p2.y()));
    QVector3D gp4 = planeToWorld(QVector2D(p1.x(), p2.y()));
    
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(5);
    polyline->AddVertex(gp_Pnt(gp1.x(), gp1.y(), gp1.z()));
    polyline->AddVertex(gp_Pnt(gp2.x(), gp2.y(), gp2.z()));
    polyline->AddVertex(gp_Pnt(gp3.x(), gp3.y(), gp3.z()));
    polyline->AddVertex(gp_Pnt(gp4.x(), gp4.y(), gp4.z()));
    polyline->AddVertex(gp_Pnt(gp1.x(), gp1.y(), gp1.z()));
    
    // 創建呈現
    d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());
    
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_NOC_WHITE,
        Aspect_TOL_DASH,
        2.0
    );
    
    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);
    
    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(10);
    d->presentation->Display();
}

void RubberBand::updatePolyline() {
    if (d->points.size() < 1 || !d->hasCurrentPoint) {
        return;
    }
    
    // 創建多段線
    int numPoints = d->points.size() + 1;
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(numPoints);
    
    for (const QVector2D& pt : d->points) {
        QVector3D p = planeToWorld(pt);
        polyline->AddVertex(gp_Pnt(p.x(), p.y(), p.z()));
    }
    
    QVector3D currentP = planeToWorld(d->currentPoint);
    polyline->AddVertex(gp_Pnt(currentP.x(), currentP.y(), currentP.z()));
    
    // 創建呈現
    d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());
    
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_NOC_WHITE,
        Aspect_TOL_DASH,
        2.0
    );
    
    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);
    
    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(10);
    d->presentation->Display();
}

void RubberBand::updateCircle() {
    if (d->points.isEmpty() || !d->hasCurrentPoint) {
        return;
    }
    
    // 計算半徑
    QVector2D center = d->points[0];
    float radius = (d->currentPoint - center).length();
    
    if (radius < 0.001) {
        return;
    }
    
    // 創建圓
    QVector3D centerWorld = planeToWorld(center);
    gp_Pnt centerPnt(centerWorld.x(), centerWorld.y(), centerWorld.z());
    gp_Dir normalDir(d->plane.normal.x(), d->plane.normal.y(), d->plane.normal.z());
    gp_Circ circle(gp_Ax2(centerPnt, normalDir), radius);
    
    // 創建圓的點陣列
    const int numSegments = 64;
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(numSegments + 1);
    
    for (int i = 0; i <= numSegments; ++i) {
        double angle = 2.0 * M_PI * i / numSegments;
        gp_Pnt pt = circle.Value(angle);
        polyline->AddVertex(pt);
    }
    
    // 創建呈現
    d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());
    
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_NOC_WHITE,
        Aspect_TOL_DASH,
        2.0
    );
    
    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);
    
    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(10);
    d->presentation->Display();
}

void RubberBand::updateArc() {
    if (d->points.size() < 2 || !d->hasCurrentPoint) {
        return;
    }
    
    // 創建三點弧
    QVector3D p1 = planeToWorld(d->points[0]);
    QVector3D p2 = planeToWorld(d->points[1]);
    QVector3D p3 = planeToWorld(d->currentPoint);
    
    gp_Pnt gp1(p1.x(), p1.y(), p1.z());
    gp_Pnt gp2(p2.x(), p2.y(), p2.z());
    gp_Pnt gp3(p3.x(), p3.y(), p3.z());
    
    try {
        GC_MakeArcOfCircle arcMaker(gp1, gp2, gp3);
        if (arcMaker.IsDone()) {
            Handle(Geom_TrimmedCurve) arc = arcMaker.Value();
            
            // 創建弧的點陣列
            const int numSegments = 32;
            Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(numSegments + 1);
            
            double u1 = arc->FirstParameter();
            double u2 = arc->LastParameter();
            
            for (int i = 0; i <= numSegments; ++i) {
                double u = u1 + (u2 - u1) * i / numSegments;
                gp_Pnt pt = arc->Value(u);
                polyline->AddVertex(pt);
            }
            
            // 創建呈現
            d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());
            
            Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
                Quantity_NOC_WHITE,
                Aspect_TOL_DASH,
                2.0
            );
            
            Handle(Graphic3d_Group) group = d->presentation->NewGroup();
            group->SetGroupPrimitivesAspect(aspect->Aspect());
            group->AddPrimitiveArray(polyline);
            
            d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
            d->presentation->SetDisplayPriority(10);
            d->presentation->Display();
        }
    } catch (...) {
        qWarning() << "[RubberBand] Failed to create arc";
    }
}

QVector3D RubberBand::planeToWorld(const QVector2D& planePt) const {
    return d->plane.origin + d->plane.uAxis * planePt.x() + d->plane.vAxis * planePt.y();
}

} // namespace view
} // namespace aicad