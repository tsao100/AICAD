/**
 * @file ShapingFeature.cpp
 */
#include "ShapingFeature.h"
#include "Document.h"

#include <QDebug>
#include <QJsonObject>

namespace aicad {
namespace cad {

ShapingFeature::ShapingFeature(Document* parent)
    : Feature(parent)
{
}

void ShapingFeature::setOperationAndTarget(SolidOp op, Feature* target)
{
    if (op == SolidOp::NewBody) target = nullptr;
    if (target == this) target = nullptr;       // 不可以自己當自己的目標

    m_op = op;
    m_pendingTargetId.clear();

    if (m_target.data() != target) {
        if (m_target) QObject::disconnect(m_targetConn);
        m_target = target;

        if (m_target) {
            // 比照 ChamferSolid：目標 shape 一變就走 Document::rebuildFeature() 重建，
            // 這樣 CadView 才會收到 featureShapeUpdated 並重繪。
            m_targetConn = connect(
                m_target.data(), &Feature::shapeChanged,
                this, [this]() {
                    if (Document* doc = document()) doc->rebuildFeature(this);
                    else rebuild();
                });
        }
        setFeatureParent(m_target.data());
    }

    syncDependenciesAndTree();
}

void ShapingFeature::syncDependenciesAndTree()
{
    Document* doc = document();
    if (!doc || !doc->findFeature(id())) return;      // 還沒登記進文件：登記時會一併處理

    doc->refreshFeatureDependencies(this);
    doc->setTreeItemParent(id(), parent() ? parent()->id() : QString());
}

void ShapingFeature::resolveTargetReference(Document* doc)
{
    if (!doc || m_pendingTargetId.isEmpty()) return;

    if (Feature* t = doc->findFeature(m_pendingTargetId)) {
        setOperationAndTarget(m_op, t);               // 會清掉 m_pendingTargetId
    } else {
        qWarning() << "[ShapingFeature]" << name()
                   << "cannot resolve targetId:" << m_pendingTargetId;
    }
}

QSet<QString> ShapingFeature::targetDependencies() const
{
    QSet<QString> deps;
    if (m_target) deps.insert(m_target->id());
    return deps;
}

void ShapingFeature::writeOpJson(QJsonObject& obj) const
{
    obj["operation"] = solidops::opToString(m_op);
    obj["targetId"]  = targetId();
}

void ShapingFeature::readOpJson(const QJsonObject& obj)
{
    // 舊檔（沒有這兩個欄位）＝ NewBody，行為與以前完全相同
    m_op = solidops::opFromString(obj["operation"].toString());
    m_pendingTargetId = obj["targetId"].toString();
}

TopoDS_Shape ShapingFeature::combineWithTarget(const TopoDS_Shape& tool)
{
    m_lastTool = tool;
    if (m_op == SolidOp::NewBody) return tool;

    if (!m_target) {
        setError(QStringLiteral("尚未指定目標實體（%1需要一個目標實體）")
                     .arg(solidops::opLabel(m_op)));
        return TopoDS_Shape();
    }
    const TopoDS_Shape targetShape = m_target->shape();
    if (targetShape.IsNull()) {
        setError(QStringLiteral("目標實體「%1」目前沒有有效的形狀").arg(m_target->name()));
        return TopoDS_Shape();
    }

    QString err;
    const TopoDS_Shape result = solidops::applyOp(targetShape, tool, m_op, &err);
    if (result.IsNull())
        setError(err.isEmpty() ? QStringLiteral("布林運算失敗") : err);
    return result;
}

} // namespace cad
} // namespace aicad
