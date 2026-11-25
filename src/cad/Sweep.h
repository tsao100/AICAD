/**
 * @file Sweep.h
 * @brief 掃掠特徵：把「輪廓草圖」沿「路徑草圖」掃出實體。
 *
 * 路徑草圖內所有曲線會自動接成一條連續路徑（可開放或封閉）。
 * 輪廓通常畫在路徑起點、且垂直於路徑的平面上；輪廓不在起點時，
 * 會保持輪廓與路徑起點的相對位置一起沿路徑移動。
 *
 * 方位模式：
 *   • CorrectedFrenet：校正過的 Frenet 標架（預設，最穩定）
 *   • Frenet         ：標準 Frenet 標架（遇到直線段可能不穩）
 *   • Fixed          ：輪廓方向固定，只平移（適合直線路徑）
 * 運算方式（新實體／聯集／挖除／交集）見 ShapingFeature。
 */
#pragma once

#include "ShapingFeature.h"
#include "Sketch.h"

#include <QPointer>

namespace aicad {
namespace cad {

class Sweep : public ShapingFeature {
    Q_OBJECT
public:
    enum class Orientation { CorrectedFrenet = 0, Frenet = 1, Fixed = 2 };

    explicit Sweep(Document* parent = nullptr);
    ~Sweep() override = default;

    FeatureType type() const override { return FeatureType::Sweep; }
    bool rebuild() override;

    Sketch* profileSketch() const { return m_profile.data(); }
    Sketch* pathSketch() const { return m_path.data(); }
    void setProfileSketch(Sketch* sketch);
    void setPathSketch(Sketch* sketch);

    int regionIndex() const { return m_regionIndex; }          ///< -1 = 全部區域
    void setRegionIndex(int i) { m_regionIndex = i; }
    Orientation orientation() const { return m_orientation; }
    void setOrientation(Orientation o) { m_orientation = o; }

    QSet<QString> featureDependencies() const override;
    void resolveReferences(Document* doc);

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;

private:
    QPointer<Sketch> m_profile;
    QPointer<Sketch> m_path;
    QString          m_pendingProfileId;
    QString          m_pendingPathId;
    int              m_regionIndex = -1;
    Orientation      m_orientation = Orientation::CorrectedFrenet;
};

} // namespace cad
} // namespace aicad
