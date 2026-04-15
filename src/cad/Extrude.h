/**
 * @file Extrude.h
 * @brief 擠出特徵類別
 * @author AICAD Team
 * @date 2025-01-08
 */

#ifndef AICAD_CAD_EXTRUDE_H
#define AICAD_CAD_EXTRUDE_H

#include "Feature.h"
#include "core/ParameterStore.h"

namespace aicad {
namespace cad {

class Sketch;

/**
 * @brief 擠出特徵
 * 
 * 將 2D 草圖擠出成 3D 實體
 * - 支援正向和反向擠出
 * - 支援對稱擠出
 * - 支援錐度角
 * 
 * 使用範例:
 * @code
 * Sketch* sketch = doc->createSketch(Plane::xy());
 * sketch->addRectangle(QVector2D(0, 0), QVector2D(100, 50));
 * 
 * Extrude* extrude = new Extrude(doc);
 * extrude->setSketch(sketch);
 * extrude->setHeight(25.0);
 * extrude->rebuild();
 * @endcode
 */
class Extrude : public Feature {
    Q_OBJECT
    Q_PROPERTY(double height READ height WRITE setHeight NOTIFY heightChanged)
    Q_PROPERTY(bool reversed READ isReversed WRITE setReversed NOTIFY reversedChanged)
    
public:
    /**
     * @brief 建構子
     */
    explicit Extrude(Document* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~Extrude() override;
    
    /**
     * @brief 取得特徵類型
     */
    FeatureType type() const override { return FeatureType::Extrude; }
    
    /**
     * @brief 重建特徵
     */
    bool rebuild() override;
    
    // 參數設定
    Sketch* sketch() const { return m_sketch; }
    void setSketch(Sketch* sketch);
    
    double height() const { return m_heightExpr.cachedValue; }
    void setHeight(double height);
    
    bool isReversed() const { return m_reversed; }
    void setReversed(bool reversed);
    
    bool isSymmetric() const { return m_symmetric; }
    void setSymmetric(bool symmetric);
    
    double draftAngle() const { return m_draftAngle; }
    void setDraftAngle(double angle);
    
    /**
     * @brief 序列化
     */
    QJsonObject toJson() const override;
    
    /**
     * @brief 反序列化
     */
    bool fromJson(const QJsonObject& json) override;

    // ← 新增：表達式介面
    QString heightExpression() const { return m_heightExpr.expression; }
    void    setHeightExpression(const QString& expr);

    // ← 新增：覆寫依賴宣告
    QSet<QString> featureDependencies() const override;

Q_SIGNALS:
    /**
     * @brief 高度改變時發出
     */
    void heightChanged(double height);
    
    /**
     * @brief 方向反轉時發出
     */
    void reversedChanged(bool reversed);
    
    /**
     * @brief 草圖改變時發出
     */
    void sketchChanged(Sketch* sketch);

private:
    Sketch* m_sketch;           ///< 參考草圖
    core::ParameterExpr m_heightExpr;   /// ← 設計意圖（表達式）
    bool m_reversed;            ///< 是否反向
    bool m_symmetric;           ///< 是否對稱
    double m_draftAngle;        ///< 拔模角度（未來實作）
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_EXTRUDE_H
