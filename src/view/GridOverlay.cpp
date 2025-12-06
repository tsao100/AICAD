// src/view/GridOverlay.cpp

#include "GridOverlay.h"
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Graphic3d_Group.hxx>
#include <QDebug>

namespace aicad {
namespace view {

class GridOverlay::Private {
public:
    Handle(V3d_View) view;
    Handle(Prs3d_Presentation) presentation;
    
    QVector3D origin;
    QVector3D normal;
    QVector3D uAxis;
    QVector3D vAxis;
    
    int gridCount;
    double gridSpacing;
    
    QColor gridColor;
    QColor xAxisColor;
    QColor yAxisColor;
    
    bool visible;
    
    Private()
        : origin(0, 0, 0)
        , normal(0, 0, 1)
        , uAxis(1, 0, 0)
        , vAxis(0, 1, 0)
        , gridCount(20)
        , gridSpacing(10.0)
        , gridColor(Qt::gray)
        , xAxisColor(Qt::red)
        , yAxisColor(Qt::green)
        , visible(false)
    {
    }
};

GridOverlay::GridOverlay(Handle(V3d_View) view, QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    d->view = view;
    qDebug() << "[GridOverlay] Created";
}

GridOverlay::~GridOverlay() {
    clearPresentation();
    delete d;
    qDebug() << "[GridOverlay] Destroyed";
}

void GridOverlay::setOrigin(const QVector3D& origin) {
    d->origin = origin;
    if (d->visible) {
        render();
    }
}

void GridOverlay::setSize(int gridCount, double gridSpacing) {
    d->gridCount = gridCount;
    d->gridSpacing = gridSpacing;
    if (d->visible) {
        render();
    }
    
    qDebug() << "[GridOverlay] Size set:" << gridCount << "x" << gridSpacing;
}

void GridOverlay::setPlane(const QVector3D& normal,
                           const QVector3D& uAxis,
                           const QVector3D& vAxis)
{
    d->normal = normal;
    d->uAxis = uAxis;
    d->vAxis = vAxis;
    if (d->visible) {
        render();
    }
    
    qDebug() << "[GridOverlay] Plane set";
}

void GridOverlay::show() {
    d->visible = true;
    render();
    
    qDebug() << "[GridOverlay] Shown";
}

void GridOverlay::hide() {
    d->visible = false;
    clearPresentation();
    
    if (!d->view.IsNull()) {
        d->view->Redraw();
    }
    
    qDebug() << "[GridOverlay] Hidden";
}

bool GridOverlay::isVisible() const {
    return d->visible;
}

void GridOverlay::setColor(const QColor& color) {
    d->gridColor = color;
    if (d->visible) {
        render();
    }
}

void GridOverlay::setAxisColors(const QColor& xColor, const QColor& yColor) {
    d->xAxisColor = xColor;
    d->yAxisColor = yColor;
    if (d->visible) {
        render();
    }
}

void GridOverlay::render() {
    if (!d->visible || d->view.IsNull()) {
        return;
    }
    
    clearPresentation();
    
    double halfSize = d->gridCount * d->gridSpacing / 2.0;
    int centerIndex = d->gridCount / 2;
    
    // 建立 presentation
    d->presentation = new Prs3d_Presentation(
        d->view->Viewer()->StructureManager());
    
    // 計算所需的線段數
    int linesPerDirection = d->gridCount + 1;
    int totalLines = linesPerDirection * 2 - 2;  // 扣除中心軸線
    
    // 建立網格線 array
    Handle(Graphic3d_ArrayOfPolylines) gridLines = 
        new Graphic3d_ArrayOfPolylines(totalLines * 2, totalLines);
    
    // U 方向的網格線 (平行於 V 軸)
    for (int i = 0; i <= d->gridCount; ++i) {
        if (i == centerIndex) continue;  // 跳過中心線 (稍後繪製為軸線)
        
        double u = -halfSize + i * d->gridSpacing;
        QVector3D p1 = d->origin + d->uAxis * u + d->vAxis * (-halfSize);
        QVector3D p2 = d->origin + d->uAxis * u + d->vAxis * halfSize;
        
        gridLines->AddBound(2);
        gridLines->AddVertex(gp_Pnt(p1.x(), p1.y(), p1.z()));
        gridLines->AddVertex(gp_Pnt(p2.x(), p2.y(), p2.z()));
    }
    
    // V 方向的網格線 (平行於 U 軸)
    for (int i = 0; i <= d->gridCount; ++i) {
        if (i == centerIndex) continue;  // 跳過中心線
        
        double v = -halfSize + i * d->gridSpacing;
        QVector3D p1 = d->origin + d->uAxis * (-halfSize) + d->vAxis * v;
        QVector3D p2 = d->origin + d->uAxis * halfSize + d->vAxis * v;
        
        gridLines->AddBound(2);
        gridLines->AddVertex(gp_Pnt(p1.x(), p1.y(), p1.z()));
        gridLines->AddVertex(gp_Pnt(p2.x(), p2.y(), p2.z()));
    }
    
    // 添加網格線到 presentation
    Handle(Prs3d_LineAspect) gridAspect = new Prs3d_LineAspect(
        Quantity_Color(d->gridColor.redF(), d->gridColor.greenF(), 
                      d->gridColor.blueF(), Quantity_TOC_RGB),
        Aspect_TOL_SOLID,
        1.0);
    
    Handle(Graphic3d_Group) gridGroup = d->presentation->NewGroup();
    gridGroup->SetGroupPrimitivesAspect(gridAspect->Aspect());
    gridGroup->AddPrimitiveArray(gridLines);
    
    // X 軸 (U 軸方向，紅色)
    Handle(Graphic3d_ArrayOfPolylines) xAxis = new Graphic3d_ArrayOfPolylines(2, 1);
    QVector3D xStart = d->origin + d->uAxis * (-halfSize);
    QVector3D xEnd = d->origin + d->uAxis * halfSize;
    xAxis->AddBound(2);
    xAxis->AddVertex(gp_Pnt(xStart.x(), xStart.y(), xStart.z()));
    xAxis->AddVertex(gp_Pnt(xEnd.x(), xEnd.y(), xEnd.z()));

    Handle(Prs3d_LineAspect) xAxisAspect = new Prs3d_LineAspect(
    Quantity_Color(d->xAxisColor.redF(), d->xAxisColor.greenF(), 
                  d->xAxisColor.blueF(), Quantity_TOC_RGB),
    Aspect_TOL_SOLID,
    2.0);

Handle(Graphic3d_Group) xAxisGroup = d->presentation->NewGroup();
xAxisGroup->SetGroupPrimitivesAspect(xAxisAspect->Aspect());
xAxisGroup->AddPrimitiveArray(xAxis);

// Y 軸 (V 軸方向，綠色)
Handle(Graphic3d_ArrayOfPolylines) yAxis = new Graphic3d_ArrayOfPolylines(2, 1);
QVector3D yStart = d->origin + d->vAxis * (-halfSize);
QVector3D yEnd = d->origin + d->vAxis * halfSize;
yAxis->AddBound(2);
yAxis->AddVertex(gp_Pnt(yStart.x(), yStart.y(), yStart.z()));
yAxis->AddVertex(gp_Pnt(yEnd.x(), yEnd.y(), yEnd.z()));

Handle(Prs3d_LineAspect) yAxisAspect = new Prs3d_LineAspect(
    Quantity_Color(d->yAxisColor.redF(), d->yAxisColor.greenF(), 
                  d->yAxisColor.blueF(), Quantity_TOC_RGB),
    Aspect_TOL_SOLID,
    2.0);

Handle(Graphic3d_Group) yAxisGroup = d->presentation->NewGroup();
yAxisGroup->SetGroupPrimitivesAspect(yAxisAspect->Aspect());
yAxisGroup->AddPrimitiveArray(yAxis);

// 設定 presentation 屬性
d->presentation->SetZLayer(Graphic3d_ZLayerId_Default);
d->presentation->Display();

d->view->Redraw();
}
void GridOverlay::clearPresentation() {
if (!d->presentation.IsNull()) {
d->presentation->Clear();
d->presentation->Erase();
d->presentation.Nullify();
}
}
} // namespace view
} // namespace aicad
