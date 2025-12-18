/**
 * @file Extrude.h
 * @brief 擠出特徵
 * @author Ben
 * @date 2024-12-04
 */

#ifndef AICAD_CAD_FEATURES_EXTRUDE_H
#define AICAD_CAD_FEATURES_EXTRUDE_H

#include "Feature.h"

namespace aicad {
namespace cad {

class Sketch;

/**
 * @brief 擠出特徵
 * 
 * Extrude 將一個 2D 草圖沿其法向量方向擠出，產生 3D 實體。
 * 
 * 使用範例:
 * @code
 * Sketch* sketch = new Sketch(CustomPlane::XY());
 * sketch->addPolyline(rectanglePoints);
 * sketch->update();
 * 
 * Extrude* extrude = new Extrude(sketch, 10.0);  // 擠出 10 單位
 * extrude->update();
 * 
 * TopoDS_Shape solid = extrude->shape();
 * @endcode
 */
class Extrude : public Feature {
    Q_OBJECT
    Q_PROPERTY(double height READ height WRITE setHeight NOTIFY heightChanged)
    
public:
    /**
     * @brief 建構子
     * @param sketch 要擠出的草圖
     * @param height 擠出高度（可為負值）
     * @param parent 父物件
     */
    explicit Extrude(Sketch* sketch, double height, QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~Extrude() override;
    
    /**
     * @brief 取得特徵類型
     */
    FeatureType type() const override { return FeatureType::Extrude; }
    
    /**
     * @brief 計算擠出形狀
     * @return 擠出的 3D 實體
     */
    TopoDS_Shape compute() override;
    
    /**
     * @brief 取得依賴的特徵（草圖）
     */
    QVector<Feature*> dependencies() const override;
    
    /**
     * @brief 取得草圖
     */
    Sketch* sketch() const;
    
    /**
     * @brief 設定草圖
     * @param sketch 新的草圖
     */
    void setSketch(Sketch* sketch);
    
    /**
     * @brief 取得擠出高度
     */
    double height() const;
    
    /**
     * @brief 設定擠出高度
     * @param height 新的高度（可為負值）
     */
    void setHeight(double height);
    
Q_SIGNALS:
    /**
     * @brief 高度改變時發出
     */
    void heightChanged(double height);
    
    /**
     * @brief 草圖改變時發出
     */
    void sketchChanged(Sketch* sketch);
    
private:
    class Private;
    Private* d;
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_FEATURES_EXTRUDE_H