/**
 * @file RubberBand.cpp
 * @brief RubberBand 實作
 * @author Felicia
 * @date 2024-12-04
 */

#include "RubberBand.h"
#include "../cad/geometry/CustomPlane.h"

#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Graphic3d_Group.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <Quantity_Color.hxx>
#include <QDebug>
#include <QtMath>
#include <QColor>

namespace aicad {
namespace view {

class RubberBand::Private {
public:
    Handle(V3d_View) view;
    Handle(AIS_InteractiveContext) context;
    Handle(Prs3d_Presentation) presentation;
    
    cad::CustomPlane plane;
    RubberBandMode mode = RubberBandMode::None;
    
    QVector<QVector2D> points;
    QVector2D currentPoint;
    bool hasCurrentPoint = false;
    
    QColor color = QColor(255, 255, 255);  // 白色
    double lineWidth = 2.0;
    int lineStyle = 1;  // 虛線
    bool visible = true;
};

RubberBand::RubberBand(Handle(V3d_View) view,
                       Handle(AIS_InteractiveContext) context,
                       const cad::CustomPlane& plane,
                       QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    d->view = view;
    d->context = context;
    d->plane = plane;
    
    qDebug() << "[RubberBand] Created";
}

RubberBand::~RubberBand() {
    qDebug() << "[RubberBand] Destroying...";
    clear();
    delete d;
}

void RubberBand::setMode(RubberBandMode mode) {
    if (d->mode == mode) {
        return;
    }
    
    qDebug() << "[RubberBand] Mode changed to:" << static_cast<int>(mode);
    d->mode = mode;
    d->points.clear();
    d->hasCurrentPoint = false;
    clearPresentation();
}

RubberBandMode RubberBand::mode() const {
    return d->mode;
}

void RubberBand::setPlane(const cad::CustomPlane& plane) {
    d->plane = plane;
    qDebug() << "[RubberBand] Plane updated";
}

void RubberBand::setBasePoint(const QVector2D& point) {
    d->points.clear();
    d->points.append(point);
    qDebug() << "[RubberBand] Base point set:" << point;
}

QVector2D RubberBand::basePoint() const {
    return d->points.isEmpty() ? QVector2D(0, 0) : d->points.first();
}

void RubberBand::updateCurrentPoint(const QVector2D& point) {
    d->currentPoint = point;
    d->hasCurrentPoint = true;
    render();
}

void RubberBand::addPoint(const QVector2D& point) {
    d->points.append(point);
    qDebug() << "[RubberBand] Point added. Total points:" << d->points.size();
    Q_EMIT updated();
}

QVector<QVector2D> RubberBand::points() const {
    return d->points;
}

void RubberBand::clear() {
    d->points.clear();
    d->hasCurrentPoint = false;
    d->mode = RubberBandMode::None;
    clearPresentation();
    qDebug() << "[RubberBand] Cleared";
}

void RubberBand::clearPresentation() {
    if (!d->presentation.IsNull()) {
        d->presentation->Clear();
        d->presentation->Erase();
        d->presentation.Nullify();
    }
}

gp_Pnt RubberBand::planeToWorld(const QVector2D& pt) const {
    QVector3D worldPt = d->plane.origin + 
                        d->plane.uAxis * pt.x() + 
                        d->plane.vAxis * pt.y();
    return gp_Pnt(worldPt.x(), worldPt.y(), worldPt.z());
}

void RubberBand::render() {
    if (!d->visible || d->mode == RubberBandMode::None) {
        return;
    }
    
    if (!d->hasCurrentPoint) {
        return;
    }
    
    // 清除舊的顯示
    clearPresentation();
    
    // 根據模式渲染
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
        default:
            break;
    }
    
    // 更新視圖
    if (!d->view.IsNull()) {
        d->view->Redraw();
    }
    
    Q_EMIT updated();
}

void RubberBand::renderLine() {
    if (d->points.isEmpty()) {
        return;
    }
    
    // 建立線段
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(2);
    polyline->AddVertex(planeToWorld(d->points[0]));
    polyline->AddVertex(planeToWorld(d->currentPoint));
    
    // 建立 Presentation
    if (d->context.IsNull()) {
        return;
    }
    
    d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());
    
    // 設定線條樣式
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_Color(d->color.redF(), d->color.greenF(), d->color.blueF(), Quantity_TOC_RGB),
        Aspect_TOL_DASH,
        d->lineWidth
    );
    
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
    
    QVector2D p1 = d->points[0];
    QVector2D p2 = d->currentPoint;
    
    // 建立矩形四個角點
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(5);
    polyline->AddVertex(planeToWorld(QVector2D(p1.x(), p1.y())));
    polyline->AddVertex(planeToWorld(QVector2D(p2.x(), p1.y())));
    polyline->AddVertex(planeToWorld(QVector2D(p2.x(), p2.y())));
    polyline->AddVertex(planeToWorld(QVector2D(p1.x(), p2.y())));
    polyline->AddVertex(planeToWorld(QVector2D(p1.x(), p1.y())));  // 閉合
    
    if (d->context.IsNull()) {
        return;
    }
    
    d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());
    
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_Color(d->color.redF(), d->color.greenF(), d->color.blueF(), Quantity_TOC_RGB),
        Aspect_TOL_DASH,
        d->lineWidth
    );
    
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
    
    // 建立折線 (所有已點擊的點 + 當前滑鼠位置)
    int numPoints = d->points.size() + 1;
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(numPoints);
    
    for (const QVector2D& pt : d->points) {
        polyline->AddVertex(planeToWorld(pt));
    }
    polyline->AddVertex(planeToWorld(d->currentPoint));
    
    if (d->context.IsNull()) {
        return;
    }
    
    d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());
    
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_Color(d->color.redF(), d->color.greenF(), d->color.blueF(), Quantity_TOC_RGB),
        Aspect_TOL_DASH,
        d->lineWidth
    );
    
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
    
    QVector2D center = d->points[0];
    QVector2D edge = d->currentPoint;
    
    // 計算半徑
    float radius = (edge - center).length();
    
    if (radius < 0.001f) {
        return;
    }
    
    // 建立圓 (用多段線近似)
    const int segments = 64;
    Handle(Graphic3d_ArrayOfPolylines) polyline = new Graphic3d_ArrayOfPolylines(segments + 1);
    
    for (int i = 0; i <= segments; ++i) {
        float angle = (2.0f * M_PI * i) / segments;
        float x = center.x() + radius * qCos(angle);
        float y = center.y() + radius * qSin(angle);
        polyline->AddVertex(planeToWorld(QVector2D(x, y)));
    }
    
    if (d->context.IsNull()) {
        return;
    }
    
    d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());
    
    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
        Quantity_Color(d->color.redF(), d->color.greenF(), d->color.blueF(), Quantity_TOC_RGB),
        Aspect_TOL_DASH,
        d->lineWidth
    );
    
    Handle(Graphic3d_Group) group = d->presentation->NewGroup();
    group->SetGroupPrimitivesAspect(aspect->Aspect());
    group->AddPrimitiveArray(polyline);
    
    d->presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    d->presentation->SetDisplayPriority(10);
    d->presentation->Display();
}

void RubberBand::setColor(const QColor& color) {
    d->color = color;
}

void RubberBand::setLineWidth(double width) {
    d->lineWidth = width;
}

void RubberBand::setLineStyle(int style) {
    d->lineStyle = style;
}

bool RubberBand::isVisible() const {
    return d->visible;
}

void RubberBand::setVisible(bool visible) {
    d->visible = visible;
    if (!visible) {
        clearPresentation();
    }
}

} // namespace view
} // namespace aicad
