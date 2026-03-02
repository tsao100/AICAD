/**
 * @file ViewGrid.cpp
 * @brief ViewGrid 類別實作
 * @author Felicia
 * @date 2024-12-04
 */

#include "ViewGrid.h"
#include "cad/PlaneManager.h"

#include <QDebug>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Graphic3d_Group.hxx>
#include <Quantity_Color.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <Aspect_TypeOfMarker.hxx>
#include <gp_Pnt.hxx>

namespace aicad {
namespace view {

class ViewGrid::Private {
public:
    Handle(AIS_InteractiveContext) context;
    Handle(Prs3d_Presentation) gridPresentation;
    Handle(Prs3d_Presentation) axesPresentation;
    
    cad::Plane* plane;
    int gridSize;
    float gridSpacing;
    GridStyle style;
    bool showAxesFlag;
    bool visible;
    
    // 網格顏色
    float gridColorR;
    float gridColorG;
    float gridColorB;
    
    Private()
        : gridSize(20)
        , gridSpacing(10.0f)
        , style(GridStyle::Lines)
        , showAxesFlag(true)
        , visible(false)
        , gridColorR(0.4f)
        , gridColorG(0.4f)
        , gridColorB(0.5f)
    {
    }
};

ViewGrid::ViewGrid(const Handle(AIS_InteractiveContext)& context, QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    d->context = context;
    cad::PlaneManager* manager = cad::PlaneManager::instance();
    d->plane = manager->activePlane();

    // 如果沒有活動平面，嘗試取得或建立 XY 平面
    if (!d->plane) {
        QList<cad::Plane*> planes = manager->planes();
        for (cad::Plane* p : planes) {
            if (p->isXY()) {
                d->plane = p;
                break;
            }
        }

        // 還是沒有，建立新的 XY 平面
        if (!d->plane) {
            d->plane = manager->createPlane(cad::Plane::Type::XY, "RubberBandPlane");
        }
    }

    qDebug() << "[ViewGrid] Created";
}

ViewGrid::~ViewGrid() {
    clear();
    delete d;
    qDebug() << "[ViewGrid] Destroyed";
}

void ViewGrid::setPlane(cad::Plane* plane) {
    d->plane = plane;

    if (d->visible) {
        update();
    }
}

cad::Plane* ViewGrid::plane() const {
    return d->plane;
}

void ViewGrid::setSize(int size) {
    if (size < 1) {
        size = 1;
    }
    
    d->gridSize = size;
    
    if (d->visible) {
        update();
    }
}

int ViewGrid::size() const {
    return d->gridSize;
}

void ViewGrid::setSpacing(float spacing) {
    if (spacing < 0.1f) {
        spacing = 0.1f;
    }
    
    d->gridSpacing = spacing;
    
    if (d->visible) {
        update();
    }
}

float ViewGrid::spacing() const {
    return d->gridSpacing;
}

void ViewGrid::setStyle(GridStyle style) {
    d->style = style;
    
    if (d->visible) {
        update();
    }
}

GridStyle ViewGrid::style() const {
    return d->style;
}

void ViewGrid::setShowAxes(bool show) {
    d->showAxesFlag = show;
    
    if (d->visible) {
        update();
    }
}

bool ViewGrid::showAxes() const {
    return d->showAxesFlag;
}

void ViewGrid::setGridColor(float r, float g, float b) {
    d->gridColorR = r;
    d->gridColorG = g;
    d->gridColorB = b;
    
    if (d->visible) {
        update();
    }
}

void ViewGrid::show() {
    if (d->visible) {
        return;
    }
    
    d->visible = true;
    update();
    
    Q_EMIT visibilityChanged(true);
}

void ViewGrid::hide() {
    if (!d->visible) {
        return;
    }
    
    d->visible = false;
    clear();
    
    Q_EMIT visibilityChanged(false);
}

bool ViewGrid::isVisible() const {
    return d->visible;
}

void ViewGrid::update() {
    if (!d->visible || d->context.IsNull()) {
        return;
    }
    
    clear();
   // createGridGeometry();
    
    if (d->showAxesFlag) {
       // createAxes();
    }
}

void ViewGrid::createGridGeometry() {
    if (d->context.IsNull()) {
        return;
    }
    
    float halfSize = d->gridSize * d->gridSpacing / 2.0f;
    
    d->gridPresentation = new Prs3d_Presentation(
        d->context->MainPrsMgr()->StructureManager());
    
    if (d->style == GridStyle::Lines) {
        // 計算需要的線條數量（跳過中心線）
        int linesU = d->gridSize + 1 - 1;  // 減去中心線
        int linesV = d->gridSize + 1 - 1;
        int totalVertices = 2 * (linesU + linesV);
        
        Handle(Graphic3d_ArrayOfPolylines) lines = 
            new Graphic3d_ArrayOfPolylines(totalVertices, linesU + linesV);
        
        // 平行於 U 軸的線
        for (int i = 0; i <= d->gridSize; ++i) {
            if (i == d->gridSize / 2) continue;  // 跳過中心
            
            float v = -halfSize + i * d->gridSpacing;
            QVector3D p1 = d->plane->origin() + d->plane->xAxis() * (-halfSize) + d->plane->yAxis() * v;
            QVector3D p2 = d->plane->origin() + d->plane->xAxis() * halfSize + d->plane->yAxis() * v;
            
            lines->AddBound(2);
            lines->AddVertex(gp_Pnt(p1.x(), p1.y(), p1.z()));
            lines->AddVertex(gp_Pnt(p2.x(), p2.y(), p2.z()));
        }
        
        // 平行於 V 軸的線
        for (int i = 0; i <= d->gridSize; ++i) {
            if (i == d->gridSize / 2) continue;  // 跳過中心
            
            float u = -halfSize + i * d->gridSpacing;
            QVector3D p1 = d->plane->origin() + d->plane->xAxis() * u + d->plane->yAxis() * (-halfSize);
            QVector3D p2 = d->plane->origin() + d->plane->xAxis() * u + d->plane->yAxis() * halfSize;
            
            lines->AddBound(2);
            lines->AddVertex(gp_Pnt(p1.x(), p1.y(), p1.z()));
            lines->AddVertex(gp_Pnt(p2.x(), p2.y(), p2.z()));
        }
        
        Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
            Quantity_Color(d->gridColorR, d->gridColorG, d->gridColorB, Quantity_TOC_RGB),
            Aspect_TOL_SOLID,
            1.0
        );
        
        Handle(Graphic3d_Group) group = d->gridPresentation->NewGroup();
        group->SetGroupPrimitivesAspect(aspect->Aspect());
        group->AddPrimitiveArray(lines);
        
    } else if (d->style == GridStyle::Dots) {
        // 點狀網格
        int totalPoints = (d->gridSize + 1) * (d->gridSize + 1);
        Handle(Graphic3d_ArrayOfPoints) points = 
            new Graphic3d_ArrayOfPoints(totalPoints);
        
        for (int i = 0; i <= d->gridSize; ++i) {
            for (int j = 0; j <= d->gridSize; ++j) {
                float u = -halfSize + i * d->gridSpacing;
                float v = -halfSize + j * d->gridSpacing;
                QVector3D p = d->plane->origin() + d->plane->xAxis() * u + d->plane->yAxis() * v;
                points->AddVertex(gp_Pnt(p.x(), p.y(), p.z()));
            }
        }
        
        Handle(Prs3d_PointAspect) aspect = new Prs3d_PointAspect(
            Aspect_TOM_POINT,
            Quantity_Color(d->gridColorR, d->gridColorG, d->gridColorB, Quantity_TOC_RGB),
            2.0
        );
        
        Handle(Graphic3d_Group) group = d->gridPresentation->NewGroup();
        group->SetGroupPrimitivesAspect(aspect->Aspect());
        group->AddPrimitiveArray(points);
        
    } else if (d->style == GridStyle::Crosses) {
        // 十字網格
        int totalLines = (d->gridSize + 1) * (d->gridSize + 1) * 2;
        Handle(Graphic3d_ArrayOfPolylines) crosses = 
            new Graphic3d_ArrayOfPolylines(totalLines * 2, totalLines);
        
        float crossSize = d->gridSpacing * 0.1f;
        
        for (int i = 0; i <= d->gridSize; ++i) {
            for (int j = 0; j <= d->gridSize; ++j) {
                float u = -halfSize + i * d->gridSpacing;
                float v = -halfSize + j * d->gridSpacing;
                QVector3D center = d->plane->origin() + d->plane->xAxis() * u + d->plane->yAxis() * v;
                
                // 水平線
                QVector3D h1 = center - d->plane->xAxis() * crossSize;
                QVector3D h2 = center + d->plane->xAxis() * crossSize;
                crosses->AddBound(2);
                crosses->AddVertex(gp_Pnt(h1.x(), h1.y(), h1.z()));
                crosses->AddVertex(gp_Pnt(h2.x(), h2.y(), h2.z()));
                
                // 垂直線
                QVector3D v1 = center - d->plane->yAxis() * crossSize;
                QVector3D v2 = center + d->plane->yAxis() * crossSize;
                crosses->AddBound(2);
                crosses->AddVertex(gp_Pnt(v1.x(), v1.y(), v1.z()));
                crosses->AddVertex(gp_Pnt(v2.x(), v2.y(), v2.z()));
            }
        }
        
        Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
            Quantity_Color(d->gridColorR, d->gridColorG, d->gridColorB, Quantity_TOC_RGB),
            Aspect_TOL_SOLID,
            1.0
        );
        
