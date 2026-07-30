#include "GeometryRelationshipAnalyzer.h"
#include "../Sketch.h"
#include <QVector2D>
#include <cmath>

namespace aicad::cad {

namespace {

constexpr float kPosEpsilon    = 1e-3f;  ///< 座標重合容許誤差（草圖平面單位）
constexpr float kAngleEpsilon  = 1e-3f;  ///< 平行/垂直判斷的 cos/sin 容許誤差
constexpr float kRadiusEpsilon = 1e-2f;  ///< 相切/同心判斷的半徑容許誤差

/// 查詢 sketch 既有約束中，是否有一筆「純幾何約束」的兩個 refs 分別指向
/// geomUuidA / geomUuidB（不管 handle，也不管順序）。這是最權威的關係來源
/// ——如果求解器已經確認兩個幾何平行/垂直/同心/相切/重合，直接採信，
/// 不需要再用數值容許誤差去猜。
bool hasConstraintBetween(const Sketch* sketch, const QString& uuidA,
                           const QString& uuidB, ConstraintType type)
{
    if (!sketch) return false;
    for (const auto& c : sketch->constraints()) {
        if (c.type != type) continue;
        bool hasA = false, hasB = false;
        for (const auto& r : c.refs) {
            if (r.geomUuid == uuidA) hasA = true;
            if (r.geomUuid == uuidB) hasB = true;
        }
        if (hasA && hasB) return true;
    }
    return false;
}

const SketchLine*   asLine  (const SketchGeometry* g) { return dynamic_cast<const SketchLine*>(g); }
const SketchCircle* asCircle(const SketchGeometry* g) { return dynamic_cast<const SketchCircle*>(g); }
const SketchArc*    asArc   (const SketchGeometry* g) { return dynamic_cast<const SketchArc*>(g); }

/// 圓/弧的圓心與半徑（弧透過 GeomHandle::Center / Start 解析出來，
/// 見 DimensionLineAIS::drawRadiusDimension 的既有做法，這裡沿用同一套邏輯）
bool circleOrArcCenterRadius(const GeomRef& ref, const Sketch* sketch,
                              QVector2D& center, float& radius)
{
    auto* geom = sketch->findGeometry(ref.geomUuid);
    if (auto* circ = asCircle(geom)) {
        center = circ->center;
        radius = static_cast<float>(circ->radius);
        return true;
    }
    if (auto* arc = asArc(geom)) {
        center = GeomRef(ref.geomUuid, GeomHandle::Center).resolvePosition(sketch);
        QVector2D startPt = GeomRef(ref.geomUuid, GeomHandle::Start).resolvePosition(sketch);
        radius = (startPt - center).length();
        return true;
    }
    return false;
}

} // namespace

GeomRelationship GeometryRelationshipAnalyzer::analyze(
    const GeomRef& a, const GeomRef& b, const Sketch* sketch)
{
    if (!sketch) return GeomRelationship::Independent;

    // ── 1. 優先查既有約束（最權威）────────────────────────────────────────
    if (hasConstraintBetween(sketch, a.geomUuid, b.geomUuid, ConstraintType::Coincident))
        return GeomRelationship::Coincident;
    if (hasConstraintBetween(sketch, a.geomUuid, b.geomUuid, ConstraintType::Concentric))
        return GeomRelationship::Concentric;
    if (hasConstraintBetween(sketch, a.geomUuid, b.geomUuid, ConstraintType::Tangent))
        return GeomRelationship::Tangent;
    if (hasConstraintBetween(sketch, a.geomUuid, b.geomUuid, ConstraintType::Perpendicular))
        return GeomRelationship::Perpendicular;
    if (hasConstraintBetween(sketch, a.geomUuid, b.geomUuid, ConstraintType::Parallel))
        return GeomRelationship::Parallel;

    // ── 2. 退回幾何數值計算 ──────────────────────────────────────────────
    QVector2D pa = a.resolvePosition(sketch);
    QVector2D pb = b.resolvePosition(sketch);

    // 兩點重合（含：兩個獨立點、兩個端點、或圓心重合的簡化判斷）
    if ((pa - pb).length() < kPosEpsilon)
        return GeomRelationship::Coincident;

    auto* geomA = sketch->findGeometry(a.geomUuid);
    auto* geomB = sketch->findGeometry(b.geomUuid);
    const bool aWholeLine = geomA && asLine(geomA) && a.handle == GeomHandle::WholeGeom;
    const bool bWholeLine = geomB && asLine(geomB) && b.handle == GeomHandle::WholeGeom;

    if (aWholeLine && bWholeLine) {
        auto* la = asLine(geomA);
        auto* lb = asLine(geomB);
        QVector2D dirA = (la->end - la->start).normalized();
        QVector2D dirB = (lb->end - lb->start).normalized();
        float cross = dirA.x() * dirB.y() - dirA.y() * dirB.x();
        float dot   = QVector2D::dotProduct(dirA, dirB);
        if (std::abs(cross) < kAngleEpsilon) return GeomRelationship::Parallel;
        if (std::abs(dot)   < kAngleEpsilon) return GeomRelationship::Perpendicular;
        return GeomRelationship::Intersecting;
    }

    const bool aWholeRound = geomA && (asCircle(geomA) || asArc(geomA))
                             && a.handle == GeomHandle::WholeGeom;
    const bool bWholeRound = geomB && (asCircle(geomB) || asArc(geomB))
                             && b.handle == GeomHandle::WholeGeom;

    if (aWholeRound && bWholeRound) {
        QVector2D ca, cb; float ra = 0, rb = 0;
        if (circleOrArcCenterRadius(a, sketch, ca, ra) &&
            circleOrArcCenterRadius(b, sketch, cb, rb)) {
            float d = (ca - cb).length();
            if (d < kPosEpsilon) return GeomRelationship::Concentric;
            if (std::abs(d - (ra + rb)) < kRadiusEpsilon ||
                std::abs(d - std::abs(ra - rb)) < kRadiusEpsilon)
                return GeomRelationship::Tangent;
        }
        return GeomRelationship::Independent;
    }

    if ((aWholeLine && bWholeRound) || (bWholeLine && aWholeRound)) {
        const GeomRef& lineRef  = aWholeLine ? a : b;
        const GeomRef& roundRef = aWholeLine ? b : a;
        auto* geomLine = sketch->findGeometry(lineRef.geomUuid);
        auto* la = asLine(geomLine);
        QVector2D ca; float ra = 0;
        if (la && circleOrArcCenterRadius(roundRef, sketch, ca, ra)) {
            QVector2D ab = (la->end - la->start);
            float abLen = ab.length();
            if (abLen > 1e-4f) {
                QVector2D perp(-ab.y() / abLen, ab.x() / abLen);
                float dist = std::abs(QVector2D::dotProduct(ca - la->start, perp));
                if (std::abs(dist - ra) < kRadiusEpsilon)
                    return GeomRelationship::Tangent;
            }
        }
    }

    return GeomRelationship::Independent;
}

} // namespace aicad::cad
