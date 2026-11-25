/**
 * @file HoleFeature.h
 * @brief 孔精靈：用草圖裡的點（與圓的圓心）定位，在目標實體上鑽出簡單孔／沉頭孔／錐孔。
 *
 * 使用方式：先在要打孔的面（或平行於它的平面）上畫一張草圖，在草圖裡放點（或畫圓，取圓心）。
 * 孔的方向：預設自動朝向實體內部（沿草圖法向的正或反方向，取起點落在實體內的那一邊），
 *          必要時可再勾「反向」。
 * 本特徵固定是「挖除」，繼承 ShapingFeature 以共用目標實體處理；
 * 重建時把「工具形狀」（所有孔的聯集）記起來，供陣列／鏡射的「特徵」模式重複。
 */
#pragma once

#include "ShapingFeature.h"
#include "Sketch.h"

#include <QPointer>

namespace aicad {
namespace cad {

class HoleFeature : public ShapingFeature {
    Q_OBJECT
public:
    enum class HoleType { Simple = 0, Counterbore = 1, Countersink = 2 };

    struct Params {
        HoleType type = HoleType::Simple;
        double diameter = 6.0;
        bool   throughAll = true;
        double depth = 10.0;           ///< 非貫穿時，從表面起算的深度
        double cboreDiameter = 11.0;   ///< 沉頭孔：沉孔直徑
        double cboreDepth = 6.5;       ///< 沉頭孔：沉孔深度
        double csinkDiameter = 12.0;   ///< 錐孔：表面處的直徑
        double csinkAngle = 90.0;      ///< 錐孔：錐角（全角，度）
        bool   flip = false;           ///< 反向（在自動方向之後再反一次）
        QString spec;                  ///< 規格說明（例如 "M6 通孔"），僅供顯示與重新編輯
    };

    explicit HoleFeature(Document* parent = nullptr);

    FeatureType type() const override { return FeatureType::Hole; }
    bool rebuild() override;

    Sketch* sketch() const { return m_sketch.data(); }
    void setSketch(Sketch* sketch);

    const Params& params() const { return m_p; }
    void setParams(const Params& p) { m_p = p; }

    /// 檢查參數是否合理；不合理回傳 false 並填入訊息
    static bool validate(const Params& p, QString* err);

    QSet<QString> featureDependencies() const override;
    void resolveReferences(Document* doc);

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;

private:
    QPointer<Sketch> m_sketch;
    QString          m_pendingSketchId;
    Params           m_p;
};

} // namespace cad
} // namespace aicad