        Handle(Graphic3d_Group) group = d->gridPresentation->NewGroup();
        group->SetGroupPrimitivesAspect(aspect->Aspect());
        group->AddPrimitiveArray(crosses);
    }
    
    d->gridPresentation->SetZLayer(Graphic3d_ZLayerId_Default);
    d->gridPresentation->Display();
}

void ViewGrid::createAxes() {
    if (d->context.IsNull()) {
        return;
    }
    
    float halfSize = d->gridSize * d->gridSpacing / 2.0f;
    
    d->axesPresentation = new Prs3d_Presentation(
        d->context->MainPrsMgr()->StructureManager());
    
    // X 軸 (沿 U 軸，紅色)
    Handle(Graphic3d_ArrayOfPolylines) xAxis = new Graphic3d_ArrayOfPolylines(2, 1);
    QVector3D xStart = d->plane->origin() + d->plane->xAxis() * (-halfSize);
    QVector3D xEnd = d->plane->origin() + d->plane->xAxis() * halfSize;
    xAxis->AddBound(2);
    xAxis->AddVertex(gp_Pnt(xStart.x(), xStart.y(), xStart.z()));
    xAxis->AddVertex(gp_Pnt(xEnd.x(), xEnd.y(), xEnd.z()));
    
    Handle(Prs3d_LineAspect) xAxisAspect = new Prs3d_LineAspect(
        Quantity_NOC_RED,
        Aspect_TOL_SOLID,
        2.0
    );
    
    Handle(Graphic3d_Group) xAxisGroup = d->axesPresentation->NewGroup();
    xAxisGroup->SetGroupPrimitivesAspect(xAxisAspect->Aspect());
    xAxisGroup->AddPrimitiveArray(xAxis);
    
    // Y 軸 (沿 V 軸，綠色)
    Handle(Graphic3d_ArrayOfPolylines) yAxis = new Graphic3d_ArrayOfPolylines(2, 1);
    QVector3D yStart = d->plane->origin() + d->plane->yAxis() * (-halfSize);
    QVector3D yEnd = d->plane->origin() + d->plane->yAxis() * halfSize;
    yAxis->AddBound(2);
    yAxis->AddVertex(gp_Pnt(yStart.x(), yStart.y(), yStart.z()));
    yAxis->AddVertex(gp_Pnt(yEnd.x(), yEnd.y(), yEnd.z()));
    
    Handle(Prs3d_LineAspect) yAxisAspect = new Prs3d_LineAspect(
        Quantity_NOC_GREEN,
        Aspect_TOL_SOLID,
        2.0
    );
    
    Handle(Graphic3d_Group) yAxisGroup = d->axesPresentation->NewGroup();
    yAxisGroup->SetGroupPrimitivesAspect(yAxisAspect->Aspect());
    yAxisGroup->AddPrimitiveArray(yAxis);
    
    d->axesPresentation->SetZLayer(Graphic3d_ZLayerId_Default);
    d->axesPresentation->Display();
}

void ViewGrid::clear() {
    if (!d->gridPresentation.IsNull()) {
        d->gridPresentation->Clear();
        d->gridPresentation->Erase();
        d->gridPresentation.Nullify();
    }
    
    if (!d->axesPresentation.IsNull()) {
        d->axesPresentation->Clear();
        d->axesPresentation->Erase();
        d->axesPresentation.Nullify();
    }
}

} // namespace view
} // namespace aicad
