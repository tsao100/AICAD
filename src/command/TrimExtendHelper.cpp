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
#include <QVector3D>
#include <QSet>
#include <QPair>
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

/// 取樣 [fromAngle, toAngle] 這段弧（沿 fromAngle→toAngle 的正向/逆時針
/// 方向，繞經 2π 邊界時自動處理），供 TRIM/EXTEND 的 hover 預覽疊層畫成
/// 折線用。純視覺近似，count+1 個取樣點。
QVector<QVector2D> sampleArcSpan(cad::Sketch* sketch, const Handle(Geom_Circle)& circ,
                                 double fromAngle, double toAngle, int count = 24)
{
    QVector<QVector2D> pts;
    if (circ.IsNull() || count < 2) return pts;

    double span = toAngle - fromAngle;
    span = std::fmod(span, 2.0 * M_PI);
    if (span < 0.0) span += 2.0 * M_PI;

    pts.reserve(count + 1);
    for (int i = 0; i <= count; ++i) {
        const double t = double(i) / double(count);
        pts.append(pointOnCircle(sketch, circ, fromAngle + span * t));
    }
    return pts;
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
            // ⚠️ 找到既有點就沿用其 UUID，但仍要把座標校正到「這次真正算
            // 出來的 pos」——否則被沿用的點會停在它原本（在容許誤差內、
            // 但不精確相等）的舊座標，跟緊接著用這個 uuid 建立的新線段/
            // 弧的端點座標（addLineGeom()/addArcGeom() 用的是這次算出來
            // 的 pos，不是點的舊座標）出現真實存在但肉眼難以察覺的落差：
            // 線／弧渲染出來在新位置是對的，但這個 SketchPoint 的權威座標
            // （之後 grip 拖曳、GDIM 標註、其他共用此點的幾何）仍然停在
            // 舊位置沒有跟著更新。用 movePoint() 而不是直接改欄位，確保
            // 同步邏輯（syncGeometryFromPoints()／geometryChanged 訊號）
            // 一併跑到，其餘共用此點的幾何也會一起校正。
            // ⚠️ 用 Origin::Endpoint 而不是 Origin::Intersection——這個點
            // 現在就是新裁切出來那條線／弧的正式端點，語意上跟其他任何
            // 一條線的端點完全一樣，應該用同樣的紅色端點標記渲染
            // （SketchPointAIS::Compute() 對 Origin::Endpoint 特別處理成
            // 固定大小的紅色實心圓點；Origin::Intersection 則是依約束求
            // 解狀態變色的小圖示，肉眼不容易注意到，這正是先前回報「裁
            // 切中間段產生新線段時沒有補新的 sketchpoint」的原因——點其
            // 實有建立，只是視覺上不明顯，容易被誤以為沒有生成）。
            if ((pt->pos - pos).lengthSquared() > 1e-12f)
                sketch->movePoint(pt->uuid, pos);
            return pt->uuid;
        }
    }
    return sketch->addPoint(pos, SketchPoint::Origin::Endpoint);
}


// ─────────────────────────────────────────────────────────────────────────
// TRIM — 依目標型別分派
// ─────────────────────────────────────────────────────────────────────────

struct LineTrimRange { bool valid = false; double lower = 0.0, upper = 1.0; };

/// TRIM 對 Line 目標的核心運算：找出 clickPt 所在、要被移除的參數區間
/// [lower, upper]（t=0 對應 targetShape.p1，t=1 對應 targetShape.p2）。
/// 純運算，不觸碰 sketch——trimLine()（實際執行）與 previewTrimAt()
/// （hover 預覽）共用同一份邏輯，確保兩者結果一致。
LineTrimRange computeLineTrimRange(const GeomShape2D& targetShape,
                                   const QVector<QVector2D>& intersections,
                                   const QVector2D& clickPt)
{
    LineTrimRange r;
    QVector<double> params;
    for (const QVector2D& pt : intersections)
        params.append(g2d::paramOnLine(pt, targetShape.p1, targetShape.p2));
    if (params.isEmpty()) return r;

    std::sort(params.begin(), params.end());
    const double tClick = g2d::paramOnLine(clickPt, targetShape.p1, targetShape.p2);

    double lower = 0.0;
    double upper = 1.0;
    for (double t : params) {
        if (t <= tClick + kParamEps && t > lower) lower = t;
        if (t >= tClick - kParamEps && t < upper) upper = t;
    }
    if (upper - lower < 1e-6) return r;  // 點擊處剛好落在交點上，沒有可刪除的段落

    r.valid = true;
    r.lower = lower;
    r.upper = upper;
    return r;
}

bool trimLine(cad::Sketch* sketch, SketchLine* line, const GeomShape2D& targetShape,
             const QStringList& cuttingUuids, const QVector2D& clickPt)
{
    const auto intersections = collectIntersections(sketch, targetShape, line->uuid, cuttingUuids);
    const LineTrimRange range = computeLineTrimRange(targetShape, intersections, clickPt);
    if (!range.valid) return false;

    const QVector2D lowerPt = targetShape.p1 + (targetShape.p2 - targetShape.p1) * float(range.lower);
    const QVector2D upperPt = targetShape.p1 + (targetShape.p2 - targetShape.p1) * float(range.upper);

    const bool keepsOnlyUpperSide = (range.lower <= 1e-6);        // 只剩 [upper, 1]
    const bool keepsOnlyLowerSide = (range.upper >= 1.0 - 1e-6);  // 只剩 [0, lower]

    // ⚠️ 只保留一截時，沿用「原本這條線」，直接把沒有保留那一端的端點
    // movePoint() 到裁切邊界——跟 extendLine() 的做法完全一致（見標頭檔
    // 「hover 即時預覽」段落與 extendLine() 的實作）。不刪除重建整條線，
    // 有兩個好處：(1) 這條線身上原有的約束（Horizontal/Vertical/固定長度
    // …）不會因為 removeGeometry() 的級聯刪除而消失；(2) 端點權威座標
    // 一定透過 movePoint() 更新，不會有「線渲染在新位置、點還停在舊位置」
    // 的落差（這正是先前回報的「trim 後 sketchpoint 位置沒更新」的根因；
    // extend 因為本來就是走 movePoint() 這條路徑，所以沒有這個問題）。
    if (keepsOnlyUpperSide && !keepsOnlyLowerSide) {
        sketch->movePoint(line->startUuid, upperPt);
        sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
        return true;
    }
    if (keepsOnlyLowerSide && !keepsOnlyUpperSide) {
        sketch->movePoint(line->endUuid, lowerPt);
        sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
        return true;
    }

    // 裁切點落在線段中間，兩截都要保留：沒有辦法只靠 movePoint() 解決
    // （一條線就是只有兩個端點，變兩截勢必要多一條線），但仍然盡量沿用
    // 原本這條線當作 [0, lower] 那一截（movePoint() 移動 endUuid），只有
    // [upper, 1] 這一截需要真的新建——原本的遠端位置（origP2）在移動
    // endUuid 之前先記下來，因為 endUuid 被移走後就不能再代表那個位置了。
    const QVector2D origP2   = targetShape.p2;
    const GeomRole  origRole = line->role;

    sketch->movePoint(line->endUuid, lowerPt);

    const QString upperStartUuid = findOrCreatePoint(sketch, upperPt);
    const QString upperEndUuid   = findOrCreatePoint(sketch, origP2);
    const QString newUuid =
        sketch->addLineGeom(upperPt, origP2, upperStartUuid, upperEndUuid, origRole);
    if (auto* ng = sketch->findGeometry(newUuid)) ng->role = origRole;

    sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
    return true;
}

struct CircleTrimRange { bool valid = false; double lower = 0.0, upper = 0.0; };

/// TRIM 對 Circle 目標的核心運算：找出 clickPt 所在的相鄰交點角度區間
/// [lower, upper]（會被移除的那一段弧）。純運算，不觸碰 sketch——
/// trimCircle() 與 previewTrimAt() 共用。
CircleTrimRange computeCircleTrimRange(cad::Sketch* sketch, const GeomShape2D& targetShape,
                                       const QVector<QVector2D>& intersections,
                                       const QVector2D& clickPt)
{
    CircleTrimRange r;
    if (targetShape.occCircle.IsNull()) return r;

    QVector<double> angles;
    for (const QVector2D& pt : intersections)
        angles.append(g2d::normalizeAngle(angleOfPoint(sketch, targetShape.occCircle, pt)));
    if (angles.size() < 2) return r;  // 圓至少要兩個交點才能裁切成弧

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
    if (std::abs(upper - lower) < 1e-6) return r;

    r.valid = true;
    r.lower = lower;
    r.upper = upper;
    return r;
}

bool trimCircle(cad::Sketch* sketch, SketchCircle* circ, const GeomShape2D& targetShape,
                const QStringList& cuttingUuids, const QVector2D& clickPt)
{
    const auto intersections = collectIntersections(sketch, targetShape, circ->uuid, cuttingUuids);
    const CircleTrimRange range = computeCircleTrimRange(sketch, targetShape, intersections, clickPt);
    if (!range.valid) return false;

    // 保留「從 upper 逆時針掃到 lower」的弧（即跳過被點擊的 [lower, upper] 段）。
    const QVector2D lowerPt = pointOnCircle(sketch, targetShape.occCircle, range.lower);
    const QVector2D upperPt = pointOnCircle(sketch, targetShape.occCircle, range.upper);

    double span = range.lower - range.upper;
    if (span < 0.0) span += 2.0 * M_PI;
    const double midAngle = range.upper + span / 2.0;
    const QVector2D midPt = pointOnCircle(sketch, targetShape.occCircle, midAngle);

    const QString origCenterUuid = circ->centerUuid;
    const GeomRole origRole      = circ->role;

    const QString lowerUuid = findOrCreatePoint(sketch, lowerPt);
    const QString upperUuid = findOrCreatePoint(sketch, upperPt);

    sketch->removeGeometry(circ->uuid);

    const QString newUuid =
        sketch->addArcGeom(upperPt, midPt, lowerPt, upperUuid, lowerUuid, origCenterUuid);
    if (auto* ng = sketch->findGeometry(newUuid)) ng->role = origRole;

    sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
    return !newUuid.isEmpty();
}

