/**
 * @file Plane.h
 * @brief 平面定義類別
 * @author AICAD Team
 * @date 2025-01-08
 */

#ifndef AICAD_CAD_PLANE_H
#define AICAD_CAD_PLANE_H

#include <QVector2D>
#include <QVector3D>
#include <QString>
#include <gp_Pln.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>

namespace aicad {
namespace cad {

/**
 * @brief 平面類別
 * 
 * 定義 3D 空間中的平面，包含原點、法向量和座標軸
 * 用於草圖建立和幾何投影
 * 
 * 使用範例:
 * @code
 * // 建立 XY 平面
 * Plane xyPlane = Plane::xy();
 * 
 * // 建立自訂平面
 * Plane customPlane(
 *     QVector3D(0, 0, 10),  // 原點
 *     QVector3D(0, 0, 1),   // 法向量
 *     QVector3D(1, 0, 0)    // X軸方向
 * );
 * 
 * // 轉換為 OCCT 格式
 * gp_Pln gpPlane = customPlane.toGpPln();
 * @endcode
 */
class Plane {
public:
    /**
     * @brief 預設建構子 - 建立 XY 平面
     */
    Plane();
    
    /**
     * @brief 完整建構子
     * @param origin 平面原點
     * @param normal 平面法向量（會自動正規化）
     * @param xAxis X軸方向（會自動正規化並確保垂直於法向量）
     */
    Plane(const QVector3D& origin, 
          const QVector3D& normal, 
          const QVector3D& xAxis);
    
    /**
     * @brief 從 OCCT gp_Pln 建立
     */
    static Plane fromGpPln(const gp_Pln& gpPlane);
    
    /**
     * @brief 從 OCCT gp_Ax2 建立
     */
    static Plane fromGpAx2(const gp_Ax2& ax2);
    
    // 標準平面工廠方法
    static Plane xy();      ///< XY 平面 (Z = 0)
    static Plane xz();      ///< XZ 平面 (Y = 0)
    static Plane yz();      ///< YZ 平面 (X = 0)
    
    // 取得器
    QVector3D origin() const { return m_origin; }
    QVector3D normal() const { return m_normal; }
    QVector3D xAxis() const { return m_xAxis; }
    QVector3D yAxis() const { return m_yAxis; }
    
    // 設定器
    void setOrigin(const QVector3D& origin);
    void setNormal(const QVector3D& normal);
    void setXAxis(const QVector3D& xAxis);
    
    /**
     * @brief 取得平面顯示名稱
     * @return 人類可讀的平面名稱，例如 "XY", "XZ", "Custom"
     */
    QString displayName() const;
    
    /**
     * @brief 轉換為 OCCT gp_Pln
     */
    gp_Pln toGpPln() const;
    
    /**
     * @brief 轉換為 OCCT gp_Ax2（座標系統）
     */
    gp_Ax2 toGpAx2() const;
    
    /**
     * @brief 將 2D 平面座標轉換為 3D 世界座標
     * @param u 平面 X 座標
     * @param v 平面 Y 座標
     * @return 3D 世界座標
     */
    QVector3D toWorld(double u, double v) const;
    
    /**
     * @brief 將 3D 世界座標投影到平面並取得 2D 座標
     * @param worldPoint 3D 世界座標
     * @return 平面上的 2D 座標 (u, v)
     */
    QVector2D toPlane(const QVector3D& worldPoint) const;
    
    /**
     * @brief 計算點到平面的距離（有號距離）
     * @param point 3D 點
     * @return 距離（正值表示在法向量方向）
     */
    double distanceTo(const QVector3D& point) const;
    
    /**
     * @brief 判斷是否為標準平面
     */
    bool isXY() const;
    bool isXZ() const;
    bool isYZ() const;
    
    /**
     * @brief 判斷兩平面是否平行
     */
    bool isParallelTo(const Plane& other, double tolerance = 1e-6) const;
    
    /**
     * @brief 判斷兩平面是否相等
     */
    bool equals(const Plane& other, double tolerance = 1e-6) const;

private:
    /**
     * @brief 正規化並重新計算座標軸
     * 確保法向量和 X/Y 軸彼此垂直且為單位向量
     */
    void normalize();
    
    QVector3D m_origin;   ///< 平面原點
    QVector3D m_normal;   ///< 平面法向量（Z軸方向）
    QVector3D m_xAxis;    ///< 平面 X 軸方向（U軸）
    QVector3D m_yAxis;    ///< 平面 Y 軸方向（V軸）
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_PLANE_H