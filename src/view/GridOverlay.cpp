/**
 * @file GridOverlay.cpp
 * @brief GridOverlay 實作
 * @author Felicia
 * @date 2024-12-04
 */

#include "GridOverlay.h"
#include "../cad/geometry/CustomPlane.h"

#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Graphic3d_Group.hxx>
#include <Quantity_Color.hxx>
#include <QDebug>

namespace aicad {
namespace view {

class GridOverlay::Private {
public:
    Handle(V3d_View) view;
    Handle(AIS_InteractiveContext) context;
    Handle(Prs3d_Presentation) presentation;
    
    cad::CustomPlane plane;
    int gridSize = 20;
    float gridSpacing = 10.0f;
    
    bool visible = false;
    bool majorGridEnabled = true;
    bool minorGridEnabled = false;
    bool axesEnabled = true;
    
    QColor majorGridColor = QColor(100, 100, 100);  // 深灰
    QColor minorGridColor = QColor(60, 60, 60);     // 更深灰
    QColor xAxisColor = QColor(255, 0, 0);          // 紅色
    QColor yAxisColor = QColor(0, 255, 0);          // 綠色
};

GridOverlay::GridOverlay(Handle(V3d_View) view,
                         Handle(AIS_InteractiveContext) context,
                         QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    d->view = view;
    d->context = context;
    d->plane = cad::CustomPlane::XY();  // 預設 XY 平面
    
    qDebug() << "[GridOverlay] Created";
}

GridOverlay::~GridOverlay() {
    qDebug() << "[GridOverlay] Destroying...";
    clear();
    delete d;
}

void GridOverlay::setPlane(const cad::CustomPlane& plane) {
    d->plane = plane;
    qDebug() << "[GridOverlay] Plane set to:" << plane.getDisplayName();
    
    if (d->visible) {
        update();
    }
}

cad::CustomPlane GridOverlay::plane() const {
    return d->plane;
}

void GridOverlay::setGridSize(int size) {
    if (size < 5) size = 5;
    if (size > 100) size = 100;
    
    d->gridSize = size;
    
    if (d->visible) {
        update();
    }
}

int GridOverlay::gridSize() const {
    return d->gridSize;
}

void GridOverlay::setGridSpacing(float spacing) {
    if (spacing < 1.0f) spacing = 1.0f;
    if (spacing > 1000.0f) spacing = 1000.0f;
    
    d->gridSpacing = spacing;
    
    if (d->visible) {
        update();
    }
}

float GridOverlay::gridSpacing() const {
    return d->gridSpacing;
}

void GridOverlay::setMajorGridColor(const QColor& color) {
    d->majorGridColor = color;
}

void GridOverlay::setMinorGridColor(const QColor& color) {
    d->minorGridColor = color;
}

void GridOverlay::setXAxisColor(const QColor& color) {
    d->xAxisColor = color;
}

void GridOverlay::setYAxisColor(const QColor& color) {
    d->yAxisColor = color;
}

void GridOverlay::show() {
    if (d->visible) {
        return;
    }
    
    d->visible = true;
    createGrid();
    Q_EMIT visibilityChanged(true);
    qDebug() << "[GridOverlay] Shown";
}

void GridOverlay::hide() {
    if (!d->visible) {
        return;
    }
    
    d->visible = false;
    clearPresentation();
    Q_EMIT visibilityChanged(false);
    qDebug() << "[GridOverlay] Hidden";
}

void GridOverlay::clear() {
    clearPresentation();
    d->visible = false;
}

void GridOverlay::update() {
    if (d->visible) {
        clearPresentation();
        createGrid();
        Q_EMIT updated();
    }
}

bool GridOverlay::isVisible() const {
    return d->visible;
}

void GridOverlay::setVisible(bool visible) {
    if (visible) {
        show();
    } else {
        hide();
    }
}

void GridOverlay::setMajorGridEnabled(bool enabled) {
    d->majorGridEnabled = enabled;
    if (d->visible) {
        update();
    }
}

void GridOverlay::setMinorGridEnabled(bool enabled) {
    d->minorGridEnabled = enabled;
    if (d->visible) {
        update();
    }
}

void GridOverlay::setAxesEnabled(bool enabled) {
    d->axesEnabled = enabled;
    if (d->visible) {
        update();
    }
}

void GridOverlay::clearPresentation() {
    if (!d->presentation.IsNull()) {
        d->presentation->Clear();
        d->presentation->Erase();
        d->presentation.Nullify();
    }
}

void GridOverlay::createGrid() {
    if (d->context.IsNull()) {
        qWarning() << "[GridOverlay] Context is null";
        return;
    }
    
    clearPresentation();
    
    d->presentation = new Prs3d_Presentation(d->context->MainPrsMgr()->StructureManager());
    
    float halfSize = d->gridSize * d->gridSpacing / 2.0f;
    int centerIndex = d->gridSize / 2;
    
    // 轉換平面座標到世界座標的輔助函式
    auto toWorld = [this](float u, float v) -> gp_Pnt {
        QVector3D worldPt = d->plane.origin + 
                           d->plane.uAxis * u + 
                           d->plane.vAxis * v;
        return gp_Pnt(worldPt.x(), worldPt.y(), worldPt.z());
    };
    
    // 1. 繪製主網格線 (平行於 U 軸)
    if (d->majorGridEnabled) {
        int lineCount = 0;
        for (int i = 0; i <= d->gridSize; ++i) {
            if (i != centerIndex) {  // 中心線會單獨繪製為軸線
                lineCount++;
            }
        }
        
        Handle(Graphic3d_ArrayOfPolylines) gridLines = 
            new Graphic3d_ArrayOfPolylines(lineCount * 2, lineCount);
        
        for (int i = 0; i <= d->gridSize; ++i) {
            if (i == centerIndex) continue;
            
            float v = -halfSize + i * d->gridSpacing;
            gridLines->AddBound(2);
            gridLines->AddVertex(toWorld(-halfSize, v));
            gridLines->AddVertex(toWorld(halfSize, v));
        }
        
        Handle(Prs3d_LineAspect) gridAspect = new Prs3d_LineAspect(
            Quantity_Color(d->majorGridColor.redF(), 
                          d->majorGridColor.greenF(), 
                          d->majorGridColor.blueF(), 
                          Quantity_TOC_RGB),
            Aspect_TOL_SOLID,
            1.0
        );
        
        Handle(Graphic3d_Group) gridGroup = d->presentation->NewGroup();
        gridGroup->SetGroupPrimitivesAspect(gridAspect->Aspect());
        gridGroup->AddPrimitiveArray(gridLines);
    }
    
    // 2. 繪製主網格線 (平行於 V 軸)
    if (d->majorGridEnabled) {
        int lineCount = 0;
        for (int i = 0; i <= d->gridSize; ++i) {
            if (i != centerIndex) {
                lineCount++;
            }
        }
        
        Handle(Graphic3d_ArrayOfPolylines) gridLines = 
            new Graphic3d_ArrayOfPolylines(lineCount * 2, lineCount);
        
        for (int i = 0; i <= d->gridSize; ++i) {
            if (i == centerIndex) continue;
            
            float u = -halfSize + i * d->gridSpacing;
            gridLines->AddBound(2);
            gridLines->AddVertex(toWorld(u, -halfSize));
            gridLines->AddVertex(toWorld(u, halfSize));
        }
        
        Handle(Prs3d_LineAspect) gridAspect = new Prs3d_LineAspect(
            Quantity_Color(d->majorGridColor.redF(), 
                          d->majorGridColor.greenF(), 
                          d->majorGridColor.blueF(), 
                          Quantity_TOC_RGB),
            Aspect_TOL_SOLID,
            1.0
        );
        
        Handle(Graphic3d_Group) gridGroup = d->presentation->NewGroup();
        gridGroup->SetGroupPrimitivesAspect(gridAspect->Aspect());
        gridGroup->AddPrimitiveArray(gridLines);
    }
    
    // 3. 繪製 X 軸 (沿 U 軸方向，紅色)
    if (d->axesEnabled) {
        Handle(Graphic3d_ArrayOfPolylines) xAxis = new Graphic3d_ArrayOfPolylines(2, 1);
        xAxis->AddBound(2);
        xAxis->AddVertex(toWorld(-halfSize, 0));
        xAxis->AddVertex(toWorld(halfSize, 0));
        
        Handle(Prs3d_LineAspect) xAxisAspect = new Prs3d_LineAspect(
            Quantity_Color(d->xAxisColor.redF(), 
                          d->xAxisColor.greenF(), 
                          d->xAxisColor.blueF(), 
                          Quantity_TOC_RGB),
            Aspect_TOL_SOLID,
            2.0
        );
        
        Handle(Graphic3d_Group) xAxisGroup = d->presentation->NewGroup();
        xAxisGroup->SetGroupPrimitivesAspect(xAxisAspect->Aspect());
        xAxisGroup->AddPrimitiveArray(xAxis);
    }
    
    // 4. 繪製 Y 軸 (沿 V 軸方向，綠色)
    if (d->axesEnabled) {
        Handle(Graphic3d_ArrayOfPolylines) yAxis = new Graphic3d_ArrayOfPolylines(2, 1);
        yAxis->AddBound(2);
        yAxis->AddVertex(toWorld(0, -halfSize));
        yAxis->AddVertex(toWorld(0, halfSize));
        
        Handle(Prs3d_LineAspect) yAxisAspect = new Prs3d_LineAspect(
            Quantity_Color(d->yAxisColor.redF(), 
                          d->yAxisColor.greenF(), 
                          d->yAxisColor.blueF(), 
                          Quantity_TOC_RGB),
            Aspect_TOL_SOLID,
            2.0
        );
        
        Handle(Graphic3d_Group) yAxisGroup = d->presentation->NewGroup();
        yAxisGroup->SetGroupPrimitivesAspect(yAxisAspect->Aspect());
        yAxisGroup->AddPrimitiveArray(yAxis);
    }
    
    // 設定 Z 層級
    d->presentation->SetZLayer(Graphic3d_ZLayerId_Default);
    d->presentation->Display();
    
    // 更新視圖
    if (!d->view.IsNull()) {
        d->view->Redraw();
    }
    
    qDebug() << "[GridOverlay] Grid created. Size:" << d->gridSize 
             << "Spacing:" << d->gridSpacing;
}

} // namespace view
} // namespace aicad