struct ArcTrimRange {
    bool   valid = false;
    double s0 = 0.0, e0 = 0.0;              ///< 弧自身的起訖角（normalize 過）
    double lower = 0.0, upper = 0.0;        ///< 要被移除的 [lower, upper] 區間
    double keepSpanLower = 0.0;             ///< [s0, lower] 的弧長角度（供取中點用）
    double keepSpanUpper = 0.0;             ///< [upper, e0] 的弧長角度
    bool   hasLowerPart = false;            ///< 是否保留 [s0, lower] 這一段
    bool   hasUpperPart = false;            ///< 是否保留 [upper, e0] 這一段
};

/// TRIM 對 Arc 目標的核心運算：在弧自身的 [s0, e0] 範圍內找出緊鄰
/// clickPt 的交點/端點邊界 [lower, upper]（要被移除的區間）。純運算，
/// 不觸碰 sketch——trimArc() 與 previewTrimAt() 共用。
ArcTrimRange computeArcTrimRange(cad::Sketch* sketch, const GeomShape2D& targetShape,
                                 const QVector<QVector2D>& intersections,
                                 const QVector2D& clickPt)
{
    ArcTrimRange r;
    if (targetShape.occCircle.IsNull()) return r;

    QVector<double> angles;
    for (const QVector2D& pt : intersections)
        angles.append(g2d::normalizeAngle(angleOfPoint(sketch, targetShape.occCircle, pt)));
    if (angles.isEmpty()) return r;

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

    if (!hasLowerPart && !hasUpperPart) return r;  // 點擊處剛好在端點上，沒有可刪除段落

    r.valid = true;
    r.s0 = s0; r.e0 = e0;
    r.lower = lower; r.upper = upper;
    r.keepSpanLower = keepSpanLower;
    r.keepSpanUpper = keepSpanUpper;
    r.hasLowerPart = hasLowerPart;
    r.hasUpperPart = hasUpperPart;
    return r;
}

bool trimArc(cad::Sketch* sketch, SketchArc* arc, const GeomShape2D& targetShape,
            const QStringList& cuttingUuids, const QVector2D& clickPt)
{
    const auto intersections = collectIntersections(sketch, targetShape, arc->uuid, cuttingUuids);
    const ArcTrimRange range = computeArcTrimRange(sketch, targetShape, intersections, clickPt);
    if (!range.valid) return false;

    const Handle(Geom_Circle) occCircle = targetShape.occCircle;

    // 沿用「原本這個弧」，把它的張角重建成 [startAngle, endAngle]，並
    // movePoint() 兩個端點到新位置——跟 extendArc() 的做法完全一致（直接
    // 改 arc->curve + movePoint()，不刪除重建整個幾何物件）。理由同
    // trimLine()：保留原有約束、端點權威座標保證跟著 movePoint() 同步
    // 更新。
    auto reshapeArc = [&](SketchArc* a, double startAngle, double endAngle) -> bool {
        const QVector2D startPt = pointOnCircle(sketch, occCircle, startAngle);
        const QVector2D endPt   = pointOnCircle(sketch, occCircle, endAngle);
        double span = endAngle - startAngle;
        span = std::fmod(span, 2.0 * M_PI);
        if (span < 0.0) span += 2.0 * M_PI;
        const QVector2D midPt = pointOnCircle(sketch, occCircle, startAngle + span / 2.0);

        // ⚠️ 直接重建 curve（不經過 Sketch::addArcGeom()），要用世界座標
        // 的 gp_Pnt，而不是平面座標（見檔頭「v2 修正紀錄」）。
        GC_MakeArcOfCircle maker(planeToWorld(sketch, startPt),
                                 planeToWorld(sketch, midPt),
                                 planeToWorld(sketch, endPt));
        if (!maker.IsDone()) {
            qWarning() << "[TrimExtendHelper] Arc 裁切失敗（三點共線或重合），uuid=" << a->uuid;
            return false;
        }
        a->curve = maker.Value();
        sketch->movePoint(a->startUuid, startPt);
        sketch->movePoint(a->endUuid, endPt);
        return true;
    };

    if (range.hasLowerPart && !range.hasUpperPart) {
        // 只剩 [s0, lower] 這一截：沿用原本這個弧。
        if (!reshapeArc(arc, range.s0, range.lower)) return false;
        sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
        return true;
    }
    if (range.hasUpperPart && !range.hasLowerPart) {
        // 只剩 [upper, e0] 這一截：沿用原本這個弧。
        if (!reshapeArc(arc, range.upper, range.e0)) return false;
        sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
        return true;
    }

    // 裁切點落在弧中間，兩截都要保留：沒有辦法只靠 movePoint() 解決，
    // 但仍然盡量沿用原本這個弧當作 [s0, lower] 那一截，只有 [upper, e0]
    // 這一截需要真的新建——原本的遠端位置（endAngle=e0）在重建 curve
    // 之前先記下來，因為 endUuid 被 reshapeArc() 移走後就不能再代表那個
    // 位置了。
    const GeomRole   origRole      = arc->role;
    const QString    origCenterUuid = arc->centerUuid;
    const QVector2D  origEndPt     = pointOnCircle(sketch, occCircle, range.e0);

    if (!reshapeArc(arc, range.s0, range.lower)) return false;

    double span2 = range.e0 - range.upper;
    span2 = std::fmod(span2, 2.0 * M_PI);
    if (span2 < 0.0) span2 += 2.0 * M_PI;
    const QVector2D newStartPt = pointOnCircle(sketch, occCircle, range.upper);
    const QVector2D newMidPt   = pointOnCircle(sketch, occCircle, range.upper + span2 / 2.0);

    const QString startUuid = findOrCreatePoint(sketch, newStartPt);
    const QString endUuid   = findOrCreatePoint(sketch, origEndPt);
    const QString newUuid = sketch->addArcGeom(newStartPt, newMidPt, origEndPt,
                                                startUuid, endUuid, origCenterUuid);
    if (auto* ng = sketch->findGeometry(newUuid)) ng->role = origRole;

    sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
    return true;
}

// ─────────────────────────────────────────────────────────────────────────
// EXTEND — 依目標型別分派
// ─────────────────────────────────────────────────────────────────────────

struct LineExtendResult { bool valid = false; bool extendStart = false; double bestT = 0.0; };

/// EXTEND 對 Line 目標的核心運算：決定要延伸哪一端（離 clickPt 較近的
/// 端點）與延伸後的新參數 t（沿 targetShape.p1→p2 方向，可能 <0 或 >1）。
/// 純運算，不觸碰 sketch——extendLine() 與 previewExtendAt() 共用。
LineExtendResult computeLineExtend(cad::Sketch* sketch, SketchLine* line,
                                   const GeomShape2D& targetShape,
                                   const QStringList& boundaryUuids,
                                   const QVector2D& clickPt)
{
    LineExtendResult r;

    const double distToStart = double((clickPt - targetShape.p1).length());
    const double distToEnd   = double((clickPt - targetShape.p2).length());
    r.extendStart = distToStart < distToEnd;

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
    if (candidateParams.isEmpty()) return r;

    const double tMoving = r.extendStart ? 0.0 : 1.0;
    double bestT = tMoving;
    bool found = false;
    for (double t : candidateParams) {
        const bool extending = r.extendStart ? (t < tMoving - kParamEps)
                                             : (t > tMoving + kParamEps);
        if (!extending) continue;
        if (!found) { bestT = t; found = true; continue; }
        if (r.extendStart) { if (t > bestT) bestT = t; }
        else                { if (t < bestT) bestT = t; }
    }
    if (!found) return r;

    r.valid = true;
    r.bestT = bestT;
    return r;
}

bool extendLine(cad::Sketch* sketch, SketchLine* line, const GeomShape2D& targetShape,
                const QStringList& boundaryUuids, const QVector2D& clickPt)
{
    const LineExtendResult result = computeLineExtend(sketch, line, targetShape, boundaryUuids, clickPt);
    if (!result.valid) return false;

    const QString movingUuid = result.extendStart ? line->startUuid : line->endUuid;
    const QVector2D newPos =
        targetShape.p1 + (targetShape.p2 - targetShape.p1) * float(result.bestT);
    sketch->movePoint(movingUuid, newPos);
    sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
    return true;
}

struct ArcExtendResult { bool valid = false; bool extendStartSide = false; double newAngle = 0.0; };

