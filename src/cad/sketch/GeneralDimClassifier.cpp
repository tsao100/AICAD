#include "GeneralDimClassifier.h"
#include "../Sketch.h"
#include <cmath>

namespace aicad::cad {

// ─────────────────────────────────────────────────────────────────────────────
// 輔助
// ─────────────────────────────────────────────────────────────────────────────

bool GeneralDimClassifier::isPointLike(const GeomRef& r, const Sketch* sketch)
{
    if (!sketch) return false;
    auto* geom = sketch->findGeometry(r.geomUuid);
    if (!geom) return false;
    if (geom->type == SketchGeometryType::Point)
        return true;
    // Line endpoint handles
    if (geom->type == SketchGeometryType::Line) {
        return r.handle == GeomHandle::Start ||
               r.handle == GeomHandle::End;
    }
    // Arc endpoint / center handles
    if (geom->type == SketchGeometryType::Arc) {
        return r.handle == GeomHandle::Start  ||
               r.handle == GeomHandle::End    ||
               r.handle == GeomHandle::Center;
    }
    // Circle center
    if (geom->type == SketchGeometryType::Circle) {
        return r.handle == GeomHandle::Center;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// classify 入口
// ─────────────────────────────────────────────────────────────────────────────

GeneralDimClassifier::Result
GeneralDimClassifier::classify(const QList<GeomRef>& refs, const Sketch* sketch)
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
// ─────────────────────────────────────────────────────────────────────────────

GeneralDimClassifier::Result
GeneralDimClassifier::classifySingle(const GeomRef& r, const Sketch* sketch)
{
    Result res;
    if (!sketch) return res;

    auto* geom = sketch->findGeometry(r.geomUuid);
    if (!geom) return res;

    switch (geom->type) {
    case SketchGeometryType::Line:
        // WholeGeom / Start / End 全部→ FixedLength
        res.type  = ConstraintType::FixedLength;
        res.valid = true;
        break;

    case SketchGeometryType::Circle:
        if (r.handle == GeomHandle::Center) {
            // 點選圓心 → CoordinateDim（單點座標）
            res.type  = ConstraintType::CoordinateDim;
            res.valid = true;
        } else {
            // WholeGeom → FixedDiameter
            res.type  = ConstraintType::FixedDiameter;
            res.valid = true;
        }
        break;

    case SketchGeometryType::Arc:
        // 先給 FixedRadius；command 狀態機會再詢問 R/L
        res.type  = ConstraintType::FixedRadius;
        res.valid = true;
        break;

    case SketchGeometryType::Point:
        // 獨立點 → 需要第二個選取
        res.needMore  = true;
        res.nextPrompt = "[GDIM] 再選一個幾何元素";
        break;

    default:
        // Line.Start / Line.End / Arc.Start / Arc.End → 需要第二個選取
        if (isPointLike(r, sketch)) {
            res.needMore   = true;
            res.nextPrompt = "[GDIM] 再選一個幾何元素";
        }
        break;
    }

    // Line endpoint handle 也需要第二個選取
    if (geom->type == SketchGeometryType::Line &&
        (r.handle == GeomHandle::Start || r.handle == GeomHandle::End)) {
        res = Result{};
        res.needMore   = true;
        res.nextPrompt = "[GDIM] 再選一個幾何元素";
    }

    return res;
}

// ─────────────────────────────────────────────────────────────────────────────
// classifyPair
// ─────────────────────────────────────────────────────────────────────────────

GeneralDimClassifier::Result
GeneralDimClassifier::classifyPair(const GeomRef& a, const GeomRef& b,
                                    const Sketch* sketch)
{
    Result res;
    if (!sketch) return res;

    auto* geomA = sketch->findGeometry(a.geomUuid);
    auto* geomB = sketch->findGeometry(b.geomUuid);
    if (!geomA || !geomB) return res;

    bool aIsPoint = isPointLike(a, sketch);
    bool bIsPoint = isPointLike(b, sketch);
    bool aIsLine  = (geomA->type == SketchGeometryType::Line);
    bool bIsLine  = (geomB->type == SketchGeometryType::Line);

    if (aIsPoint && bIsPoint) {
        // 取得兩點位置
        QVector2D pa = a.resolvePosition(sketch);
        QVector2D pb = b.resolvePosition(sketch);
        double dx = std::abs(static_cast<double>(pb.x() - pa.x()));
        double dy = std::abs(static_cast<double>(pb.y() - pa.y()));

        if (dx > dy * 2.0) {
            res.type     = ConstraintType::FixedHorizDist;
            res.distMode = DistanceMode::PointToPoint;
        } else if (dy > dx * 2.0) {
            res.type     = ConstraintType::FixedVertDist;
            res.distMode = DistanceMode::PointToPoint;
        } else {
            res.type     = ConstraintType::FixedDistance;
            res.distMode = DistanceMode::PointToPoint;
        }
        res.valid = true;
        return res;
    }

    if (aIsPoint && bIsLine) {
        res.type     = ConstraintType::FixedDistance;
        res.distMode = DistanceMode::PointToLine;
        res.valid    = true;
        return res;
    }
    if (bIsPoint && aIsLine) {
        res.type     = ConstraintType::FixedDistance;
        res.distMode = DistanceMode::PointToLine;
        res.valid    = true;
        return res;
    }

    if (aIsLine && bIsLine) {
        // 判斷是否平行：比較方向向量
        auto* lineA = dynamic_cast<const SketchLine*>(geomA);
        auto* lineB = dynamic_cast<const SketchLine*>(geomB);
        if (lineA && lineB) {
            QVector2D dirA = (lineA->end - lineA->start).normalized();
            QVector2D dirB = (lineB->end - lineB->start).normalized();
            double cross = static_cast<double>(
                dirA.x() * dirB.y() - dirA.y() * dirB.x());
            if (std::abs(cross) < 1e-4) {
                // 平行
                res.type     = ConstraintType::FixedDistance;
                res.distMode = DistanceMode::LineToLine;
            } else {
                res.type     = ConstraintType::FixedAngleDim;
                res.distMode = DistanceMode::PointToPoint;
            }
            res.valid = true;
            return res;
        }
    }

    // 其他組合（圓/弧 + 任意）→ 取中心距
    res.type     = ConstraintType::FixedDistance;
    res.distMode = DistanceMode::PointToPoint;
    res.valid    = true;
    return res;
}

} // namespace aicad::cad
