/**
 * @file SolidModifier.h
 * @brief 修改既有實體的特徵：3D 圓角（FilletSolid）與抽殼（ShellSolid）。
 *
 * 兩者都有「來源實體」（比照 ChamferSolid）：來源 shape 變動時自動重建，
 * 被選的邊／面以幾何簽章定址（重建後的新 shape 內重新配對）。
 */
#pragma once

#include "Feature.h"
#include "SolidOps.h"

#include <QPointer>
#include <QVector>
#include <QMetaObject>

namespace aicad {
namespace cad {

/// 圓角與抽殼共用的「單一來源」處理
class SolidModifier : public Feature {
    Q_OBJECT
public:
    explicit SolidModifier(Document* parent = nullptr);
    ~SolidModifier() override = default;

    void setSourceFeature(Feature* src);
    Feature* sourceFeature() const { return m_source.data(); }
    QString sourceFeatureId() const { return m_source ? m_source->id() : m_pendingSourceId; }

    QSet<QString> featureDependencies() const override;
    void resolveReferences(Document* doc);

protected:
    void writeSourceJson(QJsonObject& obj) const;
    void readSourceJson(const QJsonObject& obj);

    QPointer<Feature>       m_source;
    QMetaObject::Connection m_sourceConn;
    QString                 m_pendingSourceId;
};

// ───────────────────────── 3D 圓角 ─────────────────────────
class FilletSolid : public SolidModifier {
    Q_OBJECT
public:
    explicit FilletSolid(Document* parent = nullptr);

    FeatureType type() const override { return FeatureType::Fillet; }
    bool rebuild() override;

    double radius() const { return m_radius; }
    void setRadius(double r) { m_radius = r; }

    void setEdgeSignatures(const QVector<EdgeSignature>& s) { m_edges = s; }
    const QVector<EdgeSignature>& edgeSignatures() const { return m_edges; }

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;

private:
    double m_radius = 1.0;
    QVector<EdgeSignature> m_edges;
};

// ───────────────────────── 抽殼 ─────────────────────────
class ShellSolid : public SolidModifier {
    Q_OBJECT
public:
    explicit ShellSolid(Document* parent = nullptr);

    FeatureType type() const override { return FeatureType::Shell; }
    bool rebuild() override;

    /// 壁厚。正 = 向內抽殼（外形不變），負 = 向外加厚
    double thickness() const { return m_thickness; }
    void setThickness(double t) { m_thickness = t; }

    void setFaceSignatures(const QVector<FaceSignature>& s) { m_faces = s; }
    const QVector<FaceSignature>& faceSignatures() const { return m_faces; }

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;

private:
    double m_thickness = 1.0;
    QVector<FaceSignature> m_faces;
};

} // namespace cad
} // namespace aicad
