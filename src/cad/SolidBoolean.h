/**
 * @file SolidBoolean.h
 * @brief 實體之間的布林運算：基底實體 A 與工具實體 B 做聯集／差集／交集。
 *        A、B 都變成中間結果（被隱藏），只顯示運算結果。
 */
#pragma once

#include "Feature.h"
#include "SolidOps.h"

#include <QPointer>
#include <QMetaObject>

namespace aicad {
namespace cad {

class SolidBoolean : public Feature {
    Q_OBJECT
public:
    explicit SolidBoolean(Document* parent = nullptr);
    ~SolidBoolean() override = default;

    FeatureType type() const override { return FeatureType::Boolean; }
    bool rebuild() override;

    /// op 只接受 Fuse／Cut／Common（Cut = A − B）
    SolidOp operation() const { return m_op; }
    void setOperation(SolidOp op) { m_op = (op == SolidOp::NewBody) ? SolidOp::Fuse : op; }

    Feature* baseFeature() const { return m_base.data(); }
    Feature* toolFeature() const { return m_tool.data(); }
    void setFeatures(Feature* base, Feature* tool);

    QSet<QString> featureDependencies() const override;
    void resolveReferences(Document* doc);

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;

private:
    void rewire(QPointer<Feature>& slot, QMetaObject::Connection& conn, Feature* f);

    SolidOp                 m_op = SolidOp::Fuse;
    QPointer<Feature>       m_base;
    QPointer<Feature>       m_tool;
    QMetaObject::Connection m_baseConn;
    QMetaObject::Connection m_toolConn;
    QString                 m_pendingBaseId;
    QString                 m_pendingToolId;
};

} // namespace cad
} // namespace aicad
