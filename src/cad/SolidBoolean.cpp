/**
 * @file SolidBoolean.cpp
 */
#include "SolidBoolean.h"
#include "Document.h"

#include <QDebug>
#include <QJsonObject>

namespace aicad {
namespace cad {

SolidBoolean::SolidBoolean(Document* parent)
    : Feature(parent)
{
    setName("Boolean");
}

void SolidBoolean::rewire(QPointer<Feature>& slot, QMetaObject::Connection& conn, Feature* f)
{
    if (slot.data() == f) return;
    if (slot) disconnect(conn);
    slot = f;
    if (slot) {
        conn = connect(slot.data(), &Feature::shapeChanged, this, [this]() {
            if (Document* doc = document()) doc->rebuildFeature(this);
            else rebuild();
        });
    }
}

void SolidBoolean::setFeatures(Feature* base, Feature* tool)
{
    rewire(m_base, m_baseConn, base);
    rewire(m_tool, m_toolConn, tool);
    m_pendingBaseId.clear();
    m_pendingToolId.clear();
    setFeatureParent(m_base.data());       // 特徵樹：結果掛在基底實體底下
}

bool SolidBoolean::rebuild()
{
    if (!m_base || !m_tool) { setError(QStringLiteral("請指定基底實體與工具實體")); return false; }
    if (m_base.data() == m_tool.data()) {
        setError(QStringLiteral("基底與工具不可為同一個實體"));
        return false;
    }

    const TopoDS_Shape a = m_base->shape();
    const TopoDS_Shape b = m_tool->shape();
    if (a.IsNull() || b.IsNull()) {
        setError(QStringLiteral("基底或工具實體目前沒有有效的形狀"));
        return false;
    }

    QString err;
    TopoDS_Shape r = solidops::applyOp(a, b, m_op, &err);
    if (r.IsNull()) {
        setError(err.isEmpty() ? QStringLiteral("布林運算失敗") : err);
        return false;
    }
    r = solidops::finalizeSolid(r, &err);
    if (r.IsNull()) {
        setError(err.isEmpty() ? QStringLiteral("運算結果沒有任何實體") : err);
        return false;
    }
    setShape(r);
    return true;
}

QSet<QString> SolidBoolean::featureDependencies() const
{
    QSet<QString> deps;
    if (m_base) deps.insert(m_base->id());
    if (m_tool) deps.insert(m_tool->id());
    return deps;
}

void SolidBoolean::resolveReferences(Document* doc)
{
    if (!doc) return;
    if (m_pendingBaseId.isEmpty() && m_pendingToolId.isEmpty()) return;

    Feature* a = m_pendingBaseId.isEmpty() ? m_base.data() : doc->findFeature(m_pendingBaseId);
    Feature* b = m_pendingToolId.isEmpty() ? m_tool.data() : doc->findFeature(m_pendingToolId);
    if (!a || !b) {
        qWarning() << "[SolidBoolean]" << name() << "cannot resolve base/tool";
        return;
    }
    setFeatures(a, b);
    doc->refreshFeatureDependencies(this);
}

QJsonObject SolidBoolean::toJson() const
{
    QJsonObject obj = Feature::toJson();
    obj["operation"] = solidops::opToString(m_op);
    obj["baseId"] = m_base ? m_base->id() : m_pendingBaseId;
    obj["toolId"] = m_tool ? m_tool->id() : m_pendingToolId;
    return obj;
}

bool SolidBoolean::fromJson(const QJsonObject& json)
{
    if (!Feature::fromJson(json)) return false;
    setOperation(solidops::opFromString(json["operation"].toString()));
    m_pendingBaseId = json["baseId"].toString();
    m_pendingToolId = json["toolId"].toString();
    return true;
}

} // namespace cad
} // namespace aicad
