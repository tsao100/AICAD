/**
 * @file Sketch.h
 * @brief 2D 草圖特徵
 * @author Ben
 * @date 2024-12-04
 */

#ifndef AICAD_CAD_FEATURES_SKETCH_H
#define AICAD_CAD_FEATURES_SKETCH_H

#include "Feature.h"
#include "../geometry/CustomPlane.h"
#include <QVector2D>
#include <QVector>

namespace aicad {
namespace cad {

/**
 * @brief 2D 草圖特徵
 * 
 * Sketch 表示在一個平面上繪製的 2D 幾何圖形集合。
 * 支援多條折線 (polylines)，每條折線由一系列 2D 點組成。
 * 
 * 使用範例:
 * @code
 * Sketch* sketch = new Sketch(CustomPlane::XY());
 * sketch->setName("Sketch1");
 * 
 * // 新增矩形
 * QVector<QVector2D> rect;
 * rect << QVector2D(0, 0) << QVector2D(100, 0)
 *      << QVector2D(100, 50) << QVector2D(0, 50)
 *      << QVector2D(0, 0);
 * sketch->addPolyline(rect);
 * 
 * // 計算形狀
 * TopoDS_Shape shape = sketch->compute();
 * @endcode
 */
class Sketch : public Feature {
    Q_OBJECT
    
public:
    /**
     * @brief 建構子
     * @param plane 草圖平面
     * @param parent 父物件
     */
    explicit Sketch(const CustomPlane& plane, QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~Sketch() override;
    
    /**
     * @brief 取得特徵類型
     */
    FeatureType type() const override { return FeatureType::Sketch; }
    
    /**
     * @brief 計算草圖形狀
     * @return 草圖的線框 (TopoDS_Wire 或 TopoDS_Compound)
     */
    TopoDS_Shape compute() override;
    
    /**
     * @brief 取得草圖平面
     */
    CustomPlane plane() const;
    
    /**
     * @brief 設定草圖平面
     * @param plane 新的平面
     * 
     * @note 設定平面會標記草圖為無效，需要重新計算
     */
    void setPlane(const CustomPlane& plane);
    
    /**
     * @brief 新增折線
     * @param points 2D 點列表 (相對於草圖平面)
     * 
     * 點的座標是相對於草圖平面的 2D 座標 (u, v)
     */
    void addPolyline(const QVector<QVector2D>& points);
    
    /**
     * @brief 取得所有折線
     * @return 折線列表
     */
    QVector<QVector<QVector2D>> polylines() const;
    
    /**
     * @brief 取得第 i 條折線
     * @param index 索引
     * @return 折線點列表
     */
    QVector<QVector2D> polyline(int index) const;
    
    /**
     * @brief 取得折線數量
     */
    int polylineCount() const;
    
    /**
     * @brief 移除第 i 條折線
     * @param index 索引
     */
    void removePolyline(int index);
    
    /**
     * @brief 清除所有幾何
     */
    void clear();
    
    /**
     * @brief 檢查草圖是否為空
     */
    bool isEmpty() const;
    
Q_SIGNALS:
    /**
     * @brief 幾何改變時發出
     */
    void geometryChanged();
    
    /**
     * @brief 平面改變時發出
     */
    void planeChanged(const CustomPlane& plane);
    
private:
    /**
     * @brief 計算單一折線的 Wire
     * @param points 2D 點列表
     * @return TopoDS_Wire 或空形狀
     */
    TopoDS_Shape computeSingleWire(const QVector<QVector2D>& points);

    class Private;
    Private* d;
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_FEATURES_SKETCH_H
