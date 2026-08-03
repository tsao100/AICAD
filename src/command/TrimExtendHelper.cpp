/**
 * @file TrimExtendHelper.cpp
 * @brief 見 TrimExtendHelper.h 檔頭說明。
 *
 * v2 修正紀錄（測試回饋後）：
 *  - 修正 Arc 世界/平面座標混淆的系統性 bug：SketchArc::curve 內部是「世界」
 *    座標（Sketch::addArcGeom() 用 planeToWorld() 建構），先前版本誤把
 *    arc->curve->Value() 的 .X()/.Y() 直接當平面座標使用，只有在草圖恰好
 *    在 XY 平面且原點與世界原點重合時數值才會剛好相同。現在全面比照
 *    SketchGripProvider::computeGrips() 對 Arc grip 拖曳的正確作法：內部
 *    運算全程用世界座標的 gp_Pnt／Handle(Geom_Circle)，只在最終要呼叫
 *    Sketch::movePoint()（需要平面座標）或要組出要傳給
 *    Sketch::addArcGeom()（該函式內部自己會 planeToWorld，需要平面座標）
 *    的地方才轉換。
 *  - FILLET/CHAMFER 修正共用角點 bug：兩條線在真實草圖中經常本來就共用
 *    同一個端點（相連的兩段直線），先前版本會把同一個 SketchPoint 先後
 *    移動到兩個不同的切點/倒角點，第二次呼叫覆蓋第一次，導致弧/倒角線
 *    位置錯誤、且其中一條線沒有被正確裁切。現在會偵測這個情形並自動
 *    「解耦」——保留原點給其中一條線，另一條線改配一個新建的獨立點。
 *  - 新增 Spline／Ellipse 作為剪切邊/邊界邊的支援（見下方說明；兩者都
 *    不支援作為 TRIM/EXTEND 的「目標」，只能拿來剪切/延伸別的幾何）。
 */
#include "TrimExtendHelper.h"

#include "../cad/Sketch.h"
#include "../cad/sketch/SketchGeom2DMath.h"

#include <ElCLib.hxx>
#include <Geom_Circle.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GC_MakeCircle.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <QDebug>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <optional>

namespace aicad {
namespace command {
namespace trimext {

using namespace cad;
namespace g2d = cad::geom2d;

namespace {

constexpr double kParamEps = 1e-4;   ///< 線段參數 t 的邊界容許誤差
constexpr double kPosEps   = 1e-4;   ///< findOrCreatePoint 的座標容許誤差（sketch 平面單位）

// ─────────────────────────────────────────────────────────────────────────
// 世界／平面座標轉換輔助（見檔頭「v2 修正紀錄」）
// ─────────────────────────────────────────────────────────────────────────

QVector2D worldToPlane(cad::Sketch* sketch, const gp_Pnt& worldPt)
{
    if (!sketch || !sketch->plane())
        return QVector2D(float(worldPt.X()), float(worldPt.Y()));
    return sketch->plane()->toPlane(
        QVector3D(float(worldPt.X()), float(worldPt.Y()), float(worldPt.Z())));
}

gp_Pnt planeToWorld(cad::Sketch* sketch, const QVector2D& planePt)
{
    if (!sketch) return gp_Pnt(planePt.x(), planePt.y(), 0.0);
    const QVector3D w = sketch->planeToWorld(planePt);
    return gp_Pnt(w.x(), w.y(), w.z());
}

// ─────────────────────────────────────────────────────────────────────────
// GeomShape2D — Line/Circle/Arc 的統一幾何描述，供交點運算使用
// ─────────────────────────────────────────────────────────────────────────

struct GeomShape2D {
    enum class Kind { Line, Circle, Arc } kind = Kind::Line;

    // Line：平面座標
    QVector2D p1, p2;

    // Circle / Arc：center/radius 為平面座標／單位，供 geom2d 純數學函式
    // （不需要知道世界座標系）使用。
    QVector2D center;
    double    radius     = 0.0;
    double    startAngle = 0.0;   ///< Arc only（弧度，與 curve->FirstParameter() 一致）
    double    endAngle   = 0.0;   ///< Arc only

