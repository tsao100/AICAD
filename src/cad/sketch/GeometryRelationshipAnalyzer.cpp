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
///
/// ⚠️ 修正：先前用兩個獨立的 bool（hasA／hasB）分別記錄「refs 裡有沒有
/// 出現 uuidA」「有沒有出現 uuidB」，最後 `hasA && hasB` 才算數——但當
/// uuidA == uuidB（呼叫端是「同一條線自己的 Start/End」，見
/// GeneralDimClassifier::inferPair() 的 sameLine 特例）時，refs 裡只要有
/// 任何一筆等於這條線的 uuid，就會同時把 hasA 和 hasB 都設成 true（因為
/// uuidA 和 uuidB 是同一個字串），誤判成「這條線與它自己」有這個關係。
/// 這在有任何約束（不論哪種型別，只要在下面五種型別裡）把這條線跟「另一
/// 個」幾何連在一起時就會誤觸發——最典型的情況就是矩形：相鄰兩邊的端點
/// 常用 Coincident 約束銜接、或相鄰/對邊有 Perpendicular／Parallel
/// 約束，只要這條線是其中一個 ref，就會被誤判成「與自己 Coincident」，
/// 導致 inferPair() 在下面的 switch 直接 return nullopt——這正是「矩形
/// （相連的垂直/水平線）hover 任一線完全沒有預覽，但沒有任何約束牽連的
/// 獨立斜線正常」的根本原因。
/// 改為要求 uuidA／uuidB 各自由「不同的」ref 項目滿足（即使 uuidA==uuidB
/// 也一樣，必須是兩個不同的 refs 陣列索引），才視為「這個約束真的連接了
/// 兩個東西」，避免同一個 ref 被同時算成兩邊。
bool hasConstraintBetween(const Sketch* sketch, const QString& uuidA,
                           const QString& uuidB, ConstraintType type)
{
    if (!sketch) return false;
    for (const auto& c : sketch->constraints()) {
        if (c.type != type) continue;
        int idxA = -1, idxB = -1;
        for (int i = 0; i < c.refs.size(); ++i) {
            if (idxA < 0 && c.refs[i].geomUuid == uuidA) { idxA = i; continue; }
            if (idxB < 0 && c.refs[i].geomUuid == uuidB) { idxB = i; }
        }
        if (idxA >= 0 && idxB >= 0 && idxA != idxB) return true;
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
    auto* geomA = sketch->findGeometry(a.geomUuid);
    auto* geomB = sketch->findGeometry(b.geomUuid);
    const bool aWholeLine = geomA && asLine(geomA) && a.handle == GeomHandle::WholeGeom;
    const bool bWholeLine = geomB && asLine(geomB) && b.handle == GeomHandle::WholeGeom;

    // ⚠️ 修正：「兩點重合」判斷先前寫在這個位置，對「整條線」（WholeGeom）
    // 也套用了 resolvePosition()——但 GeomRef::resolvePosition() 對
    // WholeGeom 線沒有真正的「代表點」概念，實作上是退回
    // geom->points[0]（也就是線的起點）。這對「線＋點」配對（例如點到線
    // 的垂距）在概念上完全錯誤：這裡比的是「點到線的『起點』距離」，不是
    // 「點到線本身的距離」，會在使用者原本期望顯示垂距預覽的情境下，
    // 誤判成兩者「重合」而直接 return nullopt、預覽整個消失——尤其當
    // 這條線的起點剛好離該點很近時最容易觸發（GDIM 選垂直/水平線後，
    // hover 原點的預覽消失，很可能就是這個誤判）。
    // 只有「兩者都不是整條幾何」（也就是真的都能代表單一點位置，例如
    // 兩個獨立點、兩個端點、或不同幾何各自的圓心）時，才做這個重合檢查。
    if (!aWholeLine && !bWholeLine) {
        QVector2D pa = a.resolvePosition(sketch);
        QVector2D pb = b.resolvePosition(sketch);
        if ((pa - pb).length() < kPosEpsilon)
            return GeomRelationship::Coincident;
    }

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
