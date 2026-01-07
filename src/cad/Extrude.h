/**
 * @file Extrude.h
 * @brief 擠出特徵類別，從草圖建立 3D 實體
 * @author Ben
 * @date 2025-01-06
 */

#ifndef AICAD_CAD_EXTRUDE_H
#define AICAD_CAD_EXTRUDE_H

#include "Feature.h"

namespace aicad {
namespace cad {

class Sketch;

/**
 * @brief 擠出方向
 */
enum class ExtrudeDirection {
    Normal,      // 沿草圖法向
    Reversed,    // 反向
    Symmetric    // 對稱
};

/**
 * @brief 擠出特徵類別
 * 
 * Extrude 從 2D 草圖建立 3D 實體:
 * - 參考基準草圖
 * - 設定擠出距離和方向
 * - 生成 3D 幾何
 * - 支援布林運算 (未來擴充)
 * 
 * 使用範例:
 * @code
 * Sketch* sketch = doc->createSketch("XY");
 * sketch->addRectangle(QVector2D(0, 0), QVector2D(10, 10));
 * 
 * Extrude* extrude = dynamic_cast<Extrude*>(
 *     doc->createExtrude(sketch, 5.0)
 * );
 * extrude->setDirection(ExtrudeDirection::Symmetric);
 * @endcode
 */
class Extrude : public Feature {
    Q_OBJECT
    Q_PROPERTY(double distance READ distance WRITE setDistance NOTIFY distanceChanged)
    Q_PROPERTY(ExtrudeDirection direction READ direction WRITE setDirection NOTIFY directionChanged)
    
public:
    /**
     * @brief 建構子
     * @param doc 父文件
     * @param label OCCT 標籤
     * @param sketch 基準草圖
     * @param distance 擠出距離
     */
    explicit Extrude(Document* doc, TDF_Label label, 
                     Sketch* sketch, double distance);
    
    /**
     * @brief 解構子
     */
    ~Extrude() override;
    
    /**
     * @brief 取得特徵類型
     */
    FeatureType type() const override { return FeatureType::Extrude; }
    
    /**
     * @brief 取得基準草圖
     */
    Sketch* sketch() const;
    
    /**
     * @brief 取得擠出距離
     */
    double distance() const;
    
    /**
     * @brief 設定擠出距離
     */
    void setDistance(double distance);
    
    /**
     * @brief 取得擠出方向
     */
    ExtrudeDirection direction() const;
    
    /**
     * @brief 設定擠出方向
     */
    void setDirection(ExtrudeDirection direction);
    
    /**
     * @brief 重建特徵
     */
    bool rebuild() override;
    
    /**
     * @brief 計算擠出體積
     */
    double volume() const;
    
    /**
     * @brief 計算擠出表面積
     */
    double surfaceArea() const;
    
Q_SIGNALS:
    /**
     * @brief 距離改變時發出
     */
    void distanceChanged(double distance);
    
    /**
     * @brief 方向改變時發出
     */
    void directionChanged(ExtrudeDirection direction);
    
private:
    /**
     * @brief 從草圖建立 3D 形狀
     */
    TopoDS_Shape createExtrusionShape();
    
    class Private;
    Private* d;
};

/**
 * @brief 將擠出方向轉換為字串
 */
QString extrudeDirectionToString(ExtrudeDirection direction);

/**
 * @brief 將字串轉換為擠出方向
 */
ExtrudeDirection stringToExtrudeDirection(const QString& str);

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_EXTRUDE_H