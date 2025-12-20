// src/view/RubberBand.cpp

#include "RubberBand.h"
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <Graphic3d_Group.hxx>
#include <QDebug>
#include <cmath>

namespace aicad {
namespace view {

class RubberBand::Private {
public:
    Handle(V3d_View) view;
    Handle(Prs3d_Presentation) presentation;
    
    RubberBandMode mode;
    QVector2D basePoint;
    QVector2D currentPoint;
    QVector<QVector2D> points;
    
    QColor color;
    double lineWidth;
    Qt::PenStyle lineStyle;
    
    Private()
        : mode(RubberBandMode::None)
        , color(Qt::white)
        , lineWidth(2.0)
        , lineStyle(Qt::DashLine)
    {
    }
};

RubberBand::RubberBand(Handle(V3d_View) view, QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    d->view = view;
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
    
    clear();
    d->mode = mode;
    d->points.clear();
    
    qDebug() << "[RubberBand] Mode changed:" << static_cast<int>(mode);
}

RubberBandMode RubberBand::mode() const {
    return d->mode;
}

void RubberBand::setBasePoint(const QVector2D& point) {
    d->basePoint = point;
    d->points.clear();
    d->points.append(point);
    
    qDebug() << "[RubberBand] Base point set:" << point;
}

QVector2D RubberBand::basePoint() const {
    return d->basePoint;
}

void RubberBand::updateCurrentPoint(const QVector2D& point) {
    d->currentPoint = point;
    render();
}

QVector2D RubberBand::currentPoint() const {
    return d->currentPoint;
}

void RubberBand::addPoint(const QVector2D& point) {
    d->points.append(point);
    Q_EMIT pointAdded(point);
    
    qDebug() << "[RubberBand] Point added:" << point << "Total:" << d->points.size();
}

QVector<QVector2D> RubberBand::points() const {
    return d->points;
}

void RubberBand::clear() {
    clearPresentation();
    d->points.clear();
    d->mode = RubberBandMode::None;
    
    if (!d->view.IsNull()) {
        d->view->Redraw();
    }
    
    Q_EMIT cleared();
    
    qDebug() << "[RubberBand] Cleared";
}

void RubberBand::render() {
    if (d->mode == RubberBandMode::None) {
        return;
    }
    
    clearPresentation();
    
    switch (d->mode) {
    case RubberBandMode::Line:
        renderLine();
        break;
    case RubberBandMode::Rectangle:
        renderRectangle();
        break;
    case RubberBandMode::Polyline:
        renderPolyline();
        break;
    case RubberBandMode::Circle:
        renderCircle();
        break;
    case RubberBandMode::Arc:
        renderArc();
        break;
    default:
        break;
    }
    
    if (!d->view.IsNull()) {
        d->view->Redraw();
    }
}

void RubberBand::setColor(const QColor& color) {
    d->color = color;
}

void RubberBand::setLineWidth(double width) {
    d->lineWidth = width;
}

void RubberBand::setLineStyle(Qt::PenStyle style) {
    d->lineStyle = style;
}

void RubberBand::renderLine() {
    if (d->points.isEmpty()) {
        return;
    }
    
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(2);
    polyline->AddVertex(gp_Pnt(d->basePoint.x(), d->basePoint.y(), 0));
    polyline->AddVertex(gp_Pnt(d->currentPoint.x(), d->currentPoint.y(), 0));
    
    d->presentation = new Prs3d_Presentation(
        d->view->Viewer()->StructureManager());
    
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_Color(d->color.redF(), d->color.greenF(), d->color.blueF(), Quantity_TOC_RGB),
        Aspect_TOL_DASH,
        d->lineWidth);
    
    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);
    
    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(10);
    d->presentation->Display();
}

void RubberBand::renderRectangle() {
    if (d->points.isEmpty()) {
        return;
    }
    
    QVector2D p1 = d->basePoint;
    QVector2D p2 = d->currentPoint;
    
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(5);
    polyline->AddVertex(gp_Pnt(p1.x(), p1.y(), 0));
    polyline->AddVertex(gp_Pnt(p2.x(), p1.y(), 0));
    polyline->AddVertex(gp_Pnt(p2.x(), p2.y(), 0));
    polyline->AddVertex(gp_Pnt(p1.x(), p2.y(), 0));
    polyline->AddVertex(gp_Pnt(p1.x(), p1.y(), 0));  // 閉合
    
    d->presentation = new Prs3d_Presentation(
        d->view->Viewer()->StructureManager());
    
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_Color(d->color.redF(), d->color.greenF(), d->color.blueF(), Quantity_TOC_RGB),
        Aspect_TOL_DASH,
        d->lineWidth);
    
    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);
    
    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(10);
    d->presentation->Display();
}

void RubberBand::renderPolyline() {
    if (d->points.size() < 1) {
        return;
    }
    
    int numPoints = d->points.size() + 1;  // 包含當前點
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(numPoints);
    
    for (const QVector2D& pt : d->points) {
        polyline->AddVertex(gp_Pnt(pt.x(), pt.y(), 0));
    }
    polyline->AddVertex(gp_Pnt(d->currentPoint.x(), d->currentPoint.y(), 0));
    
    d->presentation = new Prs3d_Presentation(
        d->view->Viewer()->StructureManager());
    
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_Color(d->color.redF(), d->color.greenF(), d->color.blueF(), Quantity_TOC_RGB),
        Aspect_TOL_DASH,
        d->lineWidth);
    
    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);
    
    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(10);
    d->presentation->Display();
}

void RubberBand::renderCircle() {
    if (d->points.isEmpty()) {
        return;
    }
    
    // 計算半徑
    QVector2D center = d->basePoint;
    double radius = (d->currentPoint - center).length();
    
    if (radius < 0.001) {
        return;
    }
    
    // 生成圓的點
    const int segments = 64;
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(segments + 1);
    
    for (int i = 0; i <= segments; ++i) {
        double angle = 2.0 * M_PI * i / segments;
        double x = center.x() + radius * std::cos(angle);
        double y = center.y() + radius * std::sin(angle);
        polyline->AddVertex(gp_Pnt(x, y, 0));
    }
    
    d->presentation = new Prs3d_Presentation(
        d->view->Viewer()->StructureManager());
    
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_Color(d->color.redF(), d->color.greenF(), d->color.blueF(), Quantity_TOC_RGB),
        Aspect_TOL_DASH,
        d->lineWidth);
    
    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);
    
    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(10);
    d->presentation->Display();
}

void RubberBand::renderArc() {
    // TODO: 實作圓弧渲染
    qDebug() << "[RubberBand] Arc rendering not yet implemented";
}

void RubberBand::clearPresentation() {
    if (!d->presentation.IsNull()) {
        d->presentation->Clear();
        d->presentation->Erase();
        d->presentation.Nullify();
    }
}

} // namespace view
} // namespace aicad
