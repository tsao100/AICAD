#include "GeneralDimClassifier.h"
#include "../Sketch.h"
#include <cmath>

namespace aicad::cad {

// ─────────────────────────────────────────────────────────────────────────────
// isPointLike
// ─────────────────────────────────────────────────────────────────────────────

bool GeneralDimClassifier::isPointLike(const GeomRef& r, const Sketch* sketch)
{
    if (!sketch) return false;
    // 獨立 SketchPoint
    if (sketch->point(r.geomUuid)) return true;

    auto* geom = sketch->findGeometry(r.geomUuid);
    if (!geom) return false;

    switch (geom->type) {
    case SketchGeometryType::Point:
        return true;
    case SketchGeometryType::Line:
        return r.handle == GeomHandle::Start || r.handle == GeomHandle::End;
    case SketchGeometryType::Arc:
        return r.handle == GeomHandle::Start
            || r.handle == GeomHandle::End
            || r.handle == GeomHandle::Center;
    case SketchGeometryType::Circle:
        return r.handle == GeomHandle::Center;
    default:
        return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// classify 入口
// ─────────────────────────────────────────────────────────────────────────────

GeneralDimClassifier::Result
GeneralDimClassifier::classify(const QList<GeomRef>& refs, const Sketch* sketch,
                               bool /*allowHorizVert*/)
{
    if (refs.isEmpty()) {
        Result r; r.needMore = true;
        r.nextPrompt = initialPrompt();
        return r;
    }
    if (refs.size() == 1)
        return classifySingle(refs[0], sketch);
    return classifyPair(refs[0], refs[1], sketch);
}

// ─────────────────────────────────────────────────────────────────────────────
// classifySingle
//
// 決策表：
//   整條線   → FixedLength（自動）
//   整個圓   → 需選單：FixedDiameter / FixedRadius
//   整條弧   → 需選單：FixedRadius / FixedArcLength
//   線端點   → 點狀，WaitSecond（或選單 FixedX/Y/Coord）
//   弧端點   → 點狀，WaitSecond（或選單 FixedX/Y/Coord）
//   弧圓心   → 點狀，選單 FixedX/Y/Coord/WaitSecond
//   圓心     → 點狀，選單 FixedX/Y/Coord/WaitSecond
//   獨立點   → 點狀，選單 FixedX/Y/Coord/WaitSecond
// ─────────────────────────────────────────────────────────────────────────────

GeneralDimClassifier::Result
GeneralDimClassifier::classifySingle(const GeomRef& r, const Sketch* sketch)
{
    Result res;
    if (!sketch) return res;

    // 獨立 SketchPoint
    if (sketch->point(r.geomUuid)) {
        res.needMenu   = true;
        res.menuKey    = MenuKey::PointCoord;
        res.nextPrompt = "GDIM 點：X座標(X) / Y座標(Y) / XY座標(C) / 量距第二點(D)";
        return res;
    }

    auto* geom = sketch->findGeometry(r.geomUuid);
    if (!geom) return res;

    switch (geom->type) {

    case SketchGeometryType::Line:
        if (r.handle == GeomHandle::Start || r.handle == GeomHandle::End) {
            // 線端點 → 點狀選單
            res.needMenu   = true;
            res.menuKey    = MenuKey::PointCoord;
            res.nextPrompt = "GDIM 線端點：X座標(X) / Y座標(Y) / XY座標(C) / 量距第二點(D)";
        } else {
            // 整條線 → 選單：線長 / 量距第二點（點到線垂距、線到線）
            res.needMenu   = true;
            res.menuKey    = MenuKey::LineType;
            res.nextPrompt = "GDIM 線段：線長(L) / 量距第二點(D)";
        }
        break;

    case SketchGeometryType::Circle:
        if (r.handle == GeomHandle::Center) {
            // 圓心 → 座標選單
            res.needMenu   = true;
            res.menuKey    = MenuKey::PointCoord;
            res.nextPrompt = "GDIM 圓心：X座標(X) / Y座標(Y) / XY座標(C) / 量距第二點(D)";
        } else {
            // 整個圓 → 直徑/半徑選單
            res.needMenu   = true;
            res.menuKey    = MenuKey::CircleType;
            res.nextPrompt = "GDIM 圓：直徑(D) / 半徑(R)";
        }
        break;

    case SketchGeometryType::Arc:
        if (r.handle == GeomHandle::Start || r.handle == GeomHandle::End) {
            // 弧端點 → 點狀選單
            res.needMenu   = true;
            res.menuKey    = MenuKey::PointCoord;
            res.nextPrompt = "GDIM 弧端點：X座標(X) / Y座標(Y) / XY座標(C) / 量距第二點(D)";
        } else if (r.handle == GeomHandle::Center) {
            // 弧圓心 → 點狀選單
            res.needMenu   = true;
            res.menuKey    = MenuKey::PointCoord;
            res.nextPrompt = "GDIM 弧圓心：X座標(X) / Y座標(Y) / XY座標(C) / 量距第二點(D)";
        } else {
            // 整條弧 → 半徑/弧長選單
            res.needMenu   = true;
            res.menuKey    = MenuKey::ArcType;
            res.nextPrompt = "GDIM 弧：半徑(R) / 弧長(L)";
        }
        break;

    case SketchGeometryType::Point:
        res.needMenu   = true;
        res.menuKey    = MenuKey::PointCoord;
        res.nextPrompt = "GDIM 點：X座標(X) / Y座標(Y) / XY座標(C) / 量距第二點(D)";
        break;

    default:
        break;
    }

    return res;
}

// ─────────────────────────────────────────────────────────────────────────────
// classifyPair
//
// 規則（a = 第一選，b = 第二選）：
//
//   a 必須是「點狀」或「整條線/圓/弧」才進入此函數。
//   如果 a 是 WaitSecond（點狀），b 的合法範圍：
//     b = 點狀        → FixedDistance(P2P)，拖曳切換 H/V
//     b = 整條線      → FixedDistance(PointToLine) 垂距
//     b = 整個圓/弧   → FixedDistance(P2P) 點到圓心距
//     b = 同一物件    → 拒絕
//   如果 a 是整條線（進入此函數表示使用者強制雙選），b：
//     b = 整條線（平行） → FixedDistance(LineToLine)
//     b = 整條線（相交） → FixedAngleDim
//     其他            → 拒絕
// ─────────────────────────────────────────────────────────────────────────────

GeneralDimClassifier::Result
GeneralDimClassifier::classifyPair(const GeomRef& a, const GeomRef& b,
                                    const Sketch* sketch)
{
    Result res;
    if (!sketch) return res;

    auto* geomA = sketch->findGeometry(a.geomUuid);
    auto* geomB = sketch->findGeometry(b.geomUuid);
    const bool aIsIndepPt = (!geomA && sketch->point(a.geomUuid));
    const bool bIsIndepPt = (!geomB && sketch->point(b.geomUuid));
    if (!geomA && !aIsIndepPt) return res;
    if (!geomB && !bIsIndepPt) return res;

    const bool aIsPoint       = aIsIndepPt || isPointLike(a, sketch);
    const bool bIsPoint       = bIsIndepPt || isPointLike(b, sketch);
    const bool aIsWholeLine   = geomA && geomA->type == SketchGeometryType::Line
                                && a.handle == GeomHandle::WholeGeom;
    const bool bIsWholeLine   = geomB && geomB->type == SketchGeometryType::Line
                                && b.handle == GeomHandle::WholeGeom;
    const bool bIsWholeArc    = geomB && geomB->type == SketchGeometryType::Arc
                                && b.handle == GeomHandle::WholeGeom;
    const bool bIsWholeCircle = geomB && geomB->type == SketchGeometryType::Circle
                                && b.handle == GeomHandle::WholeGeom;

    // ── 防止選到同一物件 ────────────────────────────────────────────────────
    if (!a.geomUuid.isEmpty() && a.geomUuid == b.geomUuid)
        return res;  // invalid

    // ── a 是點狀（WaitSecond 進入 classifyPair）─────────────────────────────
    if (aIsPoint) {

        if (bIsPoint) {
            // 點 + 點 → FixedDistance(PointToPoint)，拖曳切換 H/V
            res.type     = ConstraintType::FixedDistance;
            res.distMode = DistanceMode::PointToPoint;
            res.valid    = true;
            return res;
        }

        if (bIsWholeLine) {
            // 點 + 線 → 垂距
            res.type     = ConstraintType::FixedDistance;
            res.distMode = DistanceMode::PointToLine;
            res.valid    = true;
            return res;
        }

        if (bIsWholeArc || bIsWholeCircle) {
            // 點 + 弧/圓 → 點到圓心距
            res.type     = ConstraintType::FixedDistance;
            res.distMode = DistanceMode::PointToPoint;
            res.valid    = true;
            return res;
        }

        // 點 + 其他點狀 handle
        res.type     = ConstraintType::FixedDistance;
        res.distMode = DistanceMode::PointToPoint;
        res.valid    = true;
        return res;
    }

    // ── a 是整條線（線+線角度/間距）────────────────────────────────────────
    if (aIsWholeLine && bIsWholeLine) {
        auto* lineA = dynamic_cast<const SketchLine*>(geomA);
        auto* lineB = dynamic_cast<const SketchLine*>(geomB);
        if (lineA && lineB) {
            QVector2D dirA = (lineA->end - lineA->start).normalized();
            QVector2D dirB = (lineB->end - lineB->start).normalized();
            double cross = static_cast<double>(
                dirA.x() * dirB.y() - dirA.y() * dirB.x());
            if (std::abs(cross) < 1e-4) {
                // 平行 → 線間距
                res.type     = ConstraintType::FixedDistance;
                res.distMode = DistanceMode::LineToLine;
            } else {
                // 相交 → 角度
                res.type     = ConstraintType::FixedAngleDim;
                res.distMode = DistanceMode::PointToPoint;
            }
            res.valid = true;
            return res;
        }
    }

    // ── a 是整條線 + b 是點（垂距，順序調換）───────────────────────────────
    if (aIsWholeLine && bIsPoint) {
        res.type     = ConstraintType::FixedDistance;
        res.distMode = DistanceMode::PointToLine;
        res.valid    = true;
        return res;
    }

    // ── a 是整條線 + b 是圓/弧（圓心到線垂距）──────────────────────────────
    if (aIsWholeLine && (bIsWholeArc || bIsWholeCircle)) {
        res.type     = ConstraintType::FixedDistance;
        res.distMode = DistanceMode::PointToLine;
        res.valid    = true;
        return res;
    }

    // ── 其他組合（圓/弧 整體 + 圓/弧 整體）→ 兩圓心距 ──────────────────────
    const bool aIsWholeArc    = geomA && geomA->type == SketchGeometryType::Arc
                                && a.handle == GeomHandle::WholeGeom;
    const bool aIsWholeCircle = geomA && geomA->type == SketchGeometryType::Circle
                                && a.handle == GeomHandle::WholeGeom;
    if ((aIsWholeArc || aIsWholeCircle) && (bIsWholeArc || bIsWholeCircle)) {
        res.type     = ConstraintType::FixedDistance;
        res.distMode = DistanceMode::PointToPoint;
        res.valid    = true;
        return res;
    }

    return res;  // invalid
}

} // namespace aicad::cad