/**
 * @file DefinedPlane.cpp
 */
#include "DefinedPlane.h"
#include "Document.h"
#include "Plane.h"
#include "PlaneManager.h"
#include "Sketch.h"
#include "railway/RailwayAlignment.h"

#include <QDebug>
#include <QJsonObject>
#include <QTimer>
#include <QtMath>
#include <cmath>

#include <BRepBuilderAPI_MakeFace.hxx>
#include <Standard_Failure.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

namespace aicad {
namespace cad {

namespace {
constexpr double kPlaneSize = 100.0;       // 顯示用方形邊長（與標準參考平面同大）
constexpr double kEps = 1e-9;

using V3 = QVector3D;

// ── 2D 幾何小工具 ──────────────────────────────────────────────────────────
bool circumcenter(const QVector2D& a, const QVector2D& b, const QVector2D& c, QVector2D& out)
{
    const double ax = a.x(), ay = a.y(), bx = b.x(), by = b.y(), cx = c.x(), cy = c.y();
    const double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::abs(d) < kEps) return false;
    const double a2 = ax * ax + ay * ay, b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
    out = QVector2D(float((a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d),
                    float((a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d));
    return true;
}

SketchGeometry* findGeom(const Document* doc, const QString& sketchId, const QString& geomUuid,
                         Sketch*& sk, QString* err)
{
    sk = doc ? qobject_cast<Sketch*>(doc->findFeature(sketchId)) : nullptr;
    if (!sk) { if (err) *err = QStringLiteral("找不到參照的草圖"); return nullptr; }
    SketchGeometry* g = sk->findGeometry(geomUuid);
    if (!g) { if (err) *err = QStringLiteral("草圖「%1」中找不到參照的幾何（可能已被刪除）").arg(sk->name()); return nullptr; }
    return g;
}

/// 幾何上的一點（草圖平面座標）：起點／終點／中點／圓心
bool geomPoint2D(SketchGeometry* g, int which, QVector2D& out, QString* err)
{
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    if (g->type == SketchGeometryType::Point) {
        out = static_cast<SketchPoint*>(g)->pos;
        return true;
    }
    if (which == WhichCenter) {
        if (g->type == SketchGeometryType::Circle) { out = static_cast<SketchCircle*>(g)->center; return true; }
        if (g->type == SketchGeometryType::Arc && g->points.size() >= 3)
            return circumcenter(g->points[0], g->points[1], g->points[2], out) || fail(QStringLiteral("圓弧三點共線，無法求圓心"));
        return fail(QStringLiteral("此幾何沒有圓心（只有圓與圓弧有）"));
    }
    if (g->points.isEmpty()) return fail(QStringLiteral("此幾何沒有可取的點"));
    if (which == WhichStart) { out = g->points.first(); return true; }
    if (which == WhichEnd)   { out = g->points.last();  return true; }
    // 中點
    if (g->type == SketchGeometryType::Line && g->points.size() >= 2) { out = (g->points[0] + g->points[1]) * 0.5f; return true; }
    if (g->type == SketchGeometryType::Arc && g->points.size() >= 3)  { out = g->points[1]; return true; }
    out = g->points[g->points.size() / 2];
    return true;
}

/// 幾何端點的切線（草圖平面座標）：線＝起→終；弧＝沿弧行進方向；多段線／樣條＝端部線段方向
bool geomTangent2D(SketchGeometry* g, int which, QVector2D& pt, QVector2D& tan, QString* err)
{
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    const auto& P = g->points;
    switch (g->type) {
    case SketchGeometryType::Line:
        if (P.size() < 2) return fail(QStringLiteral("直線資料不完整"));
        pt  = (which == WhichStart) ? P[0] : P[1];
        tan = P[1] - P[0];
        break;
    case SketchGeometryType::Arc: {
        if (P.size() < 3) return fail(QStringLiteral("圓弧資料不完整"));
        QVector2D c;
        if (!circumcenter(P[0], P[1], P[2], c)) return fail(QStringLiteral("圓弧三點共線"));
        pt = (which == WhichStart) ? P[0] : P[2];
        const QVector2D r = pt - c;
        QVector2D t(-r.y(), r.x());                                         // 逆時針切線
        const double cr = double((P[1] - P[0]).x()) * double((P[2] - P[1]).y())
                        - double((P[1] - P[0]).y()) * double((P[2] - P[1]).x());
        if (cr < 0) t = -t;                                                 // 起→中→終 為順時針
        tan = t;
        break;
    }
    case SketchGeometryType::Polyline:
    case SketchGeometryType::Spline:
        if (P.size() < 2) return fail(QStringLiteral("此幾何點數不足"));
        if (which == WhichStart) { pt = P[0];               tan = P[1] - P[0]; }
        else                     { pt = P[P.size() - 1];    tan = P[P.size() - 1] - P[P.size() - 2]; }
        break;
    default:
        return fail(QStringLiteral("此幾何沒有端點切線（支援線、圓弧、多段線、樣條）"));
    }
    if (tan.length() < kEps) return fail(QStringLiteral("端點切線長度為 0"));
    tan.normalize();
    return true;
}

bool alignRange(const railway::TrackCenterLine* t, double p)
{
    double p0 = 0, p1 = 0;
    if (!t->chainageRangeForCalc(p0, p1)) return true;
    return p >= p0 - 1e-6 && p <= p1 + 1e-6;
}

bool evalPoint(const Document* doc, const PlanePointRef& r, V3& out, QString* err)
{
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    switch (r.kind) {
    case PlanePointRef::Coord:
        out = V3(float(r.x), float(r.y), float(r.z));
        return true;
    case PlanePointRef::GeomPoint: {
        Sketch* sk = nullptr;
        SketchGeometry* g = findGeom(doc, r.sketchId, r.geomUuid, sk, err);
        if (!g) return false;
        QVector2D p;
        if (!geomPoint2D(g, r.which, p, err)) return false;
        out = sk->planeToWorld(p);
        return true;
    }
    case PlanePointRef::Alignment: {
        auto* t = doc ? doc->findTrackCenterLine(r.tclId) : nullptr;
        if (!t) return fail(QStringLiteral("找不到參照的線路"));
        if (!alignRange(t, r.chainage)) return fail(QStringLiteral("里程 %1 超出線路範圍").arg(r.chainage, 0, 'f', 3));
        const QPointF xy = t->getXYForCalc(r.chainage, 0.0);
        out = V3(float(xy.x()), float(xy.y()), float(t->getElevationForCalc(r.chainage)));
        return true;
    }
    }
    return fail(QStringLiteral("未知的點參照"));
}

bool evalDir(const Document* doc, const PlaneDirRef& r, V3& out, QString* err)
{
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    switch (r.kind) {
    case PlaneDirRef::Vector:
        out = V3(float(r.x), float(r.y), float(r.z));
        break;
    case PlaneDirRef::Axis:
        out = (r.axis == 0) ? V3(1, 0, 0) : (r.axis == 1) ? V3(0, 1, 0) : V3(0, 0, 1);
        break;
    case PlaneDirRef::GeomTangent: {
        Sketch* sk = nullptr;
        SketchGeometry* g = findGeom(doc, r.sketchId, r.geomUuid, sk, err);
        if (!g) return false;
        QVector2D pt, tan;
        if (!geomTangent2D(g, r.which, pt, tan, err)) return false;
        out = sk->planeToWorld(pt + tan) - sk->planeToWorld(pt);
        break;
    }
    case PlaneDirRef::AlignmentTangent: {
        auto* t = doc ? doc->findTrackCenterLine(r.tclId) : nullptr;
        if (!t) return fail(QStringLiteral("找不到參照的線路"));
        if (!alignRange(t, r.chainage)) return fail(QStringLiteral("里程 %1 超出線路範圍").arg(r.chainage, 0, 'f', 3));
        const double az = t->getAzimuthForCalc(r.chainage);                 // [rad]，自北順時針
        const double g  = t->getSlopeForCalc(r.chainage) / 100.0;           // 縱坡 dz/dp
        const double tn = std::sqrt(1.0 + g * g);
        out = V3(float(std::sin(az) / tn), float(std::cos(az) / tn), float(g / tn));   // 3D 切線（朝里程增加）
        break;
    }
    }
    if (out.length() < kEps) return fail(QStringLiteral("方向向量長度為 0"));
    if (r.reverse) out = -out;
    out.normalize();
    return true;
}

bool evalLine(const Document* doc, const PlaneLineRef& r, V3& point, V3& dir, QString* err)
{
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    V3 a, b;
    if (r.kind == PlaneLineRef::SketchLine) {
        Sketch* sk = nullptr;
        SketchGeometry* g = findGeom(doc, r.sketchId, r.geomUuid, sk, err);
        if (!g) return false;
        if (g->type != SketchGeometryType::Line || g->points.size() < 2)
            return fail(QStringLiteral("「線」需要是草圖中的直線"));
        a = sk->planeToWorld(g->points[0]);
        b = sk->planeToWorld(g->points[1]);
    } else {
        if (!evalPoint(doc, r.a, a, err) || !evalPoint(doc, r.b, b, err)) return false;
    }
    dir = b - a;
    if (dir.length() < kEps) return fail(QStringLiteral("線的兩端點重合"));
    dir.normalize();
    point = a;
    return true;
}

/// 預設 X 軸：世界 X 在平面上的投影；法向量接近 X 時改用世界 Y
V3 defaultX(const V3& n)
{
    V3 x(1, 0, 0);
    x -= n * V3::dotProduct(x, n);
    if (x.length() < 1e-6f) { x = V3(0, 1, 0); x -= n * V3::dotProduct(x, n); }
    x.normalize();
    return x;
}

bool standardPlane(const QString& ref, DefinedPlane::Frame& f)
{
    PlaneManager* pm = PlaneManager::instance();
    const Plane* p = nullptr;
    if      (ref == QLatin1String("XY")) p = pm->xyPlane();
    else if (ref == QLatin1String("XZ")) p = pm->xzPlane();
    else if (ref == QLatin1String("YZ")) p = pm->yzPlane();
    if (!p) return false;
    f.origin = p->origin(); f.normal = p->normal(); f.xAxis = p->xAxis();
    return true;
}

/// 基準平面（標準平面或另一個 DefinedPlane；擋循環）
bool baseFrame(const Document* doc, const QString& ref, DefinedPlane::Frame& f,
               const DefinedPlane* self, QString* err)
{
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    if (standardPlane(ref, f)) return true;

    const DefinedPlane* base = doc ? qobject_cast<DefinedPlane*>(doc->findFeature(ref)) : nullptr;
    if (!base) return fail(QStringLiteral("找不到基準平面：%1").arg(ref));
    // 沿基準鏈往上走，遇到 self 就是循環（Offset／Parallel／MidPlane／AngleFromPlane 的基準都適用）
    for (const DefinedPlane* p = base; p; ) {
        if (p == self) return fail(QStringLiteral("基準平面形成循環（不可以自己或自己的下游當基準）"));
        const PlaneDef d = p->definition();
        const DefinedPlane* next = nullptr;
        if (d.method == PlaneMethod::Offset || d.method == PlaneMethod::ParallelThroughPoint
            || d.method == PlaneMethod::AngleFromPlane || d.method == PlaneMethod::MidPlane)
            next = qobject_cast<DefinedPlane*>(doc->findFeature(d.baseRef));
        p = next;
    }
    f.origin = base->origin(); f.normal = base->normal(); f.xAxis = base->xAxis();
    return true;
}

const char* kMethodNames[] = {
    "偏移", "線端點＋切線", "一點＋法向量", "三點", "一點＋一線", "兩線", "繞軸旋轉", "中間／等分平面",
    "過一點平行", "線形里程橫斷面"
};
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// JSON
// ─────────────────────────────────────────────────────────────────────────────
QJsonObject PlanePointRef::toJson() const
{
    QJsonObject o;
    o["kind"] = int(kind);
    o["x"] = x; o["y"] = y; o["z"] = z;
    o["sketchId"] = sketchId; o["geomUuid"] = geomUuid; o["which"] = which;
    o["tclId"] = tclId; o["chainage"] = chainage;
    return o;
}
PlanePointRef PlanePointRef::fromJson(const QJsonObject& o)
{
    PlanePointRef r;
    r.kind = static_cast<Kind>(o["kind"].toInt(0));
    r.x = o["x"].toDouble(); r.y = o["y"].toDouble(); r.z = o["z"].toDouble();
    r.sketchId = o["sketchId"].toString(); r.geomUuid = o["geomUuid"].toString(); r.which = o["which"].toInt(0);
    r.tclId = o["tclId"].toString(); r.chainage = o["chainage"].toDouble();
    return r;
}

QJsonObject PlaneDirRef::toJson() const
{
    QJsonObject o;
    o["kind"] = int(kind);
    o["x"] = x; o["y"] = y; o["z"] = z;
    o["sketchId"] = sketchId; o["geomUuid"] = geomUuid; o["which"] = which;
    o["axis"] = axis; o["tclId"] = tclId; o["chainage"] = chainage; o["reverse"] = reverse;
    return o;
}
PlaneDirRef PlaneDirRef::fromJson(const QJsonObject& o)
{
    PlaneDirRef r;
    r.kind = static_cast<Kind>(o["kind"].toInt(0));
    r.x = o["x"].toDouble(0); r.y = o["y"].toDouble(0); r.z = o["z"].toDouble(1);
    r.sketchId = o["sketchId"].toString(); r.geomUuid = o["geomUuid"].toString(); r.which = o["which"].toInt(1);
    r.axis = o["axis"].toInt(2); r.tclId = o["tclId"].toString(); r.chainage = o["chainage"].toDouble();
    r.reverse = o["reverse"].toBool(false);
    return r;
}

QJsonObject PlaneLineRef::toJson() const
{
    QJsonObject o;
    o["kind"] = int(kind);
    o["sketchId"] = sketchId; o["geomUuid"] = geomUuid;
    o["a"] = a.toJson(); o["b"] = b.toJson();
    return o;
}
PlaneLineRef PlaneLineRef::fromJson(const QJsonObject& o)
{
    PlaneLineRef r;
    r.kind = static_cast<Kind>(o["kind"].toInt(0));
    r.sketchId = o["sketchId"].toString(); r.geomUuid = o["geomUuid"].toString();
    r.a = PlanePointRef::fromJson(o["a"].toObject()); r.b = PlanePointRef::fromJson(o["b"].toObject());
    return r;
}

QJsonObject PlaneDef::toJson() const
{
    QJsonObject o;
    o["method"] = int(method);
    o["baseRef"] = baseRef; o["baseRef2"] = baseRef2;
    o["offsetExpr"] = offsetExpr; o["angleExpr"] = angleExpr; o["flip"] = flip;
    o["p1"] = p1.toJson(); o["p2"] = p2.toJson(); o["p3"] = p3.toJson();
    o["dir1"] = dir1.toJson(); o["line1"] = line1.toJson(); o["line2"] = line2.toJson();
    return o;
}
PlaneDef PlaneDef::fromJson(const QJsonObject& o)
{
    PlaneDef d;
    d.method   = static_cast<PlaneMethod>(o["method"].toInt(0));
    d.baseRef  = o["baseRef"].toString(QStringLiteral("XY"));
    d.baseRef2 = o["baseRef2"].toString(QStringLiteral("XZ"));
    d.offsetExpr = o["offsetExpr"].toString(QStringLiteral("0"));
    d.angleExpr  = o["angleExpr"].toString(QStringLiteral("90"));
    d.flip = o["flip"].toBool(false);
    d.p1 = PlanePointRef::fromJson(o["p1"].toObject());
    d.p2 = PlanePointRef::fromJson(o["p2"].toObject());
    d.p3 = PlanePointRef::fromJson(o["p3"].toObject());
    d.dir1  = PlaneDirRef::fromJson(o["dir1"].toObject());
    d.line1 = PlaneLineRef::fromJson(o["line1"].toObject());
    d.line2 = PlaneLineRef::fromJson(o["line2"].toObject());
    return d;
}

// ─────────────────────────────────────────────────────────────────────────────
// 求解
// ─────────────────────────────────────────────────────────────────────────────
bool DefinedPlane::evalNumber(const Document* doc, const QString& expr, double& out)
{
    const QString e = expr.trimmed();
    if (e.isEmpty()) { out = 0.0; return true; }
    bool lit = false;
    const double v = e.toDouble(&lit);
    if (lit) { out = v; return true; }
    if (!doc) return false;
    const auto r = const_cast<Document*>(doc)->parameterStore()->evaluate(e);
    if (!r.first) return false;
    out = r.second;
    return true;
}

bool DefinedPlane::evaluate(const Document* doc, const PlaneDef& d, Frame& out,
                            const DefinedPlane* self, QString* error)
{
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };

    V3 O, N, X;             // 結果；X 零向量 = 用預設 X 軸
    bool haveX = false;

    switch (d.method) {
    // 1 從既有平面偏移
    case PlaneMethod::Offset: {
        double off = 0.0;
        if (!evalNumber(doc, d.offsetExpr, off)) return fail(QStringLiteral("偏移量運算式無法求值：%1").arg(d.offsetExpr));
        Frame b;
        if (!baseFrame(doc, d.baseRef, b, self, error)) return false;
        b.normal.normalize();
        O = b.origin + b.normal * float(off);
        N = b.normal; X = b.xAxis; haveX = true;
        break;
    }
    // 2 取線端點，切線當法向量  /  3 一點 + 法向量方向  /  10 線形里程橫斷面
    case PlaneMethod::EndpointTangent:
    case PlaneMethod::PointNormal:
    case PlaneMethod::AlignmentSection: {
        if (!evalPoint(doc, d.p1, O, error)) return false;
        if (!evalDir(doc, d.dir1, N, error)) return false;
        break;
    }
    // 4 三點
    case PlaneMethod::ThreePoints: {
        V3 a, b, c;
        if (!evalPoint(doc, d.p1, a, error) || !evalPoint(doc, d.p2, b, error) || !evalPoint(doc, d.p3, c, error)) return false;
        V3 ab = b - a, ac = c - a;
        if (ab.length() < kEps || ac.length() < kEps) return fail(QStringLiteral("三點中有重合的點"));
        N = V3::crossProduct(ab, ac);
        if (N.length() < 1e-9f * ab.length() * ac.length()) return fail(QStringLiteral("三點共線，無法決定平面"));
        O = a; X = ab; haveX = true;
        break;
    }
    // 5 過一點 + 一線
    case PlaneMethod::PointLine: {
        V3 lp, ld, p;
        if (!evalLine(doc, d.line1, lp, ld, error) || !evalPoint(doc, d.p1, p, error)) return false;
        const V3 v = p - lp;
        N = V3::crossProduct(ld, v);
        if (N.length() < 1e-9f * std::max(1.0f, v.length())) return fail(QStringLiteral("點在線上，無法決定平面"));
        O = lp; X = ld; haveX = true;
        break;
    }
    // 6 兩線共面
    case PlaneMethod::TwoLines: {
        V3 a1, d1, a2, d2;
        if (!evalLine(doc, d.line1, a1, d1, error) || !evalLine(doc, d.line2, a2, d2, error)) return false;
        V3 c = V3::crossProduct(d1, d2);
        const V3 w = a2 - a1;
        if (c.length() > 1e-6f) {                                  // 不平行：必須共面
            c.normalize();
            const double gap = std::abs(double(V3::dotProduct(w, c)));
            if (gap > 1e-4 * std::max(1.0, double(w.length())))
                return fail(QStringLiteral("兩線不共面（距離 %1）").arg(gap, 0, 'g', 4));
            N = c;
        } else {                                                    // 平行
            N = V3::crossProduct(d1, w);
            if (N.length() < 1e-9f * std::max(1.0f, w.length())) return fail(QStringLiteral("兩線重合，無法決定平面"));
        }
        O = a1; X = d1; haveX = true;
        break;
    }
    // 7 繞軸旋轉角度
    case PlaneMethod::AngleFromPlane: {
        V3 ap, ad;
        if (!evalLine(doc, d.line1, ap, ad, error)) return false;
        double deg = 0.0;
        if (!evalNumber(doc, d.angleExpr, deg)) return fail(QStringLiteral("角度運算式無法求值：%1").arg(d.angleExpr));
        Frame b;
        if (!baseFrame(doc, d.baseRef, b, self, error)) return false;
        b.normal.normalize();
        if (V3::crossProduct(ad, b.normal).length() < 1e-6f) return fail(QStringLiteral("旋轉軸與參考平面的法向量平行，無法旋轉"));
        const double th = qDegreesToRadians(deg), c = std::cos(th), s = std::sin(th);
        // Rodrigues：n' = n cosθ + (a × n) sinθ + a (a·n)(1 − cosθ)
        N = b.normal * float(c) + V3::crossProduct(ad, b.normal) * float(s)
            + ad * float(double(V3::dotProduct(ad, b.normal)) * (1.0 - c));
        O = ap; X = ad; haveX = true;
        break;
    }
    // 8 兩平面的中間／等分平面
    case PlaneMethod::MidPlane: {
        Frame a, b;
        if (!baseFrame(doc, d.baseRef, a, self, error) || !baseFrame(doc, d.baseRef2, b, self, error)) return false;
        a.normal.normalize(); b.normal.normalize();
        if (V3::crossProduct(a.normal, b.normal).length() < 1e-6f) {            // 平行 → 中間平面
            V3 nb = b.normal;
            if (V3::dotProduct(a.normal, nb) < 0) nb = -nb;
            const float dist = V3::dotProduct(b.origin - a.origin, a.normal);
            O = a.origin + a.normal * (dist * 0.5f);
            N = a.normal; X = a.xAxis; haveX = true;
        } else {                                                                 // 不平行 → 等分平面
            const V3 n = d.flip ? (a.normal + b.normal) : (a.normal - b.normal);  // flip：另一條等分線
            const float rhs = d.flip ? (V3::dotProduct(a.normal, a.origin) + V3::dotProduct(b.normal, b.origin))
                                     : (V3::dotProduct(a.normal, a.origin) - V3::dotProduct(b.normal, b.origin));
            if (n.length() < 1e-6f) return fail(QStringLiteral("兩平面重合，無等分平面"));
            O = n * (rhs / V3::dotProduct(n, n));        // 滿足 n·x = rhs 的最近原點的點
            N = n;
            X = V3::crossProduct(a.normal, b.normal);    // 沿兩平面交線
            haveX = true;
        }
        break;
    }
    // 9 過一點且平行於平面
    case PlaneMethod::ParallelThroughPoint: {
        Frame b;
        if (!baseFrame(doc, d.baseRef, b, self, error)) return false;
        if (!evalPoint(doc, d.p1, O, error)) return false;
        N = b.normal; X = b.xAxis; haveX = true;
        break;
    }
    }

    if (N.length() < kEps) return fail(QStringLiteral("法向量長度為 0"));
    N.normalize();
    if (d.flip && d.method != PlaneMethod::MidPlane) N = -N;       // MidPlane 的 flip 已用來選等分線

    // X 軸：先去掉法向量分量並正規化；不可用時用預設 X
    if (haveX) {
        X -= N * V3::dotProduct(X, N);
        if (X.length() < 1e-6f) haveX = false; else X.normalize();
    }
    if (!haveX) X = defaultX(N);

    out.origin = O; out.normal = N; out.xAxis = X;
    return true;
}

bool DefinedPlane::computeFrame(const Document* doc, const QString& baseRef, double offsetValue,
                                QVector3D& origin, QVector3D& normal, QVector3D& xAxis,
                                const DefinedPlane* self, QString* error)
{
    PlaneDef d;
    d.method = PlaneMethod::Offset;
    d.baseRef = baseRef;
    d.offsetExpr = QString::number(offsetValue, 'g', 15);
    Frame f;
    if (!evaluate(doc, d, f, self, error)) return false;
    origin = f.origin; normal = f.normal; xAxis = f.xAxis;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 其餘
// ─────────────────────────────────────────────────────────────────────────────
DefinedPlane::DefinedPlane(Document* parent)
    : Feature(parent)
{
    setName("Plane");
}

QString DefinedPlane::methodLabel(PlaneMethod m)
{
    const int i = int(m);
    return QString::fromUtf8(i >= 0 && i < int(sizeof(kMethodNames) / sizeof(kMethodNames[0])) ? kMethodNames[i] : "?");
}

QString DefinedPlane::geometryLabel(const Sketch* sk, int index)
{
    if (!sk) return QString();
    const auto geoms = sk->geometriesRef();
    if (index < 0 || index >= geoms.size() || !geoms[index]) return QString();
    const SketchGeometry* g = geoms[index];
    QString t;
    switch (g->type) {
    case SketchGeometryType::Line:     t = QStringLiteral("線"); break;
    case SketchGeometryType::Arc:      t = QStringLiteral("圓弧"); break;
    case SketchGeometryType::Circle:   t = QStringLiteral("圓"); break;
    case SketchGeometryType::Ellipse:  t = QStringLiteral("橢圓"); break;
    case SketchGeometryType::Polyline: t = QStringLiteral("多段線"); break;
    case SketchGeometryType::Spline:   t = QStringLiteral("樣條"); break;
    case SketchGeometryType::Point:    t = QStringLiteral("點"); break;
    }
    return QStringLiteral("#%1 %2").arg(index + 1).arg(t);
}

bool DefinedPlane::setDefinition(const PlaneDef& def, QString* error)
{
    Frame f;
    if (!evaluate(document(), def, f, this, error)) return false;
    m_def = def;
    double off = 0.0;
    if (def.method == PlaneMethod::Offset) evalNumber(document(), def.offsetExpr, off);
    m_offsetValue = off;
    markDirty();
    return true;
}

bool DefinedPlane::setDefinition(const QString& baseRef, const QString& offsetExpr, QString* error)
{
    PlaneDef d;
    d.method = PlaneMethod::Offset;
    d.baseRef = baseRef;
    d.offsetExpr = offsetExpr.trimmed().isEmpty() ? QStringLiteral("0") : offsetExpr.trimmed();
    return setDefinition(d, error);
}

QString DefinedPlane::baseDisplayName() const
{
    auto nameOf = [this](const QString& ref) -> QString {
        if (ref == QLatin1String("XY")) return QStringLiteral("XY Plane");
        if (ref == QLatin1String("XZ")) return QStringLiteral("XZ Plane");
        if (ref == QLatin1String("YZ")) return QStringLiteral("YZ Plane");
        if (document()) if (Feature* f = document()->findFeature(ref)) return f->name();
        return ref;
    };
    switch (m_def.method) {
    case PlaneMethod::Offset:
    case PlaneMethod::ParallelThroughPoint:
    case PlaneMethod::AngleFromPlane:
        return nameOf(m_def.baseRef);
    case PlaneMethod::MidPlane:
        return nameOf(m_def.baseRef) + QStringLiteral(" / ") + nameOf(m_def.baseRef2);
    default:
        return methodLabel(m_def.method);
    }
}

QString DefinedPlane::description() const
{
    if (m_def.method == PlaneMethod::Offset)
        return QStringLiteral("偏移（%1，%2）").arg(baseDisplayName()).arg(m_offsetValue);
    return methodLabel(m_def.method);
}

Plane* DefinedPlane::createSketchPlane() const
{
    return PlaneManager::instance()->createPlane(m_frame.origin, m_frame.normal, m_frame.xAxis, name());
}

void DefinedPlane::requestRebuild()
{
    if (m_rebuildPending) return;
    m_rebuildPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_rebuildPending = false;
        if (Document* doc = document()) doc->rebuildFeature(this);
        else rebuild();
    });
}

void DefinedPlane::refreshSources()
{
    for (const QMetaObject::Connection& c : std::as_const(m_conns)) disconnect(c);
    m_conns.clear();
    Document* doc = document();
    if (!doc) return;

    QSet<QString> sketchIds, tclIds, planeIds;
    auto addPoint = [&](const PlanePointRef& r) {
        if (r.kind == PlanePointRef::GeomPoint) sketchIds.insert(r.sketchId);
        if (r.kind == PlanePointRef::Alignment) tclIds.insert(r.tclId);
    };
    auto addDir = [&](const PlaneDirRef& r) {
        if (r.kind == PlaneDirRef::GeomTangent) sketchIds.insert(r.sketchId);
        if (r.kind == PlaneDirRef::AlignmentTangent) tclIds.insert(r.tclId);
    };
    auto addLine = [&](const PlaneLineRef& r) {
        if (r.kind == PlaneLineRef::SketchLine) sketchIds.insert(r.sketchId);
        else { addPoint(r.a); addPoint(r.b); }
    };
    addPoint(m_def.p1); addPoint(m_def.p2); addPoint(m_def.p3); addDir(m_def.dir1);
    addLine(m_def.line1); addLine(m_def.line2);
    planeIds.insert(m_def.baseRef); planeIds.insert(m_def.baseRef2);

    for (const QString& id : std::as_const(sketchIds))
        if (auto* sk = qobject_cast<Sketch*>(doc->findFeature(id)))
        {
            m_conns << connect(sk, &Sketch::geometryChanged, this, [this]() { requestRebuild(); });
            m_conns << connect(sk, &Sketch::rebuilt,         this, [this]() { requestRebuild(); });   // 約束求解後點位移動
        }
    for (const QString& id : std::as_const(tclIds))
        if (auto* t = doc->findTrackCenterLine(id))
            m_conns << connect(t, &railway::TrackCenterLine::dataChanged, this, [this]() { requestRebuild(); });
    for (const QString& id : std::as_const(planeIds))
        if (auto* p = qobject_cast<DefinedPlane*>(doc->findFeature(id)))
            if (p != this) m_conns << connect(p, &Feature::shapeChanged, this, [this]() { requestRebuild(); });
}

bool DefinedPlane::rebuild()
{
    QString err;
    Frame f;
    if (!evaluate(document(), m_def, f, this, &err)) {
        setError(err);
        return false;
    }
    m_frame = f;
    if (m_def.method == PlaneMethod::Offset) evalNumber(document(), m_def.offsetExpr, m_offsetValue);

    try {
        const gp_Ax3 ax(gp_Pnt(f.origin.x(), f.origin.y(), f.origin.z()),
                        gp_Dir(f.normal.x(), f.normal.y(), f.normal.z()),
                        gp_Dir(f.xAxis.x(),  f.xAxis.y(),  f.xAxis.z()));
        BRepBuilderAPI_MakeFace mk(gp_Pln(ax), -kPlaneSize / 2, kPlaneSize / 2, -kPlaneSize / 2, kPlaneSize / 2);
        if (!mk.IsDone()) { setError(QStringLiteral("建立平面顯示面失敗")); return false; }
        setShape(mk.Shape());
        clearError();
    } catch (const Standard_Failure& ex) {
        setError(QStringLiteral("OCC error: %1").arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown"));
        return false;
    }
    refreshSources();       // 參照的草圖／線形／基準平面改變時自動重算
    return true;
}

QJsonObject DefinedPlane::toJson() const
{
    QJsonObject obj = Feature::toJson();
    obj["type"] = typeString();
    obj["def"]  = m_def.toJson();
    // 舊版欄位（偏移平面）：保留以利舊程式讀取
    obj["baseRef"]          = m_def.baseRef;
    obj["offsetExpression"] = m_def.offsetExpr;
    return obj;
}

bool DefinedPlane::fromJson(const QJsonObject& json)
{
    Feature::fromJson(json);
    if (json.contains("def")) {
        m_def = PlaneDef::fromJson(json["def"].toObject());
    } else {                                       // 舊檔：只有偏移平面
        m_def = PlaneDef();
        m_def.method     = PlaneMethod::Offset;
        m_def.baseRef    = json["baseRef"].toString(QStringLiteral("XY"));
        m_def.offsetExpr = json["offsetExpression"].toString(QStringLiteral("0"));
    }
    double off = 0.0;
    bool lit = false;
    off = m_def.offsetExpr.toDouble(&lit);
    m_offsetValue = lit ? off : 0.0;               // 非數值運算式於 rebuild() 求值
    return true;
}

} // namespace cad
} // namespace aicad
