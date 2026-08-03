#include "GeneralDimClassifier.h"
#include "GeometryRelationshipAnalyzer.h"
#include "../Sketch.h"
#include <cmath>

namespace aicad::cad {

// ─────────────────────────────────────────────────────────────────────────────
// isPointLike（未變更）
// ─────────────────────────────────────────────────────────────────────────────

bool GeneralDimClassifier::isPointLike(const GeomRef& r, const Sketch* sketch)
{
    if (!sketch) return false;
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
// 內部小工具：依位移向量分類「水平／垂直／對角」方位帶
// 無選單版第 1 節：水平帶 ±25°（貼近左右方向）、垂直帶 90°±25°（貼近上下
// 方向）、其餘（含 45° 附近）視為對角帶。
// ─────────────────────────────────────────────────────────────────────────────

namespace {

enum class Zone { Horizontal, Vertical, Diagonal };

Zone classifyZone(const QVector2D& delta)
{
    if (delta.lengthSquared() < 1e-8f)
        return Zone::Diagonal;  // 位移過小（滑鼠幾乎在原點上），保守回傳對角

    double angleDeg = std::atan2(static_cast<double>(delta.y()),
                                  static_cast<double>(delta.x())) * 180.0 / M_PI;  // 值域約為 -180..180
    double a = std::abs(angleDeg);

    if (a <= 25.0 || a >= 155.0) return Zone::Horizontal;  // 貼近 0° / 180°
    if (std::abs(a - 90.0) <= 25.0) return Zone::Vertical; // 貼近 ±90°
    return Zone::Diagonal;                                 // 其餘（含 45° 附近）
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// inferPointLike —— 獨立點 / 線端點 / 弧端點 / 弧圓心 / 圓心
// ─────────────────────────────────────────────────────────────────────────────

std::optional<GeneralDimClassifier::Inference>
GeneralDimClassifier::inferPointLike(const GeomRef& r, const Sketch* sketch,
                                      const QVector2D& mousePt)
{
    if (!sketch) return std::nullopt;
    QVector2D p = r.resolvePosition(sketch);
    Zone zone = classifyZone(mousePt - p);

    Inference inf;
    switch (zone) {
    case Zone::Horizontal:
        inf.kind = AnnotationKind::X; inf.label = "X 座標"; break;
    case Zone::Vertical:
        inf.kind = AnnotationKind::Y; inf.label = "Y 座標"; break;
    case Zone::Diagonal:
        inf.kind = AnnotationKind::Coordinate; inf.label = "XY 座標"; break;
    }
    inf.distMode = DistanceMode::PointToPoint;
    return inf;
}

// ─────────────────────────────────────────────────────────────────────────────
// inferSingle —— 表 1：單一幾何的位置推論規則
// ─────────────────────────────────────────────────────────────────────────────

std::optional<GeneralDimClassifier::Inference>
GeneralDimClassifier::inferSingle(const GeomRef& r, const Sketch* sketch,
                                   const QVector2D& mousePt)
{
    if (!sketch) return std::nullopt;

    // 獨立 SketchPoint
    if (sketch->point(r.geomUuid))
        return inferPointLike(r, sketch, mousePt);

    auto* geom = sketch->findGeometry(r.geomUuid);
    if (!geom) return std::nullopt;

    switch (geom->type) {

    case SketchGeometryType::Point:
        return inferPointLike(r, sketch, mousePt);

    case SketchGeometryType::Line:
        if (r.handle == GeomHandle::Start || r.handle == GeomHandle::End)
            return inferPointLike(r, sketch, mousePt);
        // 整條線：無歧義，只有一種單幾何型別 —— 線長
        {
            Inference inf;
            inf.kind = AnnotationKind::Length;
            inf.label = "線長";
            return inf;
        }

    case SketchGeometryType::Circle: {
        if (r.handle == GeomHandle::Center)
            return inferPointLike(r, sketch, mousePt);
        auto* circ = dynamic_cast<const SketchCircle*>(geom);
        if (!circ) return std::nullopt;
        float dist = (mousePt - circ->center).length();
        Inference inf;
        if (dist < circ->radius) { inf.kind = AnnotationKind::Radius;   inf.label = "半徑 R"; }
        else                     { inf.kind = AnnotationKind::Diameter; inf.label = "直徑 Ø"; }
        return inf;
    }

    case SketchGeometryType::Arc: {
        if (r.handle == GeomHandle::Start || r.handle == GeomHandle::End
            || r.handle == GeomHandle::Center)
            return inferPointLike(r, sketch, mousePt);

        QVector2D center = GeomRef(r.geomUuid, GeomHandle::Center).resolvePosition(sketch);
        QVector2D start   = GeomRef(r.geomUuid, GeomHandle::Start).resolvePosition(sketch);
        float radius = (start - center).length();
        float dist   = (mousePt - center).length();

        Inference inf;
        if (dist < radius) { inf.kind = AnnotationKind::Radius;    inf.label = "半徑 R"; }
        else                { inf.kind = AnnotationKind::ArcLength; inf.label = "弧長 ~"; }
        return inf;
    }

    default:
        return std::nullopt;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// canPair / inferPair —— 表 1：雙幾何的位置推論規則
// ─────────────────────────────────────────────────────────────────────────────

std::optional<GeneralDimClassifier::Inference>
GeneralDimClassifier::inferPair(const GeomRef& a, const GeomRef& b,
                                 const Sketch* sketch, const QVector2D& mousePt)
{
    if (!sketch) return std::nullopt;

    auto* geomA = sketch->findGeometry(a.geomUuid);
    auto* geomB = sketch->findGeometry(b.geomUuid);
    const bool aIsIndepPt = (!geomA && sketch->point(a.geomUuid));
    const bool bIsIndepPt = (!geomB && sketch->point(b.geomUuid));
    if (!geomA && !aIsIndepPt) return std::nullopt;
    if (!geomB && !bIsIndepPt) return std::nullopt;

    const bool aIsPoint = aIsIndepPt || isPointLike(a, sketch);
    const bool bIsPoint = bIsIndepPt || isPointLike(b, sketch);

    // 不允許選到同一個點（例如同一個獨立點被選了兩次）。
    // ⚠️ 修正：先前直接比較 a.geomUuid == b.geomUuid 會誤殺「同一條線的
    // 起點與終點」這種合法配對——線的 Start/End 兩個 GeomRef 的 geomUuid
    // 都是「線」本身的 uuid（見 GeomRef::resolvedPointUuid()：Line 的
    // Start/End 是從 line->startUuid / line->endUuid 這兩個不同的
    // SketchPoint 解析出來的，geomUuid 欄位只是「掛在哪個幾何底下」，
    // 不代表兩者是同一個點）。這導致「點錨點在線的一端、hover 到同一條線
    // 的另一端」時 inferPair() 一律回傳 nullopt，退回單點的 X/Y/XY 預覽，
    // 使用者因此完全看不到「一條線」水平/垂直/對齊距離的 H/V/Align 預覽
    // （無選單版第 1 節表格第 5 列：點+點依滑鼠位置判斷水平/垂直/對齊）。
    // 改為比較「實際解析出的點 UUID」，只有兩者真的是同一個點時才拒絕。
    if (aIsPoint && bIsPoint) {
        const QString aPtUuid = a.resolvedPointUuid(sketch);
        const QString bPtUuid = b.resolvedPointUuid(sketch);
        if (!aPtUuid.isEmpty() && aPtUuid == bPtUuid)
            return std::nullopt;
    } else if (!a.geomUuid.isEmpty() && a.geomUuid == b.geomUuid) {
        return std::nullopt;
    }

    // ── 依 Constraint/關係推論（優先於下面逐一比對幾何型別的規則）───────────
    // 重合的兩點不提供距離；同心的兩個圓/弧「中心距」沒有意義（＝0），
    // 交給呼叫端 fallback 回單幾何的 R/D 預覽（見 GeneralDimCommand）。
    switch (GeometryRelationshipAnalyzer::analyze(a, b, sketch)) {
    case GeomRelationship::Coincident:
    case GeomRelationship::Concentric:
        return std::nullopt;
    default:
        break;
    }

    const bool aIsWholeLine   = geomA && geomA->type == SketchGeometryType::Line
                                && a.handle == GeomHandle::WholeGeom;
    const bool bIsWholeLine   = geomB && geomB->type == SketchGeometryType::Line
                                && b.handle == GeomHandle::WholeGeom;
    const bool aIsWholeArc    = geomA && geomA->type == SketchGeometryType::Arc
                                && a.handle == GeomHandle::WholeGeom;
    const bool bIsWholeArc    = geomB && geomB->type == SketchGeometryType::Arc
                                && b.handle == GeomHandle::WholeGeom;
    const bool aIsWholeCircle = geomA && geomA->type == SketchGeometryType::Circle
                                && a.handle == GeomHandle::WholeGeom;
    const bool bIsWholeCircle = geomB && geomB->type == SketchGeometryType::Circle
                                && b.handle == GeomHandle::WholeGeom;

    // ── 點 + 點：唯一需要滑鼠位置的雙幾何組合 ───────────────────────────────
    // 依遊標相對兩點連線／中點的方位：偏左右→水平距離；偏上下→垂直距離；
    // 偏對角（接近兩點連線延伸方向）→對齊距離（真實距離）。
    if (aIsPoint && bIsPoint) {
        QVector2D pa  = a.resolvePosition(sketch);
        QVector2D pb  = b.resolvePosition(sketch);
        QVector2D mid = (pa + pb) * 0.5f;
        Zone zone = classifyZone(mousePt - mid);

        Inference inf;
        inf.distMode = DistanceMode::PointToPoint;
        switch (zone) {
        case Zone::Horizontal: inf.kind = AnnotationKind::HorizDist; inf.label = "水平距離"; break;
        case Zone::Vertical:   inf.kind = AnnotationKind::VertDist;  inf.label = "垂直距離"; break;
        case Zone::Diagonal:   inf.kind = AnnotationKind::Distance;  inf.label = "對齊距離"; break;
        }
        return inf;
    }

    // ── 點 + 線／線 + 點：無歧義，唯一型別＝垂距 ────────────────────────────
    // 正規化成 [點, 線] 固定順序（量測公式假設 refs[0]=點、refs[1]=線）。
    if (aIsPoint && bIsWholeLine) {
        Inference inf;
        inf.kind = AnnotationKind::Distance; inf.distMode = DistanceMode::PointToLine;
        inf.label = "垂距"; inf.pairedRefs = { a, b };
        return inf;
    }
    if (bIsPoint && aIsWholeLine) {
        Inference inf;
        inf.kind = AnnotationKind::Distance; inf.distMode = DistanceMode::PointToLine;
        inf.label = "垂距"; inf.pairedRefs = { b, a };
        return inf;
    }

    // ── 點 + 弧／點 + 圓：無歧義，唯一型別＝點到圓心距離 ────────────────────
    if (aIsPoint && (bIsWholeArc || bIsWholeCircle)) {
        Inference inf;
        inf.kind = AnnotationKind::Distance; inf.distMode = DistanceMode::PointToPoint;
        inf.label = "距離";
        inf.pairedRefs = { a, GeomRef(b.geomUuid, GeomHandle::Center) };
        return inf;
    }
    if (bIsPoint && (aIsWholeArc || aIsWholeCircle)) {
        Inference inf;
        inf.kind = AnnotationKind::Distance; inf.distMode = DistanceMode::PointToPoint;
        inf.label = "距離";
        inf.pairedRefs = { b, GeomRef(a.geomUuid, GeomHandle::Center) };
        return inf;
    }

    // ── 線 + 線：無歧義，由幾何本身（平行/相交）唯一決定 ────────────────────
    if (aIsWholeLine && bIsWholeLine) {
        auto* lineA = dynamic_cast<const SketchLine*>(geomA);
        auto* lineB = dynamic_cast<const SketchLine*>(geomB);
        if (!lineA || !lineB) return std::nullopt;

        QVector2D dirA = (lineA->end - lineA->start).normalized();
        QVector2D dirB = (lineB->end - lineB->start).normalized();
        double cross = static_cast<double>(
            dirA.x() * dirB.y() - dirA.y() * dirB.x());

        if (std::abs(cross) < 1e-4) {
            // 平行 → 線間距
            Inference inf;
            inf.kind = AnnotationKind::Distance; inf.distMode = DistanceMode::LineToLine;
            inf.label = "線間距";
            return inf;
        }

        // 相交 → 夾角；垂直時角度恆為 90°，只具參考意義
        double dot = static_cast<double>(QVector2D::dotProduct(dirA, dirB));
        bool isPerp = std::abs(dot) < 1e-4;

        Inference inf;
        inf.kind = AnnotationKind::AngleDim; inf.label = "夾角";
        inf.isReferenceOnly = isPerp;
        return inf;
    }

    // ── 線 + 弧／線 + 圓：無歧義，唯一型別＝圓心到線垂距 ────────────────────
    // 正規化成 [圓心衍生點, 線]。
    if (aIsWholeLine && (bIsWholeArc || bIsWholeCircle)) {
        Inference inf;
        inf.kind = AnnotationKind::Distance; inf.distMode = DistanceMode::PointToLine;
        inf.label = "垂距";
        inf.pairedRefs = { GeomRef(b.geomUuid, GeomHandle::Center), a };
        return inf;
    }
    if (bIsWholeLine && (aIsWholeArc || aIsWholeCircle)) {
        Inference inf;
        inf.kind = AnnotationKind::Distance; inf.distMode = DistanceMode::PointToLine;
        inf.label = "垂距";
        inf.pairedRefs = { GeomRef(a.geomUuid, GeomHandle::Center), b };
        return inf;
    }

    // ── 圓/弧 + 圓/弧：無歧義，唯一型別＝圓心距 ─────────────────────────────
    if ((aIsWholeArc || aIsWholeCircle) && (bIsWholeArc || bIsWholeCircle)) {
        Inference inf;
        inf.kind = AnnotationKind::Distance; inf.distMode = DistanceMode::PointToPoint;
        inf.label = "圓心距";
        inf.pairedRefs = { GeomRef(a.geomUuid, GeomHandle::Center),
                            GeomRef(b.geomUuid, GeomHandle::Center) };
        return inf;
    }

    return std::nullopt;  // 無法配對
}

bool GeneralDimClassifier::canPair(const GeomRef& a, const GeomRef& b, const Sketch* sketch)
{
    // 合法性判斷與滑鼠位置無關（唯一依賴滑鼠位置的「點+點」組合，其合法性
    // 本身不受滑鼠影響，只有回傳的型別會變），故傳入原點即可。
    return inferPair(a, b, sketch, QVector2D()).has_value();
}

} // namespace aicad::cad
