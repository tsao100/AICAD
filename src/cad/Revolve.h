/**
 * @file Revolve.h
 * @brief 旋轉特徵：把草圖輪廓繞一條軸旋轉成實體。
 *
 * 旋轉軸（三選一，都在輪廓所在的草圖內）：
 *   • 草圖 X 軸
 *   • 草圖 Y 軸
 *   • 草圖內的任一條直線（建議畫成「建構線／中心線」，這樣不會被當成輪廓的一部分）
 *
 * 角度為度數（可用參數運算式），範圍 −360° ~ 360°；360° = 整圈。
 * 選項：反向、對稱（以輪廓所在平面為中心兩側各一半）。
 * 運算方式（新實體／聯集／挖除／交集）與目標實體見 ShapingFeature。
 */
#pragma once

#include "ShapingFeature.h"
#include "Sketch.h"
#include "core/ParameterStore.h"

#include <QPointer>
#include <gp_Ax1.hxx>

namespace aicad {
namespace cad {

class Revolve : public ShapingFeature {
    Q_OBJECT
public:
    enum class AxisKind { SketchX = 0, SketchY = 1, SketchLine = 2 };

    explicit Revolve(Document* parent = nullptr);
    ~Revolve() override = default;

    FeatureType type() const override { return FeatureType::Revolve; }
    bool rebuild() override;

    // ── 參數 ──
    Sketch* sketch() const { return m_sketch.data(); }
    void setSketch(Sketch* sketch);

    int regionIndex() const { return m_regionIndex; }      ///< -1 = 全部區域
    AxisKind axisKind() const { return m_axisKind; }
    QString axisLineUuid() const { return m_axisLineUuid; }
    QString angleExpression() const { return m_angleExpr.expression; }
    double angle() const { return m_angleExpr.cachedValue; }
    bool isReversed() const { return m_reversed; }
    bool isSymmetric() const { return m_symmetric; }

    /**
     * @brief 一次設定全部參數（不觸發重建；呼叫端接著 markDirty()）。
     * @return false = 運算式無法求值或角度不合理（完全不修改）
     */
    bool setParameters(const QString& angleExpr, AxisKind kind, const QString& axisLineUuid,
                       bool reversed, bool symmetric, int regionIndex, QString* error = nullptr);

    QSet<QString> featureDependencies() const override;
    void resolveReferences(Document* doc);

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;

private:
    bool resolveAxis(gp_Ax1& out, QString* err) const;

    QPointer<Sketch>    m_sketch;
    QString             m_pendingSketchId;
    int                 m_regionIndex = -1;
    AxisKind            m_axisKind = AxisKind::SketchY;
    QString             m_axisLineUuid;
    core::ParameterExpr m_angleExpr{360.0};
    bool                m_reversed  = false;
    bool                m_symmetric = false;
};

} // namespace cad
} // namespace aicad
