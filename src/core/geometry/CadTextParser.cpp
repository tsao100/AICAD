/**
 * @file CadTextParser.cpp
 * @brief 見 CadTextParser.h 檔頭說明。
 */
#include "CadTextParser.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <vector>

namespace aicad {
namespace core {
namespace cadtext {

using emf::Pt;
using emf::Primitive;
using emf::PrimKind;

namespace {

constexpr double kPi = 3.14159265358979323846;

bool readNums(std::istringstream& is, std::vector<double>& out, size_t n)
{
    out.clear();
    for (size_t i = 0; i < n; ++i) {
        double v;
        if (!(is >> v) || !std::isfinite(v)) return false;
        out.push_back(v);
    }
    return true;
}

double dist(const Pt& a, const Pt& b) { return std::hypot(a.x - b.x, a.y - b.y); }

void pushPoly(std::vector<Primitive>& out, std::vector<Pt> pts, bool closed)
{
    std::vector<Pt> q;
    for (const Pt& p : pts)
        if (q.empty() || dist(q.back(), p) > 1e-12) q.push_back(p);
    if (closed && q.size() > 1 && dist(q.front(), q.back()) <= 1e-12) q.pop_back();
    if (q.size() < 2) return;
    if (closed && q.size() < 3) closed = false;
    Primitive pr;
    pr.kind = PrimKind::Polyline;
    pr.pts = std::move(q);
    pr.closed = closed;
    out.push_back(std::move(pr));
}

// 以 bulge 描述的弧：p0→p1，bulge>0 為逆時針。弧上中點在弦的右側（b>0）。
Primitive bulgeArc(const Pt& p0, const Pt& p1, double b)
{
    const double dx = p1.x - p0.x, dy = p1.y - p0.y;
    const double L = std::hypot(dx, dy);
    const double nlx = -dy / L, nly = dx / L;          // 左法向
    const double s = b * L / 2.0;                       // 弧高（帶號）
    Primitive pr;
    pr.kind = PrimKind::Arc;
    pr.pts = { p0, Pt{ (p0.x + p1.x) / 2.0 - nlx * s, (p0.y + p1.y) / 2.0 - nly * s }, p1 };
    return pr;
}

void bboxOf(const Primitive& pr, double& x0, double& y0, double& x1, double& y1)
{
    auto add = [&](double x, double y) {
        x0 = std::min(x0, x); y0 = std::min(y0, y);
        x1 = std::max(x1, x); y1 = std::max(y1, y);
    };
    if (pr.kind == PrimKind::Circle) {
        add(pr.center.x - pr.r, pr.center.y - pr.r);
        add(pr.center.x + pr.r, pr.center.y + pr.r);
    } else if (pr.kind == PrimKind::Ellipse) {
        const double c = std::cos(pr.angleRad), sn = std::sin(pr.angleRad);
        const double hx = std::sqrt(pr.r*pr.r*c*c + pr.r2*pr.r2*sn*sn);
        const double hy = std::sqrt(pr.r*pr.r*sn*sn + pr.r2*pr.r2*c*c);
        add(pr.center.x - hx, pr.center.y - hy);
        add(pr.center.x + hx, pr.center.y + hy);
    } else if (pr.kind == PrimKind::Arc) {
        // 精確外框：以三點求圓，再檢查象限點是否落在弧內
        const Pt &a = pr.pts[0], &m = pr.pts[1], &c = pr.pts[2];
        add(a.x, a.y); add(c.x, c.y);
        const double d = 2.0 * (a.x * (m.y - c.y) + m.x * (c.y - a.y) + c.x * (a.y - m.y));
        if (std::fabs(d) < 1e-18) { add(m.x, m.y); return; }
        const double a2 = a.x*a.x + a.y*a.y, m2 = m.x*m.x + m.y*m.y, c2 = c.x*c.x + c.y*c.y;
        const double ux = (a2*(m.y - c.y) + m2*(c.y - a.y) + c2*(a.y - m.y)) / d;
        const double uy = (a2*(c.x - m.x) + m2*(a.x - c.x) + c2*(m.x - a.x)) / d;
        const double r = std::hypot(a.x - ux, a.y - uy);
        const double aa = std::atan2(a.y - uy, a.x - ux);
        const double am = std::atan2(m.y - uy, m.x - ux);
        const double ac = std::atan2(c.y - uy, c.x - ux);
        auto norm = [](double v) { v = std::fmod(v, 2*kPi); return v < 0 ? v + 2*kPi : v; };
        const bool ccw = norm(am - aa) < norm(ac - aa);       // 中點在 start→end 的 CCW 範圍內
        for (int q = 0; q < 4; ++q) {
            const double t = q * kPi / 2.0;
            const bool in = ccw ? norm(t - aa) <= norm(ac - aa) : norm(aa - t) <= norm(aa - ac);
            if (in) add(ux + r * std::cos(t), uy + r * std::sin(t));
        }
    } else {
        for (const Pt& p : pr.pts) add(p.x, p.y);
    }
}

} // namespace

bool looksLikeCadText(const std::string& text)
{
    size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
    return text.compare(i, 10, "AICAD-CAD1") == 0;
}

emf::Result parse(const std::string& text)
{
    emf::Result res;
    if (!looksLikeCadText(text)) { res.error = "Not AICAD-CAD1 text"; return res; }

    std::vector<Primitive> out;
    std::istringstream all(text);
    std::string line;
    bool first = true;
    while (std::getline(all, line)) {
        std::istringstream is(line);
        std::string tag;
        if (!(is >> tag)) continue;
        if (first) { first = false; continue; }           // 標頭行
        ++res.recordCount;
        std::vector<double> v;

        if (tag == "L") {
            if (!readNums(is, v, 4)) { ++res.unknownRecords; continue; }
            pushPoly(out, { {v[0], v[1]}, {v[2], v[3]} }, false);
        } else if (tag == "C") {
            if (!readNums(is, v, 3) || !(v[2] > 0)) { ++res.unknownRecords; continue; }
            Primitive pr; pr.kind = PrimKind::Circle; pr.center = {v[0], v[1]}; pr.r = v[2];
            out.push_back(std::move(pr));
        } else if (tag == "A") {
            if (!readNums(is, v, 6)) { ++res.unknownRecords; continue; }
            Primitive pr; pr.kind = PrimKind::Arc;
            pr.pts = { {v[0], v[1]}, {v[2], v[3]}, {v[4], v[5]} };
            out.push_back(std::move(pr));
        } else if (tag == "P") {
            double closedD, nD;
            if (!(is >> closedD >> nD) || nD < 2 || nD > 10000000) { ++res.unknownRecords; continue; }
            const size_t n = size_t(nD);
            std::vector<Pt> vt; vt.resize(n); std::vector<double> bu; bu.resize(n);
            bool ok = true;
            for (size_t i = 0; i < n && ok; ++i)
                ok = bool(is >> vt[i].x >> vt[i].y >> bu[i]);
            if (!ok) { ++res.unknownRecords; continue; }
            const bool closed = closedD != 0.0;
            const size_t nseg = closed ? n : n - 1;
            bool allStraight = true;
            for (size_t i = 0; i < nseg; ++i) if (std::fabs(bu[i]) > 1e-9) allStraight = false;
            if (allStraight) { pushPoly(out, vt, closed); continue; }
            std::vector<Pt> run;
            for (size_t i = 0; i < nseg; ++i) {
                const Pt& p0 = vt[i]; const Pt& p1 = vt[(i + 1) % n];
                if (std::fabs(bu[i]) <= 1e-9 || dist(p0, p1) < 1e-12) {
                    if (run.empty()) run.push_back(p0);
                    run.push_back(p1);
                } else {
                    pushPoly(out, run, false); run.clear();
                    out.push_back(bulgeArc(p0, p1, bu[i]));
                }
            }
            pushPoly(out, run, false);
        } else if (tag == "E") {
            if (!readNums(is, v, 7) || !(v[4] > 0)) { ++res.unknownRecords; continue; }
            const double ax = v[2], ay = v[3], ratio = v[4], t0 = v[5], t1 = v[6];
            const double major = std::hypot(ax, ay);
            if (major <= 0) { ++res.unknownRecords; continue; }
            double sweep = std::fmod(t1 - t0, 2 * kPi); if (sweep <= 1e-9) sweep += 2 * kPi;
            if (std::fabs(sweep - 2 * kPi) < 1e-6 || std::fabs(t1 - t0) >= 2 * kPi - 1e-6) {
                Primitive pr; pr.center = {v[0], v[1]};
                if (std::fabs(ratio - 1.0) < 1e-9) { pr.kind = PrimKind::Circle; pr.r = major; }
                else { pr.kind = PrimKind::Ellipse; pr.r = major; pr.r2 = major * ratio; pr.angleRad = std::atan2(ay, ax); }
                out.push_back(std::move(pr));
            } else {                                        // 橢圓弧 → 約每 5° 取樣
                const double bx = -ay * ratio, by = ax * ratio;    // 短軸向量
                const int N = std::max(8, int(std::ceil(sweep / (5.0 * kPi / 180.0))));
                std::vector<Pt> pts;
                for (int i = 0; i <= N; ++i) {
                    const double t = t0 + sweep * i / N;
                    pts.push_back({ v[0] + std::cos(t) * ax + std::sin(t) * bx,
                                    v[1] + std::cos(t) * ay + std::sin(t) * by });
                }
                pushPoly(out, pts, false);
            }
        } else if (tag == "S") {
            double closedD, nD;
            if (!(is >> closedD >> nD) || nD < 2 || nD > 10000000) { ++res.unknownRecords; continue; }
            std::vector<Pt> pts; pts.resize(size_t(nD));
            bool ok = true;
            for (size_t i = 0; i < pts.size() && ok; ++i) ok = bool(is >> pts[i].x >> pts[i].y);
            if (!ok) { ++res.unknownRecords; continue; }
            pushPoly(out, pts, closedD != 0.0);
        } else {
            ++res.unknownRecords;
        }
    }

    if (out.empty()) { res.error = "No geometry in the AICAD-CAD1 text"; return res; }

    double x0 = std::numeric_limits<double>::max(), y0 = x0;
    double x1 = std::numeric_limits<double>::lowest(), y1 = x1;
    for (const Primitive& pr : out) bboxOf(pr, x0, y0, x1, y1);
    res.minX = x0; res.minY = y0; res.maxX = x1; res.maxY = y1;
    res.prims = std::move(out);
    res.ok = true;
    return res;
}

} // namespace cadtext
} // namespace core
} // namespace aicad
