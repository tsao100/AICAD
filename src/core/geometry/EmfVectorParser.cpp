/**
 * @file EmfVectorParser.cpp
 * @brief 見 EmfVectorParser.h 檔頭說明。
 */
#include "EmfVectorParser.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace aicad {
namespace core {
namespace emf {

namespace {

constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// ── EMF record type ids（只列出會用到的）────────────────────────────────
enum : uint32_t {
    EMR_HEADER = 1, EMR_POLYBEZIER = 2, EMR_POLYGON = 3, EMR_POLYLINE = 4,
    EMR_POLYBEZIERTO = 5, EMR_POLYLINETO = 6, EMR_POLYPOLYLINE = 7,
    EMR_POLYPOLYGON = 8, EMR_SETWINDOWEXTEX = 9, EMR_SETWINDOWORGEX = 10,
    EMR_SETVIEWPORTEXTEX = 11, EMR_SETVIEWPORTORGEX = 12, EMR_EOF = 14,
    EMR_SETMAPMODE = 17, EMR_MOVETOEX = 27, EMR_SCALEVIEWPORTEXTEX = 31,
    EMR_SCALEWINDOWEXTEX = 32, EMR_SAVEDC = 33, EMR_RESTOREDC = 34,
    EMR_SETWORLDTRANSFORM = 35, EMR_MODIFYWORLDTRANSFORM = 36,
    EMR_SELECTOBJECT = 37, EMR_CREATEPEN = 38, EMR_CREATEBRUSHINDIRECT = 39,
    EMR_DELETEOBJECT = 40, EMR_ANGLEARC = 41, EMR_ELLIPSE = 42,
    EMR_RECTANGLE = 43, EMR_ARC = 45, EMR_CHORD = 46, EMR_PIE = 47,
    EMR_LINETO = 54, EMR_ARCTO = 55, EMR_SETARCDIRECTION = 57,
    EMR_BEGINPATH = 59, EMR_ENDPATH = 60, EMR_CLOSEFIGURE = 61,
    EMR_FILLPATH = 62, EMR_STROKEANDFILLPATH = 63, EMR_STROKEPATH = 64,
    EMR_BITBLT = 76, EMR_STRETCHBLT = 77, EMR_MASKBLT = 78, EMR_PLGBLT = 79,
    EMR_SETDIBITSTODEVICE = 80, EMR_STRETCHDIBITS = 81,
    EMR_EXTTEXTOUTA = 83, EMR_EXTTEXTOUTW = 84, EMR_POLYBEZIER16 = 85,
    EMR_POLYGON16 = 86, EMR_POLYLINE16 = 87, EMR_POLYBEZIERTO16 = 88,
    EMR_POLYLINETO16 = 89, EMR_POLYPOLYLINE16 = 90, EMR_POLYPOLYGON16 = 91,
    EMR_EXTCREATEPEN = 95, EMR_SMALLTEXTOUT = 108, EMR_ALPHABLEND = 114,
    EMR_TRANSPARENTBLT = 116
};

enum { MM_TEXT = 1, MM_LOMETRIC = 2, MM_HIMETRIC = 3, MM_LOENGLISH = 4,
       MM_HIENGLISH = 5, MM_TWIPS = 6, MM_ISOTROPIC = 7, MM_ANISOTROPIC = 8 };

// ── 小工具：little-endian 讀取（不依賴主機位元組序）─────────────────────
struct Rec {
    const unsigned char* p;
    uint32_t             size;   // 含 8 byte 表頭

