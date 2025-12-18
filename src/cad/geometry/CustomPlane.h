/**
 * @file CustomPlane.h
 * @brief 自訂平面類別 (完整實作)
 * @author Ben
 * @date 2024-12-04
 */

#ifndef AICAD_CAD_GEOMETRY_CUSTOMPLANE_H
#define AICAD_CAD_GEOMETRY_CUSTOMPLANE_H

#include <QString>
#include <QVector3D>
#include <gp_Pln.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>

namespace aicad {
namespace cad {

/**
 * @brief 自訂平面結構
 * 
 * 表示一個 3D 空間中的平面，包含原點、法向量和兩個正交的軸向量。
 * 用於定義草圖平面和座標系統。
 * 
 * 座標系統定義：
 * - origin: 平面原點
 * - normal: 平面法向量 (Z 軸方向)
 * - uAxis: 平面 U 軸 (X 軸方向)
 * - vAxis: 平面 V 軸 (Y 軸方向)
 * 
 * 使用範例:
 * @code
 * CustomPlane plane = CustomPlane::XY();
 * plane.setOrigin(QVector3D(10, 20, 30));
 * gp_Pln occPlane = plane.toGpPln();
 * @endcode
 */
struct CustomPlane {
    QVector3D origin;   ///< 平面原點
    QVector3D normal;   ///< 平面法向量 (單位向量)
    QVector3D uAxis;    ///< U 軸方向 (X軸，單位向量)
    QVector3D vAxis;    ///< V 軸方向 (Y軸，單位向量)
    
    /**
     * @brief 預設建構子 (建立 XY 平面)
     */
    CustomPlane();
    
    /**
     * @brief 建構子
     * @param origin 平面原點
     * @param normal 平面法向量
     * @param uAxis U 軸方向
     */
    CustomPlane(const QVector3D& origin, 
                const QVector3D& normal, 
                const QVector3D& uAxis);
    
    /**
     * @brief 建立 XY 平面 (Z = 0)
     * @return XY 平面實例
     */
    static CustomPlane XY();
    
    /**
     * @brief 建立 XZ 平面 (Y = 0)
     * @return XZ 平面實例
     */
    static CustomPlane XZ();
    
    /**
     * @brief 建立 YZ 平面 (X = 0)
     * @return YZ 平面實例
     */
    static CustomPlane YZ();
    
    /**
     * @brief 從任意三點建立平面
     * @param p1 第一點
     * @param p2 第二點
     * @param p3 第三點
     * @return 通過三點的平面
     */
    static CustomPlane fromThreePoints(const QVector3D& p1,
                                       const QVector3D& p2,
                                       const QVector3D& p3);
    
    /**
     * @brief 從原點和法向量建立平面
     * @param origin 平面原點
     * @param normal 平面法向量
     * @return 平面實例
     */
    static CustomPlane fromOriginNormal(const QVector3D& origin,
                                        const QVector3D& normal);
    
    /**
     * @brief 設定平面原點
     */
    void setOrigin(const QVector3D& origin);
    
    /**
     * @brief 設定平面法向量 (會自動正規化)
     */
    void setNormal(const QVector3D& normal);
    
    /**
     * @brief 取得顯示名稱
     * @return 平面的可讀名稱 (如 "XY", "XZ", "Custom")
     */
    QString getDisplayName() const;
    
    /**
     * @brief 轉換為 OCCT gp_Pln
     * @return OCCT 平面物件
     */
    gp_Pln toGpPln() const;
    
    /**
     * @brief 轉換為 OCCT gp_Ax2 (座標系統)
     * @return OCCT 座標系統物件
     */
    gp_Ax2 toGpAx2() const;
    
    /**
     * @brief 檢查平面是否有效
     * @return 若軸向量正交且為單位向量則回傳 true
     */
    bool isValid() const;
    
    /**
     * @brief 正規化軸向量 (確保為單位向量)
     */
    void normalize();
    
    /**
     * @brief 計算點到平面的距離
     * @param point 3D 點
     * @return 點到平面的垂直距離 (帶正負號)
     */
    double distanceToPoint(const QVector3D& point) const;
    
    /**
     * @brief 將 3D 點投影到平面
     * @param point 3D 點
     * @return 投影後的點
     */
    QVector3D projectPoint(const QVector3D& point) const;
    
    /**
     * @brief 檢查兩個平面是否平行
     * @param other 另一個平面
     * @param tolerance 容差 (預設 1e-6)
     * @return 若平行則回傳 true
     */
    bool isParallelTo(const CustomPlane& other, double tolerance = 1e-6) const;
    
    /**
     * @brief 檢查兩個平面是否相等
     * @param other 另一個平面
     * @param tolerance 容差 (預設 1e-6)
     * @return 若相等則回傳 true
     */
    bool equals(const CustomPlane& other, double tolerance = 1e-6) const;
    
    /**
     * @brief 相等運算子
     */
    bool operator==(const CustomPlane& other) const;
    
    /**
     * @brief 不等運算子
     */
    bool operator!=(const CustomPlane& other) const;
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_GEOMETRY_CUSTOMPLANE_H