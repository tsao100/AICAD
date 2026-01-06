/**
 * @file CoordinateConverter.h
 * @brief 座標系統轉換工具
 * @author Felicia
 * @date 2024-12-04
 */

#ifndef AICAD_VIEW_COORDINATECONVERTER_H
#define AICAD_VIEW_COORDINATECONVERTER_H

#include <QPoint>
#include <QVector2D>
#include <QVector3D>
#include <QWidget>
#include <V3d_View.hxx>
#include <gp_Pln.hxx>
#include <gp_Lin.hxx>

namespace aicad {

// 前向宣告
namespace cad {
    struct CustomPlane;
}

namespace view {

/**
 * @brief 座標轉換工具類別
 * 
 * 提供各種座標系統之間的轉換:
 * - Qt 螢幕座標 <-> OCCT 視圖座標
 * - 螢幕座標 <-> 3D 世界座標
 * - 3D 世界座標 <-> 2D 平面座標
 */
class CoordinateConverter {
public:
    /**
     * @brief Qt 座標轉換為 OCCT 座標
     * @param widget Qt 視窗
     * @param qtPos Qt 滑鼠座標
     * @param occX OCCT X 座標 (輸出)
     * @param occY OCCT Y 座標 (輸出)
     * 
     * 在 Windows 高 DPI 下需要乘以 devicePixelRatio
     */
    static void qtToOcct(const QWidget* widget, 
                         const QPoint& qtPos,
                         Standard_Integer& occX, 
                         Standard_Integer& occY);
    
    /**
     * @brief OCCT 座標轉換為 Qt 座標
     */
    static QPoint occtToQt(const QWidget* widget,
                           Standard_Integer occX,
                           Standard_Integer occY);
    
    /**
     * @brief 螢幕座標轉換為 3D 世界座標
     * @param view OCCT 視圖
     * @param screenPos 螢幕座標
     * @return 3D 世界座標
     */
    static QVector3D screenToWorld(const Handle(V3d_View)& view,
                                   const QPoint& screenPos,
                                   const QWidget* widget);
    
    /**
     * @brief 3D 世界座標轉換為螢幕座標
     */
    static QPoint worldToScreen(const Handle(V3d_View)& view,
                               const QVector3D& worldPos,
                               const QWidget* widget);
    
    /**
     * @brief 螢幕座標投影到指定平面
     * @param view OCCT 視圖
     * @param screenPos 螢幕座標
     * @param plane 目標平面
     * @param widget Qt 視窗
     * @return 平面上的 2D 座標，投影失敗回傳 (0, 0)
     */
    static QVector2D screenToPlane(const Handle(V3d_View)& view,
                                   const QPoint& screenPos,
                                   const cad::CustomPlane& plane,
                                   const QWidget* widget);
    
    /**
     * @brief 平面 2D 座標轉換為 3D 世界座標
     */
    static QVector3D planeToWorld(const QVector2D& planePos,
                                  const cad::CustomPlane& plane);
    
    /**
     * @brief 建立從螢幕點出發的拾取射線
     * @param view OCCT 視圖
     * @param screenPos 螢幕座標
     * @param widget Qt 視窗
     * @return 拾取射線
     */
    static gp_Lin createPickingRay(const Handle(V3d_View)& view,
                                   const QPoint& screenPos,
                                   const QWidget* widget);
    
    /**
     * @brief 計算射線與平面的交點
     * @param ray 射線
     * @param plane 平面
     * @param intersection 交點 (輸出)
     * @return 是否有交點
     */
    static bool intersectRayPlane(const gp_Lin& ray,
                                  const gp_Pln& plane,
                                  gp_Pnt& intersection);
};

} // namespace view
} // namespace aicad

#endif // AICAD_VIEW_COORDINATECONVERTER_H