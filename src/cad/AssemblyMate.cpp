/**
 * @file AssemblyMate.cpp
 */
#include "AssemblyMate.h"
#include "AssemblyPart.h"
#include "Document.h"

#include <QObject>
#include <cmath>

namespace aicad {
namespace cad {

using namespace asmsolve;

namespace { constexpr double kPi = 3.14159265358979323846; }

AssemblyMate::AssemblyMate(Document* parent) : Feature(parent)
{
    setName(QStringLiteral("Mate"));
}

AssemblyMate::Data AssemblyMate::data() const { return m_data; }

bool AssemblyMate::usesValue() const
{
    return mateUsesValue(m_data.type, m_data.a.kind, m_data.b.kind);
}

bool AssemblyMate::usesOrient() const
{
    return mateUsesOrient(m_data.type, m_data.a.kind, m_data.b.kind);
}

bool AssemblyMate::evalExpr(const Document* doc, const QString& expr, double& out)
{
    const QString e = expr.trimmed();
    if (e.isEmpty()) { out = 0.0; return true; }
    bool lit = false;
    const double v = e.toDouble(&lit);
    if (lit) { out = v; return true; }
    if (!doc) return false;
    const auto r = const_cast<Document*>(doc)->parameterStore()->evaluate(e);
    if (!r.first) return false;
    out = r.second;
    return true;
}

bool AssemblyMate::setData(const Data& d, QString* error)
{
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    if (!isValidPair(d.type, d.a.kind, d.b.kind))
        return fail(QObject::tr("「%1」不能用在這兩種幾何（%2 與 %3）")
                        .arg(typeLabel(d.type))
                        .arg(d.a.kind == RefKind::Point ? QObject::tr("點") : d.a.kind == RefKind::Axis ? QObject::tr("軸線") : QObject::tr("平面"))
                        .arg(d.b.kind == RefKind::Point ? QObject::tr("點") : d.b.kind == RefKind::Axis ? QObject::tr("軸線") : QObject::tr("平面")));
    if (d.a.ownerId == d.b.ownerId)
        return fail(d.a.isWorld() ? QObject::tr("兩個參照都屬於世界，沒有可移動的零件")
                                  : QObject::tr("兩個參照屬於同一個零件，配合不會產生任何作用"));
    double v = 0.0;
    if (mateUsesValue(d.type, d.a.kind, d.b.kind)) {
        if (!evalExpr(document(), d.valueExpr, v))
            return fail(QObject::tr("數值運算式無法求值：%1").arg(d.valueExpr));
        if (d.type == MateType::Angle && (v < -180.0 - 1e-9 || v > 180.0 + 1e-9))
            return fail(QObject::tr("夾角需在 -180° ～ 180° 之間"));
    }
    m_data = d;
    m_value = v;
    return true;
}

QString AssemblyMate::typeLabel(MateType t)
{
    switch (t) {
    case MateType::Coincident:     return QObject::tr("重合");
    case MateType::Collinear:      return QObject::tr("共線");
    case MateType::Coplanar:       return QObject::tr("共面");
    case MateType::Parallel:       return QObject::tr("平行");
    case MateType::Perpendicular:  return QObject::tr("垂直");
    case MateType::Distance:       return QObject::tr("距離");
    case MateType::Angle:          return QObject::tr("夾角");
    }
    return QString();
}

QString AssemblyMate::orientLabel(Orient o)
{
    switch (o) {
    case Orient::Aligned: return QObject::tr("同向");
    case Orient::Opposed: return QObject::tr("反向");
    default:              return QObject::tr("不限");
    }
}

QString AssemblyMate::ownerName(const MateRefData& r) const
{
    if (r.isWorld()) return QObject::tr("世界");
    const Feature* f = document() ? document()->findFeature(r.ownerId) : nullptr;
    return f ? f->name() : QObject::tr("（已刪除）");
}

QString AssemblyMate::description() const
{
    QString s = QStringLiteral("%1 · %2 %3 ↔ %4 %5")
                    .arg(typeLabel(m_data.type), ownerName(m_data.a), m_data.a.label, ownerName(m_data.b), m_data.b.label);
    if (usesValue()) {
        s += m_data.type == MateType::Angle ? QStringLiteral(" · %1°").arg(m_value, 0, 'g', 6)
                                            : QStringLiteral(" · %1").arg(m_value, 0, 'g', 8);
    }
    if (usesOrient()) s += QStringLiteral(" · ") + orientLabel(m_data.orient);
    return s;
}

int AssemblyMate::refreshReferences()
{
    int n = 0;
    Document* doc = document();
    if (!doc) return 0;
    for (MateRefData* r : { &m_data.a, &m_data.b }) {
        if (r->isWorld() || r->source == RefSource::Datum) continue;
        const auto* part = qobject_cast<AssemblyPart*>(doc->findFeature(r->ownerId));
        if (part && assemblygeom::refresh(part->localShape(), *r)) ++n;
    }
    return n;
}

bool AssemblyMate::rebuild()
{
    // 參數運算式改變 → 重新求值並重新求解（非同步、合併多次請求）
    double v = 0.0;
    if (mateUsesValue(m_data.type, m_data.a.kind, m_data.b.kind)) {
        if (!evalExpr(document(), m_data.valueExpr, v)) {
            setError(QObject::tr("數值運算式無法求值：%1").arg(m_data.valueExpr));
            return false;
        }
        m_value = v;
    }
    if (Document* doc = document()) doc->scheduleAssemblySolve();
    return true;
}

QJsonObject AssemblyMate::toJson() const
{
    QJsonObject obj = Feature::toJson();
    obj["type"]      = typeString();
    obj["mateType"]  = static_cast<int>(m_data.type);
    obj["orient"]    = static_cast<int>(m_data.orient);
    obj["valueExpr"] = m_data.valueExpr;
    obj["refA"]      = m_data.a.toJson();
    obj["refB"]      = m_data.b.toJson();
    return obj;
}

bool AssemblyMate::fromJson(const QJsonObject& json)
{
    Feature::fromJson(json);
    m_data.type      = static_cast<MateType>(json["mateType"].toInt());
    m_data.orient    = static_cast<Orient>(json["orient"].toInt());
    m_data.valueExpr = json["valueExpr"].toString(QStringLiteral("0"));
    m_data.a         = MateRefData::fromJson(json["refA"].toObject());
    m_data.b         = MateRefData::fromJson(json["refB"].toObject());
    double v = 0.0;
    m_value = evalExpr(document(), m_data.valueExpr, v) ? v : 0.0;
    return true;
}

} // namespace cad
} // namespace aicad