    bool has(uint32_t off, uint32_t n) const { return off + n <= size; }
    uint32_t u32(uint32_t off) const {
        return uint32_t(p[off]) | (uint32_t(p[off+1]) << 8) |
               (uint32_t(p[off+2]) << 16) | (uint32_t(p[off+3]) << 24);
    }
    int32_t i32(uint32_t off) const { return int32_t(u32(off)); }
    int16_t i16(uint32_t off) const {
        return int16_t(uint16_t(p[off]) | (uint16_t(p[off+1]) << 8));
    }
    float f32(uint32_t off) const {
        const uint32_t v = u32(off);
        float f; std::memcpy(&f, &v, 4); return f;
    }
};

// 3x3 仿射（列向量慣例同 GDI XFORM：x' = x*m11 + y*m21 + dx）
struct Xf {
    double m11 = 1, m12 = 0, m21 = 0, m22 = 1, dx = 0, dy = 0;
};

// result = a × b（GDI 列向量慣例：先套 a、再套 b）
Xf mul(const Xf& a, const Xf& b)
{
    Xf r;
    r.m11 = a.m11*b.m11 + a.m12*b.m21;
    r.m12 = a.m11*b.m12 + a.m12*b.m22;
    r.m21 = a.m21*b.m11 + a.m22*b.m21;
    r.m22 = a.m21*b.m12 + a.m22*b.m22;
    r.dx  = a.dx*b.m11  + a.dy*b.m21 + b.dx;
    r.dy  = a.dx*b.m12  + a.dy*b.m22 + b.dy;
    return r;
}

struct Gdi {
    int    mapMode = MM_TEXT;
    double winOrgX = 0, winOrgY = 0, winExtX = 1, winExtY = 1;
    double vpOrgX  = 0, vpOrgY  = 0, vpExtX  = 1, vpExtY  = 1;
    Xf     world;
    double curX = 0, curY = 0;      // 目前位置（邏輯座標）
    int    arcDir = 1;              // 1 = AD_COUNTERCLOCKWISE, 2 = AD_CLOCKWISE
    bool   penNull = false;
};

struct Obj { bool isPen = false; bool null = false; };

struct Ctx {
    Gdi                  g;
    std::vector<Gdi>     stack;
    std::vector<Obj>     objs;
    // 裝置 → 毫米
    double pxPerMmX = 4.0, pxPerMmY = 4.0;
    double mmPerPxX = 0.25, mmPerPxY = 0.25;
    // 路徑
    bool inPath = false;
    std::vector<Primitive> pathPrims;
    // 目前正在累積的折線（MoveTo/LineTo 序列）
    std::vector<Pt> fig;
    bool figClosed = false;
};

// 邏輯座標 → 毫米（Y 向上）
Pt mapPt(const Ctx& c, double lx, double ly)
{
    const Gdi& g = c.g;
    // 世界轉換
    const double wx = lx*g.world.m11 + ly*g.world.m21 + g.world.dx;
    const double wy = lx*g.world.m12 + ly*g.world.m22 + g.world.dy;

    double dxp, dyp;   // 裝置座標（像素，Y 向下）
    switch (g.mapMode) {
    case MM_LOMETRIC: case MM_HIMETRIC: case MM_LOENGLISH:
    case MM_HIENGLISH: case MM_TWIPS: {
        double unitMm = 0.1;
        if (g.mapMode == MM_HIMETRIC)  unitMm = 0.01;
        if (g.mapMode == MM_LOENGLISH) unitMm = 0.254;
        if (g.mapMode == MM_HIENGLISH) unitMm = 0.0254;
        if (g.mapMode == MM_TWIPS)     unitMm = 25.4 / 1440.0;
        dxp = (wx - g.winOrgX) * unitMm * c.pxPerMmX + g.vpOrgX;
        dyp = -(wy - g.winOrgY) * unitMm * c.pxPerMmY + g.vpOrgY;
        break;
    }
    case MM_ISOTROPIC: case MM_ANISOTROPIC: {
        const double sx = (g.winExtX != 0.0) ? g.vpExtX / g.winExtX : 1.0;
        const double sy = (g.winExtY != 0.0) ? g.vpExtY / g.winExtY : 1.0;
        dxp = (wx - g.winOrgX) * sx + g.vpOrgX;
        dyp = (wy - g.winOrgY) * sy + g.vpOrgY;
        break;
    }
    case MM_TEXT: default:
        dxp = (wx - g.winOrgX) + g.vpOrgX;
        dyp = (wy - g.winOrgY) + g.vpOrgY;
        break;
    }
    return Pt{ dxp * c.mmPerPxX, -dyp * c.mmPerPxY };
}

double dist(const Pt& a, const Pt& b) { return std::hypot(a.x - b.x, a.y - b.y); }

// ── 圖元輸出 ────────────────────────────────────────────────────────────
std::vector<Primitive>& dst(Ctx& c, std::vector<Primitive>& out)
{
    return c.inPath ? c.pathPrims : out;
}

void pushPolyline(std::vector<Primitive>& to, std::vector<Pt> pts, bool closed)
{
    // 去掉連續重複點；封閉時若最後一點等於第一點也去掉
    std::vector<Pt> q;
    q.reserve(pts.size());
    for (const Pt& p : pts)
        if (q.empty() || dist(q.back(), p) > 1e-9) q.push_back(p);
    if (closed && q.size() > 1 && dist(q.front(), q.back()) <= 1e-9) q.pop_back();
    if (q.size() < 2) return;
    if (closed && q.size() < 3) closed = false;
    Primitive pr;
    pr.kind = PrimKind::Polyline;
    pr.pts = std::move(q);
    pr.closed = closed;
    to.push_back(std::move(pr));
}

void flushFig(Ctx& c, std::vector<Primitive>& out)
{
    if (c.fig.size() >= 2)
        pushPolyline(dst(c, out), c.fig, c.figClosed);
    c.fig.clear();
    c.figClosed = false;
}

// 讀 n 個點（32-bit POINTL 或 16-bit POINTS），轉毫米
bool readPoints(const Ctx& c, const Rec& r, uint32_t off, uint32_t n,
                bool is16, std::vector<Pt>& out)
{
    const uint32_t step = is16 ? 4u : 8u;
    if (n > 50000000u) return false;
    if (!r.has(off, n * step)) return false;
    out.reserve(out.size() + n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t o = off + i * step;
        const double lx = is16 ? r.i16(o)     : r.i32(o);
        const double ly = is16 ? r.i16(o + 2) : r.i32(o + 4);
        out.push_back(mapPt(c, lx, ly));
    }
    return true;
}

// 三次貝茲 p0..p3 攤平（不含 p0）
void flattenCubic(const Pt& p0, const Pt& p1, const Pt& p2, const Pt& p3,
                  int segs, std::vector<Pt>& out)
{
    segs = std::max(2, segs);
    for (int i = 1; i <= segs; ++i) {
        const double t = double(i) / segs, u = 1.0 - t;
        const double a = u*u*u, b = 3*u*u*t, cc = 3*u*t*t, d = t*t*t;
        out.push_back(Pt{ a*p0.x + b*p1.x + cc*p2.x + d*p3.x,
                          a*p0.y + b*p1.y + cc*p2.y + d*p3.y });
    }
}

double normPos(double a)   // → [0, 2π)
{
    a = std::fmod(a, kTwoPi);
    if (a < 0) a += kTwoPi;
    return a;
}

// 橢圓／圓的弧：box 以兩個對角點（毫米）給出
void emitArcLike(Ctx& c, std::vector<Primitive>& out, const Pt& b1, const Pt& b2,
                 const Pt* s, const Pt* e, bool wholeIfNoArc)
{
    const Pt ctr{ (b1.x + b2.x) / 2.0, (b1.y + b2.y) / 2.0 };
    const double rx = std::fabs(b2.x - b1.x) / 2.0;
    const double ry = std::fabs(b2.y - b1.y) / 2.0;
    if (rx <= 1e-9 || ry <= 1e-9) return;

    const bool circular = std::fabs(rx - ry) <= 0.005 * std::max(rx, ry);
    auto& to = dst(c, out);

    if (!s || !e) {   // 整個橢圓／圓
        (void)wholeIfNoArc;
        Primitive pr;
        pr.center = ctr;
        if (circular) {
            pr.kind = PrimKind::Circle;
            pr.r = (rx + ry) / 2.0;
        } else {
            pr.kind = PrimKind::Ellipse;
            if (rx >= ry) { pr.r = rx; pr.r2 = ry; pr.angleRad = 0.0; }
            else          { pr.r = ry; pr.r2 = rx; pr.angleRad = kPi / 2.0; }
        }
        to.push_back(std::move(pr));
        return;
    }

    // 圓弧：以參數角（對橢圓先除以半軸）求起訖角
    const double aS = std::atan2((s->y - ctr.y) / ry, (s->x - ctr.x) / rx);
    const double aE = std::atan2((e->y - ctr.y) / ry, (e->x - ctr.x) / rx);
    double sweep;
    if (c.g.arcDir == 2) {   // 順時針
        sweep = -normPos(aS - aE);
    } else {                 // 逆時針（GDI 預設）
        sweep = normPos(aE - aS);
    }
    if (std::fabs(sweep) < 1e-9) sweep = (c.g.arcDir == 2) ? -kTwoPi : kTwoPi;  // 起訖同點 = 整圓

    if (circular && std::fabs(std::fabs(sweep) - kTwoPi) < 1e-9) {
        Primitive pr; pr.kind = PrimKind::Circle; pr.center = ctr; pr.r = (rx + ry) / 2.0;
        to.push_back(std::move(pr));
        return;
    }

    if (circular) {
        const double r = (rx + ry) / 2.0;
        auto at = [&](double a) { return Pt{ ctr.x + r*std::cos(a), ctr.y + r*std::sin(a) }; };
        Primitive pr;
        pr.kind = PrimKind::Arc;
        pr.pts = { at(aS), at(aS + sweep / 2.0), at(aS + sweep) };
        to.push_back(std::move(pr));
    } else {
        // 橢圓弧：草圖沒有橢圓弧圖元，攤平成折線（每約 5° 一段）
        const int n = std::max(4, int(std::ceil(std::fabs(sweep) / (5.0 * kPi / 180.0))));
        std::vector<Pt> pts;
        for (int i = 0; i <= n; ++i) {
            const double a = aS + sweep * double(i) / n;
            pts.push_back(Pt{ ctr.x + rx*std::cos(a), ctr.y + ry*std::sin(a) });
        }
        pushPolyline(to, pts, false);
    }
}

// ────────────────────────────────────────────────────────────────────────
// 折線後處理：圓／弧還原、Douglas-Peucker
// ────────────────────────────────────────────────────────────────────────

struct CircleFit { bool ok = false; Pt c; double r = 0; };

// Kåsa 代數擬合（先平移到重心增加數值穩定性）
CircleFit fitCircle(const std::vector<Pt>& pts)
{
    CircleFit f;
    const size_t n = pts.size();
    if (n < 3) return f;
    double mx = 0, my = 0;
    for (const Pt& p : pts) { mx += p.x; my += p.y; }
    mx /= double(n); my /= double(n);

    // 解 [Σu² Σuv Σu][a]   [Σu(u²+v²)]
    //    [Σuv Σv² Σv][b] = [Σv(u²+v²)]   其中 center=(a/2,b/2) 的形式，
    //    [Σu  Σv  n ][c]   [Σ(u²+v²) ]
    double Suu=0,Suv=0,Svv=0,Su=0,Sv=0,Suz=0,Svz=0,Sz=0;
    for (const Pt& p : pts) {
        const double u = p.x - mx, v = p.y - my, z = u*u + v*v;
        Suu += u*u; Suv += u*v; Svv += v*v; Su += u; Sv += v;
        Suz += u*z; Svz += v*z; Sz += z;
    }
    double A[3][4] = {
        { Suu, Suv, Su, Suz },
        { Suv, Svv, Sv, Svz },
        { Su,  Sv,  double(n), Sz }
    };
    // 高斯消去（部分主元）
    for (int i = 0; i < 3; ++i) {
        int piv = i;
        for (int k = i + 1; k < 3; ++k)
            if (std::fabs(A[k][i]) > std::fabs(A[piv][i])) piv = k;
        if (std::fabs(A[piv][i]) < 1e-14) return f;   // 共線／退化
        if (piv != i) for (int j = 0; j < 4; ++j) std::swap(A[i][j], A[piv][j]);
        for (int k = i + 1; k < 3; ++k) {
            const double m = A[k][i] / A[i][i];
            for (int j = i; j < 4; ++j) A[k][j] -= m * A[i][j];
        }
    }
    double sol[3];
    for (int i = 2; i >= 0; --i) {
        double s = A[i][3];
        for (int j = i + 1; j < 3; ++j) s -= A[i][j] * sol[j];
        sol[i] = s / A[i][i];
    }
    const double cu = sol[0] / 2.0, cv = sol[1] / 2.0;
    const double r2 = sol[2] + cu*cu + cv*cv;
    if (!(r2 > 0.0)) return f;
    f.ok = true;
    f.c = Pt{ mx + cu, my + cv };
    f.r = std::sqrt(r2);
    return f;
}

double ptLineDist(const Pt& p, const Pt& a, const Pt& b)
{
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double L = std::hypot(dx, dy);
    if (L < 1e-12) return dist(p, a);
    return std::fabs((p.x - a.x) * dy - (p.y - a.y) * dx) / L;
}

// 嘗試把一條折線還原成圓／弧。成功回傳 true 並填 outPrim。
bool tryRecoverArc(const Primitive& pl, double quantum, const Options& opt,
                   Primitive& outPrim)
{
    const std::vector<Pt>& P = pl.pts;
    const size_t n = P.size();
    if (n < size_t(opt.minArcVerts)) return false;
    if (pl.closed && n < size_t(opt.minCircleVerts)) return false;

    const CircleFit f = fitCircle(P);
    if (!f.ok) return false;

    const double tol = std::max(0.0025 * f.r, 1.5 * quantum);
    if (f.r < 3.0 * quantum) return false;

    for (const Pt& p : P)
        if (std::fabs(dist(p, f.c) - f.r) > tol) return false;

    // 角度必須單調、步距不能太大
    const double maxStep = opt.maxStepDeg * kPi / 180.0;
    std::vector<double> ang(n);
    for (size_t i = 0; i < n; ++i) ang[i] = std::atan2(P[i].y - f.c.y, P[i].x - f.c.x);

    auto wrapDelta = [](double d) {
        while (d >  kPi) d -= kTwoPi;
        while (d <= -kPi) d += kTwoPi;
        return d;
    };
    int sign = 0;
    double total = 0.0;
    std::vector<double> cum(n, 0.0);
    const size_t steps = pl.closed ? n : n - 1;
    for (size_t i = 0; i < steps; ++i) {
        const double d = wrapDelta(ang[(i + 1) % n] - ang[i]);
        if (std::fabs(d) > maxStep || std::fabs(d) < 1e-9) return false;
        const int sg = d > 0 ? 1 : -1;
        if (sign == 0) sign = sg; else if (sg != sign) return false;
        total += d;
        if (i + 1 < n) cum[i + 1] = total;
    }

    if (pl.closed) {
        // 必須繞滿一圈
        if (std::fabs(std::fabs(total) - kTwoPi) > 0.5 * maxStep) return false;
        outPrim = Primitive();
        outPrim.kind = PrimKind::Circle;
        outPrim.center = f.c;
        outPrim.r = f.r;
        return true;
    }

    // 開放：弦偏離需明顯大於量化誤差，否則這是直線而非弧
    double sag = 0.0;
    for (const Pt& p : P) sag = std::max(sag, ptLineDist(p, P.front(), P.back()));
    if (sag < 2.5 * quantum) return false;
    if (std::fabs(total) >= kTwoPi - 1e-6) return false;

    // 中點：取擬合圓上、掃角一半處
    const double aMid = ang[0] + total / 2.0;
    outPrim = Primitive();
    outPrim.kind = PrimKind::Arc;
    outPrim.pts = { P.front(),
                    Pt{ f.c.x + f.r * std::cos(aMid), f.c.y + f.r * std::sin(aMid) },
                    P.back() };
    return true;
}

void dpRec(const std::vector<Pt>& P, size_t a, size_t b, double tol,
           std::vector<char>& keep)
{
    if (b <= a + 1) return;
    double dmax = -1.0; size_t idx = a;
    for (size_t i = a + 1; i < b; ++i) {
        const double d = ptLineDist(P[i], P[a], P[b]);
        if (d > dmax) { dmax = d; idx = i; }
    }
    if (dmax > tol) {
        keep[idx] = 1;
        dpRec(P, a, idx, tol, keep);
        dpRec(P, idx, b, tol, keep);
    }
}

void simplifyPolyline(Primitive& pl, double tol)
{
    std::vector<Pt> P = pl.pts;
    if (pl.closed) P.push_back(P.front());
    if (P.size() < 3) return;
    std::vector<char> keep(P.size(), 0);
    keep.front() = keep.back() = 1;

    if (pl.closed) {
        // 封閉折線：固定「第一點」與「離它最遠的點」，兩段各自 DP
        size_t far = 1; double dm = -1.0;
        for (size_t i = 1; i + 1 < P.size(); ++i) {
            const double d = dist(P[i], P[0]);
            if (d > dm) { dm = d; far = i; }
        }
        keep[far] = 1;
        dpRec(P, 0, far, tol, keep);
        dpRec(P, far, P.size() - 1, tol, keep);
    } else {
        dpRec(P, 0, P.size() - 1, tol, keep);
    }
    std::vector<Pt> q;
    for (size_t i = 0; i < P.size(); ++i) if (keep[i]) q.push_back(P[i]);
    if (pl.closed) q.pop_back();
    if (q.size() >= 2 && (!pl.closed || q.size() >= 3)) pl.pts = std::move(q);
}

void bboxOf(const Primitive& pr, double& x0, double& y0, double& x1, double& y1)
{
    auto add = [&](double x, double y) {
        x0 = std::min(x0, x); y0 = std::min(y0, y);
        x1 = std::max(x1, x); y1 = std::max(y1, y);
    };
    switch (pr.kind) {
    case PrimKind::Circle:
        add(pr.center.x - pr.r, pr.center.y - pr.r);
        add(pr.center.x + pr.r, pr.center.y + pr.r);
        break;
    case PrimKind::Ellipse: {
        const double c = std::cos(pr.angleRad), sn = std::sin(pr.angleRad);
        const double hx = std::sqrt(pr.r*pr.r*c*c + pr.r2*pr.r2*sn*sn);
        const double hy = std::sqrt(pr.r*pr.r*sn*sn + pr.r2*pr.r2*c*c);
        add(pr.center.x - hx, pr.center.y - hy);
        add(pr.center.x + hx, pr.center.y + hy);
        break;
    }
    default:
        for (const Pt& p : pr.pts) add(p.x, p.y);
        break;
    }
}

} // namespace

// ════════════════════════════════════════════════════════════════════════
Result parse(const unsigned char* data, std::size_t size, const Options& opt)
{
    Result res;
    if (!data || size < 88) { res.error = "EMF data too short"; return res; }

    Rec hdr{ data, uint32_t(std::min<std::size_t>(size, 0xFFFFFFFFu)) };
    if (hdr.u32(0) != EMR_HEADER || hdr.u32(40) != 0x464D4520u) {
        res.error = "Not an EMF (signature mismatch)";
        return res;
    }

    Ctx c;
    // 裝置尺寸 ↔ 毫米（EMR_HEADER: szlDevice @72, szlMillimeters @80）
    {
        const double devX = hdr.i32(72), devY = hdr.i32(76);
        const double mmX  = hdr.i32(80), mmY  = hdr.i32(84);
        if (devX > 0 && devY > 0 && mmX > 0 && mmY > 0) {
            c.mmPerPxX = mmX / devX;  c.mmPerPxY = mmY / devY;
        } else {
            // 退而求其次：rclFrame (0.01 mm) / rclBounds (px)
            const double bw = double(hdr.i32(16)) - hdr.i32(8) + 1.0;
            const double bh = double(hdr.i32(20)) - hdr.i32(12) + 1.0;
            const double fw = (double(hdr.i32(32)) - hdr.i32(24)) / 100.0;
            const double fh = (double(hdr.i32(36)) - hdr.i32(28)) / 100.0;
            if (bw > 0 && bh > 0 && fw > 0 && fh > 0) {
                c.mmPerPxX = fw / bw; c.mmPerPxY = fh / bh;
            }
        }
        c.pxPerMmX = 1.0 / c.mmPerPxX;
        c.pxPerMmY = 1.0 / c.mmPerPxY;
    }
    res.quantumMm = std::max(c.mmPerPxX, c.mmPerPxY);

    std::vector<Primitive> out;
    std::size_t off = 0;
    bool sawEof = false;

    while (off + 8 <= size) {
        Rec r{ data + off, 0 };
        const uint32_t type = r.u32(0);
        const uint32_t rsz  = r.u32(4);
        if (rsz < 8 || (rsz & 3u) != 0 || off + rsz > size) break;   // 截斷／損毀
        r.size = rsz;
        ++res.recordCount;

        switch (type) {
        // ── 狀態 ───────────────────────────────────────────────────────
        case EMR_SETMAPMODE:
            if (r.has(8, 4)) c.g.mapMode = int(r.u32(8));
            break;
        case EMR_SETWINDOWEXTEX:
            if (r.has(8, 8)) { c.g.winExtX = r.i32(8); c.g.winExtY = r.i32(12); }
            break;
        case EMR_SETWINDOWORGEX:
            if (r.has(8, 8)) { c.g.winOrgX = r.i32(8); c.g.winOrgY = r.i32(12); }
            break;
        case EMR_SETVIEWPORTEXTEX:
            if (r.has(8, 8)) { c.g.vpExtX = r.i32(8); c.g.vpExtY = r.i32(12); }
            break;
        case EMR_SETVIEWPORTORGEX:
            if (r.has(8, 8)) { c.g.vpOrgX = r.i32(8); c.g.vpOrgY = r.i32(12); }
            break;
        case EMR_SCALEVIEWPORTEXTEX:
        case EMR_SCALEWINDOWEXTEX:
            if (r.has(8, 16)) {
                const double xn = r.i32(8),  xd = r.i32(12);
                const double yn = r.i32(16), yd = r.i32(20);
                double& ex = (type == EMR_SCALEVIEWPORTEXTEX) ? c.g.vpExtX : c.g.winExtX;
                double& ey = (type == EMR_SCALEVIEWPORTEXTEX) ? c.g.vpExtY : c.g.winExtY;
                if (xd != 0) ex = ex * xn / xd;
                if (yd != 0) ey = ey * yn / yd;
            }
            break;
        case EMR_SETWORLDTRANSFORM:
            if (r.has(8, 24)) {
                c.g.world.m11 = r.f32(8);  c.g.world.m12 = r.f32(12);
                c.g.world.m21 = r.f32(16); c.g.world.m22 = r.f32(20);
                c.g.world.dx  = r.f32(24); c.g.world.dy  = r.f32(28);
            }
            break;
        case EMR_MODIFYWORLDTRANSFORM:
            if (r.has(8, 28)) {
                Xf x;
                x.m11 = r.f32(8);  x.m12 = r.f32(12);
                x.m21 = r.f32(16); x.m22 = r.f32(20);
                x.dx  = r.f32(24); x.dy  = r.f32(28);
                const uint32_t mode = r.u32(32);
                if (mode == 1)      c.g.world = Xf();                    // MWT_IDENTITY
                else if (mode == 2) c.g.world = mul(x, c.g.world);       // LEFTMULTIPLY
                else if (mode == 3) c.g.world = mul(c.g.world, x);       // RIGHTMULTIPLY
                else if (mode == 4) c.g.world = x;                       // MWT_SET
            }
            break;
        case EMR_SAVEDC:
            c.stack.push_back(c.g);
            break;
        case EMR_RESTOREDC: {
            int rel = r.has(8, 4) ? r.i32(8) : -1;
            if (rel < 0) {
                int n = -rel;
                while (n-- > 0 && !c.stack.empty()) { c.g = c.stack.back(); c.stack.pop_back(); }
            }
            break;
        }
        case EMR_SETARCDIRECTION:
            if (r.has(8, 4)) c.g.arcDir = int(r.u32(8));
            break;

        // ── 物件（只追蹤「筆是否為 NULL」，用來略過純填色圖形）──────────
        case EMR_CREATEPEN:
            if (r.has(8, 8)) {
                const uint32_t ih = r.u32(8);
                if (ih < 100000) {
                    if (c.objs.size() <= ih) c.objs.resize(ih + 1);
                    c.objs[ih] = Obj{ true, (r.u32(12) & 0xFu) == 5u };
                }
            }
            break;
        case EMR_EXTCREATEPEN:
            if (r.has(8, 24)) {
                const uint32_t ih = r.u32(8);
                // EMREXTCREATEPEN: ihPen, offBmi, cbBmi, offBits, cbBits, elp.elpPenStyle
                if (ih < 100000 && r.has(28, 4)) {
                    if (c.objs.size() <= ih) c.objs.resize(ih + 1);
                    c.objs[ih] = Obj{ true, (r.u32(28) & 0xFu) == 5u };
                }
            }
            break;
        case EMR_CREATEBRUSHINDIRECT:
            if (r.has(8, 4)) {
                const uint32_t ih = r.u32(8);
                if (ih < 100000) {
                    if (c.objs.size() <= ih) c.objs.resize(ih + 1);
                    c.objs[ih] = Obj{ false, false };
                }
            }
            break;
        case EMR_DELETEOBJECT:
            if (r.has(8, 4)) {
                const uint32_t ih = r.u32(8);
                if (ih < c.objs.size()) c.objs[ih] = Obj{};
            }
            break;
        case EMR_SELECTOBJECT:
            if (r.has(8, 4)) {
                const uint32_t h = r.u32(8);
                if (h & 0x80000000u) {              // stock object
                    const uint32_t idx = h & 0x7FFFFFFFu;
                    if (idx == 8u) c.g.penNull = true;                 // NULL_PEN
                    else if (idx == 6u || idx == 7u) c.g.penNull = false;  // WHITE/BLACK_PEN
                } else if (h < c.objs.size() && c.objs[h].isPen) {
                    c.g.penNull = c.objs[h].null;
                }
            }
            break;

        // ── 目前位置／線 ───────────────────────────────────────────────
        case EMR_MOVETOEX:
            if (r.has(8, 8)) {
                flushFig(c, out);
                c.g.curX = r.i32(8); c.g.curY = r.i32(12);
            }
            break;
        case EMR_LINETO:
            if (r.has(8, 8)) {
                if (c.fig.empty()) c.fig.push_back(mapPt(c, c.g.curX, c.g.curY));
                c.g.curX = r.i32(8); c.g.curY = r.i32(12);
                c.fig.push_back(mapPt(c, c.g.curX, c.g.curY));
            }
            break;
        case EMR_CLOSEFIGURE:
            c.figClosed = true;
            flushFig(c, out);
            break;

        // ── 多點記錄 ───────────────────────────────────────────────────
        case EMR_POLYLINE: case EMR_POLYLINE16:
        case EMR_POLYGON:  case EMR_POLYGON16: {
            flushFig(c, out);
            const bool is16 = (type == EMR_POLYLINE16 || type == EMR_POLYGON16);
            const bool poly = (type == EMR_POLYGON   || type == EMR_POLYGON16);
            if (!r.has(8, 20)) break;
            const uint32_t n = r.u32(24);
            std::vector<Pt> pts;
            if (!readPoints(c, r, 28, n, is16, pts)) break;
            if (poly && c.g.penNull && !c.inPath) { ++res.filledSkipped; break; }
            pushPolyline(dst(c, out), pts, poly);
            break;
        }
        case EMR_POLYPOLYLINE: case EMR_POLYPOLYLINE16:
        case EMR_POLYPOLYGON:  case EMR_POLYPOLYGON16: {
            flushFig(c, out);
            const bool is16 = (type == EMR_POLYPOLYLINE16 || type == EMR_POLYPOLYGON16);
            const bool poly = (type == EMR_POLYPOLYGON    || type == EMR_POLYPOLYGON16);
            if (!r.has(8, 24)) break;
            const uint32_t np = r.u32(24), cptl = r.u32(28);
            if (np > 10000000u || !r.has(32, np * 4)) break;
            if (poly && c.g.penNull && !c.inPath) { ++res.filledSkipped; break; }
            std::vector<Pt> all;
            if (!readPoints(c, r, 32 + np * 4, cptl, is16, all)) break;
            size_t pos = 0;
            for (uint32_t i = 0; i < np; ++i) {
                const uint32_t cnt = r.u32(32 + i * 4);
                if (pos + cnt > all.size()) break;
                std::vector<Pt> sub(all.begin() + long(pos), all.begin() + long(pos + cnt));
                pushPolyline(dst(c, out), sub, poly);
                pos += cnt;
            }
            break;
        }
        case EMR_POLYLINETO: case EMR_POLYLINETO16: {
            const bool is16 = (type == EMR_POLYLINETO16);
            if (!r.has(8, 20)) break;
            const uint32_t n = r.u32(24);
            std::vector<Pt> pts;
            if (!readPoints(c, r, 28, n, is16, pts) || pts.empty()) break;
            if (c.fig.empty()) c.fig.push_back(mapPt(c, c.g.curX, c.g.curY));
            c.fig.insert(c.fig.end(), pts.begin(), pts.end());
            // 目前位置（邏輯座標）更新為最後一點
            const uint32_t lastOff = 28 + (n - 1) * (is16 ? 4u : 8u);
            c.g.curX = is16 ? r.i16(lastOff) : r.i32(lastOff);
            c.g.curY = is16 ? r.i16(lastOff + 2) : r.i32(lastOff + 4);
            break;
        }
        case EMR_POLYBEZIER: case EMR_POLYBEZIER16:
        case EMR_POLYBEZIERTO: case EMR_POLYBEZIERTO16: {
            const bool is16 = (type == EMR_POLYBEZIER16 || type == EMR_POLYBEZIERTO16);
            const bool to   = (type == EMR_POLYBEZIERTO || type == EMR_POLYBEZIERTO16);
            if (!r.has(8, 20)) break;
            const uint32_t n = r.u32(24);
            std::vector<Pt> pts;
            if (!readPoints(c, r, 28, n, is16, pts) || pts.empty()) break;
            std::vector<Pt> chain;
            size_t i = 0;
            if (to) chain.push_back(mapPt(c, c.g.curX, c.g.curY));
            else    { chain.push_back(pts[0]); i = 1; }
            Pt p0 = chain.back();
            for (; i + 2 < pts.size(); i += 3) {
                flattenCubic(p0, pts[i], pts[i + 1], pts[i + 2], opt.bezierSegments, chain);
                p0 = pts[i + 2];
            }
            if (to) {
                if (c.fig.empty()) c.fig.push_back(chain.front());
                c.fig.insert(c.fig.end(), chain.begin() + 1, chain.end());
                const uint32_t lastOff = 28 + (n - 1) * (is16 ? 4u : 8u);
                c.g.curX = is16 ? r.i16(lastOff) : r.i32(lastOff);
                c.g.curY = is16 ? r.i16(lastOff + 2) : r.i32(lastOff + 4);
            } else {
                flushFig(c, out);
                pushPolyline(dst(c, out), chain, false);
            }
            break;
        }

        // ── 矩形／橢圓／弧 ─────────────────────────────────────────────
        case EMR_RECTANGLE:
            if (r.has(8, 16)) {
                flushFig(c, out);
                if (c.g.penNull && !c.inPath) { ++res.filledSkipped; break; }
                const double l = r.i32(8), t = r.i32(12), rr = r.i32(16), b = r.i32(20);
                std::vector<Pt> q = { mapPt(c, l, t), mapPt(c, rr, t),
                                      mapPt(c, rr, b), mapPt(c, l, b) };
                pushPolyline(dst(c, out), q, true);
            }
            break;
        case EMR_ELLIPSE:
            if (r.has(8, 16)) {
                flushFig(c, out);
                if (c.g.penNull && !c.inPath) { ++res.filledSkipped; break; }
                const Pt a = mapPt(c, r.i32(8), r.i32(12));
                const Pt b = mapPt(c, r.i32(16), r.i32(20));
                emitArcLike(c, out, a, b, nullptr, nullptr, true);
            }
            break;
        case EMR_ARC: case EMR_ARCTO:
            if (r.has(8, 32)) {
                flushFig(c, out);
                const Pt a  = mapPt(c, r.i32(8),  r.i32(12));
                const Pt b  = mapPt(c, r.i32(16), r.i32(20));
                const Pt s  = mapPt(c, r.i32(24), r.i32(28));
                const Pt e  = mapPt(c, r.i32(32), r.i32(36));
                emitArcLike(c, out, a, b, &s, &e, false);
                if (type == EMR_ARCTO) { c.g.curX = r.i32(32); c.g.curY = r.i32(36); }
            }
            break;
        case EMR_ANGLEARC:
            if (r.has(8, 20)) {
                flushFig(c, out);
                const double cx = r.i32(8), cy = r.i32(12), rad = r.u32(16);
                const double a0 = double(r.f32(20)) * kPi / 180.0;
                const double sw = double(r.f32(24)) * kPi / 180.0;
                const Pt ctr = mapPt(c, cx, cy);
                const Pt rim = mapPt(c, cx + rad, cy);
                const double R = dist(ctr, rim);
                if (R > 1e-9 && std::fabs(sw) > 1e-9) {
                    Primitive pr;
                    if (std::fabs(sw) >= kTwoPi - 1e-9) {
                        pr.kind = PrimKind::Circle; pr.center = ctr; pr.r = R;
                    } else {
                        auto at = [&](double a) { return Pt{ ctr.x + R*std::cos(a), ctr.y + R*std::sin(a) }; };
                        pr.kind = PrimKind::Arc;
                        pr.pts = { at(a0), at(a0 + sw / 2.0), at(a0 + sw) };
                    }
                    dst(c, out).push_back(std::move(pr));
                }
                c.g.curX = cx + rad * std::cos(a0 + sw);
                c.g.curY = cy + rad * std::sin(a0 + sw);
            }
            break;
        case EMR_CHORD: case EMR_PIE:
            // 少見；視為純填色，略過
            flushFig(c, out);
            ++res.filledSkipped;
            break;

        // ── 路徑 ───────────────────────────────────────────────────────
        case EMR_BEGINPATH:
            flushFig(c, out);
            c.inPath = true;
            c.pathPrims.clear();
            break;
        case EMR_ENDPATH:
            flushFig(c, out);
            c.inPath = false;
            break;
        case EMR_STROKEPATH: case EMR_STROKEANDFILLPATH:
            flushFig(c, out);
            if (!c.g.penNull)
                for (Primitive& p : c.pathPrims) out.push_back(std::move(p));
            c.pathPrims.clear();
            break;
        case EMR_FILLPATH:
            flushFig(c, out);
            if (!c.pathPrims.empty()) ++res.filledSkipped;
            c.pathPrims.clear();
            break;

        // ── 文字／點陣圖（無法轉成幾何）───────────────────────────────
        case EMR_EXTTEXTOUTA: case EMR_EXTTEXTOUTW: case EMR_SMALLTEXTOUT:
            ++res.textRecords;
            break;
        case EMR_BITBLT: case EMR_STRETCHBLT: case EMR_MASKBLT: case EMR_PLGBLT:
        case EMR_SETDIBITSTODEVICE: case EMR_STRETCHDIBITS:
        case EMR_ALPHABLEND: case EMR_TRANSPARENTBLT:
            ++res.bitmapRecords;
            break;

        case EMR_EOF:
            sawEof = true;
            break;
        default:
            break;
        }
        if (sawEof) break;
        off += rsz;
    }
    flushFig(c, out);   // 收尾：把最後一段 MoveTo/LineTo 輸出

    // ── 後處理 ──────────────────────────────────────────────────────────
    const double quantum = res.quantumMm;
    for (Primitive& pr : out) {
        if (pr.kind != PrimKind::Polyline) continue;
        if (opt.recoverArcs) {
            Primitive rec;
            if (tryRecoverArc(pr, quantum, opt, rec)) {
                if (rec.kind == PrimKind::Circle) ++res.recoveredCircles;
                else                              ++res.recoveredArcs;
                pr = std::move(rec);
                continue;
            }
        }
        if (opt.simplify) simplifyPolyline(pr, 0.6 * quantum);
    }

    if (out.empty()) {
        res.error = "No vector geometry found in the EMF";
        return res;
    }

    double x0 = std::numeric_limits<double>::max(),  y0 = x0;
    double x1 = std::numeric_limits<double>::lowest(), y1 = x1;
    for (const Primitive& pr : out) bboxOf(pr, x0, y0, x1, y1);
    res.minX = x0; res.minY = y0; res.maxX = x1; res.maxY = y1;
    res.prims = std::move(out);
    res.ok = true;
    return res;
}


// ════════════════════════════════════════════════════════════════════════
// 比例尺標記
// ════════════════════════════════════════════════════════════════════════
namespace {

void recomputeBBox(Result& r)
{
    double x0 = std::numeric_limits<double>::max(),  y0 = x0;
    double x1 = std::numeric_limits<double>::lowest(), y1 = x1;
    for (const Primitive& pr : r.prims) bboxOf(pr, x0, y0, x1, y1);
    if (!r.prims.empty()) { r.minX = x0; r.minY = y0; r.maxX = x1; r.maxY = y1; }
}

// 圖元 idx 以外，是否有任何頂點／端點落在 p 的 tol 範圍內
bool touchesOthers(const Result& r, const std::vector<size_t>& skip, const Pt& p, double tol)
{
    for (size_t i = 0; i < r.prims.size(); ++i) {
        if (std::find(skip.begin(), skip.end(), i) != skip.end()) continue;
        const Primitive& pr = r.prims[i];
        if (pr.kind == PrimKind::Circle || pr.kind == PrimKind::Ellipse) {
            if (std::fabs(dist(p, pr.center) - pr.r) <= tol) return true;
            continue;
        }
        for (const Pt& q : pr.pts) if (dist(p, q) <= tol) return true;
    }
    return false;
}

} // namespace

ScaleMarker extractScaleMarker(Result& r)
{
    ScaleMarker best;
    if (!r.ok || r.prims.size() < 3) return best;   // 至少 標記 2 條 + 其他圖形

    const double q   = std::max(r.quantumMm, 1e-9);
    const double tol = 2.0 * q;
    const double W = r.maxX - r.minX, H = r.maxY - r.minY;
    const double cornerTol = 0.05 * std::max(W, H);

    struct Cand { Pt corner; double lh, lv; Pt farH, farV; std::vector<size_t> idx; };
    std::vector<Cand> cands;

    struct Seg { size_t idx; Pt lo, hi; };   // lo = 左/下端，hi = 右/上端
    std::vector<Seg> hs, vs;

    for (size_t i = 0; i < r.prims.size(); ++i) {
        const Primitive& pr = r.prims[i];
        if (pr.kind != PrimKind::Polyline || pr.closed) continue;

        if (pr.pts.size() == 2) {
            Pt a = pr.pts[0], b = pr.pts[1];
            if (std::fabs(a.y - b.y) <= tol && std::fabs(a.x - b.x) > 4 * q) {
                if (a.x > b.x) std::swap(a, b);
                hs.push_back({ i, a, b });
            } else if (std::fabs(a.x - b.x) <= tol && std::fabs(a.y - b.y) > 4 * q) {
                if (a.y > b.y) std::swap(a, b);
                vs.push_back({ i, a, b });
            }
        } else if (pr.pts.size() == 3) {          // 一條畫成 L 形的 3 點折線
            const Pt c0 = pr.pts[1];
            const Pt a = pr.pts[0], b = pr.pts[2];
            for (int swp = 0; swp < 2; ++swp) {
                const Pt h = swp ? b : a, v = swp ? a : b;   // 嘗試兩種臂的分配
                if (std::fabs(h.y - c0.y) <= tol && h.x - c0.x > 4 * q &&
                    std::fabs(v.x - c0.x) <= tol && v.y - c0.y > 4 * q) {
                    Cand c;
                    c.corner = c0; c.lh = h.x - c0.x; c.lv = v.y - c0.y;
                    c.farH = h; c.farV = v; c.idx = { i };
                    cands.push_back(c);
                }
            }
        }
    }
    for (const Seg& h : hs)
        for (const Seg& v : vs) {
            if (dist(h.lo, v.lo) > tol) continue;           // 角點需重合（左端＝下端）
            Cand c;
            c.corner = Pt{ (h.lo.x + v.lo.x) / 2, (h.lo.y + v.lo.y) / 2 };
            c.lh = h.hi.x - h.lo.x; c.lv = v.hi.y - v.lo.y;
            c.farH = h.hi; c.farV = v.hi; c.idx = { h.idx, v.idx };
            cands.push_back(c);
        }

    double bestScore = std::numeric_limits<double>::max();
    const Cand* chosen = nullptr;
    for (const Cand& c : cands) {
        const double L = (c.lh + c.lv) / 2.0;
        if (std::fabs(c.lh - c.lv) > 2.0 * q + 0.02 * L) continue;                 // 兩臂等長
        if (touchesOthers(r, c.idx, c.farH, tol) || touchesOthers(r, c.idx, c.farV, tol)) continue;
        const double score = (c.corner.x - r.minX) + (c.corner.y - r.minY);        // 越靠左下越好
        if (std::fabs(c.corner.x - r.minX) > cornerTol ||
            std::fabs(c.corner.y - r.minY) > cornerTol) continue;
        if (score < bestScore) { bestScore = score; chosen = &c; }
    }
    if (!chosen) return best;

    best.found    = true;
    best.corner   = chosen->corner;
    best.lengthMm = (chosen->lh + chosen->lv) / 2.0;
    best.pixels   = best.lengthMm / q;

    std::vector<size_t> rm = chosen->idx;
    std::sort(rm.rbegin(), rm.rend());
    for (size_t i : rm) r.prims.erase(r.prims.begin() + long(i));
    recomputeBBox(r);
    return best;
}

} // namespace emf
} // namespace core
} // namespace aicad
