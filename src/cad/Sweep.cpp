/**
 * @file Sweep.cpp
 */
#include "Sweep.h"
#include "Document.h"

#include <QDebug>
#include <QJsonObject>

#include <BRep_Builder.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <GeomFill_Trihedron.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Wire.hxx>

namespace aicad {
namespace cad {

Sweep::Sweep(Document* parent)
    : ShapingFeature(parent)
{
    setName("Sweep");
}

void Sweep::setProfileSketch(Sketch* sketch)
{
    if (m_profile.data() == sketch) return;
    if (m_profile) disconnect(m_profile.data(), nullptr, this, nullptr);
    m_profile = sketch;
    if (m_profile) {
        connect(m_profile.data(), &Sketch::shapeChanged,    this, &Sweep::rebuildRequested);
        connect(m_profile.data(), &Sketch::geometryChanged, this, &Sweep::rebuildRequested);
    }
}

void Sweep::setPathSketch(Sketch* sketch)
{
    if (m_path.data() == sketch) return;
    if (m_path) disconnect(m_path.data(), nullptr, this, nullptr);
    m_path = sketch;
    if (m_path) {
        connect(m_path.data(), &Sketch::shapeChanged,    this, &Sweep::rebuildRequested);
        connect(m_path.data(), &Sketch::geometryChanged, this, &Sweep::rebuildRequested);
    }
}

bool Sweep::rebuild()
{
    if (!m_profile) { setError(QStringLiteral("尚未指定輪廓草圖")); return false; }
    if (!m_path)    { setError(QStringLiteral("尚未指定路徑草圖")); return false; }
    if (m_profile.data() == m_path.data()) {
        setError(QStringLiteral("輪廓與路徑不可使用同一個草圖"));
        return false;
    }

    QString err;
    const QList<TopoDS_Face> faces = solidops::profileFaces(m_profile.data(), m_regionIndex, &err);
    if (faces.isEmpty()) {
        setError(err.isEmpty() ? QStringLiteral("輪廓草圖沒有封閉輪廓") : err);
        return false;
    }

    TopoDS_Wire spine;
    if (!solidops::pathWireFromSketch(m_path.data(), spine, &err)) {
        setError(err);
        return false;
    }

    GeomFill_Trihedron mode = GeomFill_IsCorrectedFrenet;
    switch (m_orientation) {
    case Orientation::Frenet: mode = GeomFill_IsFrenet; break;
    case Orientation::Fixed:  mode = GeomFill_IsFixed;  break;
    default: break;
    }

    try {
        BRep_Builder bb;
        TopoDS_Compound tools;
        bb.MakeCompound(tools);

        for (const TopoDS_Face& face : faces) {
            BRepOffsetAPI_MakePipe mk(spine, face, mode, Standard_False);
            if (!mk.IsDone() || mk.Shape().IsNull()) {
                setError(QStringLiteral("掃掠失敗：請確認輪廓垂直於路徑起點，且路徑沒有過於尖銳的轉角"));
                return false;
            }
            bb.Add(tools, mk.Shape());
        }

        TopoDS_Shape tool = solidops::finalizeSolid(tools, &err);
        if (tool.IsNull()) {
            setError(err.isEmpty() ? QStringLiteral("掃掠結果無效") : err);
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
        setError(QStringLiteral("掃掠發生 OCC 例外：%1")
                     .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown"));
        return false;
    }
}

QSet<QString> Sweep::featureDependencies() const
{
    QSet<QString> deps = targetDependencies();
    if (m_profile) deps.insert(m_profile->id());
    if (m_path)    deps.insert(m_path->id());
    return deps;
}

void Sweep::resolveReferences(Document* doc)
{
    if (!doc) return;
    resolveTargetReference(doc);

    if (!m_pendingProfileId.isEmpty()) {
        if (auto* sk = qobject_cast<Sketch*>(doc->findFeature(m_pendingProfileId))) {
            setProfileSketch(sk);
            if (!sk->parent()) sk->setFeatureParent(this);
            m_pendingProfileId.clear();
        } else {
            qWarning() << "[Sweep]" << name() << "cannot resolve profile:" << m_pendingProfileId;
        }
    }
    if (!m_pendingPathId.isEmpty()) {
        if (auto* sk = qobject_cast<Sketch*>(doc->findFeature(m_pendingPathId))) {
            setPathSketch(sk);
            if (!sk->parent()) sk->setFeatureParent(this);
            m_pendingPathId.clear();
        } else {
            qWarning() << "[Sweep]" << name() << "cannot resolve path:" << m_pendingPathId;
        }
    }
    doc->refreshFeatureDependencies(this);
}

QJsonObject Sweep::toJson() const
{
    QJsonObject obj = Feature::toJson();
    obj["profileSketchId"] = m_profile ? m_profile->id() : m_pendingProfileId;
    obj["pathSketchId"]    = m_path ? m_path->id() : m_pendingPathId;
    obj["regionIndex"]     = m_regionIndex;
    obj["orientation"]     = static_cast<int>(m_orientation);
    writeOpJson(obj);
    return obj;
}

bool Sweep::fromJson(const QJsonObject& json)
{
    if (!Feature::fromJson(json)) return false;
    m_pendingProfileId = json["profileSketchId"].toString();
    m_pendingPathId    = json["pathSketchId"].toString();
    m_regionIndex      = json["regionIndex"].toInt(-1);
    m_orientation      = static_cast<Orientation>(json["orientation"].toInt(0));
    readOpJson(json);
    return true;
}

} // namespace cad
} // namespace aicad
