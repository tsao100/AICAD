#include "SketchConstraint.h"
#include "../../core/ParameterStore.h"
#include "../Sketch.h"
#include <QJsonArray>

namespace aicad::cad {

// ──────────────────────────────────────────────────────────────────────────────
// GeomRef
// ──────────────────────────────────────────────────────────────────────────────
QJsonObject GeomRef::toJson() const {
    QJsonObject o;
    o["geomUuid"] = geomUuid;
    o["handle"]   = static_cast<int>(handle);
    return o;
}
GeomRef GeomRef::fromJson(const QJsonObject& j) {
    return GeomRef(j["geomUuid"].toString(),
                   static_cast<GeomHandle>(j["handle"].toInt()));
}

// ── Task C: GeomRef point resolution ─────────────────────────────────────────
QString GeomRef::resolvedPointUuid(const Sketch* sketch) const
{
    if (!sketch || geomUuid.isEmpty()) return {};
    auto* geom = sketch->findGeometry(geomUuid);
    if (!geom) return {};

    // 直接引用 SketchPoint
    if (geom->type == SketchGeometryType::Point)
        return geomUuid;

    // Line：Start / End
    if (auto* line = dynamic_cast<const SketchLine*>(geom)) {
        if (handle == GeomHandle::Start) return line->startUuid;
        if (handle == GeomHandle::End)   return line->endUuid;
    }

    // Arc：Start / End / Center
    if (auto* arc = dynamic_cast<const SketchArc*>(geom)) {
        if (handle == GeomHandle::Start)  return arc->startUuid;
        if (handle == GeomHandle::End)    return arc->endUuid;
        if (handle == GeomHandle::Center) return arc->centerUuid;
    }

    // Circle：Center or WholeGeom
    if (auto* circ = dynamic_cast<const SketchCircle*>(geom)) {
        if (handle == GeomHandle::Center ||
            handle == GeomHandle::WholeGeom) return circ->centerUuid;
    }

    return {};
}

bool GeomRef::isDirectPoint(const Sketch* sketch) const
{
    if (!sketch) return false;
    auto* g = sketch->findGeometry(geomUuid);
    return g && g->type == SketchGeometryType::Point;
}

QVector2D GeomRef::resolvePosition(const Sketch* sketch) const
{
    if (!sketch) return {};
    QString ptUuid = resolvedPointUuid(sketch);
    if (!ptUuid.isEmpty()) {
        if (auto* pt = sketch->point(ptUuid))
            return pt->pos;
    }
    // Fallback：直接從 geom->points 取得
    auto* geom = sketch->findGeometry(geomUuid);
    if (!geom) return {};
    if (!geom->points.isEmpty()) return geom->points[0];
    return {};
}

// ──────────────────────────────────────────────────────────────────────────────
// SketchConstraint 工廠
// ──────────────────────────────────────────────────────────────────────────────
SketchConstraint SketchConstraint::makeCoincident(const GeomRef& a, const GeomRef& b) {
    SketchConstraint c; c.type = ConstraintType::Coincident;
    c.refs = {a, b}; return c;
}
SketchConstraint SketchConstraint::makeHorizontal(const QString& lineUuid) {
    SketchConstraint c; c.type = ConstraintType::Horizontal;
    c.refs = { GeomRef(lineUuid, GeomHandle::Curve) }; return c;
}
SketchConstraint SketchConstraint::makeVertical(const QString& lineUuid) {
    SketchConstraint c; c.type = ConstraintType::Vertical;
    c.refs = { GeomRef(lineUuid, GeomHandle::Curve) }; return c;
}
SketchConstraint SketchConstraint::makeParallel(const QString& a, const QString& b) {
    SketchConstraint c; c.type = ConstraintType::Parallel;
    c.refs = { GeomRef(a, GeomHandle::Curve), GeomRef(b, GeomHandle::Curve) }; return c;
}
SketchConstraint SketchConstraint::makePerpendicular(const QString& a, const QString& b) {
    SketchConstraint c; c.type = ConstraintType::Perpendicular;
    c.refs = { GeomRef(a, GeomHandle::Curve), GeomRef(b, GeomHandle::Curve) }; return c;
}
SketchConstraint SketchConstraint::makeTangent(const QString& a, const QString& b) {
    SketchConstraint c; c.type = ConstraintType::Tangent;
    c.refs = { GeomRef(a, GeomHandle::Curve), GeomRef(b, GeomHandle::Curve) }; return c;
}
SketchConstraint SketchConstraint::makeEqualLength(const QString& a, const QString& b) {
    SketchConstraint c; c.type = ConstraintType::EqualLength;
    c.refs = { GeomRef(a, GeomHandle::Curve), GeomRef(b, GeomHandle::Curve) }; return c;
}
SketchConstraint SketchConstraint::makeEqualRadius(const QString& a, const QString& b) {
    SketchConstraint c; c.type = ConstraintType::EqualRadius;
    c.refs = { GeomRef(a, GeomHandle::Center), GeomRef(b, GeomHandle::Center) }; return c;
}
SketchConstraint SketchConstraint::makeConcentric(const QString& a, const QString& b) {
    SketchConstraint c; c.type = ConstraintType::Concentric;
    c.refs = { GeomRef(a, GeomHandle::Center), GeomRef(b, GeomHandle::Center) }; return c;
}
SketchConstraint SketchConstraint::makeFixed(const QString& uuid) {
    SketchConstraint c; c.type = ConstraintType::Fixed;
    c.refs = { GeomRef(uuid, GeomHandle::WholeGeom) }; return c;
}
SketchConstraint SketchConstraint::makeFixedDistance(const GeomRef& a, const GeomRef& b, double dist) {
    SketchConstraint c; c.type = ConstraintType::FixedDistance;
    c.refs = {a, b}; c.value = dist; return c;
}
SketchConstraint SketchConstraint::makeFixedRadius(const QString& uuid, double r) {
    SketchConstraint c; c.type = ConstraintType::FixedRadius;
    c.refs = { GeomRef(uuid, GeomHandle::RadiusValue) }; c.value = r; return c;
}
SketchConstraint SketchConstraint::makeFixedX(const GeomRef& pt, double x) {
    SketchConstraint c; c.type = ConstraintType::FixedX;
    c.refs = {pt}; c.value = x; return c;
}
SketchConstraint SketchConstraint::makeFixedY(const GeomRef& pt, double y) {
    SketchConstraint c; c.type = ConstraintType::FixedY;
    c.refs = {pt}; c.value = y; return c;
}
SketchConstraint SketchConstraint::makePointOnCurve(const GeomRef& pt, const QString& curveUuid) {
    SketchConstraint c; c.type = ConstraintType::PointOnCurve;
    c.refs = {pt, GeomRef(curveUuid, GeomHandle::Curve)}; return c;
}
SketchConstraint SketchConstraint::makeMidpoint(const GeomRef& pt, const QString& lineUuid) {
    SketchConstraint c; c.type = ConstraintType::Midpoint;
    c.refs = {pt, GeomRef(lineUuid, GeomHandle::Curve)}; return c;
}

bool SketchConstraint::isDimensional() const {
    switch (type) {
    case ConstraintType::FixedDistance:
    case ConstraintType::FixedRadius:
    case ConstraintType::FixedX:
    case ConstraintType::FixedY:
    case ConstraintType::FixedAngleDim:
    case ConstraintType::FixedAngle:
    case ConstraintType::FixedLength:
    case ConstraintType::FixedDiameter:
    case ConstraintType::FixedHorizDist:
    case ConstraintType::FixedVertDist:
    case ConstraintType::FixedArcLength:
    case ConstraintType::CoordinateDim:
        return true;
    default:
        return false;
    }
}

bool SketchConstraint::evaluateValue(const aicad::core::ParameterStore* store) {
    if (paramExpr.isEmpty()) return true;
    if (!store) return false;
    auto [ok, v] = store->evaluate(paramExpr);
    if (ok) value = v;
    return ok;
}

int SketchConstraint::dofConsumed() const {
    switch (type) {
    case ConstraintType::Coincident:      return 2;
    case ConstraintType::Midpoint:        return 2;
    case ConstraintType::Symmetric:       return 2;
    case ConstraintType::PointOnCurve:    return 1;
    case ConstraintType::PointOnMidpoint: return 2;
    case ConstraintType::Horizontal:      return 1;
    case ConstraintType::Vertical:        return 1;
    case ConstraintType::Parallel:        return 1;
    case ConstraintType::Perpendicular:   return 1;
    case ConstraintType::Collinear:       return 2;
    case ConstraintType::EqualLength:     return 1;
    case ConstraintType::FixedAngle:      return 1;
    case ConstraintType::Concentric:      return 2;
    case ConstraintType::EqualRadius:     return 1;
    case ConstraintType::Tangent:         return 1;
    case ConstraintType::FixedDistance:   return 1;
    case ConstraintType::FixedRadius:     return 1;
    case ConstraintType::FixedX:          return 1;
    case ConstraintType::FixedY:          return 1;
    case ConstraintType::FixedAngleDim:   return 1;
    case ConstraintType::Fixed:           return 999; // all DOF
    case ConstraintType::FixedLength:     return 1;
    case ConstraintType::FixedDiameter:   return 1;
    case ConstraintType::FixedHorizDist:  return 1;
    case ConstraintType::FixedVertDist:   return 1;
    case ConstraintType::FixedArcLength:  return 1;
    case ConstraintType::CoordinateDim:   return 2;
    default: return 0;
    }
}

QJsonObject SketchConstraint::toJson() const {
    QJsonObject o;
    o["uuid"]      = uuid;
    o["type"]      = static_cast<int>(type);
    o["value"]     = value;
    o["value2"]    = value2;
    o["paramExpr"] = paramExpr;
    o["driving"]   = driving;
    o["distMode"]  = static_cast<int>(distMode);
    o["dimOffX"]   = dimLineOffsetX;
    o["dimOffY"]   = dimLineOffsetY;
    QJsonArray arr;
    for (const auto& r : refs) arr.append(r.toJson());
    o["refs"] = arr;
    return o;
}

SketchConstraint SketchConstraint::fromJson(const QJsonObject& j) {
    SketchConstraint c;
    c.uuid      = j["uuid"].toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    c.type      = static_cast<ConstraintType>(j["type"].toInt());
    c.value     = j["value"].toDouble(0.0);
    c.value2    = j["value2"].toDouble(0.0);
    c.paramExpr = j["paramExpr"].toString();
    c.driving   = j["driving"].toBool(true);
    c.distMode  = static_cast<DistanceMode>(j["distMode"].toInt(0));
    c.dimLineOffsetX = j["dimOffX"].toDouble(0.0);
    c.dimLineOffsetY = j["dimOffY"].toDouble(0.0);
    for (const auto& rv : j["refs"].toArray())
        c.refs.append(GeomRef::fromJson(rv.toObject()));
    return c;
}


SketchConstraint SketchConstraint::makeSymmetric(const GeomRef& a, const GeomRef& b, const QString& axisUuid) {
    SketchConstraint c;
    c.type = ConstraintType::Symmetric;
    c.refs = { a, b, GeomRef(axisUuid, GeomHandle::Curve) };
    return c;
}

SketchConstraint SketchConstraint::makeCollinear(const QString& lineA, const QString& lineB) {
    SketchConstraint c;
    c.type = ConstraintType::Collinear;
    c.refs = { GeomRef(lineA, GeomHandle::Curve), GeomRef(lineB, GeomHandle::Curve) };
    return c;
}

// ── General Dimension 工廠方法 ───────────────────────────────────────────────

SketchConstraint SketchConstraint::makeFixedLength(const QString& lineUuid, double len) {
    SketchConstraint c; c.type = ConstraintType::FixedLength; c.value = len;
    c.refs = { GeomRef(lineUuid, GeomHandle::Curve) }; return c;
}

SketchConstraint SketchConstraint::makeFixedDiameter(const QString& geomUuid, double dia) {
    SketchConstraint c; c.type = ConstraintType::FixedDiameter; c.value = dia;
    c.refs = { GeomRef(geomUuid, GeomHandle::WholeGeom) }; return c;
}

SketchConstraint SketchConstraint::makeFixedHorizDist(const GeomRef& a, const GeomRef& b, double d) {
    SketchConstraint c; c.type = ConstraintType::FixedHorizDist; c.value = d;
    c.refs = {a, b}; return c;
}

SketchConstraint SketchConstraint::makeFixedVertDist(const GeomRef& a, const GeomRef& b, double d) {
    SketchConstraint c; c.type = ConstraintType::FixedVertDist; c.value = d;
    c.refs = {a, b}; return c;
}

SketchConstraint SketchConstraint::makeFixedArcLength(const QString& arcUuid, double len) {
    SketchConstraint c; c.type = ConstraintType::FixedArcLength; c.value = len;
    c.refs = { GeomRef(arcUuid, GeomHandle::WholeGeom) }; return c;
}

SketchConstraint SketchConstraint::makeCoordinateDim(const GeomRef& point, double x, double y) {
    SketchConstraint c; c.type = ConstraintType::CoordinateDim;
    c.value = x; c.value2 = y;
    c.refs = {point}; return c;
}

} // namespace aicad::cad