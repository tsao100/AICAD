/**
 * @file SolidModifier.cpp
 */
#include "SolidModifier.h"
#include "Document.h"

#include <QDebug>
#include <cmath>
#include <QJsonArray>
#include <QJsonObject>

#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffset_Mode.hxx>
#include <GeomAbs_JoinType.hxx>
#include <Standard_Failure.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>

namespace aicad {
namespace cad {

// ═════════════════════════ SolidModifier ═════════════════════════

SolidModifier::SolidModifier(Document* parent)
    : Feature(parent)
{
}

void SolidModifier::setSourceFeature(Feature* src)
{
    if (m_source.data() == src) return;

    if (m_source) disconnect(m_sourceConn);
    m_source = src;
    m_pendingSourceId.clear();

    if (m_source) {
        m_sourceConn = connect(m_source.data(), &Feature::shapeChanged, this, [this]() {
            if (Document* doc = document()) doc->rebuildFeature(this);
            else rebuild();
        });
        setFeatureParent(m_source.data());
    }
}

QSet<QString> SolidModifier::featureDependencies() const
{
    QSet<QString> deps;
    if (m_source) deps.insert(m_source->id());
    return deps;
}

void SolidModifier::resolveReferences(Document* doc)
{
    if (!doc || m_pendingSourceId.isEmpty()) return;

    if (Feature* src = doc->findFeature(m_pendingSourceId)) {
        setSourceFeature(src);
        doc->refreshFeatureDependencies(this);
    } else {
        qWarning() << "[SolidModifier]" << name() << "cannot resolve sourceFeatureId:" << m_pendingSourceId;
    }
}

void SolidModifier::writeSourceJson(QJsonObject& obj) const
{
    obj["sourceFeatureId"] = sourceFeatureId();
}

void SolidModifier::readSourceJson(const QJsonObject& obj)
{
    m_pendingSourceId = obj["sourceFeatureId"].toString();
}

// ═════════════════════════ FilletSolid ═════════════════════════

FilletSolid::FilletSolid(Document* parent)
    : SolidModifier(parent)
{
    setName("Fillet");
}

bool FilletSolid::rebuild()
{
    if (!m_source) { setError(QStringLiteral("尚未指定來源實體")); return false; }

    const TopoDS_Shape base = m_source->shape();
    if (base.IsNull()) {
        setError(QStringLiteral("來源實體「%1」目前沒有有效的形狀").arg(m_source->name()));
        return false;
    }
    if (m_radius <= 1e-9) { setError(QStringLiteral("圓角半徑必須大於 0")); return false; }
    if (m_edges.isEmpty()) { setError(QStringLiteral("尚未選擇要圓角的邊")); return false; }

    try {
        BRepFilletAPI_MakeFillet mk(base);

        QList<TopoDS_Edge> added;
        for (const EdgeSignature& sig : m_edges) {
            const TopoDS_Edge e = solidops::matchEdge(base, sig);
            if (e.IsNull()) continue;
            bool dup = false;
            for (const TopoDS_Edge& a : added)
                if (a.IsSame(e)) { dup = true; break; }
            if (dup) continue;
            mk.Add(m_radius, e);
            added.append(e);
        }
        if (added.isEmpty()) {
            setError(QStringLiteral("找不到要圓角的邊（來源實體的形狀變動太大）"));
            return false;
        }

        mk.Build();
        if (!mk.IsDone() || mk.Shape().IsNull()) {
            setError(QStringLiteral("圓角失敗：半徑可能太大，或邊的相鄰面無法接合"));
            return false;
        }

        QString err;
        const TopoDS_Shape result = solidops::finalizeSolid(mk.Shape(), &err);
        if (result.IsNull()) {
            setError(err.isEmpty() ? QStringLiteral("圓角結果無效") : err);
            return false;
        }
        setShape(result);
        return true;

    } catch (const Standard_Failure& ex) {
        setError(QStringLiteral("圓角發生 OCC 例外：%1")
                     .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown"));
        return false;
    }
}

QJsonObject FilletSolid::toJson() const
{
    QJsonObject obj = Feature::toJson();
    writeSourceJson(obj);
    obj["radius"] = m_radius;
    QJsonArray arr;
    for (const EdgeSignature& s : m_edges) arr.append(solidops::toJson(s));
    obj["edges"] = arr;
    return obj;
}

bool FilletSolid::fromJson(const QJsonObject& json)
{
    if (!Feature::fromJson(json)) return false;
    readSourceJson(json);
    m_radius = json["radius"].toDouble(1.0);
    m_edges.clear();
    for (const QJsonValue& v : json["edges"].toArray())
        m_edges.append(solidops::edgeSignatureFromJson(v.toObject()));
    return true;
}

// ═════════════════════════ ShellSolid ═════════════════════════

ShellSolid::ShellSolid(Document* parent)
    : SolidModifier(parent)
{
    setName("Shell");
}

bool ShellSolid::rebuild()
{
    if (!m_source) { setError(QStringLiteral("尚未指定來源實體")); return false; }

    const TopoDS_Shape base = m_source->shape();
    if (base.IsNull()) {
        setError(QStringLiteral("來源實體「%1」目前沒有有效的形狀").arg(m_source->name()));
        return false;
    }
    if (std::abs(m_thickness) <= 1e-9) { setError(QStringLiteral("壁厚不可為 0")); return false; }
    if (m_faces.isEmpty()) { setError(QStringLiteral("請至少選擇一個要開口的面")); return false; }

    try {
        TopTools_ListOfShape closing;
        for (const FaceSignature& sig : m_faces) {
            const TopoDS_Face f = solidops::matchFace(base, sig);
            if (f.IsNull()) continue;
            bool dup = false;
            for (TopTools_ListOfShape::Iterator it(closing); it.More(); it.Next())
                if (it.Value().IsSame(f)) { dup = true; break; }
            if (!dup) closing.Append(f);
        }
        if (closing.IsEmpty()) {
            setError(QStringLiteral("找不到要開口的面（來源實體的形狀變動太大）"));
            return false;
        }

        BRepOffsetAPI_MakeThickSolid mk;
        mk.MakeThickSolidByJoin(base, closing, -m_thickness, 1.0e-3,
                                BRepOffset_Skin, Standard_False, Standard_False, GeomAbs_Arc);
        mk.Build();
        if (!mk.IsDone() || mk.Shape().IsNull()) {
            setError(QStringLiteral("抽殼失敗：壁厚可能太大，或曲面無法等距"));
            return false;
        }

        QString err;
        const TopoDS_Shape result = solidops::finalizeSolid(mk.Shape(), &err);
        if (result.IsNull()) {
            setError(err.isEmpty() ? QStringLiteral("抽殼結果無效") : err);
            return false;
        }
        setShape(result);
        return true;

    } catch (const Standard_Failure& ex) {
        setError(QStringLiteral("抽殼發生 OCC 例外：%1")
                     .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown"));
        return false;
    }
}

QJsonObject ShellSolid::toJson() const
{
    QJsonObject obj = Feature::toJson();
    writeSourceJson(obj);
    obj["thickness"] = m_thickness;
    QJsonArray arr;
    for (const FaceSignature& s : m_faces) arr.append(solidops::toJson(s));
    obj["faces"] = arr;
    return obj;
}

bool ShellSolid::fromJson(const QJsonObject& json)
{
    if (!Feature::fromJson(json)) return false;
    readSourceJson(json);
    m_thickness = json["thickness"].toDouble(1.0);
    m_faces.clear();
    for (const QJsonValue& v : json["faces"].toArray())
        m_faces.append(solidops::faceSignatureFromJson(v.toObject()));
    return true;
}

} // namespace cad
} // namespace aicad
