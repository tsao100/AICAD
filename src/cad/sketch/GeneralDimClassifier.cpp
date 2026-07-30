#include "GeneralDimClassifier.h"
#include "GeometryRelationshipAnalyzer.h"
#include "../Sketch.h"
#include <QStringList>
#include <cmath>

namespace aicad::cad {

// ─────────────────────────────────────────────────────────────────────────────
// isPointLike（與 Phase 2 前完全相同，未變更）
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
// 內部小工具：組一個候選項
// ─────────────────────────────────────────────────────────────────────────────
static GeneralDimClassifier::Candidate makeCand(
    AnnotationKind kind, const QString& label, QChar shortcut,
    bool needsSecondPick = false, DistanceMode mode = DistanceMode::PointToPoint,
    bool useSupplementAngle = false)
{
    GeneralDimClassifier::Candidate c;
    c.kind = kind; c.label = label; c.shortcut = shortcut;
    c.needsSecondPick = needsSecondPick; c.distMode = mode;
    c.useSupplementAngle = useSupplementAngle;
    return c;
}

/// 點狀幾何（獨立點 / 線端點 / 弧端點 / 弧圓心 / 圓心）的共用候選清單
static QList<GeneralDimClassifier::Candidate> pointLikeCandidates()
{
    return {
        makeCand(AnnotationKind::X,          "X 座標",    'X'),
        makeCand(AnnotationKind::Y,          "Y 座標",    'Y'),
        makeCand(AnnotationKind::Coordinate, "XY 座標",   'C'),
        makeCand(AnnotationKind::Distance,   "量距第二點", 'D', /*needsSecondPick=*/true),
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// classifyAll 入口
// ─────────────────────────────────────────────────────────────────────────────

QList<GeneralDimClassifier::Candidate>
GeneralDimClassifier::classifyAll(const QList<GeomRef>& refs, const Sketch* sketch)
{
    if (refs.isEmpty()) return {};
    if (refs.size() == 1) return classifySingle(refs[0], sketch);
    return classifyPair(refs[0], refs[1], sketch);
}

// ─────────────────────────────────────────────────────────────────────────────
// classifySingle —— 決策表與 Phase 2 前相同，僅將「彈出選單」改為
// 「直接回傳全部候選」
// ─────────────────────────────────────────────────────────────────────────────

QList<GeneralDimClassifier::Candidate>
GeneralDimClassifier::classifySingle(const GeomRef& r, const Sketch* sketch)
{
    if (!sketch) return {};

    // 獨立 SketchPoint
    if (sketch->point(r.geomUuid))
        return pointLikeCandidates();

    auto* geom = sketch->findGeometry(r.geomUuid);
    if (!geom) return {};

    switch (geom->type) {

    case SketchGeometryType::Line:
        if (r.handle == GeomHandle::Start || r.handle == GeomHandle::End)
            return pointLikeCandidates();
        // 整條線：預設就是線長。水平/垂直投影不做成 Tab 候選——
        // 那是 GeneralDimCommand::subscribePreview() 在 WaitDimPlace 階段
        // 依滑鼠拖曳方向即時切換 對齊/水平/垂直 的既有機制（見該函式），
        // 這裡刻意不重複實作，避免兩套切換方式互相打架。
        return {
            makeCand(AnnotationKind::Length,   "線長",       'L'),
            makeCand(AnnotationKind::Distance, "量距第二點", 'D', /*needsSecondPick=*/true),
        };

    case SketchGeometryType::Circle:
        if (r.handle == GeomHandle::Center)
            return pointLikeCandidates();
        // 整個圓：直徑 / 半徑
        return {
            makeCand(AnnotationKind::Diameter, "直徑 Ø", 'D'),
            makeCand(AnnotationKind::Radius,   "半徑 R", 'R'),
        };

    case SketchGeometryType::Arc:
        if (r.handle == GeomHandle::Start || r.handle == GeomHandle::End
            || r.handle == GeomHandle::Center)
            return pointLikeCandidates();
        // 整條弧：半徑 / 弧長
        return {
            makeCand(AnnotationKind::Radius,    "半徑 R", 'R'),
            makeCand(AnnotationKind::ArcLength, "弧長 ~", 'L'),
        };

    case SketchGeometryType::Point:
        return pointLikeCandidates();

    default:
        return {};
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// classifyPair —— 規則與 Phase 2 前完全相同（配對規則本身就是唯一決定的，
// 不涉及「使用者選擇」），僅將回傳格式包成單筆 Candidate 陣列
// ─────────────────────────────────────────────────────────────────────────────

QList<GeneralDimClassifier::Candidate>
GeneralDimClassifier::classifyPair(const GeomRef& a, const GeomRef& b,
                                    const Sketch* sketch)
{
    if (!sketch) return {};

    auto* geomA = sketch->findGeometry(a.geomUuid);
    auto* geomB = sketch->findGeometry(b.geomUuid);
    const bool aIsIndepPt = (!geomA && sketch->point(a.geomUuid));
    const bool bIsIndepPt = (!geomB && sketch->point(b.geomUuid));
    if (!geomA && !aIsIndepPt) return {};
    if (!geomB && !bIsIndepPt) return {};

    // ── Auto_DIM.md 第六節「依 Constraint/關係推論」──────────────────────────
    // 先問關係分析器：重合的兩點不提供距離；同心的兩個圓/弧只提供半徑/直徑
    // （中心距離＝0 沒有意義）。這一步優先於下面逐一比對幾何型別的規則，
    // 因為關係分析器可能查到「求解器已確認的約束」，比純幾何數值計算更權威。
    switch (GeometryRelationshipAnalyzer::analyze(a, b, sketch)) {
    case GeomRelationship::Coincident:
        return {};  // 「若 Coincident → 不提供 Distance」
    case GeomRelationship::Concentric: {
        // 「若 Concentric → 只提供 Diameter / Radius」：兩個圓/弧任選一個
        // 的單一候選清單即可（半徑/直徑對兩個同心圓來說是同一組概念）
        auto single = classifySingle(a, sketch);
        if (!single.isEmpty()) return single;
        return classifySingle(b, sketch);
    }
    default:
        break;  // 其餘關係（Parallel/Perpendicular/Intersecting/Tangent/
                // Independent）交給下面既有的逐一比對規則處理
    }

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
        return {};  // invalid

    // ── a 是點狀 ────────────────────────────────────────────────────────────
    if (aIsPoint) {
        if (bIsPoint) {
            // Auto_DIM.md 第三節「Point + Point：Distance / Horizontal / Vertical /
            // Aligned」。三種都是合法候選，用 Tab 切換；預設先給「對齊距離」
            // （兩點連線的直線距離），因為這是最常見的用法。
            // 註：WaitDimPlace 階段沿用既有的 subscribePreview() 滑鼠拖曳
            // 切換機制（依滑鼠角度即時切 對齊/水平/垂直），這裡的候選清單
            // 讓「選型別」這一步在滑鼠開始拖曳之前，也能直接用 Tab 循環到位。
            return {
                makeCand(AnnotationKind::Distance,  "對齊距離", 'D', false, DistanceMode::PointToPoint),
                makeCand(AnnotationKind::HorizDist,  "水平距離", 'H', false, DistanceMode::PointToPoint),
                makeCand(AnnotationKind::VertDist,   "垂直距離", 'V', false, DistanceMode::PointToPoint),
            };
        }
        if (bIsWholeLine)
            return { makeCand(AnnotationKind::Distance, "垂距", 'D', false, DistanceMode::PointToLine) };
        if (bIsWholeArc || bIsWholeCircle)
            return { makeCand(AnnotationKind::Distance, "距離", 'D', false, DistanceMode::PointToPoint) };
        return { makeCand(AnnotationKind::Distance, "距離", 'D', false, DistanceMode::PointToPoint) };
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
                return { makeCand(AnnotationKind::Distance, "線間距", 'D', false, DistanceMode::LineToLine) };
            }
            // 相交 → 角度；若垂直，角度是固定 90°，標記為 Reference
            // （Auto_DIM.md 第六節：「若 Perpendicular → 角度固定，只提供 90° Reference」）
            double dot = static_cast<double>(QVector2D::dotProduct(dirA, dirB));
            bool isPerp = std::abs(dot) < 1e-4;

            auto angleCand = makeCand(AnnotationKind::AngleDim, "夾角", 'A');
            angleCand.isReferenceOnly = isPerp;

            if (isPerp) {
                // 垂直：夾角恆為 90°，補角也是 90°，兩者相同沒有意義，
                // 只回傳一個候選即可
                return { angleCand };
            }

            // 非垂直相交：夾角／補角是同一組交點的兩種角度讀法
            // （Auto_DIM.md 第三節「Line + Line 相交：Angle / Supplement Angle」），
            // 用 Tab 切換
            auto supplementCand = makeCand(AnnotationKind::AngleDim, "補角", 'S',
                                            false, DistanceMode::PointToPoint,
                                            /*useSupplementAngle=*/true);
            return { angleCand, supplementCand };
        }
    }

    // ── a 是整條線 + b 是點（垂距，順序調換）───────────────────────────────
    if (aIsWholeLine && bIsPoint)
        return { makeCand(AnnotationKind::Distance, "垂距", 'D', false, DistanceMode::PointToLine) };

    // ── a 是整條線 + b 是圓/弧（圓心到線垂距）──────────────────────────────
    if (aIsWholeLine && (bIsWholeArc || bIsWholeCircle))
        return { makeCand(AnnotationKind::Distance, "垂距", 'D', false, DistanceMode::PointToLine) };

    // ── 其他組合（圓/弧 整體 + 圓/弧 整體）→ 兩圓心距 ──────────────────────
    const bool aIsWholeArc    = geomA && geomA->type == SketchGeometryType::Arc
                                && a.handle == GeomHandle::WholeGeom;
    const bool aIsWholeCircle = geomA && geomA->type == SketchGeometryType::Circle
                                && a.handle == GeomHandle::WholeGeom;
    if ((aIsWholeArc || aIsWholeCircle) && (bIsWholeArc || bIsWholeCircle))
        return { makeCand(AnnotationKind::Distance, "距離", 'D', false, DistanceMode::PointToPoint) };

    return {};  // invalid
}

// ─────────────────────────────────────────────────────────────────────────────
// candidatesPrompt
// ─────────────────────────────────────────────────────────────────────────────

QString GeneralDimClassifier::candidatesPrompt(const QList<Candidate>& candidates, int highlightIndex)
{
    if (candidates.isEmpty()) return QString();
    QStringList parts;
    for (int i = 0; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        QString item = c.label;
        if (!c.shortcut.isNull()) item += QString("(%1)").arg(c.shortcut);
        if (c.isReferenceOnly) item += QStringLiteral("[Reference]");
        if (i == highlightIndex) item = "[" + item + "]";
        parts << item;
    }
    QString prompt = "GDIM " + parts.join(" / ");
    if (candidates.size() > 1)
        prompt += "\xe3\x80\x80[Tab/Space 切換候選，Enter 確認]";
    else
        prompt += "\xe3\x80\x80[Enter 確認]";
    return prompt;
}

// ─────────────────────────────────────────────────────────────────────────────
// pickCandidateByMouse —— Auto_DIM.md 第八節
// ─────────────────────────────────────────────────────────────────────────────

int GeneralDimClassifier::pickCandidateByMouse(
    const QList<Candidate>& candidates, const GeomRef& primaryRef,
    const Sketch* sketch, const QVector2D& mousePlanePt)
{
    if (!sketch || candidates.isEmpty()) return -1;

    auto indexOfKind = [&](AnnotationKind k) -> int {
        for (int i = 0; i < candidates.size(); ++i)
            if (candidates[i].kind == k) return i;
        return -1;
    };

    auto* geom = sketch->findGeometry(primaryRef.geomUuid);
    if (!geom) return -1;

    if (primaryRef.handle != GeomHandle::WholeGeom) return -1;

    // ── Circle：圓內 → 半徑；圓外 → 直徑 ─────────────────────────────────────
    if (auto* circ = dynamic_cast<const SketchCircle*>(geom)) {
        float dist = (mousePlanePt - circ->center).length();
        int idx = indexOfKind(dist < circ->radius
                               ? AnnotationKind::Radius
                               : AnnotationKind::Diameter);
        return idx;
    }

    // ── Arc：靠近圓心 → 半徑；靠近弧本身/外側 → 弧長 ─────────────────────────
    if (dynamic_cast<const SketchArc*>(geom)) {
        QVector2D center = GeomRef(primaryRef.geomUuid, GeomHandle::Center).resolvePosition(sketch);
        QVector2D start   = GeomRef(primaryRef.geomUuid, GeomHandle::Start).resolvePosition(sketch);
        float radius = (start - center).length();
        float dist   = (mousePlanePt - center).length();
        // 靠近圓心（< 60% 半徑）視為「圓心附近」，其餘（含弧上/弧外側）視為「弧外側」
        int idx = indexOfKind(dist < radius * 0.6f
                               ? AnnotationKind::Radius
                               : AnnotationKind::ArcLength);
        return idx;
    }

    return -1;  // Line/Point 的候選之間沒有自然的滑鼠位置對應關係
}

} // namespace aicad::cad
