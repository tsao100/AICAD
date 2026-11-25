/**
 * @file SketchLoft.h
 * @brief 草圖放樣特徵：依序通過多個草圖的輪廓（斷面）建立實體。
 *
 * 每個草圖取面積最大的封閉輪廓之外環當斷面。各斷面的邊數、起點位置不同時
 * 仍可放樣，但斷面形狀相近、起點對齊時結果最乾淨。
 * 選項：直紋（相鄰斷面間用直線連接，否則為平滑曲面）。
 * 運算方式（新實體／聯集／挖除／交集）見 ShapingFeature。
 *
 * 注意：這與 FeatureType::Loft（鐵道斷面放樣 ProfileLoftSolid）是不同的特徵。
 */
#pragma once

#include "ShapingFeature.h"
#include "Sketch.h"

#include <QList>
#include <QPointer>
#include <QStringList>

namespace aicad {
namespace cad {

class SketchLoft : public ShapingFeature {
    Q_OBJECT
public:
    explicit SketchLoft(Document* parent = nullptr);
    ~SketchLoft() override = default;

    FeatureType type() const override { return FeatureType::SketchLoft; }
    bool rebuild() override;

    QList<Sketch*> sections() const;
    void setSections(const QList<Sketch*>& sections);

    bool isRuled() const { return m_ruled; }
    void setRuled(bool r) { m_ruled = r; }

    QSet<QString> featureDependencies() const override;
    void resolveReferences(Document* doc);

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;

private:
    QList<QPointer<Sketch>> m_sections;
    QStringList             m_pendingIds;
    bool                    m_ruled = false;
};

} // namespace cad
} // namespace aicad