    /// Circle／Arc 專用，**世界座標**：供 ElCLib::Parameter()/Value() 使用。
    /// Arc 的情形直接沿用來源 arc->curve 的 BasisCurve（確保角度參考系與
    /// FirstParameter()/LastParameter() 完全一致）；Circle 的情形是用三個
    /// 世界座標取樣點透過 GC_MakeCircle 現場建構（讓 OCCT 自動推導正確的
    /// 平面法向量，不管草圖在 XY/XZ/YZ 或任意平面上都正確，不能自行假設
    /// 法向量固定是 (0,0,1)）。
    Handle(Geom_Circle) occCircle;
};

/// 從單一 SketchGeometry 萃取 GeomShape2D。只支援 Line／Circle／Arc。
std::optional<GeomShape2D> extractShape(cad::Sketch* sketch, SketchGeometry* g)
{
    if (!sketch || !g) return std::nullopt;

    if (auto* line = dynamic_cast<SketchLine*>(g)) {
        GeomShape2D s;
        s.kind = GeomShape2D::Kind::Line;
        s.p1 = line->start;
        s.p2 = line->end;
        return s;
    }

    if (auto* circ = dynamic_cast<SketchCircle*>(g)) {
        GeomShape2D s;
        s.kind = GeomShape2D::Kind::Circle;
        s.center = circ->center;
        s.radius = circ->radius;

        // 用三個世界座標取樣點建構 Geom_Circle，讓 OCCT 自動推導正確的
        // 平面法向量。
        const double r = circ->radius;
        const QVector2D sp0 = circ->center + QVector2D(float(r), 0.0f);
        const QVector2D sp1 = circ->center + QVector2D(float(r * std::cos(2.0 * M_PI / 3.0)),
                                                        float(r * std::sin(2.0 * M_PI / 3.0)));
        const QVector2D sp2 = circ->center + QVector2D(float(r * std::cos(4.0 * M_PI / 3.0)),
                                                        float(r * std::sin(4.0 * M_PI / 3.0)));
        GC_MakeCircle circMaker(planeToWorld(sketch, sp0),
                                planeToWorld(sketch, sp1),
                                planeToWorld(sketch, sp2));
        if (circMaker.IsDone()) s.occCircle = circMaker.Value();
        return s;
    }

    if (auto* arc = dynamic_cast<SketchArc*>(g)) {
        if (arc->curve.IsNull()) return std::nullopt;
        Handle(Geom_Circle) baseCircle = Handle(Geom_Circle)::DownCast(arc->curve->BasisCurve());
        if (baseCircle.IsNull()) return std::nullopt;

        GeomShape2D s;
        s.kind = GeomShape2D::Kind::Arc;
        s.center     = worldToPlane(sketch, baseCircle->Location());
        s.radius     = baseCircle->Radius();
        s.startAngle = arc->curve->FirstParameter();
        s.endAngle   = arc->curve->LastParameter();
        s.occCircle  = baseCircle;
        return s;
    }

    // Polyline／Ellipse／Point：不支援作為 TRIM/EXTEND 的目標。Spline 另有
    // extractCuttingShapes() 處理（見下方）。
    return std::nullopt;
}

/// 從一個「剪切邊/邊界邊」幾何萃取可用於求交點的形狀清單（可能不只一個）。
///  - Line／Circle／Arc：與 extractShape() 相同，回傳單一形狀。
///  - Spline：沒有解析交點公式（NURBS 曲線一般需要數值疊代求解），這裡用
///    其取樣點陣列（SketchGeometry::points，繪製時的容差取樣點）折線近似，
///    每相鄰兩點視為一段 Line 分別求交——這是「近似」而非精確的曲線交點，
///    但已足以在大多數實務裁切/延伸情境下正確判斷交點位置。Spline 本身
///    不支援作為 TRIM/EXTEND 的「目標」（裁切/延伸 Spline 本身需要能表達
///    「部分 Spline」，目前資料模型沒有這個能力）。
///  - Ellipse：同樣不支援作為目標（原因見 TrimExtendHelper.h），但可以
///    作為剪切邊/邊界邊——不過橢圓交點沒有納入本函式的 GeomShape2D 統一
///    模型（橢圓不是圓，circleCircleIntersect 等公式不適用），改由呼叫端
///    在需要時另外呼叫 geom2d::lineEllipseIntersect()（只有目標是 Line
///    時才有意義，見 trimLine()/extendLine()）。
QVector<GeomShape2D> extractCuttingShapes(cad::Sketch* sketch, SketchGeometry* g)
{
    QVector<GeomShape2D> result;
    if (!sketch || !g) return result;

    if (auto* spline = dynamic_cast<SketchSpline*>(g)) {
        const QVector<QVector2D>& pts = spline->points;
        for (int i = 0; i + 1 < pts.size(); ++i) {
            GeomShape2D s;
            s.kind = GeomShape2D::Kind::Line;
            s.p1 = pts[i];
            s.p2 = pts[i + 1];
            result.append(s);
        }
        return result;
    }

    if (auto shape = extractShape(sketch, g)) {
        result.append(*shape);
    }
    return result;
}

QVector2D pointOnCircle(cad::Sketch* sketch, const Handle(Geom_Circle)& circ, double angle)
{
    if (circ.IsNull()) return QVector2D();
    const gp_Pnt p = ElCLib::Value(angle, circ->Circ());
    return worldToPlane(sketch, p);
}

double angleOfPoint(cad::Sketch* sketch, const Handle(Geom_Circle)& circ, const QVector2D& planePt)
{
    if (circ.IsNull()) return 0.0;
    return ElCLib::Parameter(circ->Circ(), planeToWorld(sketch, planePt));
}

// ─────────────────────────────────────────────────────────────────────────
// 交點運算：intersectRaw（無界）+ clipToShape（依形狀自身範圍裁切）
// ─────────────────────────────────────────────────────────────────────────

/// 兩形狀「視為無界」（直線視為無限長、Arc 視為完整圓）的交點。
QVector<QVector2D> intersectRaw(const GeomShape2D& a, const GeomShape2D& b)
{
    const bool aIsLine = (a.kind == GeomShape2D::Kind::Line);
    const bool bIsLine = (b.kind == GeomShape2D::Kind::Line);

    if (aIsLine && bIsLine) {
        QVector<QVector2D> result;
        if (auto p = g2d::lineLineIntersect(a.p1, a.p2, b.p1, b.p2)) result.append(*p);
        return result;
    }
    if (aIsLine) {
        return g2d::lineCircleIntersect(a.p1, a.p2, b.center, b.radius);
    }
    if (bIsLine) {
        return g2d::lineCircleIntersect(b.p1, b.p2, a.center, a.radius);
    }
    return g2d::circleCircleIntersect(a.center, a.radius, b.center, b.radius);
}

/// 依形狀 s 自身的範圍（Line 的線段兩端 / Arc 的起訖角）過濾交點清單。
/// Circle 沒有邊界，全部保留。
QVector<QVector2D> clipToShape(cad::Sketch* sketch, const QVector<QVector2D>& pts,
                               const GeomShape2D& s)
{
    QVector<QVector2D> result;
    for (const QVector2D& pt : pts) {
        if (s.kind == GeomShape2D::Kind::Line) {
            const double t = g2d::paramOnLine(pt, s.p1, s.p2);
            if (t < -kParamEps || t > 1.0 + kParamEps) continue;
        } else if (s.kind == GeomShape2D::Kind::Arc) {
            if (s.occCircle.IsNull()) continue;
            const double ang = angleOfPoint(sketch, s.occCircle, pt);
            if (!g2d::angleInSweep(ang, s.startAngle, s.endAngle)) continue;
        }
        result.append(pt);
    }
    return result;
}

/// TRIM 用：兩形狀都裁切到各自的範圍內。
QVector<QVector2D> intersectBounded(cad::Sketch* sketch, const GeomShape2D& a, const GeomShape2D& b)
{
    return clipToShape(sketch, clipToShape(sketch, intersectRaw(a, b), a), b);
}

/// 收集 target 與 cuttingUuids 清單中每個幾何的所有交點（平面座標）。
/// 會自動處理 Spline（折線近似展開多段）與 Ellipse（僅在 target 是 Line
/// 時，額外呼叫 geom2d::lineEllipseIntersect()）。
QVector<QVector2D> collectIntersections(cad::Sketch* sketch, const GeomShape2D& targetShape,
                                        const QString& targetUuid, const QStringList& cuttingUuids)
{
    QVector<QVector2D> pts;

    for (const QString& cutUuid : cuttingUuids) {
        if (cutUuid == targetUuid) continue;
        SketchGeometry* cg = sketch->findGeometry(cutUuid);
        if (!cg) continue;

        if (auto* ell = dynamic_cast<SketchEllipse*>(cg)) {
            if (targetShape.kind != GeomShape2D::Kind::Line) continue;  // 僅支援 Line vs Ellipse
            const QVector<QVector2D> hits = g2d::lineEllipseIntersect(
                targetShape.p1, targetShape.p2, ell->center,
                ell->majorRadius, ell->minorRadius, ell->angle);
            for (const QVector2D& h : hits) pts.append(h);
            continue;
        }

        for (const GeomShape2D& cutShape : extractCuttingShapes(sketch, cg))
            for (const QVector2D& pt : intersectBounded(sketch, targetShape, cutShape))
                pts.append(pt);
    }
    return pts;
}

// ─────────────────────────────────────────────────────────────────────────
// findOrCreatePoint — 盡量與既有 SketchPoint 共點，避免每次裁切都新增
// 一個座標重合但 UUID 不同的獨立點
// ─────────────────────────────────────────────────────────────────────────

QString findOrCreatePoint(cad::Sketch* sketch, const QVector2D& pos)
{
    for (SketchPoint* pt : sketch->points()) {
        if ((pt->pos - pos).lengthSquared() < float(kPosEps * kPosEps)) {
            return pt->uuid;
        }
    }
    return sketch->addPoint(pos, SketchPoint::Origin::Intersection);
}

// ─────────────────────────────────────────────────────────────────────────
// TRIM — 依目標型別分派
// ─────────────────────────────────────────────────────────────────────────

bool trimLine(cad::Sketch* sketch, SketchLine* line, const GeomShape2D& targetShape,
             const QStringList& cuttingUuids, const QVector2D& clickPt)
{
    QVector<double> params;
    for (const QVector2D& pt : collectIntersections(sketch, targetShape, line->uuid, cuttingUuids))
        params.append(g2d::paramOnLine(pt, targetShape.p1, targetShape.p2));

    if (params.isEmpty()) return false;

    std::sort(params.begin(), params.end());
    const double tClick = g2d::paramOnLine(clickPt, targetShape.p1, targetShape.p2);

    double lower = 0.0;
    double upper = 1.0;
    for (double t : params) {
        if (t <= tClick + kParamEps && t > lower) lower = t;
        if (t >= tClick - kParamEps && t < upper) upper = t;
    }
    if (upper - lower < 1e-6) return false;  // 點擊處剛好落在交點上，沒有可刪除的段落

    const QVector2D lowerPt = targetShape.p1 + (targetShape.p2 - targetShape.p1) * float(lower);
    const QVector2D upperPt = targetShape.p1 + (targetShape.p2 - targetShape.p1) * float(upper);

    const QString origStartUuid = line->startUuid;
    const QString origEndUuid   = line->endUuid;
    const GeomRole origRole     = line->role;
    const QVector2D origP1      = targetShape.p1;
    const QVector2D origP2      = targetShape.p2;

    sketch->removeGeometry(line->uuid);  // 級聯刪除相關約束/標註（Phase 0 已確認）

    if (lower > 1e-6) {
        const QString lowerUuid = findOrCreatePoint(sketch, lowerPt);
        sketch->addLineGeom(origP1, lowerPt, origStartUuid, lowerUuid, origRole);
    }
    if (upper < 1.0 - 1e-6) {
        const QString upperUuid = findOrCreatePoint(sketch, upperPt);
        sketch->addLineGeom(upperPt, origP2, upperUuid, origEndUuid, origRole);
    }

    sketch->solveConstraints();
    Q_EMIT sketch->rebuildRequested();
    return true;
}

bool trimCircle(cad::Sketch* sketch, SketchCircle* circ, const GeomShape2D& targetShape,
                const QStringList& cuttingUuids, const QVector2D& clickPt)
{
    if (targetShape.occCircle.IsNull()) return false;

    QVector<double> angles;
    for (const QVector2D& pt : collectIntersections(sketch, targetShape, circ->uuid, cuttingUuids))
        angles.append(g2d::normalizeAngle(angleOfPoint(sketch, targetShape.occCircle, pt)));

    if (angles.size() < 2) return false;  // 圓至少要兩個交點才能裁切成弧

    std::sort(angles.begin(), angles.end());
    const double clickAngle =
        g2d::normalizeAngle(angleOfPoint(sketch, targetShape.occCircle, clickPt));

    // 找出 clickAngle 所在的相鄰交點區間 [lower, upper]（處理跨越 0/2π）。
    double lower = angles.last();
    double upper = angles.first();
    for (int i = 0; i < angles.size(); ++i) {
        const double a = angles[i];
        const double b = angles[(i + 1) % angles.size()];
        if (g2d::angleInSweep(clickAngle, a, b)) {
            lower = a;
            upper = b;
            break;
        }
    }
    if (std::abs(upper - lower) < 1e-6) return false;

    // 保留「從 upper 逆時針掃到 lower」的弧（即跳過被點擊的 [lower, upper] 段）。
    const QVector2D lowerPt = pointOnCircle(sketch, targetShape.occCircle, lower);
    const QVector2D upperPt = pointOnCircle(sketch, targetShape.occCircle, upper);

    double span = lower - upper;
    if (span < 0.0) span += 2.0 * M_PI;
    const double midAngle = upper + span / 2.0;
    const QVector2D midPt = pointOnCircle(sketch, targetShape.occCircle, midAngle);

    const QString origCenterUuid = circ->centerUuid;
    const GeomRole origRole      = circ->role;

    const QString lowerUuid = findOrCreatePoint(sketch, lowerPt);
    const QString upperUuid = findOrCreatePoint(sketch, upperPt);

    sketch->removeGeometry(circ->uuid);

    const QString newUuid =
        sketch->addArcGeom(upperPt, midPt, lowerPt, upperUuid, lowerUuid, origCenterUuid);
    if (auto* ng = sketch->findGeometry(newUuid)) ng->role = origRole;

    sketch->solveConstraints();
    Q_EMIT sketch->rebuildRequested();
    return !newUuid.isEmpty();
}

bool trimArc(cad::Sketch* sketch, SketchArc* arc, const GeomShape2D& targetShape,
            const QStringList& cuttingUuids, const QVector2D& clickPt)
{
    if (targetShape.occCircle.IsNull()) return false;

    QVector<double> angles;
    for (const QVector2D& pt : collectIntersections(sketch, targetShape, arc->uuid, cuttingUuids))
        angles.append(g2d::normalizeAngle(angleOfPoint(sketch, targetShape.occCircle, pt)));

    if (angles.isEmpty()) return false;

    std::sort(angles.begin(), angles.end());
    const double s0 = g2d::normalizeAngle(targetShape.startAngle);
    const double e0 = g2d::normalizeAngle(targetShape.endAngle);
    const double clickAngle =
        g2d::normalizeAngle(angleOfPoint(sketch, targetShape.occCircle, clickPt));

    // 在弧自身的 [s0, e0] 範圍內（含端點）找出緊鄰 clickAngle 的邊界：
    // lower 為 clickAngle 之前（含 s0）最近的交點/端點，upper 為之後
    // （含 e0）最近的交點/端點。
    double lower = s0;
    double upper = e0;
    auto within = [&](double a) { return g2d::angleInSweep(a, s0, e0); };

    for (double a : angles) {
        if (!within(a)) continue;
        double distFromStart = a - s0;
        if (distFromStart < 0.0) distFromStart += 2.0 * M_PI;
        double clickFromStart = clickAngle - s0;
        if (clickFromStart < 0.0) clickFromStart += 2.0 * M_PI;

        double lowerFromStart = lower - s0; if (lowerFromStart < 0.0) lowerFromStart += 2.0*M_PI;
        double upperFromStart = upper - s0; if (upperFromStart < 0.0) upperFromStart += 2.0*M_PI;

        if (distFromStart <= clickFromStart && distFromStart >= lowerFromStart) lower = a;
        if (distFromStart >= clickFromStart && distFromStart <= upperFromStart) upper = a;
    }

    double keepSpanLower = lower - s0; if (keepSpanLower < 0.0) keepSpanLower += 2.0*M_PI;
    double keepSpanUpper = e0 - upper; if (keepSpanUpper < 0.0) keepSpanUpper += 2.0*M_PI;
    const bool hasLowerPart = keepSpanLower > 1e-6;
    const bool hasUpperPart = keepSpanUpper > 1e-6;

    if (!hasLowerPart && !hasUpperPart) return false;  // 點擊處剛好在端點上，沒有可刪除段落

    const QString origStartUuid  = arc->startUuid;
    const QString origEndUuid    = arc->endUuid;
    const QString origCenterUuid = arc->centerUuid;
    const GeomRole origRole      = arc->role;
    const Handle(Geom_Circle) occCircle = targetShape.occCircle;

    sketch->removeGeometry(arc->uuid);

    if (hasLowerPart) {
        const QVector2D newStartPt = pointOnCircle(sketch, occCircle, s0);
        const QVector2D newEndPt   = pointOnCircle(sketch, occCircle, lower);
        double mid = s0 + keepSpanLower / 2.0;
        const QVector2D newMidPt = pointOnCircle(sketch, occCircle, mid);
        const QString endUuid = findOrCreatePoint(sketch, newEndPt);
        const QString newUuid = sketch->addArcGeom(newStartPt, newMidPt, newEndPt,
                                                    origStartUuid, endUuid, origCenterUuid);
        if (auto* ng = sketch->findGeometry(newUuid)) ng->role = origRole;
    }
    if (hasUpperPart) {
        const QVector2D newStartPt = pointOnCircle(sketch, occCircle, upper);
        const QVector2D newEndPt   = pointOnCircle(sketch, occCircle, e0);
        double mid = upper + keepSpanUpper / 2.0;
        const QVector2D newMidPt = pointOnCircle(sketch, occCircle, mid);
        const QString startUuid = findOrCreatePoint(sketch, newStartPt);
        const QString newUuid = sketch->addArcGeom(newStartPt, newMidPt, newEndPt,
                                                    startUuid, origEndUuid, origCenterUuid);
        if (auto* ng = sketch->findGeometry(newUuid)) ng->role = origRole;
    }

    sketch->solveConstraints();
    Q_EMIT sketch->rebuildRequested();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────
// EXTEND — 依目標型別分派
// ─────────────────────────────────────────────────────────────────────────

bool extendLine(cad::Sketch* sketch, SketchLine* line, const GeomShape2D& targetShape,
                const QStringList& boundaryUuids, const QVector2D& clickPt)
{
    const double distToStart = double((clickPt - targetShape.p1).length());
    const double distToEnd   = double((clickPt - targetShape.p2).length());
    const bool extendStart   = distToStart < distToEnd;
    const QString movingUuid = extendStart ? line->startUuid : line->endUuid;

    QVector<double> candidateParams;

    for (const QString& boundUuid : boundaryUuids) {
        if (boundUuid == line->uuid) continue;
        SketchGeometry* bg = sketch->findGeometry(boundUuid);
        if (!bg) continue;

        if (auto* ell = dynamic_cast<SketchEllipse*>(bg)) {
            const QVector<QVector2D> hits = g2d::lineEllipseIntersect(
                targetShape.p1, targetShape.p2, ell->center,
                ell->majorRadius, ell->minorRadius, ell->angle);
            for (const QVector2D& pt : hits)
                candidateParams.append(g2d::paramOnLine(pt, targetShape.p1, targetShape.p2));
            continue;
        }

        for (const GeomShape2D& boundShape : extractCuttingShapes(sketch, bg)) {
            const QVector<QVector2D> pts =
                clipToShape(sketch, intersectRaw(targetShape, boundShape), boundShape);
            for (const QVector2D& pt : pts)
                candidateParams.append(g2d::paramOnLine(pt, targetShape.p1, targetShape.p2));
        }
    }
    if (candidateParams.isEmpty()) return false;

    const double tMoving = extendStart ? 0.0 : 1.0;
    double bestT = tMoving;
    bool found = false;
    for (double t : candidateParams) {
        const bool extending = extendStart ? (t < tMoving - kParamEps)
                                           : (t > tMoving + kParamEps);
        if (!extending) continue;
        if (!found) { bestT = t; found = true; continue; }
        if (extendStart) { if (t > bestT) bestT = t; }
        else              { if (t < bestT) bestT = t; }
    }
    if (!found) return false;

    const QVector2D newPos = targetShape.p1 + (targetShape.p2 - targetShape.p1) * float(bestT);
    sketch->movePoint(movingUuid, newPos);
    sketch->solveConstraints();
    Q_EMIT sketch->rebuildRequested();
    return true;
}

bool extendArc(cad::Sketch* sketch, SketchArc* arc, const GeomShape2D& targetShape,
               const QStringList& boundaryUuids, const QVector2D& clickPt)
{
    if (targetShape.occCircle.IsNull()) return false;
    const Handle(Geom_Circle)& circ = targetShape.occCircle;

    const QVector2D startPt = pointOnCircle(sketch, circ, targetShape.startAngle);
    const QVector2D endPt   = pointOnCircle(sketch, circ, targetShape.endAngle);
    const bool extendStartSide = (clickPt - startPt).length() < (clickPt - endPt).length();

    GeomShape2D fullCircleShape = targetShape;
    fullCircleShape.kind = GeomShape2D::Kind::Circle;  // 暫時視為完整圓，交點計算時略過 sweep 篩選

    QVector<double> candidateAngles;
    for (const QString& boundUuid : boundaryUuids) {
        if (boundUuid == arc->uuid) continue;
        SketchGeometry* bg = sketch->findGeometry(boundUuid);
        if (!bg) continue;

        if (auto* ell = dynamic_cast<SketchEllipse*>(bg)) {
            // Arc 延伸邊界若是橢圓：本 MVP 不支援（橢圓與圓的解析交點需要
            // 解四次方程式，複雜度明顯更高），略過。
            Q_UNUSED(ell);
            continue;
        }

        for (const GeomShape2D& boundShape : extractCuttingShapes(sketch, bg)) {
            const QVector<QVector2D> pts =
                clipToShape(sketch, intersectRaw(fullCircleShape, boundShape), boundShape);
            for (const QVector2D& pt : pts)
                candidateAngles.append(g2d::normalizeAngle(angleOfPoint(sketch, circ, pt)));
        }
    }
    if (candidateAngles.isEmpty()) return false;

    const double s0 = g2d::normalizeAngle(targetShape.startAngle);
    const double e0 = g2d::normalizeAngle(targetShape.endAngle);

    double newAngle = extendStartSide ? s0 : e0;
    bool found = false;

    for (double ang : candidateAngles) {
        if (g2d::angleInSweep(ang, s0, e0)) continue;  // 落在既有弧段內部，不是延伸

        if (extendStartSide) {
            double delta = ang - s0; delta = std::fmod(delta, 2.0*M_PI);
            if (delta > 0.0) delta -= 2.0*M_PI;
            delta = -delta;
            double bestDelta = newAngle - s0; bestDelta = std::fmod(bestDelta, 2.0*M_PI);
            if (bestDelta > 0.0) bestDelta -= 2.0*M_PI;
            bestDelta = -bestDelta;
            if (!found || delta < bestDelta) { newAngle = ang; found = true; }
        } else {
            double delta = ang - e0; delta = std::fmod(delta, 2.0*M_PI);
            if (delta < 0.0) delta += 2.0*M_PI;
            double bestDelta = newAngle - e0; bestDelta = std::fmod(bestDelta, 2.0*M_PI);
            if (bestDelta < 0.0) bestDelta += 2.0*M_PI;
            if (!found || delta < bestDelta) { newAngle = ang; found = true; }
        }
    }
    if (!found) return false;

    const double newStart = extendStartSide ? newAngle : targetShape.startAngle;
    const double newEnd   = extendStartSide ? targetShape.endAngle : newAngle;

    const QVector2D newStartPt = pointOnCircle(sketch, circ, newStart);
    const QVector2D newEndPt   = pointOnCircle(sketch, circ, newEnd);
    double span = newEnd - newStart; span = std::fmod(span, 2.0*M_PI); if (span < 0.0) span += 2.0*M_PI;
    const QVector2D newMidPt = pointOnCircle(sketch, circ, newStart + span / 2.0);

    // ⚠️ 直接重建 arc->curve（不經過 Sketch::addArcGeom()），所以這裡要用
    // 世界座標的 gp_Pnt，而不是平面座標（見檔頭「v2 修正紀錄」）。
    GC_MakeArcOfCircle maker(planeToWorld(sketch, newStartPt),
                             planeToWorld(sketch, newMidPt),
                             planeToWorld(sketch, newEndPt));
    if (!maker.IsDone()) {
        qWarning() << "[TrimExtendHelper] Arc 延伸失敗（三點共線或重合），uuid=" << arc->uuid;
        return false;
    }
    arc->curve = maker.Value();

    sketch->movePoint(arc->startUuid, newStartPt);
    sketch->movePoint(arc->endUuid, newEndPt);

    sketch->solveConstraints();
    Q_EMIT sketch->rebuildRequested();
    return true;
}

} // 匿名 namespace

// ─────────────────────────────────────────────────────────────────────────
// 對外 API
// ─────────────────────────────────────────────────────────────────────────

bool trimAt(cad::Sketch* sketch, const QString& targetUuid,
           const QStringList& cuttingUuids, const QVector2D& clickPt)
{
    if (!sketch) return false;
    SketchGeometry* target = sketch->findGeometry(targetUuid);
    if (!target) return false;

    auto shape = extractShape(sketch, target);
    if (!shape) return false;

    switch (shape->kind) {
    case GeomShape2D::Kind::Line:
        return trimLine(sketch, static_cast<SketchLine*>(target), *shape, cuttingUuids, clickPt);
    case GeomShape2D::Kind::Circle:
        return trimCircle(sketch, static_cast<SketchCircle*>(target), *shape, cuttingUuids, clickPt);
    case GeomShape2D::Kind::Arc:
        return trimArc(sketch, static_cast<SketchArc*>(target), *shape, cuttingUuids, clickPt);
    }
    return false;
}

bool extendAt(cad::Sketch* sketch, const QString& targetUuid,
             const QStringList& boundaryUuids, const QVector2D& clickPt)
{
    if (!sketch) return false;
    SketchGeometry* target = sketch->findGeometry(targetUuid);
    if (!target) return false;

    auto shape = extractShape(sketch, target);
    if (!shape) return false;

    switch (shape->kind) {
    case GeomShape2D::Kind::Line:
        return extendLine(sketch, static_cast<SketchLine*>(target), *shape, boundaryUuids, clickPt);
    case GeomShape2D::Kind::Arc:
        return extendArc(sketch, static_cast<SketchArc*>(target), *shape, boundaryUuids, clickPt);
    case GeomShape2D::Kind::Circle:
        return false;  // 圓沒有端點可延伸
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────
// FILLET / CHAMFER
//
// 兩者共用同一套「求兩線交點 X → 依點擊位置決定各自方向 dir1/dir2 →
// 決定哪個端點要被移動」邏輯，差異只在最後一步插入的是圓弧還是直線。
// MVP 只支援兩條直線（見標頭檔說明）。
// ─────────────────────────────────────────────────────────────────────────

namespace {

/// 準備 FILLET/CHAMFER 共用的幾何前置資料。回傳 false 表示前置條件不滿足
/// （非兩條直線、平行/共線、端點退化等），呼叫端應直接回傳 false。
struct FilletChamferSetup {
    SketchLine* line1 = nullptr;
    SketchLine* line2 = nullptr;
    QVector2D   intersection;
    QVector2D   dir1, dir2;     ///< 分別指向 line1/line2 保留側的單位向量
    QString     line1PointUuid; ///< 會被移動到切點/倒角點的 line1 端點
    QString     line2PointUuid; ///< 會被移動到切點/倒角點的 line2 端點
};

/// 若 line1 與 line2 的「近角點」是同一個 SketchPoint（最常見的相連轉角
/// 情形——例如使用者用 LINE 命令連續畫出兩段相接的線，共用同一個端點），
/// 圓角/倒角後兩條線各自需要落在不同的座標，不能再共用同一個點，否則後
/// 呼叫的 movePoint() 會覆蓋先呼叫的結果（這是先前版本「弧位置不對、邊
/// 沒有被裁切」的根本原因）。這裡偵測到共用時，保留原點給 line1，另外
/// 幫 line2 建立一個新的獨立點並改寫 line2 的 startUuid/endUuid 欄位指向
/// 新點（Sketch::curvesReferencingPoint() 等查詢都是即時掃描
/// m_geometries，不是快取的反向索引，因此直接改寫欄位是安全的）。
void unshareCornerPointIfNeeded(cad::Sketch* sketch, FilletChamferSetup& s)
{
    if (s.line1PointUuid != s.line2PointUuid) return;

    const QString sharedUuid = s.line2PointUuid;
    const QString newUuid = sketch->addPoint(s.intersection, SketchPoint::Origin::Intersection);

    if (s.line2->startUuid == sharedUuid) {
        s.line2->startUuid = newUuid;
    } else if (s.line2->endUuid == sharedUuid) {
        s.line2->endUuid = newUuid;
    }
    s.line2PointUuid = newUuid;
}

std::optional<FilletChamferSetup> prepareFilletChamfer(cad::Sketch* sketch,
                                                        const QString& line1Uuid,
                                                        const QString& line2Uuid,
                                                        const QVector2D& clickPt1,
                                                        const QVector2D& clickPt2)
{
    if (!sketch || line1Uuid == line2Uuid) return std::nullopt;

    auto* line1 = dynamic_cast<SketchLine*>(sketch->findGeometry(line1Uuid));
    auto* line2 = dynamic_cast<SketchLine*>(sketch->findGeometry(line2Uuid));
    if (!line1 || !line2) return std::nullopt;  // MVP：僅支援兩條直線

    auto xOpt = g2d::lineLineIntersect(line1->start, line1->end, line2->start, line2->end);
    if (!xOpt) return std::nullopt;  // 平行或重合，無法求交點

    FilletChamferSetup setup;
    setup.line1 = line1;
    setup.line2 = line2;
    setup.intersection = *xOpt;

    setup.dir1 = line1->end - line1->start;
    if (setup.dir1.lengthSquared() < 1e-12f) return std::nullopt;
    setup.dir1.normalize();
    if (QVector2D::dotProduct(clickPt1 - setup.intersection, setup.dir1) < 0.0f)
        setup.dir1 = -setup.dir1;

    setup.dir2 = line2->end - line2->start;
    if (setup.dir2.lengthSquared() < 1e-12f) return std::nullopt;
    setup.dir2.normalize();
    if (QVector2D::dotProduct(clickPt2 - setup.intersection, setup.dir2) < 0.0f)
        setup.dir2 = -setup.dir2;

    const double d1s = double((line1->start - setup.intersection).length());
    const double d1e = double((line1->end   - setup.intersection).length());
    setup.line1PointUuid = (d1s < d1e) ? line1->startUuid : line1->endUuid;

    const double d2s = double((line2->start - setup.intersection).length());
    const double d2e = double((line2->end   - setup.intersection).length());
    setup.line2PointUuid = (d2s < d2e) ? line2->startUuid : line2->endUuid;

    unshareCornerPointIfNeeded(sketch, setup);

    return setup;
}

} // 匿名 namespace

bool filletAt(cad::Sketch* sketch, const QString& line1Uuid, const QString& line2Uuid,
             double radius, const QVector2D& clickPt1, const QVector2D& clickPt2)
{
    if (radius < 0.0) return false;

    auto setupOpt = prepareFilletChamfer(sketch, line1Uuid, line2Uuid, clickPt1, clickPt2);
    if (!setupOpt) return false;
    const FilletChamferSetup& s = *setupOpt;

    if (radius <= 1e-9) {
        // 半徑 0：退化為單純延伸相交，不插入圓弧（比照 AutoCAD 行為）。
        sketch->movePoint(s.line1PointUuid, s.intersection);
        sketch->movePoint(s.line2PointUuid, s.intersection);
        sketch->solveConstraints();
        Q_EMIT sketch->rebuildRequested();
        return true;
    }

    const double cosTheta = std::max(-1.0, std::min(1.0, double(QVector2D::dotProduct(s.dir1, s.dir2))));
    const double theta = std::acos(cosTheta);
    if (theta < 1e-4 || theta > M_PI - 1e-4) return false;  // 平行/共線，無法求圓角

    const double halfTheta = theta / 2.0;
    const double tanHalf = std::tan(halfTheta);
    const double sinHalf = std::sin(halfTheta);
    if (std::abs(tanHalf) < 1e-9 || std::abs(sinHalf) < 1e-9) return false;

    const double dAlongRay = radius / tanHalf;
    const QVector2D tangent1 = s.intersection + s.dir1 * float(dAlongRay);
    const QVector2D tangent2 = s.intersection + s.dir2 * float(dAlongRay);

    QVector2D bisector = s.dir1 + s.dir2;
    if (bisector.lengthSquared() < 1e-12f) return false;  // dir1 ≈ -dir2（理論上已被 theta 範圍排除，防呆用）
    bisector.normalize();

    const double centerDist = radius / sinHalf;
    const QVector2D center = s.intersection + bisector * float(centerDist);
    const QVector2D midPt  = center - bisector * float(radius);

    sketch->movePoint(s.line1PointUuid, tangent1);
    sketch->movePoint(s.line2PointUuid, tangent2);

    const QString newUuid = sketch->addArcGeom(tangent1, midPt, tangent2,
                                               s.line1PointUuid, s.line2PointUuid, QString());
    sketch->solveConstraints();
    Q_EMIT sketch->rebuildRequested();
    return !newUuid.isEmpty();
}

bool chamferAt(cad::Sketch* sketch, const QString& line1Uuid, const QString& line2Uuid,
              double dist1, double dist2,
              const QVector2D& clickPt1, const QVector2D& clickPt2)
{
    if (dist1 < 0.0 || dist2 < 0.0) return false;

    auto setupOpt = prepareFilletChamfer(sketch, line1Uuid, line2Uuid, clickPt1, clickPt2);
    if (!setupOpt) return false;
    const FilletChamferSetup& s = *setupOpt;

    if (dist1 <= 1e-9 && dist2 <= 1e-9) {
        // 兩個倒角距離都是 0：退化為單純延伸相交，不插入倒角線。
        sketch->movePoint(s.line1PointUuid, s.intersection);
        sketch->movePoint(s.line2PointUuid, s.intersection);
        sketch->solveConstraints();
        Q_EMIT sketch->rebuildRequested();
        return true;
    }

    const QVector2D chamferPt1 = s.intersection + s.dir1 * float(dist1);
    const QVector2D chamferPt2 = s.intersection + s.dir2 * float(dist2);

    sketch->movePoint(s.line1PointUuid, chamferPt1);
    sketch->movePoint(s.line2PointUuid, chamferPt2);

    const QString newUuid = sketch->addLineGeom(chamferPt1, chamferPt2,
                                                s.line1PointUuid, s.line2PointUuid);
    sketch->solveConstraints();
    Q_EMIT sketch->rebuildRequested();
    return !newUuid.isEmpty();
}

} // namespace trimext
} // namespace command
} // namespace aicad