/// EXTEND 對 Arc 目標的核心運算：決定要延伸哪一側（離 clickPt 較近的
/// 端點）與延伸後的新端點角度。純運算，不觸碰 sketch——extendArc() 與
/// previewExtendAt() 共用。
ArcExtendResult computeArcExtend(cad::Sketch* sketch, SketchArc* arc,
                                 const GeomShape2D& targetShape,
                                 const QStringList& boundaryUuids,
                                 const QVector2D& clickPt)
{
    ArcExtendResult r;
    if (targetShape.occCircle.IsNull()) return r;
    const Handle(Geom_Circle)& circ = targetShape.occCircle;

    const QVector2D startPt = pointOnCircle(sketch, circ, targetShape.startAngle);
    const QVector2D endPt   = pointOnCircle(sketch, circ, targetShape.endAngle);
    r.extendStartSide = (clickPt - startPt).length() < (clickPt - endPt).length();

    GeomShape2D fullCircleShape = targetShape;
    fullCircleShape.kind = GeomShape2D::Kind::Circle;  // 暫時視為完整圓，交點計算時略過 sweep 篩選

    QVector<double> candidateAngles;
    for (const QString& boundUuid : boundaryUuids) {
        if (boundUuid == arc->uuid) continue;
        SketchGeometry* bg = sketch->findGeometry(boundUuid);
        if (!bg) continue;

        if (dynamic_cast<SketchEllipse*>(bg)) {
            // Arc 延伸邊界若是橢圓：本 MVP 不支援（橢圓與圓的解析交點需要
            // 解四次方程式，複雜度明顯更高），略過。
            continue;
        }

        for (const GeomShape2D& boundShape : extractCuttingShapes(sketch, bg)) {
            const QVector<QVector2D> pts =
                clipToShape(sketch, intersectRaw(fullCircleShape, boundShape), boundShape);
            for (const QVector2D& pt : pts)
                candidateAngles.append(g2d::normalizeAngle(angleOfPoint(sketch, circ, pt)));
        }
    }
    if (candidateAngles.isEmpty()) return r;

    const double s0 = g2d::normalizeAngle(targetShape.startAngle);
    const double e0 = g2d::normalizeAngle(targetShape.endAngle);

    double newAngle = r.extendStartSide ? s0 : e0;
    bool found = false;

    for (double ang : candidateAngles) {
        if (g2d::angleInSweep(ang, s0, e0)) continue;  // 落在既有弧段內部，不是延伸

        if (r.extendStartSide) {
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
    if (!found) return r;

    r.valid = true;
    r.newAngle = newAngle;
    return r;
}

bool extendArc(cad::Sketch* sketch, SketchArc* arc, const GeomShape2D& targetShape,
               const QStringList& boundaryUuids, const QVector2D& clickPt)
{
    const ArcExtendResult result = computeArcExtend(sketch, arc, targetShape, boundaryUuids, clickPt);
    if (!result.valid) return false;

    const Handle(Geom_Circle)& circ = targetShape.occCircle;
    const double newStart = result.extendStartSide ? result.newAngle : targetShape.startAngle;
    const double newEnd   = result.extendStartSide ? targetShape.endAngle : result.newAngle;

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

    sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
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
// hover 即時預覽：與 trimAt()/extendAt() 共用同一套 compute*() 核心運算
// （見上方各 computeLineTrimRange()/computeCircleTrimRange()/
// computeArcTrimRange()/computeLineExtend()/computeArcExtend()），純讀取
// 不修改 sketch，只把結果轉成取樣折線供畫面疊層使用。
// ─────────────────────────────────────────────────────────────────────────

PreviewSegment previewTrimAt(cad::Sketch* sketch, const QString& targetUuid,
                             const QStringList& cuttingUuids, const QVector2D& hoverPt)
{
    PreviewSegment result;
    if (!sketch) return result;

    SketchGeometry* target = sketch->findGeometry(targetUuid);
    if (!target) return result;

    auto shape = extractShape(sketch, target);
    if (!shape) return result;

    const auto intersections = collectIntersections(sketch, *shape, targetUuid, cuttingUuids);

    switch (shape->kind) {
    case GeomShape2D::Kind::Line: {
        const LineTrimRange range = computeLineTrimRange(*shape, intersections, hoverPt);
        if (!range.valid) return result;
        result.points.append(shape->p1 + (shape->p2 - shape->p1) * float(range.lower));
        result.points.append(shape->p1 + (shape->p2 - shape->p1) * float(range.upper));
        result.valid = true;
        return result;
    }
    case GeomShape2D::Kind::Circle: {
        const CircleTrimRange range = computeCircleTrimRange(sketch, *shape, intersections, hoverPt);
        if (!range.valid) return result;
        result.points = sampleArcSpan(sketch, shape->occCircle, range.lower, range.upper);
        result.valid = !result.points.isEmpty();
        return result;
    }
    case GeomShape2D::Kind::Arc: {
        const ArcTrimRange range = computeArcTrimRange(sketch, *shape, intersections, hoverPt);
        if (!range.valid) return result;
        result.points = sampleArcSpan(sketch, shape->occCircle, range.lower, range.upper);
        result.valid = !result.points.isEmpty();
        return result;
    }
    }
    return result;
}

PreviewSegment previewExtendAt(cad::Sketch* sketch, const QString& targetUuid,
                               const QStringList& boundaryUuids, const QVector2D& hoverPt)
{
    PreviewSegment result;
    if (!sketch) return result;

    SketchGeometry* target = sketch->findGeometry(targetUuid);
    if (!target) return result;

    auto shape = extractShape(sketch, target);
    if (!shape) return result;

    switch (shape->kind) {
    case GeomShape2D::Kind::Line: {
        auto* line = static_cast<SketchLine*>(target);
        const LineExtendResult r = computeLineExtend(sketch, line, *shape, boundaryUuids, hoverPt);
        if (!r.valid) return result;
        const QVector2D fromPt = r.extendStart ? shape->p1 : shape->p2;
        const QVector2D toPt   = shape->p1 + (shape->p2 - shape->p1) * float(r.bestT);
        result.points = { fromPt, toPt };
        result.valid = true;
        return result;
    }
    case GeomShape2D::Kind::Arc: {
        auto* arc = static_cast<SketchArc*>(target);
        const ArcExtendResult r = computeArcExtend(sketch, arc, *shape, boundaryUuids, hoverPt);
        if (!r.valid) return result;
        const double s0 = g2d::normalizeAngle(shape->startAngle);
        const double e0 = g2d::normalizeAngle(shape->endAngle);
        // 延伸段＝從新端點回到「原本那一端」的邊界（見 extendArc() 內
        // newStart/newEnd 的組法：延伸起點側時新弧是 [newAngle, e0]，
        // 新增的那一段是 [newAngle, s0]；延伸終點側時對稱）。
        const double fromAngle = r.extendStartSide ? r.newAngle : e0;
        const double toAngle   = r.extendStartSide ? s0 : r.newAngle;
        result.points = sampleArcSpan(sketch, shape->occCircle, fromAngle, toAngle);
        result.valid = !result.points.isEmpty();
        return result;
    }
    case GeomShape2D::Kind::Circle:
        return result;  // 圓沒有端點可延伸
    }
    return result;
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
    bool        removedExistingCoincident = false;  ///< 見 filletAt() 文件說明
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

/// 掃描 sketch 現有的 Coincident 約束，找出（若存在）連結 ptA／ptB 兩個
/// SketchPoint 的那一條，回傳其 UUID（找不到則回傳空字串）。用於偵測
/// 「兩線交點原本就有明確的重合約束」這種情形（見 filletAt() 文件說明）。
QString findCoincidentConstraint(cad::Sketch* sketch, const QString& ptA, const QString& ptB)
{
    if (ptA.isEmpty() || ptB.isEmpty()) return QString();
    for (const SketchConstraint& c : sketch->constraints()) {
        if (c.type != ConstraintType::Coincident || c.refs.size() != 2) continue;
        const QString rA = c.refs[0].resolvedPointUuid(sketch);
        const QString rB = c.refs[1].resolvedPointUuid(sketch);
        if ((rA == ptA && rB == ptB) || (rA == ptB && rB == ptA))
            return c.uuid;
    }
    return QString();
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

    // 兩線的角點若原本就靠一條明確的 Coincident 約束連在一起（而非直接
    // 共用同一個 SketchPoint——那種情形已經在 unshareCornerPointIfNeeded()
    // 處理過了），這裡要在移動端點之前先把它移除，否則舊約束會要求兩點
    // 保持重合，跟圓角/倒角要把它們拉開到個別切點/倒角點的目標互相衝突。
    const QString coincUuid = findCoincidentConstraint(sketch, setup.line1PointUuid,
                                                        setup.line2PointUuid);
    if (!coincUuid.isEmpty()) {
        sketch->removeConstraint(coincUuid);
        setup.removedExistingCoincident = true;
    }

    return setup;
}

} // 匿名 namespace

FilletResult filletAt(cad::Sketch* sketch, const QString& line1Uuid, const QString& line2Uuid,
                      double radius, const QVector2D& clickPt1, const QVector2D& clickPt2)
{
    FilletResult result;
    if (radius < 0.0) return result;

    auto setupOpt = prepareFilletChamfer(sketch, line1Uuid, line2Uuid, clickPt1, clickPt2);
    if (!setupOpt) return result;
    const FilletChamferSetup& s = *setupOpt;
    result.line1PointUuid = s.line1PointUuid;
    result.line2PointUuid = s.line2PointUuid;
    result.removedExistingCoincident = s.removedExistingCoincident;

    if (radius <= 1e-9) {
        // 半徑 0：退化為單純延伸相交，不插入圓弧（比照 AutoCAD 行為）。
        // 兩個端點現在是各自獨立的點（見 prepareFilletChamfer()），移到
        // 同一個座標後仍然沒有明確的連結關係，補上一條 Coincident 約束
        // 讓它們維持重合，行為與「線-弧」情形一致（都靠明確約束銜接，
        // 而不是隱性共用同一個點物件）。
        sketch->movePoint(s.line1PointUuid, s.intersection);
        sketch->movePoint(s.line2PointUuid, s.intersection);
        sketch->addConstraint(SketchConstraint::makeCoincident(
            GeomRef(s.line1PointUuid, GeomHandle::WholeGeom),
            GeomRef(s.line2PointUuid, GeomHandle::WholeGeom)));
        sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
        result.success = true;
        return result;
    }

    const double cosTheta = std::max(-1.0, std::min(1.0, double(QVector2D::dotProduct(s.dir1, s.dir2))));
    const double theta = std::acos(cosTheta);
    if (theta < 1e-4 || theta > M_PI - 1e-4) return result;  // 平行/共線，無法求圓角

    const double halfTheta = theta / 2.0;
    const double tanHalf = std::tan(halfTheta);
    const double sinHalf = std::sin(halfTheta);
    if (std::abs(tanHalf) < 1e-9 || std::abs(sinHalf) < 1e-9) return result;

    const double dAlongRay = radius / tanHalf;
    const QVector2D tangent1 = s.intersection + s.dir1 * float(dAlongRay);
    const QVector2D tangent2 = s.intersection + s.dir2 * float(dAlongRay);

    QVector2D bisector = s.dir1 + s.dir2;
    if (bisector.lengthSquared() < 1e-12f) return result;  // dir1 ≈ -dir2（理論上已被 theta 範圍排除，防呆用）
    bisector.normalize();

    const double centerDist = radius / sinHalf;
    const QVector2D center = s.intersection + bisector * float(centerDist);
    const QVector2D midPt  = center - bisector * float(radius);

    sketch->movePoint(s.line1PointUuid, tangent1);
    sketch->movePoint(s.line2PointUuid, tangent2);

    // 弧的起訖點刻意不重用 line1PointUuid/line2PointUuid（不像先前版本那樣
    // 直接共用同一個點物件）——各自建立獨立的新點，讓呼叫端事後可以疊加
    // 明確的 Coincident 約束把它們接回去，使用者在束制清單裡才看得到、
    // 能編輯/刪除這個接合關係（見 FilletResult／filletAt() 文件說明）。
    const QString newArcUuid = sketch->addArcGeom(tangent1, midPt, tangent2,
                                                   QString(), QString(), QString());
    if (newArcUuid.isEmpty()) {
        sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
        return result;
    }

    result.arcUuid = newArcUuid;
    result.arcMidDir = midPt - center;   // 圓心→弧中點方向（草圖平面局部座標）
    if (auto* arc = dynamic_cast<SketchArc*>(sketch->findGeometry(newArcUuid))) {
        result.arcStartUuid = arc->startUuid;
        result.arcEndUuid   = arc->endUuid;
    }

    sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
    result.success = true;
    return result;
}

ChamferResult chamferAt(cad::Sketch* sketch, const QString& line1Uuid, const QString& line2Uuid,
              double dist1, double dist2,
              const QVector2D& clickPt1, const QVector2D& clickPt2)
{
    ChamferResult result;
    if (dist1 < 0.0 || dist2 < 0.0) return result;

    auto setupOpt = prepareFilletChamfer(sketch, line1Uuid, line2Uuid, clickPt1, clickPt2);
    if (!setupOpt) return result;
    const FilletChamferSetup& s = *setupOpt;
    result.line1PointUuid = s.line1PointUuid;
    result.line2PointUuid = s.line2PointUuid;
    result.removedExistingCoincident = s.removedExistingCoincident;

    // ⚠️ 2026-09 清理：這裡原本還有一段對 s.line1PointUuid/s.line2PointUuid
    // 再查一次、再移除一次 Coincident 的程式碼——但 prepareFilletChamfer()
    // 內部早就做過完全相同的查找＋移除（見該函式與 s.removedExistingCoincident
    // 的說明），這裡的第二次查找只會永遠查到空字串，是死碼。留著還有隱性
    // 風險：萬一哪天兩邊查找邏輯不同步、這裡真的查到東西並移除，
    // s.removedExistingCoincident 不會反映這次移除，回滾時就不會補回去。
    // 直接刪掉，只信任 prepareFilletChamfer() 那一份。

    if (dist1 <= 1e-9 && dist2 <= 1e-9) {
        // 兩個倒角距離都是 0：退化為單純延伸相交，不插入倒角線。
        sketch->movePoint(s.line1PointUuid, s.intersection);
        sketch->movePoint(s.line2PointUuid, s.intersection);

        // 兩個端點各自獨立（否則早被 unshareCornerPointIfNeeded 合併成同一
        // 點），現在既然退化成「單純相交」、沒有新倒角線可以順便共用端點
        // 幫忙鎖住位置，補上一條新的 Coincident 約束，讓它們之後仍保持
        // 綁定在同一點（對應：CHAMFER 完成後自動加上重合約束）。
        sketch->constrainCoincident(GeomRef(s.line1PointUuid, GeomHandle::WholeGeom),
                                     GeomRef(s.line2PointUuid, GeomHandle::WholeGeom));

        sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
        result.success = true;
        return result;
    }

    const QVector2D chamferPt1 = s.intersection + s.dir1 * float(dist1);
    const QVector2D chamferPt2 = s.intersection + s.dir2 * float(dist2);

    // ★ 2026-09 追加：回滾要用的「原始位置」快照。必須在下面兩行
    // movePoint() 之前就拍下來——s.line1->start/end 這兩個快取欄位到這裡
    // 為止都還沒被這次 chamferAt() 呼叫動過，正是使用者倒角前的原始座標。
    // 見下方 !ok 分支的完整說明：先前的版本只回滾約束跟角落標記點，沒把
    // 這兩個點的位置、也沒把新建的倒角線復原，導致 D1/D2 衝突（例如這個
    // 角落被相鄰的 fillet Tangent 約束卡住、根本沒有可行解）時，留下一段
    // 沒有任何約束在管、位置也錯誤的孤兒幾何，殃及共用點的相鄰 fillet。
    const QVector2D origP1 = (s.line1PointUuid == s.line1->startUuid) ? s.line1->start : s.line1->end;
    const QVector2D origP2 = (s.line2PointUuid == s.line2->startUuid) ? s.line2->start : s.line2->end;

    // ── （2026-09）暫時固定遠端點的做法已移除 ───────────────────────────
    // 下面會對交點 p1 加 PointOnCurve(line1)/(line2) 共線約束（見下方長篇
    // 說明）。這在 line1／line2 完全沒有其他約束、遠端點完全自由時，原本
    // 會讓 ConstraintSolver 的方程組出現精確秩虧、放大誤差甚至 crash——
    // 之前在這裡用「暫時 FixedX/FixedY 釘住遠端點、解完再移除」的方式繞
    // 過這個問題。
    //
    // 找到既有的正確修法後改掉了：問題根源其實在共用的
    // ConstraintSolver::solveLinearLS()，這裡原本就有一套已經驗證成功、
    // 拿來處理近似奇異方程組的 Levenberg-Marquardt 平滑阻尼（Tikhonov
    // regularization，先前修過圓角弧 Start/End 沒對齊的問題），只是阻尼
    // 量 λ 只看「相對最大奇異值的比例」，遇到像這裡這種整個系統本身數值
    // 尺度就偏小的精確秩虧就不夠力。已經把 λ 改成「相對比例／絕對下限」
    // 兩者取大（見 solveLinearLS() 內的說明），從根源解決、對全 App 的
    // 所有約束求解都受益，不需要再由每個呼叫端（像這裡）各自加一層暫時
    // 固定／解除的繞過邏輯，也少了本地暫時約束萬一跟既有約束衝突的額外
    // 風險。

    sketch->movePoint(s.line1PointUuid, chamferPt1);
    sketch->movePoint(s.line2PointUuid, chamferPt2);

    // 新倒角線直接沿用 line1/line2 的角點當自己的端點（reuseStart/reuseEnd），
    // 是比 Coincident 約束更強的連結（同一個 SketchPoint，0 DOF，不會有解算
    // 殘差），所以這裡不需要另外補重合約束——這點與退化分支（上面）不同，
    // 那裡沒有新線可以共用端點，才需要額外補約束。
    //
    // ★ emitSignals=false：addLineGeom() 預設一建立就立刻
    // Q_EMIT rebuildRequested()（觸發整個 Feature 重建），但接下來還要繼
    // 續加交點、FixedDistance／PointOnCurve 共 2～4 條約束才算完整——這裡
    // 先不重繪，等全部東西都加完、約束也解完，只在函式最後統一重繪一次。
    const QString newUuid = sketch->addLineGeom(chamferPt1, chamferPt2,
                                                s.line1PointUuid, s.line2PointUuid,
                                                GeomRole::Normal, /*emitSignals=*/false);

    if (!newUuid.isEmpty()) {
        // ★ 2026-09 移除：先前這裡會額外建立一個「角落」可視標記點
        //    （Origin::Intersection，只在建立當下用 s.intersection 定位一次，
        //    刻意不參與求解）。實測發現這個設計本身就有問題：使用者之後
        //    移動/編輯任一條線，這個標記點的座標完全不會跟著更新（這是
        //    設計上的刻意取捨，不是 bug），畫面上就會出現一個停在「舊」
        //    交點位置、跟兩條線目前實際交點對不上的點——反而造成混淆，
        //    看起來像是哪裡算錯了。這個點從頭到尾只是裝飾用的點選/顯示
        //    輔助，D1/D2 這個「活」的關係完全是靠下面的
        //    ConstraintType::Chamfer 約束直接參照 line1Uuid/line2Uuid 維持
        //    的，跟這個標記點有沒有存在完全無關——拿掉它不影響任何功能，
        //    只是不再讓使用者看到一個會過期、誤導人的點。需要用交點座標
        //    的地方（例如下面算尺寸線方向）直接用區域變數 s.intersection
        //    即可，不需要真的建立成一個 SketchPoint。

        // ChamferEquation 需要知道 line1PointUuid/line2PointUuid 在各自線上
        // 是 Start 還是 End，才能反查「另一端」(A1/A2) 來推導虛擬交點。
        const GeomHandle h1 = (s.line1->startUuid == s.line1PointUuid) ? GeomHandle::Start : GeomHandle::End;
        const GeomHandle h2 = (s.line2->startUuid == s.line2PointUuid) ? GeomHandle::Start : GeomHandle::End;

        // ★ 2026-09：改用單一一條 ConstraintType::Chamfer（見
        //    SketchConstraint.h 該型別／ConstraintSolver.h 的 ChamferEquation
        //    說明），取代先前 FixedDistance×2（D1=0 或 D2=0 時退化成
        //    Coincident）+ PointOnCurve×2 這 4 條各自獨立、彼此看不出關聯
        //    的通用約束——讓求解器（以及約束清單 UI、undo、刪除操作）把
        //    這整個倒角關係辨識成「一個 Chamfer」，而不是幾條事後看不出彼
        //    此關聯的獨立約束。D1=0（或 D2=0）不需要再特別切到 Coincident
        //    分支：ChamferEquation 的殘差公式在 D=0 時就是「trim 點與虛擬
        //    交點重合」，本來就是同一條方程式的自然特例。
        //
        //    ⚠️ 這不表示 ConstraintSolver 對「line1/line2 遠端點完全自由」
        //    這種精確秩虧場景的結構性風險已經解除——ChamferEquation 仍然
        //    需要靠兩線的「另一端點」推導虛擬交點，數學上無法迴避這個耦
        //    合。詳見 chamferAt() 上方（TrimExtendHelper.h）的完整說明與
        //    退回舊行為的作法。
        QStringList addedConstraintUuids;
        auto addOrTrack = [&](const SketchConstraint& c, QString& outUuid) -> bool {
            const QString cu = sketch->addConstraint(c, /*solve=*/false);
            if (cu.isEmpty()) return false;
            addedConstraintUuids.append(cu);
            outUuid = cu;
            return true;
        };

        bool ok = false;
        {
            // ★ 修正（實測 2lines.aicad 兩條單純相交的線，明明有解，
            //    solveConstraints() 卻回報 Conflict）：refs[0]/refs[1] 必須
            //    是「線」的 UUID（line1Uuid/line2Uuid），不能是
            //    s.line1PointUuid/s.line2PointUuid 這兩個「點」的 UUID——
            //    ChamferEquation::resolveChamferGeom() 靠 handle=Start/End
            //    去查「這條線的另一端點」，前提是 refs[*].geomUuid 解析出
            //    來的 GeomVarLayout 是線的 layout（4 個值：x1,y1,x2,y2）。
            //    如果傳的是點的 UUID，該點的 layout 只有 2 個值
            //    （x,y），GeomVarLayout::indexFor(GeomHandle::End) 算出來
            //    的 offset+2 會讀到相鄰、完全無關的另一個變數，
            //    ChamferEquation 就會拿著這筆垃圾資料算殘差、怎麼疊代都
            //    收斂不了，被 solveConstraints() 誤判成 Conflict——這正是
            //    「明明有解卻顯示無解」的真正原因，跟平行、距離都無關。
            SketchConstraint chamferConstraint = SketchConstraint::makeChamfer(
                GeomRef(line1Uuid, h1), GeomRef(line2Uuid, h2),
                dist1, dist2);

            // ★ 修正（實測 1.aicad 的 Chamfer 約束存檔 dimOffX/Y 都是 0，
            //    尺寸線位置不正確）：比照 filletAt() 的 arcMidDir 設定半徑
            //    尺寸箭頭方向的做法——這裡沒有現成的弧可以取「圓心→中點」
            //    方向，改用「交點 → 新倒角線中點」當作尺寸線該畫的方向，
            //    同樣是視覺上最自然、尺寸線會沿著倒角線本身畫出去的位置。
            //    makeChamfer() 建出來的 dimLineOffsetX/Y 預設是 0，不設定
            //    的話畫的時候會退回 DimensionLineAIS 的預設方向（X 軸），
            //    跟倒角線實際的方向、位置對不上。
            QVector2D dimDir = (chamferPt1 + chamferPt2) * 0.5f - s.intersection;
            if (dimDir.lengthSquared() > 1e-12f) dimDir.normalize();
            chamferConstraint.dimLineOffsetX = dimDir.x();
            chamferConstraint.dimLineOffsetY = dimDir.y();

            ok = addOrTrack(chamferConstraint, result.chamferConstraintUuid);
        }

        // ★ 上面所有 addOrTrack() 都用 solve=false 跳過了中間的求解，這裡
        // 是這一整批約束加完後「唯一」的一次 solveConstraints()——用完整
        // 方程組一次解，不再需要像先前那樣特地強調「反正每次 addConstraint
        // 都已經解過一次，這裡再解一次只是為了拿 SolveResult」。
        // 萬一 line1/line2 在倒角之前就已經有別的約束跟這裡新加的東西衝突
        // （例如已經有 FixedLength 把端點釘死在別處），一樣整批退回，避免
        // 把 sketch 留在衝突/退化狀態。
        if (ok) {
            SolveResult r = sketch->solveConstraints();
            ok = (r.status != SolveStatus::Conflict);
        }

        if (!ok) {
            for (auto it = addedConstraintUuids.rbegin(); it != addedConstraintUuids.rend(); ++it)
                sketch->removeConstraint(*it);

            // ★ 2026-09 修正：先前這裡只回滾「這個 if 區塊裡新加的約束」跟
            // 角落標記點，漏了在這之前就已經做、且不屬於
            // addedConstraintUuids 的三件事——實測（見使用者回報的
            // 1withChamfer.aicad）證實：漏了任何一件都會在這個角落留下
            // 一段沒有約束管、位置又是錯的孤兒幾何，下一次整體求解時殃及
            // 共用點的相鄰幾何（例如相鄰的 fillet 弧跟著跳掉）：
            //   1. movePoint() 已經把 line1PointUuid/line2PointUuid 搬到
            //      chamferPt1/chamferPt2──搬回 origP1/origP2。
            //   2. addLineGeom() 已經建好新的倒角線 newUuid，且它的端點就
            //      是 line1PointUuid/line2PointUuid 本身（reuseStart/
            //      reuseEnd，見上方說明）──這條線本身也要整條刪掉，不能
            //      只留著不管。
            //   3. 若這個角落原本有明確的 Coincident 約束
            //      （removedExistingCoincident），已經在
            //      prepareFilletChamfer() 裡被移除──失敗時要補回去，否則
            //      這個角落從「銳角」變成「完全沒有約束連著的兩個獨立端
            //      點」。
            sketch->removeGeometry(newUuid);
            sketch->movePoint(s.line1PointUuid, origP1);
            sketch->movePoint(s.line2PointUuid, origP2);
            if (s.removedExistingCoincident) {
                sketch->constrainCoincident(GeomRef(s.line1PointUuid, GeomHandle::WholeGeom),
                                             GeomRef(s.line2PointUuid, GeomHandle::WholeGeom));
            }
            sketch->solveConstraints();

            // 約束整批回滾、newUuid 也刪掉了，這個 UUID 已經不再指向 sketch
            // 裡任何有效的東西，不能留著誤導呼叫端——與下面
            // result.success 維持預設的 false 一致。
            result.chamferConstraintUuid.clear();
        } else {
            result.success = true;
            result.newLineUuid = newUuid;
        }
    } else {
        // addLineGeom() 理論上一定會回傳非空 UUID（見上方呼叫處），這裡只
        // 是防禦性地保留一次求解，確保萬一真的走到這裡，sketch 狀態仍然
        // 一致——正常情況下不會執行到這個分支。result 維持 success=false。
        sketch->solveConstraints();
    }

    // ★ 這是這次倒角操作「唯一」的一次求解：上面 addLineGeom() 已經用
    // emitSignals=false 跳過了它自己的即時重繪，交點／約束的加入也都不會
    // 單獨觸發重繪（addPoint() 不 emit；addConstraint() 這裡全部用
    // solve=false）。全部完成後才在這裡統一 solve 一次。
    // ⚠️ 效能修正：不再額外 Q_EMIT rebuildRequested()——solveConstraints()
    // 內部已透過 markDirty() emit 過，重複 emit 只會讓 Document::rebuildFeature()
    // 多跑一次。
    sketch->solveConstraints();
    return result;
}

bool addFilletCornerConstraints(cad::Sketch* sketch, const FilletResult& r,
                                const QString& line1Uuid, const QString& line2Uuid,
                                double radius)
{
    if (!sketch || !r.success) return false;

    if (r.arcUuid.isEmpty()) {
        // 半徑 = 0（無插入弧）：filletAt() 內部已經另外補上一條 Coincident
        // 約束把兩線端點接起來，這裡不需要再做任何事。
        return true;
    }

    // 半徑 > 0：疊加 Coincident × 2（line 端點 ↔ 弧端點）、Tangent × 2
    // （弧 ↔ 各線）、FixedRadius × 1（見 FilletCommand.h 檔頭「約束處理」
    // 說明；filletAt() 內部已把弧的起訖點建成獨立新點，就是為了讓這裡能
    // 疊加明確、可編輯/可刪除的約束）。
    //
    // ★ 全部用 solve=false 先加完整批，最後只解一次——理由與 chamferAt()
    // 內部批次加約束的原則一致（見該函式說明），也讓下面的「整批回滾」
    // 判斷有完整、一致的 addedConstraintUuids 可用。
    QStringList addedConstraintUuids;
    auto addOrTrack = [&](const SketchConstraint& c) -> bool {
        const QString cu = sketch->addConstraint(c, /*solve=*/false);
        if (cu.isEmpty()) return false;
        addedConstraintUuids.append(cu);
        return true;
    };

    bool ok = addOrTrack(SketchConstraint::makeCoincident(
        GeomRef(r.line1PointUuid, GeomHandle::WholeGeom),
        GeomRef(r.arcStartUuid,   GeomHandle::WholeGeom)));
    if (ok) ok = addOrTrack(SketchConstraint::makeCoincident(
        GeomRef(r.line2PointUuid, GeomHandle::WholeGeom),
        GeomRef(r.arcEndUuid,     GeomHandle::WholeGeom)));

    // ⚠️ makeTangent(geomA, geomB) 對應到 ConstraintSolver.cpp 的
    // TangentEquation，該方程式寫死假設 refs[0]＝線、refs[1]＝圓/弧
    // （dist(center,line)=r，見該處註解），GeomVarLayout::indexFor() 又是
    // 純粹依 handle 名稱查表、不檢查實際幾何型別——參數順序一旦寫反（弧在
    // 前、線在後），會把線的區域變數硬當成圓心/半徑去讀，讀到超出該線實
    // 際配置範圍的 index，導致 QVector 越界崩潰。這裡務必是「線在前、弧
    // 在後」。
    if (ok) ok = addOrTrack(SketchConstraint::makeTangent(line1Uuid, r.arcUuid));
    if (ok) ok = addOrTrack(SketchConstraint::makeTangent(line2Uuid, r.arcUuid));

    if (ok) {
        // 半徑尺寸箭頭畫在弧的中點方向（見 filletAt() 的 arcMidDir 說明），
        // 而不是預設的草圖 X 軸方向——makeFixedRadius() 回傳的是一般化的
        // 約束，dimLineOffsetX/Y 預設 0，畫的時候會退回 X 軸方向，所以
        // 這裡建構後、加進 sketch 之前先設定好。
        SketchConstraint radiusConstraint = SketchConstraint::makeFixedRadius(r.arcUuid, radius);
        radiusConstraint.dimLineOffsetX = r.arcMidDir.x();
        radiusConstraint.dimLineOffsetY = r.arcMidDir.y();
        const QString cu = sketch->addConstraint(radiusConstraint, /*solve=*/false);
        ok = !cu.isEmpty();
        if (ok) addedConstraintUuids.append(cu);
    }

    // ★ 修正（原本 FilletCommand.cpp 內嵌版本缺少的檢查）：這批 5 條約束
    // 若與 line1/line2 原本就有的約束衝突（例如某條線已被 FixedLength
    // 釘死在別處），舊版直接呼叫 solveConstraints() 後不看回傳結果，把
    // 衝突/退化的約束整批留在 sketch 裡，使用者拿不到任何回饋也無法自動
    // 復原。這裡比照 chamferAt() 既有的「整批加、失敗就整批退回」模式。
    if (ok) {
        SolveResult r2 = sketch->solveConstraints();
        ok = (r2.status != SolveStatus::Conflict);
    }

    if (!ok) {
        for (auto it = addedConstraintUuids.rbegin(); it != addedConstraintUuids.rend(); ++it)
            sketch->removeConstraint(*it);
        sketch->solveConstraints();
    }

    return ok;
}

OffsetResult offsetAt(cad::Sketch* sketch, const QString& curveUuid,
                      double distance, const QVector2D& sidePt)
{
    OffsetResult result;
    if (!sketch || distance <= 0.0) return result;

    auto* geom = sketch->findGeometry(curveUuid);
    if (!geom) return result;

    if (auto* line = dynamic_cast<SketchLine*>(geom)) {
        QVector2D dir = line->end - line->start;
        if (dir.lengthSquared() < 1e-12f) return result;  // 退化線段
        dir.normalize();
        const QVector2D normal(-dir.y(), dir.x());

        const float side = QVector2D::dotProduct(sidePt - line->start, normal);
        if (std::abs(side) < 1e-9f) return result;  // 點擊落在線上，方向不明確

        const QVector2D offsetVec = normal * float((side > 0.0f ? 1.0 : -1.0) * distance);
        const QVector2D p1 = line->start + offsetVec;
        const QVector2D p2 = line->end   + offsetVec;

        result.newCurveUuid = sketch->addLineGeom(p1, p2);
        result.success = !result.newCurveUuid.isEmpty();
        if (result.success) {
            // 供呼叫端疊加 Parallel／FixedDistance 約束用（見 offsetAt()
            // 文件「約束處理」說明）——起點雖然只是任取一個對應點，但因為
            // 兩點的位移向量正好就是 offsetVec（純平移，構造保證），這對
            // 起點連線在建立當下確實垂直於兩線，可以直接拿來當
            // FixedDistance 的參考點。
            result.sourceRefPointUuid = line->startUuid;
            if (auto* newLine = dynamic_cast<SketchLine*>(sketch->findGeometry(result.newCurveUuid)))
                result.newRefPointUuid = newLine->startUuid;

            sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
        }
        return result;
    }

    if (auto* circ = dynamic_cast<SketchCircle*>(geom)) {
        const double distFromCenter = double((sidePt - circ->center).length());
        const double newRadius = (distFromCenter > circ->radius) ? circ->radius + distance
                                                                  : circ->radius - distance;
        if (newRadius <= 1e-6) return result;  // 內縮超過圓心，退化

        result.newCurveUuid = sketch->addCircleGeom(circ->center, newRadius);
        result.success = !result.newCurveUuid.isEmpty();
        if (result.success) {
            result.newRadius = newRadius;  // 供呼叫端疊加 Concentric／FixedRadius 約束用
            sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
        }
        return result;
    }

    if (auto* arc = dynamic_cast<SketchArc*>(geom)) {
        SketchPoint* centerPt = sketch->point(arc->centerUuid);
        SketchPoint* startPt  = sketch->point(arc->startUuid);
        SketchPoint* endPt    = sketch->point(arc->endUuid);
        if (!centerPt || !startPt || !endPt || arc->curve.IsNull() || !sketch->plane())
            return result;

        const QVector2D center = centerPt->pos;
        const double radius = double((startPt->pos - center).length());
        if (radius < 1e-9) return result;  // 退化弧（起點與圓心重合）

        const double distFromCenter = double((sidePt - center).length());
        const double newRadius = (distFromCenter > radius) ? radius + distance
                                                             : radius - distance;
        if (newRadius <= 1e-6) return result;  // 內縮超過圓心，退化

        // 把起點/終點/中點各自沿著「圓心→該點」的方向，等比例縮放到新
        // 半徑——這樣新弧跟原弧的起訖角度完全一致（優弧/劣弧、順逆時針
        // 都自動保留），不需要另外處理角度方向的正負號判斷。
        const double scale = newRadius / radius;
        auto scaledFromCenter = [&](const QVector2D& p) -> QVector2D {
            return center + (p - center) * float(scale);
        };
        const QVector2D newStart = scaledFromCenter(startPt->pos);
        const QVector2D newEnd   = scaledFromCenter(endPt->pos);

        // 中點：直接從原弧的 OCCT curve 取實際中點（世界座標），轉回草圖
        // 平面局部座標後再套用同樣的縮放——比自己用角度重建（優弧/劣弧、
        // 順逆時針方向容易搞混）穩妥，直接複用既有幾何的真實資料。
        const double t0 = arc->curve->FirstParameter();
        const double t1 = arc->curve->LastParameter();
        const gp_Pnt worldMid = arc->curve->Value((t0 + t1) * 0.5);
        const QVector2D localMid = sketch->plane()->toPlane(
            QVector3D(float(worldMid.X()), float(worldMid.Y()), float(worldMid.Z())));
        const QVector2D newMid = scaledFromCenter(localMid);

        result.newCurveUuid = sketch->addArcGeom(newStart, newMid, newEnd);
        result.success = !result.newCurveUuid.isEmpty();
        if (result.success) {
            result.newRadius = newRadius;  // 供呼叫端疊加 Concentric／FixedRadius 約束用
            sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
        }
        return result;
    }

    return result;  // 不支援的幾何型別（Polyline／Spline／Ellipse／Point 等）
}

namespace {

/// 找出跟 pointUuid「重合」的所有其他 SketchPoint UUID（含自己）：字面上
/// 相同的 UUID，以及透過明確 Coincident 約束連結的其他點（只找一步，不做
/// 遞移傳遞——多重 Coincident 串接的情形一般使用情境下很少見，先不處理）。
/// 供 offsetChainAt() 的鏈偵測使用。
QSet<QString> coincidentPointUuids(cad::Sketch* sketch, const QString& pointUuid)
{
    QSet<QString> result;
    result.insert(pointUuid);
    for (const SketchConstraint& c : sketch->constraints()) {
        if (c.type != ConstraintType::Coincident || c.refs.size() != 2) continue;
        const QString rA = c.refs[0].resolvedPointUuid(sketch);
        const QString rB = c.refs[1].resolvedPointUuid(sketch);
        if (rA == pointUuid) result.insert(rB);
        else if (rB == pointUuid) result.insert(rA);
    }
    return result;
}

/// 取出 g（Line 或 Arc）的起訖點 UUID。回傳 false 表示 g 不是鏈支援的
/// 型別。
bool chainEndpointUuids(SketchGeometry* g, QString& outStart, QString& outEnd)
{
    if (auto* l = dynamic_cast<SketchLine*>(g)) { outStart = l->startUuid; outEnd = l->endUuid; return true; }
    if (auto* a = dynamic_cast<SketchArc*>(g))  { outStart = a->startUuid; outEnd = a->endUuid; return true; }
    return false;
}

/// 在 atPointUuid 這個接點（含跟它重合的其他點）上，找出「唯一一個」跟
/// usedUuids 無關的其他鏈成員（Line 或 Arc）。回傳 nullptr 代表：沒有其他
/// 鏈成員相連（鏈的終點）、有 2 個以上相連（分岔/T 字路口，鏈的終點）、
/// 或這個接點還連著鏈不支援的幾何（Circle／Polyline／Spline／Ellipse
/// 等；鏈只支援連續的 Line／Arc，遇到這種情形視為鏈的終點，不繼續延伸，
/// 避免跳過中間的幾何、產生錯誤的偏移轉角）。
SketchGeometry* findSingleOtherChainMember(cad::Sketch* sketch, const QString& atPointUuid,
                                           const QVector<QString>& usedUuids)
{
    const QSet<QString> group = coincidentPointUuids(sketch, atPointUuid);
    SketchGeometry* found = nullptr;
    for (const QString& pu : group) {
        for (auto* g : sketch->curvesReferencingPoint(pu)) {
            QString s, e;
            if (!chainEndpointUuids(g, s, e)) return nullptr;  // 接到鏈不支援的幾何，鏈到此為止
            if (usedUuids.contains(g->uuid)) continue;
            if (found && found->uuid != g->uuid) return nullptr;  // 分岔
            found = g;
        }
    }
    return found;
}

/// 一段鏈成員的來源型別資訊（Line 或 Arc）。
struct ChainSourceSeg {
    QString   uuid;
    bool      isArc = false;
    QVector2D arcCenter;    ///< 僅 isArc 時有意義
    double    arcRadius = 0.0;
};

/// 一段的「素樸偏移邊界」，供轉角相接求交點用：Line 用兩點表示無限長
/// 直線，Arc／Circle 用圓心＋半徑表示整個圓（Arc 的轉角交點只需要圓的
/// 資訊，弧段範圍的裁切由呼叫端事後用 addArcGeom() 的三點法自然決定）。
struct OffsetBoundary {
    bool      isCircle = false;
    QVector2D p1, p2;      ///< isCircle=false 時使用
    QVector2D center;
    double    radius = 0.0;  ///< isCircle=true 時使用
};

/// 求兩個偏移邊界的交點，多解時取離 nearRef（原始素樸偏移下這個轉角該在
/// 的位置）最近的那一個，避免選到幾何上不合理的另一側。找不到交點（平行
/// /相離/同心）回傳 std::nullopt。
std::optional<QVector2D> intersectBoundaries(const OffsetBoundary& a, const OffsetBoundary& b,
                                             const QVector2D& nearRef)
{
    QVector<QVector2D> candidates;
    if (!a.isCircle && !b.isCircle) {
        if (auto p = g2d::lineLineIntersect(a.p1, a.p2, b.p1, b.p2)) candidates.append(*p);
    } else if (a.isCircle && !b.isCircle) {
        candidates = g2d::lineCircleIntersect(b.p1, b.p2, a.center, a.radius);
    } else if (!a.isCircle && b.isCircle) {
        candidates = g2d::lineCircleIntersect(a.p1, a.p2, b.center, b.radius);
    } else {
        candidates = g2d::circleCircleIntersect(a.center, a.radius, b.center, b.radius);
    }
    if (candidates.isEmpty()) return std::nullopt;

    QVector2D best = candidates[0];
    float bestDist = (best - nearRef).lengthSquared();
    for (int k = 1; k < candidates.size(); ++k) {
        const float d = (candidates[k] - nearRef).lengthSquared();
        if (d < bestDist) { bestDist = d; best = candidates[k]; }
    }
    return best;
}

} // 匿名 namespace

OffsetChainResult offsetChainAt(cad::Sketch* sketch, const QString& startUuid,
                                double distance, const QVector2D& sidePt)
{
    OffsetChainResult result;
    if (!sketch || distance <= 0.0) return result;

    auto* startGeom = sketch->findGeometry(startUuid);
    QString startPtA, startPtB;
    if (!startGeom || !chainEndpointUuids(startGeom, startPtA, startPtB)) return result;

    auto* startPtAObj = sketch->point(startPtA);
    auto* startPtBObj = sketch->point(startPtB);
    if (!startPtAObj || !startPtBObj) return result;

    // ── 1. 追蹤連續鏈：以「節點座標序列」表示，沿著兩個方向各自延伸，
    //    直到遇到分岔／鏈不支援的幾何／端點無其他鏈成員相連（鏈的終
    //    點），或繞回起點（封閉環）為止。────────────────────────────────
    QVector<QVector2D> pathPoints   = { startPtAObj->pos, startPtBObj->pos };
    QVector<QString>   pathPointIds = { startPtA, startPtB };
    QVector<QString>   segUuids     = { startUuid };
    bool closedLoop = false;

    // 往「尾端」延伸（append）。
    while (true) {
        const QString tailPtUuid = pathPointIds.last();
        SketchGeometry* next = findSingleOtherChainMember(sketch, tailPtUuid, segUuids);
        if (!next) break;

        QString nStart, nEnd;
        chainEndpointUuids(next, nStart, nEnd);
        const QSet<QString> group = coincidentPointUuids(sketch, tailPtUuid);

        QString otherPtUuid;
        if (group.contains(nStart) && !group.contains(nEnd)) otherPtUuid = nEnd;
        else if (group.contains(nEnd) && !group.contains(nStart)) otherPtUuid = nStart;
        else break;  // 退化（兩端都在同一組），理論上不該發生，保守停止

        auto* otherPtObj = sketch->point(otherPtUuid);
        if (!otherPtObj) break;

        if (segUuids.size() >= 2 &&
            coincidentPointUuids(sketch, otherPtUuid).contains(pathPointIds.first())) {
            // 繞回起點：封閉環，這是最後一段（wrap segment），不需要再
            // 新增節點——見 segEndpoints() 對這種情形的處理。
            closedLoop = true;
            segUuids.append(next->uuid);
            break;
        }

        pathPoints.append(otherPtObj->pos);
        pathPointIds.append(otherPtUuid);
        segUuids.append(next->uuid);
    }

    // 往「頭端」延伸（prepend）——已經因為封閉環而停止的話不用再找另一個
    // 方向（起點跟終點本來就是同一個點了）。
    if (!closedLoop) {
        while (true) {
            const QString headPtUuid = pathPointIds.first();
            SketchGeometry* prev = findSingleOtherChainMember(sketch, headPtUuid, segUuids);
            if (!prev) break;

            QString pStart, pEnd;
            chainEndpointUuids(prev, pStart, pEnd);
            const QSet<QString> group = coincidentPointUuids(sketch, headPtUuid);

            QString otherPtUuid;
            if (group.contains(pStart) && !group.contains(pEnd)) otherPtUuid = pEnd;
            else if (group.contains(pEnd) && !group.contains(pStart)) otherPtUuid = pStart;
            else break;

            auto* otherPtObj = sketch->point(otherPtUuid);
            if (!otherPtObj) break;

            pathPoints.prepend(otherPtObj->pos);
            pathPointIds.prepend(otherPtUuid);
            segUuids.prepend(prev->uuid);
        }
    }

    const int segCount = segUuids.size();
    const int clickedIdx = segUuids.indexOf(startUuid);
    if (segCount < 1 || clickedIdx < 0) return result;  // 理論上不會發生

    // 第 i 段沿鏈行進方向的 from→to（封閉環最後一段的 to 繞回 pathPoints[0]）。
    auto segEndpoints = [&](int i, QVector2D& from, QVector2D& to) {
        from = pathPoints[i];
        to   = (i + 1 < pathPoints.size()) ? pathPoints[i + 1] : pathPoints[0];
    };

    // 收集每段的來源型別資訊（Line／Arc）。
    QVector<ChainSourceSeg> sources(segCount);
    for (int i = 0; i < segCount; ++i) {
        sources[i].uuid = segUuids[i];
        if (auto* arc = dynamic_cast<SketchArc*>(sketch->findGeometry(segUuids[i]))) {
            auto* c = sketch->point(arc->centerUuid);
            if (!c) return result;
            sources[i].isArc = true;
            sources[i].arcCenter = c->pos;
            QVector2D from, to;
            segEndpoints(i, from, to);
            sources[i].arcRadius = double((from - sources[i].arcCenter).length());
            if (sources[i].arcRadius < 1e-9) return result;  // 退化弧
        }
    }

    // ── 2. 決定整條鏈統一的偏移方向（正負號）：只用使用者實際點擊、對應
    //    到的那一段（startUuid）判斷，其餘每一段（不論 Line 或 Arc）都
    //    套用同一個正負號，確保整條鏈偏移到一致的同一側。Arc 段這個
    //    正負號該對應到半徑變大還是變小，見下方第 3 步的換算。────────
    QVector2D clickedFrom, clickedTo;
    segEndpoints(clickedIdx, clickedFrom, clickedTo);

    float sideSign;
    if (sources[clickedIdx].isArc) {
        const QVector2D& c = sources[clickedIdx].arcCenter;
        const double distFromCenter = double((sidePt - c).length());
        const bool clickWantsGrow = distFromCenter > sources[clickedIdx].arcRadius;
        const QVector2D fromC = clickedFrom - c, toC = clickedTo - c;
        const bool isCCW = (fromC.x() * toC.y() - fromC.y() * toC.x()) > 0.0f;
        // grow ⇔ (isCCW == (sideSign < 0))，見下方第 3 步／檔頭文件說明；
        // 這裡反推：已知 clickWantsGrow，求 sideSign。
        sideSign = (clickWantsGrow == isCCW) ? -1.0f : 1.0f;
    } else {
        QVector2D dir = clickedTo - clickedFrom;
        if (dir.lengthSquared() < 1e-12f) return result;
        dir.normalize();
        const QVector2D normal(-dir.y(), dir.x());
        sideSign = (QVector2D::dotProduct(sidePt - clickedFrom, normal) > 0.0f) ? 1.0f : -1.0f;
    }

    // ── 3. 每一段各自的「素樸」偏移邊界／端點（尚未做轉角相接處理）。──
    QVector<OffsetBoundary> boundaries(segCount);
    QVector<QVector2D> offA(segCount), offB(segCount);
    QVector<double> arcNewRadius(segCount, 0.0);
    for (int i = 0; i < segCount; ++i) {
        QVector2D from, to;
        segEndpoints(i, from, to);

        if (sources[i].isArc) {
            const QVector2D& c = sources[i].arcCenter;
            const double r = sources[i].arcRadius;
            const QVector2D fromC = from - c, toC = to - c;
            // 沿鏈行進方向（from→to）是順時針還是逆時針掃過圓心：外積
            // >0 為逆時針。「左手邊（sideSign>0，即 Line 情形 normal 指向
            // 的那一側）」對逆時針掃過的弧是朝圓心（縮小），對順時針掃過
            // 的弧是遠離圓心（放大）——見檔頭文件「Arc 段的偏移方向」
            // 說明，這裡是這個規則的正向套用。
            const bool isCCW = (fromC.x() * toC.y() - fromC.y() * toC.x()) > 0.0f;
            const bool grow = (isCCW == (sideSign < 0.0f));
            const double newR = grow ? r + distance : r - distance;
            if (newR <= 1e-6) return result;  // 內縮超過圓心，退化

            const double scale = newR / r;
            offA[i] = c + fromC * float(scale);
            offB[i] = c + toC   * float(scale);
            arcNewRadius[i] = newR;
            boundaries[i] = OffsetBoundary{ true, QVector2D(), QVector2D(), c, newR };
        } else {
            QVector2D dir = to - from;
            if (dir.lengthSquared() < 1e-12f) return result;  // 退化線段
            dir.normalize();
            const QVector2D normal(-dir.y(), dir.x());
            const QVector2D offsetVec = normal * (sideSign * float(distance));
            offA[i] = from + offsetVec;
            offB[i] = to   + offsetVec;
            boundaries[i] = OffsetBoundary{ false, offA[i], offB[i], QVector2D(), 0.0 };
        }
    }

    // ── 4. 內部轉角相接：依邊界型別分三種情形求交點（Line-Line／
    //    Line-Arc／Arc-Arc），多解時取離原始素樸偏移角點最近的那個（見
    //    intersectBoundaries()）。開放鏈的頭尾兩端（沒有相鄰段可以求交）
    //    維持素樸偏移結果不變。封閉環額外處理「最後一段↔第一段」這個
    //    wrap-around 轉角。────────────────────────────────────────────
    const int jointCount = closedLoop ? segCount : (segCount - 1);
    for (int j = 0; j < jointCount; ++j) {
        const int prevIdx = j;
        const int nextIdx = (j + 1) % segCount;
        const QVector2D nearRef = offB[prevIdx];  // 素樸偏移下，這個轉角原本該在的位置
        if (auto p = intersectBoundaries(boundaries[prevIdx], boundaries[nextIdx], nearRef)) {
            offB[prevIdx] = *p;
            offA[nextIdx] = *p;
        }
        // 找不到交點（平行/相離/同心）：維持素樸偏移結果。
    }

    // ── 5. 建立幾何：Line 段用 addLineGeom()；Arc 段比照 offsetAt() 的
    //    做法，中點直接從原弧的 OCCT curve 取樣、轉回平面座標後，沿
    //    「圓心→原中點位置」方向縮放到新半徑（用第 3 步算好的
    //    arcNewRadius，不是從轉角調整後的 offA/offB 反推，避免頭尾兩端
    //    只有一端被轉角調整時，兩次反推可能出現的極小數值落差）。每段都
    //    是各自獨立的新幾何（不與來源共用點、彼此之間也不共用點），事後
    //    靠約束銜接（與 offsetAt() 單曲線情形、filletAt() 的弧一致的
    //    慣例）。────────────────────────────────────────────────────────
    QVector<QString> newUuids(segCount);
    for (int i = 0; i < segCount; ++i) {
        if (sources[i].isArc) {
            auto* arcGeom = dynamic_cast<SketchArc*>(sketch->findGeometry(sources[i].uuid));
            if (!arcGeom || arcGeom->curve.IsNull() || !sketch->plane()) return result;

            const QVector2D& c = sources[i].arcCenter;
            const double r = sources[i].arcRadius;
            const double scale = arcNewRadius[i] / r;

            const double t0 = arcGeom->curve->FirstParameter();
            const double t1 = arcGeom->curve->LastParameter();
            const gp_Pnt worldMid = arcGeom->curve->Value((t0 + t1) * 0.5);
            const QVector2D localMid = sketch->plane()->toPlane(
                QVector3D(float(worldMid.X()), float(worldMid.Y()), float(worldMid.Z())));
            const QVector2D newMid = c + (localMid - c) * float(scale);

            newUuids[i] = sketch->addArcGeom(offA[i], newMid, offB[i]);
        } else {
            newUuids[i] = sketch->addLineGeom(offA[i], offB[i]);
        }
        if (newUuids[i].isEmpty()) return result;  // 建立失敗，整批放棄（已建立的部分留在 sketch 裡）
    }

    // ── 6. 組裝結果：每段的來源/新曲線對應與約束參考資訊、以及內部轉角
    //    的 Coincident 點對（見 offsetChainAt() 文件的「約束處理」與
    //    Line FixedDistance 的已知限制說明）。──────────────────────────
    result.segments.reserve(segCount);
    for (int i = 0; i < segCount; ++i) {
        OffsetChainSegment seg;
        seg.sourceUuid = segUuids[i];
        seg.newUuid    = newUuids[i];
        seg.isArc      = sources[i].isArc;

        if (seg.isArc) {
            // Concentric／FixedRadius 是 Arc 整體的性質，不受轉角調整
            // 影響，每一個 Arc 段都提供，沒有 Line 情形那種限制。
            seg.newRadius = arcNewRadius[i];
        } else if (auto* newLine = dynamic_cast<SketchLine*>(sketch->findGeometry(newUuids[i]))) {
            const bool headAdjusted = closedLoop || i > 0;             // 這段起點是否被轉角調整過
            const bool tailAdjusted = closedLoop || i < segCount - 1;  // 這段終點是否被轉角調整過
            if (!headAdjusted) {
                seg.sourceRefPointUuid = pathPointIds[i];
                seg.newRefPointUuid    = newLine->startUuid;
            } else if (!tailAdjusted) {
                seg.sourceRefPointUuid = pathPointIds[i + 1];
                seg.newRefPointUuid    = newLine->endUuid;
            }
            // 兩端都被調整過（中間段，或封閉環的每一段）：不提供參考點。
        }
        result.segments.append(seg);
    }

    for (int j = 0; j < jointCount; ++j) {
        const int prevIdx = j;
        const int nextIdx = (j + 1) % segCount;
        QString prevEndUuid, nextStartUuid, dummy;
        auto* prevNewGeom = sketch->findGeometry(newUuids[prevIdx]);
        auto* nextNewGeom = sketch->findGeometry(newUuids[nextIdx]);
        if (prevNewGeom && nextNewGeom &&
            chainEndpointUuids(prevNewGeom, dummy, prevEndUuid) &&
            chainEndpointUuids(nextNewGeom, nextStartUuid, dummy)) {
            result.joints.append(qMakePair(prevEndUuid, nextStartUuid));
        }
    }

    sketch->solveConstraints();   // ⚠️ 效能修正：內部已透過 markDirty() emit 過 rebuildRequested()，不再重複 emit（避免 Document::rebuildFeature() 多跑一次）
    result.success = true;
    return result;
}

} // namespace trimext
} // namespace command
} // namespace aicad
