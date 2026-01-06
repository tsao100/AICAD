/**
 * @file CoordinateConverter.cpp
 * @brief CoordinateConverter 實作
 * @author Felicia
 * @date 2024-12-04
 */

#include "CoordinateConverter.h"
#include "../cad/geometry/CustomPlane.h"

#include <IntAna_IntConicQuad.hxx>
#include <Precision.hxx>
#include <QDebug>

namespace aicad {
namespace view {

void CoordinateConverter::qtToOcct(const QWidget* widget,
                                   const QPoint& qtPos,
                                   Standard_Integer& occX,
                                   Standard_Integer& occY)
{
#ifdef _WIN32
    // Windows 高 DPI 需要乘以縮放比例
    qreal dpr = widget->devicePixelRatio();
    occX = static_cast<Standard_Integer>(qtPos.x() * dpr);
    occY = static_cast<Standard_Integer>(qtPos.y() * dpr);
#else
    // Linux/X11 座標直接對應
    occX = qtPos.x();
    occY = qtPos.y();
#endif
}

QPoint CoordinateConverter::occtToQt(const QWidget* widget,
                                     Standard_Integer occX,
                                     Standard_Integer occY)
{
#ifdef _WIN32
    qreal dpr = widget->devicePixelRatio();
    return QPoint(static_cast<int>(occX / dpr),
                  static_cast<int>(occY / dpr));
#else
    return QPoint(occX, occY);
#endif
}

QVector3D CoordinateConverter::screenToWorld(const Handle(V3d_View)& view,
                                             const QPoint& screenPos,
                                             const QWidget* widget)
{
    if (view.IsNull()) {
        qWarning() << "[CoordinateConverter] View is null";
        return QVector3D(0, 0, 0);
    }
    
    Standard_Integer occX, occY;
    qtToOcct(widget, screenPos, occX, occY);
    
    Standard_Real xv, yv, zv;
    view->Convert(occX, occY, xv, yv, zv);
    
    return QVector3D(xv, yv, zv);
}

QPoint CoordinateConverter::worldToScreen(const Handle(V3d_View)& view,
                                          const QVector3D& worldPos,
                                          const QWidget* widget)
{
    if (view.IsNull()) {
        return QPoint(0, 0);
    }
    
    Standard_Integer occX, occY;
    view->Convert(worldPos.x(), worldPos.y(), worldPos.z(), occX, occY);
    
    return occtToQt(widget, occX, occY);
}

gp_Lin CoordinateConverter::createPickingRay(const Handle(V3d_View)& view,
                                             const QPoint& screenPos,
                                             const QWidget* widget)
{
    Standard_Integer occX, occY;
    qtToOcct(widget, screenPos, occX, occY);
    
    // 取得視圖投影資訊
    Standard_Real xEye, yEye, zEye;
    Standard_Real xProj, yProj, zProj;
    view->Eye(xEye, yEye, zEye);
    view->Proj(xProj, yProj, zProj);
    
    gp_Pnt eyePoint(xEye, yEye, zEye);
    gp_Dir projDir(xProj, yProj, zProj);
    
    // 轉換螢幕點到 3D
    Standard_Real xv, yv, zv;
    view->Convert(occX, occY, xv, yv, zv);
    gp_Pnt screenPoint3D(xv, yv, zv);
    
    // 建立射線
    gp_Pnt rayStart;
    gp_Dir rayDir;
    
    if (view->Camera()->IsOrthographic()) {
        // 正交投影: 射線從螢幕點開始，方向為投影方向
        rayStart = screenPoint3D;
        rayDir = projDir;
    } else {
        // 透視投影: 射線從視點到螢幕點
        rayStart = eyePoint;
        gp_Vec direction(eyePoint, screenPoint3D);
        if (direction.Magnitude() < Precision::Confusion()) {
            rayDir = projDir;
        } else {
            rayDir = gp_Dir(direction);
        }
    }
    
    return gp_Lin(rayStart, rayDir);
}

bool CoordinateConverter::intersectRayPlane(const gp_Lin& ray,
                                            const gp_Pln& plane,
                                            gp_Pnt& intersection)
{
    IntAna_IntConicQuad intersector(ray, plane, Precision::Angular());
    
    if (intersector.IsDone() && intersector.NbPoints() > 0) {
        intersection = intersector.Point(1);
        return true;
    }
    
    return false;
}

QVector2D CoordinateConverter::screenToPlane(const Handle(V3d_View)& view,
                                             const QPoint& screenPos,
                                             const cad::CustomPlane& plane,
                                             const QWidget* widget)
{
    if (view.IsNull()) {
        return QVector2D(0, 0);
    }
    
    // 建立拾取射線
    gp_Lin pickRay = createPickingRay(view, screenPos, widget);
    
    // 轉換平面到 gp_Pln
    gp_Pln gpPlane = plane.toGpPln();
    
    // 計算交點
    gp_Pnt intersection;
    if (!intersectRayPlane(pickRay, gpPlane, intersection)) {
        qWarning() << "[CoordinateConverter] No intersection with plane";
        return QVector2D(0, 0);
    }
    
    // 轉換 3D 世界座標到平面 2D 座標
    QVector3D worldPt(intersection.X(), intersection.Y(), intersection.Z());
    QVector3D localPt = worldPt - plane.origin;
    
    float u = QVector3D::dotProduct(localPt, plane.uAxis);
    float v = QVector3D::dotProduct(localPt, plane.vAxis);
    
    return QVector2D(u, v);
}

QVector3D CoordinateConverter::planeToWorld(const QVector2D& planePos,
                                            const cad::CustomPlane& plane)
{
    return plane.origin + plane.uAxis * planePos.x() + plane.vAxis * planePos.y();
}

} // namespace view
} // namespace aicad