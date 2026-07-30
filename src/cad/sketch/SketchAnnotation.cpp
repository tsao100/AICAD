#include "SketchAnnotation.h"
#include "../../core/ParameterStore.h"
#include <QJsonArray>

namespace aicad::cad {

// ──────────────────────────────────────────────────────────────────────────────
// ToleranceSpec
// ──────────────────────────────────────────────────────────────────────────────
QJsonObject ToleranceSpec::toJson() const {
    QJsonObject o;
    o["mode"]  = static_cast<int>(mode);
    o["upper"] = upper;
    o["lower"] = lower;
    return o;
}

ToleranceSpec ToleranceSpec::fromJson(const QJsonObject& j) {
    ToleranceSpec t;
    t.mode  = static_cast<ToleranceMode>(j["mode"].toInt(0));
    t.upper = j["upper"].toDouble(0.0);
    t.lower = j["lower"].toDouble(0.0);
    return t;
}

// ──────────────────────────────────────────────────────────────────────────────
// SketchAnnotation 工廠
// ──────────────────────────────────────────────────────────────────────────────
SketchAnnotation SketchAnnotation::makeDistance(const GeomRef& a, const GeomRef& b, double d, DistanceMode mode) {
    SketchAnnotation an; an.kind = AnnotationKind::Distance;
    an.refs = {a, b}; an.value = d; an.distMode = mode; return an;
}
SketchAnnotation SketchAnnotation::makeRadius(const QString& geomUuid, double r) {
    SketchAnnotation an; an.kind = AnnotationKind::Radius;
    an.refs = { GeomRef(geomUuid, GeomHandle::RadiusValue) }; an.value = r; return an;
}
SketchAnnotation SketchAnnotation::makeDiameter(const QString& geomUuid, double d) {
    SketchAnnotation an; an.kind = AnnotationKind::Diameter;
    an.refs = { GeomRef(geomUuid, GeomHandle::WholeGeom) }; an.value = d; return an;
}
SketchAnnotation SketchAnnotation::makeX(const GeomRef& point, double x) {
    SketchAnnotation an; an.kind = AnnotationKind::X;
    an.refs = {point}; an.value = x; return an;
}
SketchAnnotation SketchAnnotation::makeY(const GeomRef& point, double y) {
    SketchAnnotation an; an.kind = AnnotationKind::Y;
    an.refs = {point}; an.value = y; return an;
}
SketchAnnotation SketchAnnotation::makeCoordinate(const GeomRef& point, double x, double y) {
    SketchAnnotation an; an.kind = AnnotationKind::Coordinate;
    an.refs = {point}; an.value = x; an.value2 = y; return an;
}
SketchAnnotation SketchAnnotation::makeAngleDim(const GeomRef& a, const GeomRef& b, double angleRad) {
    SketchAnnotation an; an.kind = AnnotationKind::AngleDim;
    an.refs = {a, b}; an.value = angleRad; return an;
}
SketchAnnotation SketchAnnotation::makeLength(const QString& lineUuid, double len) {
    SketchAnnotation an; an.kind = AnnotationKind::Length;
    an.refs = { GeomRef(lineUuid, GeomHandle::Curve) }; an.value = len; return an;
}
SketchAnnotation SketchAnnotation::makeHorizDist(const GeomRef& a, const GeomRef& b, double d) {
    SketchAnnotation an; an.kind = AnnotationKind::HorizDist;
    an.refs = {a, b}; an.value = d; return an;
}
SketchAnnotation SketchAnnotation::makeVertDist(const GeomRef& a, const GeomRef& b, double d) {
    SketchAnnotation an; an.kind = AnnotationKind::VertDist;
    an.refs = {a, b}; an.value = d; return an;
}
SketchAnnotation SketchAnnotation::makeArcLength(const QString& arcUuid, double len) {
    SketchAnnotation an; an.kind = AnnotationKind::ArcLength;
    an.refs = { GeomRef(arcUuid, GeomHandle::WholeGeom) }; an.value = len; return an;
}
SketchAnnotation SketchAnnotation::makeLeaderNote(const GeomRef& target, const QString& text) {
    SketchAnnotation an; an.kind = AnnotationKind::LeaderNote;
    an.refs = {target}; an.noteText = text; an.driving = false; return an;
}

int SketchAnnotation::dofConsumed() const {
    switch (kind) {
    case AnnotationKind::Distance:   return 1;
    case AnnotationKind::Radius:     return 1;
    case AnnotationKind::Diameter:   return 1;
    case AnnotationKind::X:          return 1;
    case AnnotationKind::Y:          return 1;
    case AnnotationKind::Coordinate: return 2;
    case AnnotationKind::AngleDim:   return 1;
    case AnnotationKind::Length:     return 1;
    case AnnotationKind::HorizDist:  return 1;
    case AnnotationKind::VertDist:   return 1;
    case AnnotationKind::ArcLength:  return 1;
    case AnnotationKind::LeaderNote: return 0;
    default: return 0;
    }
}

bool SketchAnnotation::evaluateValue(const aicad::core::ParameterStore* store) {
    if (paramExpr.isEmpty()) return true;
    if (!store) return false;
    auto [ok, v] = store->evaluate(paramExpr);
    if (ok) value = v;
    return ok;
}

// ──────────────────────────────────────────────────────────────────────────────
// 隱含約束轉換（AnnotationKind → 舊 ConstraintType，僅供 Solver 內部使用）
// ──────────────────────────────────────────────────────────────────────────────
std::optional<ConstraintType> annotationKindToConstraintType(AnnotationKind kind) {
    switch (kind) {
    case AnnotationKind::Distance:   return ConstraintType::FixedDistance;
    case AnnotationKind::Radius:     return ConstraintType::FixedRadius;
    case AnnotationKind::Diameter:   return ConstraintType::FixedDiameter;
    case AnnotationKind::X:          return ConstraintType::FixedX;
    case AnnotationKind::Y:          return ConstraintType::FixedY;
    case AnnotationKind::Coordinate: return ConstraintType::CoordinateDim;
    case AnnotationKind::AngleDim:   return ConstraintType::FixedAngleDim;
    case AnnotationKind::Length:     return ConstraintType::FixedLength;
    case AnnotationKind::HorizDist:  return ConstraintType::FixedHorizDist;
    case AnnotationKind::VertDist:   return ConstraintType::FixedVertDist;
    case AnnotationKind::ArcLength:  return ConstraintType::FixedArcLength;
    default: return std::nullopt; // LeaderNote 等非尺寸型別
    }
}

std::optional<AnnotationKind> constraintTypeToAnnotationKind(ConstraintType type) {
    switch (type) {
    case ConstraintType::FixedDistance:  return AnnotationKind::Distance;
    case ConstraintType::FixedRadius:    return AnnotationKind::Radius;
    case ConstraintType::FixedDiameter:  return AnnotationKind::Diameter;
    case ConstraintType::FixedX:         return AnnotationKind::X;
    case ConstraintType::FixedY:         return AnnotationKind::Y;
    case ConstraintType::CoordinateDim:  return AnnotationKind::Coordinate;
    case ConstraintType::FixedAngleDim:  return AnnotationKind::AngleDim;
    case ConstraintType::FixedLength:    return AnnotationKind::Length;
    case ConstraintType::FixedHorizDist: return AnnotationKind::HorizDist;
    case ConstraintType::FixedVertDist:  return AnnotationKind::VertDist;
    case ConstraintType::FixedArcLength: return AnnotationKind::ArcLength;
    default: return std::nullopt;  // 純幾何約束型別（Coincident/Parallel/FixedAngle/...）
    }
}

std::optional<SketchConstraint> SketchAnnotation::toImplicitConstraint() const {
    if (!driving || !isDimensional()) return std::nullopt;
    auto ct = annotationKindToConstraintType(kind);
    if (!ct) return std::nullopt;

    SketchConstraint c;
    c.uuid        = uuid;          // 與標註同 uuid，方便 Sketch 端一對一同步
    c.implicitOf  = uuid;
    c.type        = *ct;
    c.refs        = refs;
    c.value       = value;
    c.value2      = value2;
    c.paramExpr   = paramExpr;
    c.driving     = true;
    c.distMode    = distMode;
    c.dimLineOffsetX = dimLineOffset.x();
    c.dimLineOffsetY = dimLineOffset.y();
    return c;
}

// ──────────────────────────────────────────────────────────────────────────────
// JSON
// ──────────────────────────────────────────────────────────────────────────────
QJsonObject SketchAnnotation::toJson() const {
    QJsonObject o;
    o["uuid"]      = uuid;
    o["kind"]      = static_cast<int>(kind);
    o["value"]     = value;
    o["value2"]    = value2;
    o["paramExpr"] = paramExpr;
    o["driving"]   = driving;
    o["distMode"]  = static_cast<int>(distMode);
    o["dimOffX"]   = dimLineOffset.x();
    o["dimOffY"]   = dimLineOffset.y();

    o["prefix"]      = prefix;
    o["suffix"]      = suffix;
    o["tolerance"]   = tolerance.toJson();
    o["precision"]   = precision;
    o["isBasic"]      = isBasic;
    o["isInspection"] = isInspection;

    QJsonArray leaderArr;
    for (const auto& v : leaderVertices) {
        QJsonObject p; p["x"] = v.x(); p["y"] = v.y();
        leaderArr.append(p);
    }
    o["leaderVertices"] = leaderArr;
    o["noteText"] = noteText;

    QJsonArray refArr;
    for (const auto& r : refs) refArr.append(r.toJson());
    o["refs"] = refArr;

    return o;
}

SketchAnnotation SketchAnnotation::fromJson(const QJsonObject& j) {
    SketchAnnotation an;
    an.uuid      = j["uuid"].toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    an.kind      = static_cast<AnnotationKind>(j["kind"].toInt());
    an.value     = j["value"].toDouble(0.0);
    an.value2    = j["value2"].toDouble(0.0);
    an.paramExpr = j["paramExpr"].toString();
    an.driving   = j["driving"].toBool(true);
    an.distMode  = static_cast<DistanceMode>(j["distMode"].toInt(0));
    an.dimLineOffset = QVector2D(j["dimOffX"].toDouble(0.0), j["dimOffY"].toDouble(0.0));

    an.prefix      = j["prefix"].toString();
    an.suffix      = j["suffix"].toString();
    an.tolerance   = ToleranceSpec::fromJson(j["tolerance"].toObject());
    an.precision   = j["precision"].toInt(2);
    an.isBasic      = j["isBasic"].toBool(false);
    an.isInspection = j["isInspection"].toBool(false);

    for (const auto& v : j["leaderVertices"].toArray()) {
        QJsonObject p = v.toObject();
        an.leaderVertices.append(QVector2D(p["x"].toDouble(0.0), p["y"].toDouble(0.0)));
    }
    an.noteText = j["noteText"].toString();

    for (const auto& rv : j["refs"].toArray())
        an.refs.append(GeomRef::fromJson(rv.toObject()));

    return an;
}

} // namespace aicad::cad
