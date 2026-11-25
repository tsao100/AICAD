/**
 * @file SolidPatterns.h
 * @brief 3D 陣列（線性／環狀）與鏡射。
 *
 * 兩者共用 SolidTransformCopy：把來源重複成若干個「變換後的副本」，再和來源合併。
 * 兩種模式：
 *   • Body    ：複製「整個實體」，結果 = 來源 ∪ 副本們
 *   • Feature ：來源是一個有目標的成形特徵（挖除／加料，例如孔、擠出切除），
 *               重複它的「工具形狀」，結果 = 來源結果 再挖除（或加上）副本工具們
 *               ——這就是一般 CAD 說的「特徵陣列／特徵鏡射」。
 */
#pragma once

#include "SolidModifier.h"

#include <QVector>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

namespace aicad {
namespace cad {

class SolidTransformCopy : public SolidModifier {
    Q_OBJECT
public:
    enum class Mode { Body = 0, Feature = 1 };

    explicit SolidTransformCopy(Document* parent = nullptr);

    Mode mode() const { return m_mode; }
    void setMode(Mode m) { m_mode = m; }

    bool rebuild() override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;

protected:
    /// 產生各副本的變換（不含原件本身的單位變換）
    virtual bool buildTransforms(QVector<gp_Trsf>& out, QString* err) const = 0;
    /// Body 模式下，結果是否保留原件（鏡射可選「只留鏡像」）
    virtual bool keepOriginal() const { return true; }
    virtual void writeParams(QJsonObject& obj) const = 0;
    virtual void readParams(const QJsonObject& obj) = 0;

private:
    Mode m_mode = Mode::Body;
};

// ───────────────────────── 陣列 ─────────────────────────
struct PatternParams {
    enum class Kind { Linear = 0, Circular = 1 };
    Kind kind = Kind::Linear;

    // 線性：方向 1（必要）與方向 2（選用，形成矩形陣列）。count 含原件本身。
    gp_Vec dir1 = gp_Vec(1, 0, 0);
    int    count1 = 2;
    double spacing1 = 10.0;
    bool   useDir2 = false;
    gp_Vec dir2 = gp_Vec(0, 1, 0);
    int    count2 = 1;
    double spacing2 = 10.0;

    // 環狀：繞「通過 axisOrigin、方向 axisDir」的軸；count 含原件。
    // totalAngle ≥ 360 → 等分整圈（每份 total/count）；否則第一個到最後一個跨 totalAngle（每份 total/(count-1)）。
    gp_Pnt axisOrigin = gp_Pnt(0, 0, 0);
    gp_Vec axisDir = gp_Vec(0, 0, 1);
    int    count = 4;
    double totalAngle = 360.0;
};

class SolidPattern : public SolidTransformCopy {
    Q_OBJECT
public:
    explicit SolidPattern(Document* parent = nullptr);

    FeatureType type() const override { return FeatureType::SolidPattern; }

    const PatternParams& params() const { return m_p; }
    void setParams(const PatternParams& p) { m_p = p; }

protected:
    bool buildTransforms(QVector<gp_Trsf>& out, QString* err) const override;
    void writeParams(QJsonObject& obj) const override;
    void readParams(const QJsonObject& obj) override;

private:
    PatternParams m_p;
};

// ───────────────────────── 鏡射 ─────────────────────────
class SolidMirror : public SolidTransformCopy {
    Q_OBJECT
public:
    explicit SolidMirror(Document* parent = nullptr);

    FeatureType type() const override { return FeatureType::Mirror; }

    gp_Pnt planePoint() const { return m_point; }
    gp_Vec planeNormal() const { return m_normal; }
    void setPlane(const gp_Pnt& p, const gp_Vec& n) { m_point = p; m_normal = n; }

    void setKeepOriginal(bool k) { m_keepOriginal = k; }
    bool isKeepOriginal() const { return m_keepOriginal; }

protected:
    bool buildTransforms(QVector<gp_Trsf>& out, QString* err) const override;
    bool keepOriginal() const override { return m_keepOriginal; }
    void writeParams(QJsonObject& obj) const override;
    void readParams(const QJsonObject& obj) override;

private:
    gp_Pnt m_point = gp_Pnt(0, 0, 0);
    gp_Vec m_normal = gp_Vec(1, 0, 0);
    bool   m_keepOriginal = true;
};

} // namespace cad
} // namespace aicad
