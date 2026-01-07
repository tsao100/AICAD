/**
 * @file Sketch.h
 * @brief 草圖特徵類別，管理 2D 幾何元素
 * @author Ben
 * @date 2025-01-06
 */

#ifndef AICAD_CAD_SKETCH_H
#define AICAD_CAD_SKETCH_H

#include "Feature.h"
#include <QVector2D>
#include <QVector3D>
#include <gp_Pln.hxx>
#include <gp_Ax2.hxx>

namespace aicad {
namespace cad {

/**
 * @brief 草圖平面定義
 */
struct SketchPlane {
    QVector3D origin;    // 原點
    QVector3D normal;    // 法向量
    QVector3D xAxis;     // X 軸方向
    QVector3D yAxis;     // Y 軸方向
    
    /**
     * @brief 建立 XY 平面
     */
    static SketchPlane XY();
    
    /**
     * @brief 建立 XZ 平面
     */
    static SketchPlane XZ();
    
    /**
     * @brief 建立 YZ 平面
     */
    static SketchPlane YZ();
    
    /**
     * @brief 轉換為 OCCT 平面
     */
    gp_Pln toGpPln() const;
    
    /**
     * @brief 轉換為 OCCT 座標系
     */
    gp_Ax2 toGpAx2() const;
    
    /**
     * @brief 2D 點轉換為 3D 世界座標
     */
    QVector3D toWorld(const QVector2D& point2D) const;
    
    /**
     * @brief 3D 世界座標投影到 2D
     */
    QVector2D toLocal(const QVector3D& point3D) const;
};

/**
 * @brief 草圖元素類型
 */
enum class SketchElementType {
    Line,
    Arc,
    Circle,
    Polyline
};

/**
 * @brief 草圖元素
 */
struct SketchElement {
    SketchElementType type;
    QVector<QVector2D> points;  // 端點或控制點
    QVariantMap parameters;      // 額外參數 (半徑、角度等)
};

/**
 * @brief 草圖特徵類別
 * 
 * Sketch 管理 2D 幾何元素:
 * - 定義草圖平面
 * - 儲存線條、圓弧等元素
 * - 提供幾何操作介面
 * - 支援約束系統 (未來擴充)
 * 
 * 使用範例:
 * @code
 * Sketch* sketch = doc->createSketch("XY");
 * sketch->addLine(QVector2D(0, 0), QVector2D(10, 0));
 * sketch->addRectangle(QVector2D(0, 0), QVector2D(20, 15));
 * sketch->rebuild();
 * @endcode
 */
class Sketch : public Feature {
    Q_OBJECT
    Q_PROPERTY(QString planeName READ planeName CONSTANT)
    Q_PROPERTY(int elementCount READ elementCount NOTIFY elementCountChanged)
    
public:
    /**
     * @brief 建構子
     * @param doc 父文件
     * @param label OCCT 標籤
     * @param plane 草圖平面
     */
    explicit Sketch(Document* doc, TDF_Label label, const SketchPlane& plane);
    
    /**
     * @brief 解構子
     */
    ~Sketch() override;
    
    /**
     * @brief 取得特徵類型
     */
    FeatureType type() const override { return FeatureType::Sketch; }
    
    /**
     * @brief 取得草圖平面
     */
    SketchPlane plane() const;
    
    /**
     * @brief 取得平面名稱
     */
    QString planeName() const;
    
    /**
     * @brief 新增直線
     * @param start 起點
     * @param end 終點
     * @return 元素索引
     */
    int addLine(const QVector2D& start, const QVector2D& end);
    
    /**
     * @brief 新增矩形
     * @param corner1 第一個角點
     * @param corner2 對角點
     * @return 元素索引
     */
    int addRectangle(const QVector2D& corner1, const QVector2D& corner2);
    
    /**
     * @brief 新增圓
     * @param center 圓心
     * @param radius 半徑
     * @return 元素索引
     */
    int addCircle(const QVector2D& center, double radius);
    
    /**
     * @brief 新增圓弧
     * @param center 圓心
     * @param radius 半徑
     * @param startAngle 起始角度 (度)
     * @param endAngle 結束角度 (度)
     * @return 元素索引
     */
    int addArc(const QVector2D& center, double radius, 
               double startAngle, double endAngle);
    
    /**
     * @brief 新增多段線
     * @param points 點列表
     * @param closed 是否閉合
     * @return 元素索引
     */
    int addPolyline(const QVector<QVector2D>& points, bool closed = false);
    
    /**
     * @brief 刪除元素
     * @param index 元素索引
     * @return 成功回傳 true
     */
    bool removeElement(int index);
    
    /**
     * @brief 清空所有元素
     */
    void clear();
    
    /**
     * @brief 取得元素數量
     */
    int elementCount() const;
    
    /**
     * @brief 取得所有元素
     */
    QVector<SketchElement> elements() const;
    
    /**
     * @brief 取得特定元素
     */
    SketchElement element(int index) const;
    
    /**
     * @brief 重建特徵
     */
    bool rebuild() override;
    
    /**
     * @brief 檢查草圖是否閉合
     */
    bool isClosed() const;
    
Q_SIGNALS:
    /**
     * @brief 元素數量改變時發出
     */
    void elementCountChanged(int count);
    
    /**
     * @brief 元素被新增時發出
     */
    void elementAdded(int index);
    
    /**
     * @brief 元素被移除時發出
     */
    void elementRemoved(int index);
    
private:
    /**
     * @brief 從 OCCT 標籤載入元素
     */
    void loadElements();
    
    /**
     * @brief 儲存元素到 OCCT 標籤
     */
    void saveElements();
    
    class Private;
    Private* d;
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_SKETCH_H