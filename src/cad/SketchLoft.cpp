/**
 * @file SketchLoft.cpp
 */
#include "SketchLoft.h"
#include "Document.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

#include <BRepOffsetAPI_ThruSections.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Wire.hxx>

namespace aicad {
namespace cad {

SketchLoft::SketchLoft(Document* parent)
    : ShapingFeature(parent)
{
    setName("Loft");
}

QList<Sketch*> SketchLoft::sections() const
{
    QList<Sketch*> out;
    for (const QPointer<Sketch>& p : m_sections)
        if (p) out.append(p.data());
    return out;
}

void SketchLoft::setSections(const QList<Sketch*>& sections)
{
    for (const QPointer<Sketch>& p : m_sections)
        if (p) disconnect(p.data(), nullptr, this, nullptr);
    m_sections.clear();

    for (Sketch* sk : sections) {
        if (!sk) continue;
        m_sections.append(sk);
        connect(sk, &Sketch::shapeChanged,    this, &SketchLoft::rebuildRequested);
        connect(sk, &Sketch::geometryChanged, this, &SketchLoft::rebuildRequested);
    }
}

bool SketchLoft::rebuild()
{
    const QList<Sketch*> secs = sections();
    if (secs.size() < 2) {
        setError(QStringLiteral("放樣至少需要 2 個斷面草圖"));
        return false;
    }

    QString err;
    try {
        BRepOffsetAPI_ThruSections gen(Standard_True, m_ruled ? Standard_True : Standard_False);
        gen.CheckCompatibility(Standard_False);

        for (Sketch* sk : secs) {
            TopoDS_Wire w;
            if (!solidops::sectionWireFromSketch(sk, w, &err)) {
                setError(err.isEmpty() ? QStringLiteral("斷面「%1」沒有封閉輪廓").arg(sk->name()) : err);
                return false;
            }
            gen.AddWire(w);
        }

        gen.Build();
        if (!gen.IsDone() || gen.Shape().IsNull()) {
            setError(QStringLiteral("放樣失敗：請確認各斷面都是封閉輪廓，且順序正確"));
            return false;
        }

        TopoDS_Shape tool = solidops::finalizeSolid(gen.Shape(), &err);
        if (tool.IsNull()) {
            setError(err.isEmpty() ? QStringLiteral("放樣結果無效") : err);
            return false;
        }

        TopoDS_Shape result = combineWithTarget(tool);
        if (result.IsNull()) return false;

        if (operation() != SolidOp::NewBody) {
            result = solidops::finalizeSolid(result, &err);
            if (result.IsNull()) {
                setError(err.isEmpty() ? QStringLiteral("運算結果沒有任何實體") : err);
                return false;
            }
        }

        setShape(result);
        return true;

    } catch (const Standard_Failure& ex) {
        setError(QStringLiteral("放樣發生 OCC 例外：%1")
                     .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown"));
        return false;
    }
}

QSet<QString> SketchLoft::featureDependencies() const
{
    QSet<QString> deps = targetDependencies();
    for (const QPointer<Sketch>& p : m_sections)
        if (p) deps.insert(p->id());
    return deps;
}

void SketchLoft::resolveReferences(Document* doc)
{
    if (!doc) return;
    resolveTargetReference(doc);

    if (!m_pendingIds.isEmpty()) {
        QList<Sketch*> secs;
        for (const QString& id : m_pendingIds) {
            if (auto* sk = qobject_cast<Sketch*>(doc->findFeature(id))) {
                secs.append(sk);
                if (!sk->parent()) sk->setFeatureParent(this);
            } else {
                qWarning() << "[SketchLoft]" << name() << "cannot resolve section:" << id;
            }
        }
        setSections(secs);
        m_pendingIds.clear();
    }
    doc->refreshFeatureDependencies(this);
}

QJsonObject SketchLoft::toJson() const
{
    QJsonObject obj = Feature::toJson();
    QJsonArray ids;
    if (m_sections.isEmpty() && !m_pendingIds.isEmpty()) {
        for (const QString& id : m_pendingIds) ids.append(id);
    } else {
        for (const QPointer<Sketch>& p : m_sections)
            if (p) ids.append(p->id());
    }
    obj["sectionIds"] = ids;
    obj["ruled"]      = m_ruled;
    writeOpJson(obj);
    return obj;
}

bool SketchLoft::fromJson(const QJsonObject& json)
{
    if (!Feature::fromJson(json)) return false;
    m_pendingIds.clear();
    for (const QJsonValue& v : json["sectionIds"].toArray())
        m_pendingIds.append(v.toString());
    m_ruled = json["ruled"].toBool(false);
    readOpJson(json);
    return true;
}

} // namespace cad
} // namespace aicad
