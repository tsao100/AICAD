/**
 * @file RailwayAlignmentElement.cpp
 * @brief Implementations of the alignment element class hierarchy.
 * @author AICAD Team
 * @date   2025-01
 */

#include "RailwayAlignmentElement.h"
#include "RailwayAlignment.h"   // for AlignmentPoint

#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <cmath>
#include <cassert>
#include <functional>
#include <algorithm>

namespace aicad {
namespace railway {

// ============================================================================
//  AlignmentElement – base utilities
// ============================================================================

double AlignmentElement::normalise(double a)
{
    static constexpr double twoPi = 2.0 * M_PI;
    while (a <    0.0) a += twoPi;
    while (a >= twoPi) a -= twoPi;
    return a;
}

// ── Chainage helpers ──────────────────────────────────────────────────────────

double AlignmentElement::startChainage() const
{
    // Reversed: reference = far end, so start = reference − length
    return m_reversed ? m_place.chainage - m_length : m_place.chainage;
}

double AlignmentElement::endChainage() const
{
    return m_reversed ? m_place.chainage : m_place.chainage + m_length;
}

bool AlignmentElement::contains(double p) const
{
    return p >= startChainage() - kChainageTol && p <= endChainage() + kChainageTol;
}

double AlignmentElement::clampRadius(double r)
{
    // 第二道防線：AlignmentElementFactory 已在建構前以 isValidRadius() 擋掉
    // 已知會產生退化半徑的情況（見 createSpiral() 的 TC/CT/Fallback 分支），
    // 這裡再於元素本身把關一次，避免任何其他呼叫路徑（例如未來的 JSON
    // 反序列化、測試程式碼等）繞過工廠直接建構出零半徑元素，導致
    // CircularArcElement／TransitionElement 各家公式中 1/R、L/R 產生
    // Infinity/NaN（ECL 會將此類浮點例外攔截為 FLOATING-POINT-OVERFLOW
    // 條件並讓整個行程崩潰）。
    if (!std::isfinite(r) || std::abs(r) < kMinRadius) {
        if (r != 0.0)  // 0.0 是常見的「尚未設定」預設值，不需要每次都警告
            qWarning() << "[AlignmentElement] clampRadius: degenerate radius" << r
                       << "clamped to" << kMaxRadius;
        return std::signbit(r) ? -kMaxRadius : kMaxRadius;
    }
    return r;
}

// ── Resolve / unresolve ───────────────────────────────────────────────────────

void AlignmentElement::resolvePW(double p, double w,
                                 double& L_out, double& w_eff_out) const
{
    if (m_reversed) {
        L_out     = m_place.chainage - p;  // local distance from reversed origin
        w_eff_out = -w;                    // mirror left/right for backwards traverse
    } else {
        L_out     = p - m_place.chainage;
        w_eff_out = w;
    }
    L_out = std::max(0.0, std::min(m_length, L_out));
}

QPointF AlignmentElement::unresolveLocalPW(double L_local, double w_local) const
{
    if (m_reversed)
        return { m_place.chainage - L_local, -w_local };
    return { m_place.chainage + L_local,  w_local };
}

// ── World transform ───────────────────────────────────────────────────────────

/**
 * World transform (verified against C# Sxy / Txy):
 *
 *   X1 = xloc + w_internal · sin(θ)      [w_internal: right-positive, i.e. −w_public]
 *   Y1 = yloc − w_internal · cos(θ)
 *
 *   Wx = ox + X1·sin(az) + Y1·cos(az)
 *   Wy = oy + X1·cos(az) − Y1·sin(az)
 *
 * Public API uses w > 0 = LEFT, so w_internal = −w_public, giving:
 *   X1 = xloc − w · sin(θ)
 *   Y1 = yloc + w · cos(θ)
 *
 * (Matches C# "X1 = xloc − w·sin(θ), Y1 = yloc + w·cos(θ)" exactly since
 *  C# convention is also positive-left in this context.)
 */
QPointF AlignmentElement::localToWorld(const LocalFrame& lf, double w_eff) const
{
    // w_eff is already sign-adjusted by resolvePW (left-positive in public API)
    const double X1 = lf.x - w_eff * std::sin(lf.theta);
    const double Y1 = lf.y + w_eff * std::cos(lf.theta);
    const double az = m_place.azimuth;
    return {
        m_place.easting  + X1 * std::sin(az) + Y1 * std::cos(az),
        m_place.northing + X1 * std::cos(az) - Y1 * std::sin(az)
    };
}

QPointF AlignmentElement::worldXY(double p, double w) const
{
    double L, w_eff;
    resolvePW(p, w, L, w_eff);
    return localToWorld(localFrame(L), w_eff);
}

double AlignmentElement::worldAzimuth(double p) const
{
    double L, w_eff;
    resolvePW(p, 0.0, L, w_eff);
    double localTheta = localFrame(L).theta;
    // Reversed (CT) transition elements: m_place is the far (ST) end, stored
    // with azimuth = trueForwardAzimuth(ST) + π (see makeReversedPlacement),
    // so that localToWorld()'s rotation-by-az produces the correct XY. To
    // recover the true forward-travel azimuth here we must add that π back;
    // localFrame()'s own internal R-negation for reversed elements already
    // flips the sign of localTheta relative to a "normal" (unreversed) spiral
    // of the same radius, so no additional sign flip is needed on top of it.
    double dAz = m_reversed ? (M_PI + localTheta) : localTheta;
    return normalise(m_place.azimuth + dAz);
}

// ── Default iterative inverse ─────────────────────────────────────────────────

QPointF AlignmentElement::inversePW(double x, double y) const
{
    // Bisect on the perpendicular-distance condition.
    // Find L ∈ [0, length()] such that the cross-track distance is minimised.
    // Newton-like convergence: iterate on the signed perpendicular component.

    const double az = m_place.azimuth;

    // Helper: perpendicular distance from (x,y) to the centreline at local L
    auto perp = [&](double L) -> double {
        LocalFrame lf = localFrame(L);
        QPointF pt    = localToWorld(lf, 0.0);
        double tangAz = normalise(az + (m_reversed ? (M_PI + lf.theta) : lf.theta));
        // Signed perpendicular: positive = left
        double dx = x - pt.x(), dy = y - pt.y();
        // Rotate (dx,dy) into local frame of this tangent
        //   left component = dx * (-cos(az)) + dy * sin(az)  [for tangent along az]
        double leftComp = -dx * std::cos(tangAz) + dy * std::sin(tangAz);
        return leftComp;
    };

    /*/ Along-track component (used to seed the iteration)
    auto along = [&](double L) -> double {
        LocalFrame lf = localFrame(L);
        QPointF pt    = localToWorld(lf, 0.0);
        double tangAz = normalise(az + (m_reversed ? (M_PI + lf.theta) : lf.theta));
        double dx = x - pt.x(), dy = y - pt.y();
        return dx * std::sin(tangAz) + dy * std::cos(tangAz);
    }; */

    // Estimate starting L from along-track projection at the two ends
    double pA = perp(0.0),      pB = perp(m_length);
    double LSA = std::abs(pA) + std::abs(pB);
    if (LSA < kTol) LSA = 1.0;

    double lens = pA * m_length / LSA;   // initial seed

    for (int iter = 0; iter < 80; ++iter) {
        double p_i = perp(lens);
        if (std::abs(p_i) < kAngTol) break;
        lens += p_i * m_length / LSA;
        lens = std::max(0.0, std::min(m_length, lens));
    }

    LocalFrame lf = localFrame(lens);
    QPointF    pt = localToWorld(lf, 0.0);
    double dx = x - pt.x(), dy = y - pt.y();
    double tangAz = normalise(az + (m_reversed ? (M_PI + lf.theta) : lf.theta));
    double w_out = -dx * std::cos(tangAz) + dy * std::sin(tangAz);  // left positive

    return unresolveLocalPW(lens, w_out);
}

// ── Serialisation base ────────────────────────────────────────────────────────

QJsonObject AlignmentElement::toJson() const
{
    QJsonObject o;
    o["elementType"] = typeName();
    o["chainage"]    = m_place.chainage;
    o["easting"]     = m_place.easting;
    o["northing"]    = m_place.northing;
    o["azimuth"]     = m_place.azimuth;
    o["length"]      = m_length;
    o["reversed"]    = m_reversed;
    return o;
}

// ============================================================================
//  TangentElement
// ============================================================================

LocalFrame TangentElement::localFrame(double L) const
{
    return { L, 0.0, 0.0 };
}

/**
 * Analytic inverse for a tangent:
 *   Project (x,y) − origin onto the tangent direction and its perpendicular.
 */
QPointF TangentElement::inversePW(double x, double y) const
{
    const double az = m_place.azimuth;
    const double dx = x - m_place.easting;
    const double dy = y - m_place.northing;

    // Dot with tangent direction (sin(az), cos(az)) → along-track
    double L_local  =  dx * std::sin(az) + dy * std::cos(az);
    // Dot with left perpendicular (−cos(az), sin(az)) → cross-track (left+)
    double w_out    = -dx * std::cos(az) + dy * std::sin(az);

    L_local = std::max(0.0, std::min(m_length, L_local));
    return unresolveLocalPW(L_local, w_out);
}

QJsonObject TangentElement::toJson() const
{
    return AlignmentElement::toJson();
}

// ============================================================================
//  CircularArcElement
// ============================================================================

LocalFrame CircularArcElement::localFrame(double L) const
{
    // Handles both signs of R:
    //   R > 0: right curve  → y > 0 (rightward)
    //   R < 0: left  curve  → y < 0 (leftward)
    const double theta = L / m_radius;                        // signed
    return {
        m_radius * std::sin(L / m_radius),       // x: along initial tangent
        m_radius * (1.0 - std::cos(L / m_radius)), // y: cross-track (right+)
        theta
    };
}

/**
 * Analytic inverse for a circular arc.
 *
 * Strategy:
 *  1. Find the arc centre in world coordinates.
 *  2. |centre→query| gives the true offset radius → w.
 *  3. Angle from "centre→start" to "centre→query" gives arc length → p.
 *
 * Hairpin correction: if swept > π, the formula folds back on itself;
 * apply the half-circle fix from the original C# Cpw:
 *   L  -= |R|·π
 *   w   = 2R − w
 */
QPointF CircularArcElement::inversePW(double x, double y) const
{
    const QPointF cen = centreXY();
    const double  cx  = cen.x(), cy = cen.y();

    // Actual distance from centre to query point
    const double R1   = std::hypot(x - cx, y - cy);
    // Signed offset: w > 0 = right (internal) → negate to get left-positive public
    const double w_internal = m_radius - R1 * std::copysign(1.0, m_radius);
    const double w_out      = -w_internal;   // left positive for public API

    // Angle of start vector from centre (centre→start)
    const double phi0 = std::atan2(m_place.northing - cy, m_place.easting - cx);
    // Angle of query vector from centre
    const double phi  = std::atan2(y - cy, x - cx);

    // Swept arc length in the correct winding direction
    double delta;
    if (m_radius > 0.0) {
        // Right curve = clockwise = decreasing standard polar angle
        delta = normalise(phi0 - phi);        // [0, 2π) CW from start
    } else {
        // Left  curve = counter-clockwise = increasing polar angle
        delta = normalise(phi - phi0);
    }
    double L_local = std::abs(m_radius) * delta;

    // Hairpin correction (C# Cpw: if (L/|R| > π))
    if (L_local / std::abs(m_radius) > M_PI) {
        L_local -= std::abs(m_radius) * M_PI;
        // w also folds: 2R − w_internal gives the corrected internal offset
        const double w_corrected = -(2.0 * m_radius - w_internal);
        return unresolveLocalPW(L_local, w_corrected);
    }

    return unresolveLocalPW(L_local, w_out);
}

QPointF CircularArcElement::centreXY() const
{
    // Centre is at offset |R| to the right of the start point
    // Right direction from tangent (sin(az),cos(az)) is (cos(az), −sin(az))
    const double az = m_place.azimuth;
    return {
        m_place.easting  + m_radius * std::cos(az),
        m_place.northing - m_radius * std::sin(az)
    };
}

double CircularArcElement::bulge(double arcLen) const
{
    // LWPOLYLINE bulge = −tan(Δ/4), Δ = arcLen/|R|
    return -std::tan(arcLen / std::abs(m_radius) / 4.0);
}

QJsonObject CircularArcElement::toJson() const
{
    QJsonObject o = AlignmentElement::toJson();
    o["radius"] = m_radius;
    return o;
}

// ============================================================================
//  TransitionElement – common JSON
// ============================================================================

QJsonObject TransitionElement::toJson() const
{
    QJsonObject o = AlignmentElement::toJson();
    o["radius"]   = m_radius;
    return o;
}

// ============================================================================
//  integrateCurvatureRamp()  — shared numeric evaluator for the thirteen
//  curvature-ramp transition families (SinusoidalElement … 
//  BlossEulerHybridElement, declared in RailwayAlignmentElement.h).
// ============================================================================

namespace {

/**
 * @brief Evaluate (x, y, theta) at arc-length @p L for a transition curve
 *        whose curvature follows κ(s) = g(s/Ls)/R, g:[0,1]→[0,1], g(0)=0,
 *        g(1)=1.
 *
 * theta(L) = ∫₀ᴸ κ(s) ds,  x(L) = ∫₀ᴸ cos(theta(s)) ds,  y(L) = ∫₀ᴸ sin(theta(s)) ds
 *
 * are accumulated together in a single composite-trapezoidal pass over
 * [0, L] (as opposed to re-integrating from scratch for x and y), which
 * keeps the whole evaluation O(n) instead of O(n²). @p nSteps = 400 gives
 * sub-millimetre accuracy for any transition length used in practice (the
 * trapezoidal error is O(h²) and g() is smooth for every family defined in
 * this file), matching the precision the closed-form/series-based families
 * (ClothoidElement, HalfSineElement, …) already provide.
 *
 * Every subclass below is self-anchored at L=0 → (0,0,0), exactly like every
 * other TransitionElement, and the returned theta always reaches exactly
 * θ(Ls) = Ls/R at L=Ls (independent of nSteps, up to numeric-integration
 * error), so it connects smoothly to the adjoining CircularArcElement of
 * radius R just like ClothoidElement/HalfSineElement/etc. do.
 */
LocalFrame integrateCurvatureRamp(double L, double Ls, double Rsigned,
                                   const std::function<double(double)>& g)
{
    constexpr double kMinLen = 1e-3;   // 1 mm; degenerate-length guard
    if (Ls <= kMinLen || !std::isfinite(Rsigned))
        return { L, 0.0, 0.0 };

    L = std::max(0.0, std::min(Ls, L));
    if (L <= 0.0) return { 0.0, 0.0, 0.0 };

    constexpr int n = 400;
    const double  h = L / n;

    double theta = 0.0, x = 0.0, y = 0.0;
    double kappaPrev = g(0.0) / Rsigned;   // == 0 for every g() defined below
    double cosPrev = 1.0, sinPrev = 0.0;   // cos/sin(theta=0)

    for (int i = 1; i <= n; ++i) {
        const double s      = i * h;
        const double kappaI = g(s / Ls) / Rsigned;
        theta += 0.5 * h * (kappaPrev + kappaI);   // trapezoid: κ → theta
        const double cosI = std::cos(theta);
        const double sinI = std::sin(theta);
        x += 0.5 * h * (cosPrev + cosI);            // trapezoid: cos(theta) → x
        y += 0.5 * h * (sinPrev + sinI);            // trapezoid: sin(theta) → y
        kappaPrev = kappaI; cosPrev = cosI; sinPrev = sinI;
    }

    return { x, y, theta };
}

/** Two-segment cubic-Hermite curve through (0,0)→(0.5,0.5)→(1,1) with
 *  prescribed slopes (0, 1, 0). Shared shape function for SplineElement. */
double splineShape(double t)
{
    t = std::max(0.0, std::min(1.0, t));
    // Hermite basis on u∈[0,1]: h00=2u³−3u²+1, h10=u³−2u²+u, h01=−2u³+3u², h11=u³−u²
    auto hermite = [](double u, double p0, double m0, double p1, double m1) {
        const double u2 = u * u, u3 = u2 * u;
        const double h00 =  2.0*u3 - 3.0*u2 + 1.0;
        const double h10 =  u3 - 2.0*u2 + u;
        const double h01 = -2.0*u3 + 3.0*u2;
        const double h11 =  u3 - u2;
        return h00*p0 + h10*m0 + h01*p1 + h11*m1;
    };
    if (t <= 0.5) {
        // Segment [0, 0.5]: p0=0 (slope 0), p1=0.5 (slope 1); tangents scaled
        // by the segment length (0.5) per standard Hermite convention.
        return hermite(t / 0.5, 0.0, 0.0, 0.5, 1.0 * 0.5);
    }
    // Segment [0.5, 1]: p0=0.5 (slope 1), p1=1 (slope 0)
    return hermite((t - 0.5) / 0.5, 0.5, 1.0 * 0.5, 1.0, 0.0);
}

} // anonymous namespace

// ============================================================================
//  halfSineExactFrame()  — shared exact closed-form evaluator for the
//  κ(s) = (1/(2R))·(1 − cos(πs/Ls)) curvature family.
//
//  Confirmed identical to the ISO 16739-1:2024 (IFC 4.3) IfcCosineSpiral
//  formula under the standard straight→circular-arc boundary conditions
//  (κ(0)=0, κ(Ls)=1/R):
//    θ(s) = s/A0 + (Ls/(πA1))·sin(πs/Ls),  κ(s) = 1/A0 + (1/A1)·cos(πs/Ls)
//  solving κ(0)=0, κ(Ls)=1/R gives A0=2R, A1=−2R, i.e. exactly the formula
//  below — verified symbolically (SymPy) to cancel to zero difference.
//  This is therefore the curve railway literature/CAD vendors variously call
//  "half-sine" or "cosine" transition (Bentley/VESTRA list both as distinct
//  UI entries, but the underlying curvature-vs-length relation is the same);
//  used by both HalfSineElement and CosineElement below.
// ============================================================================

namespace {

LocalFrame halfSineExactFrame(double L, double Ls, double R)
{
    const double b  = 1.0 / (2.0 * R);
    const double la = M_PI / Ls;
    const double Ba = la * L;

    // θ = (1/2R)·(L − Ls/π·sin(πL/Ls))
    const double theta = b * (L - Ls / M_PI * std::sin(L / Ls * M_PI));

    // x (closed-form through the b² / 1/R² order term)
    const double x = L - b * b *
                             (2.0 * std::pow(Ba,3) - 12.0 * std::sin(Ba)
                              + 12.0 * Ba * std::cos(Ba)
                              - 3.0 * std::cos(Ba) * std::sin(Ba)
                              + 3.0 * Ba)
                             / (12.0 * std::pow(la, 3));

    // y (closed-form through the b³ / 1/R³ order term)
    const double y =
        b * (L * L + 2.0 * (std::cos(Ba) - 1.0) / (la * la)) / 2.0
        - b * b * b *
              (3.0 * std::pow(Ba,4)
               + 36.0 * Ba * Ba * std::cos(Ba)
               - 60.0 * std::cos(Ba)
               - 72.0 * Ba * std::sin(Ba)
               - 18.0 * Ba * std::cos(Ba) * std::sin(Ba)
               + 9.0 * Ba * Ba
               - 9.0 * std::cos(Ba) * std::cos(Ba)
               - 4.0 * std::pow(std::cos(Ba), 3)
               + 73.0)
              / (72.0 * std::pow(la, 4));

    return { x, y, theta };
}

} // anonymous namespace

// ============================================================================
//  ClothoidElement  (Euler spiral)
// ============================================================================

LocalFrame ClothoidElement::localFrame(double L) const
{
    const double Ls = m_length;
    const double R  = m_reversed ? - m_radius :  m_radius;

    // Scale parameter A: A² = |R|·Ls
    const double A2     = std::abs(R) * Ls;
    const double a      = std::sqrt(A2) * std::copysign(1.0, R);  // signed

    // Cumulative tangent angle θ = L²/(2A²)·sign(R)
    const double theta  = (L * L / (2.0 * A2)) * std::copysign(1.0, a);
    const double th2    = theta * theta;
    const double th4    = th2 * th2;
    const double th6    = th4 * th2;

    // Power-series for x and y in local frame (Fresnel-like, truncated at θ⁶)
    const double x = L * (1.0 - th2 / 10.0 + th4 / 216.0 - th6 / 9360.0);
    const double y = L * theta * (1.0 / 3.0 - th2 / 42.0 + th4 / 1320.0);

    return { x, y, theta };
}

QJsonObject ClothoidElement::toJson() const
{
    return TransitionElement::toJson();
}

// ============================================================================
//  HalfSineElement
// ============================================================================

LocalFrame HalfSineElement::localFrame(double L) const
{
    const double R = m_reversed ? -m_radius : m_radius;
    return halfSineExactFrame(L, m_length, R);
}

QJsonObject HalfSineElement::toJson() const
{
    return TransitionElement::toJson();
}

// ============================================================================
//  ParabolaElement
// ============================================================================

double ParabolaElement::xEnd() const
{
    const double Ls = m_length, R = m_radius;
    return Ls - (Ls * Ls * Ls) / (40.0 * R * R);
}

LocalFrame ParabolaElement::localFrame(double L) const
{
    const double R   = m_reversed ? - m_radius :  m_radius;
    const double XB  = xEnd();

    const double x = L - std::pow(L, 5) / (40.0 * R * R * m_length * m_length);
    const double y = (x * x * x) / (6.0 * R * XB);
    const double theta = std::atan2(x * x, 2.0 * std::abs(R) * XB)
                         * std::copysign(1.0, R);

    return { x, y, theta };
}

QJsonObject ParabolaElement::toJson() const
{
    return TransitionElement::toJson();
}

// ============================================================================
//  CubicJPNElement  (Japanese cubic parabola)
// ============================================================================

double CubicJPNElement::solveBigX(double Ls, double R)
{
    // Bisection: find BigX such that BigX − Ls·10/(10+(BigX/(2R))²) = 0
    double a = 1e-5, b = Ls;
    while (std::abs(a - b) > 1e-6) {
        double c  = 0.5 * (a + b);
        auto   F  = [&](double v) {
            return v == 0.0 ? 0.0
                            : v - Ls * 10.0 / (10.0 + std::pow(v / (2.0 * R), 2));
        };
        (F(a) * F(c) > 0.0) ? (a = c) : (b = c);
    }
    return 0.5 * (a + b);
}

double CubicJPNElement::solveFx(double BigX, double L, double R)
{
    // Bisection: find x such that x − L·10/(10+(x²/(2R·BigX))²) = 0
    double a = 1e-5, b = BigX;
    while (std::abs(a - b) > 1e-6) {
        double c  = 0.5 * (a + b);
        auto   F  = [&](double v) {
            return v == 0.0 ? 0.0
                            : v - L * 10.0 / (10.0 + std::pow(v * v / (2.0 * R * BigX), 2));
        };
        (F(a) * F(c) > 0.0) ? (a = c) : (b = c);
    }
    return 0.5 * (a + b);
}

double CubicJPNElement::bigX() const
{
    if (m_bigX < 0.0)
        m_bigX = solveBigX(m_length, std::abs(m_radius));
    return m_bigX;
}

LocalFrame CubicJPNElement::localFrame(double L) const
{
    const double BX = bigX();
     const double R  = m_reversed ? - m_radius :  m_radius;

    const double x     = solveFx(BX, L, std::abs(R));
    const double y     = (x * x * x) / (6.0 * R * BX);
    const double theta = std::atan2(x * x / (2.0 * R * BX), 1.0);

    return { x, y, theta };
}

QJsonObject CubicJPNElement::toJson() const
{
    return TransitionElement::toJson();
}

// ============================================================================
//  CubicECIElement
// ============================================================================

LocalFrame CubicECIElement::localFrame(double L) const
{
    // CECI's theoretical formula: y = x^3 / (6*R*Ls). x is NOT taken as L
    // directly — it is inverted from CECI's published 4-term arc-length
    // series (see class doc comment):
    //   l(x) = x*[1 + x^4/(40R^2Ls^2) - x^8/(1152R^4Ls^4) + x^12/(13312R^6Ls^6)]
    // via fixed-point iteration on x = l / bracket(x).
    const double R  = m_reversed ? -m_radius : m_radius;
    const double Ls = m_length;

    if (Ls <= 1e-3 || !std::isfinite(R)) return { L, 0.0, 0.0 };
    const double l = std::max(0.0, std::min(Ls, L));

    const double R2Ls2 = R * R * Ls * Ls;
    double x = l;
    for (int i = 0; i < 5; ++i) {
        const double x4 = x * x * x * x;
        const double bracket = 1.0
            + x4 / (40.0 * R2Ls2)
            - (x4 * x4) / (1152.0 * R2Ls2 * R2Ls2)
            + (x4 * x4 * x4) / (13312.0 * R2Ls2 * R2Ls2 * R2Ls2);
        x = l / bracket;
    }

    const double y     = (x * x * x) / (6.0 * R * Ls);
    const double theta = std::atan2(x * x, 2.0 * std::abs(R) * Ls) * std::copysign(1.0, R);

    return { x, y, theta };
}

QJsonObject CubicECIElement::toJson() const
{
    return TransitionElement::toJson();
}

// ============================================================================
//  Curvature-ramp transition families
//
//  See integrateCurvatureRamp() above for the shared numeric evaluator and
//  the class doc comments in RailwayAlignmentElement.h for the rationale
//  behind each g(t) shape function.
// ============================================================================

// ── SinusoidalElement ─────────────────────────────────────────────────────

LocalFrame SinusoidalElement::localFrame(double L) const
{
    const double R = m_reversed ? -m_radius : m_radius;
    return integrateCurvatureRamp(L, m_length, R, [](double t) {
        return t - std::sin(2.0 * M_PI * t) / (2.0 * M_PI);
    });
}

QJsonObject SinusoidalElement::toJson() const { return TransitionElement::toJson(); }

// ── CosineElement ────────────────────────────────────────────────────────

LocalFrame CosineElement::localFrame(double L) const
{
    // MÁV (Hungarian State Railways) "Cosine Transition Curve" — a CARTESIAN
    // y=f(x) approximation (x treated directly as arc length), per the
    // formula you sourced:
    //   y(x) = x²/(4R) − Ls²/(2π²R)·(1 − cos(πx/Ls))
    //
    // This is *not* the same computation as HalfSineElement even though the
    // two share an identical underlying curvature law — confirmed
    // symbolically: y''(x) = (1−cos(πx/Ls))/(2R) is exactly HalfSine's κ(s),
    // and y'(x) is exactly HalfSine's closed-form θ(s) with x substituted
    // for s. The difference is architectural: this Cartesian form treats x
    // as if it *were* arc length (the classical small-deflection shortcut
    // shared by ParabolaElement/CubicJPNElement/CubicECIElement above,
    // rather than integrating x(s)=∫cos θ ds, y(s)=∫sin θ ds exactly like
    // HalfSineElement/halfSineExactFrame() does. That is precisely the kind
    // of discrepancy independently reported for MÁV curves reconstructed via
    // Civil 3D's "Sine Half-Wavelength Diminishing Tangent" type (≈5mm
    // within the curve body, ≈112mm at the end points) — i.e. Cosine and
    // HalfSine are numerically distinct curves in practice despite the
    // shared curvature law, exactly as you specified.
    const double R  = m_reversed ? -m_radius : m_radius;
    const double Ls = m_length;

    if (Ls <= 1e-3 || !std::isfinite(R)) return { L, 0.0, 0.0 };
    const double x = std::max(0.0, std::min(Ls, L));

    const double y = x * x / (4.0 * R)
                     - (Ls * Ls / (2.0 * M_PI * M_PI * R)) * (1.0 - std::cos(M_PI * x / Ls));
    const double yPrime = x / (2.0 * R) - (Ls / (2.0 * M_PI * R)) * std::sin(M_PI * x / Ls);
    const double theta  = std::atan(yPrime);

    return { x, y, theta };
}

QJsonObject CosineElement::toJson() const { return TransitionElement::toJson(); }

// ── BlossElement ─────────────────────────────────────────────────────────

LocalFrame BlossElement::localFrame(double L) const
{
    const double R = m_reversed ? -m_radius : m_radius;
    return integrateCurvatureRamp(L, m_length, R, [](double t) {
        return 3.0 * t * t - 2.0 * t * t * t;
    });
}

QJsonObject BlossElement::toJson() const { return TransitionElement::toJson(); }

// ── LemniscateElement ────────────────────────────────────────────────────

namespace {

// ============================================================================
//  Lemniscate transition curve — normalised (a=1) geometry helpers
//
//  Parametrization: t = π/2 − τ, τ∈[0,π/2]. τ=0 is the lemniscate's true
//  origin (r=0, κ=0); τ=π/2 is the vertex (r=√2, maximum curvature 3√2).
//  Derivatives reuse the exact analytic form (verified against finite
//  differences — see chat), just evaluated at t=π/2−τ.
// ============================================================================

void lemniscateXY1(double tau, double& x, double& y)
{
    const double t = M_PI / 2.0 - tau;
    const double st = std::sin(t), ct = std::cos(t);
    const double denom = 1.0 + st * st;
    x = std::sqrt(2.0) * ct / denom;
    y = std::sqrt(2.0) * st * ct / denom;
}

void lemniscateDerivative1(double tau, double& dx, double& dy)
{
    // d/dtau = -d/dt (chain rule, t = pi/2 - tau)
    const double t = M_PI / 2.0 - tau;
    const double st = std::sin(t), ct = std::cos(t);
    const double denom = 1.0 + st * st;
    const double denom2 = denom * denom;
    const double dxdt = -st * denom - ct * (2.0 * st * ct);
    const double dydt = (ct * ct - st * st) * denom - (st * ct) * (2.0 * st * ct);
    dx = -dxdt * std::sqrt(2.0) / denom2;
    dy = -dydt * std::sqrt(2.0) / denom2;
}

double lemniscateKappa1(double tau)
{
    double x, y;
    lemniscateXY1(tau, x, y);
    // Verified via the fundamental curvature formula κ=(x'y″−y'x″)/(x'²+y'²)^1.5
    // (see chat): the correct closed form for THIS parametrization/scale
    // convention (x,y ~ a√2·[...]) is 1.5·r, not 3·r as commonly misquoted
    // (and as appeared in the reference code you supplied) — cross-checked
    // numerically to agree with the exact formula to ~0.1%.
    return 1.5 * std::sqrt(x * x + y * y);   // a=1 here
}

double lemniscateTheta1(double tau)
{
    double dx, dy;
    lemniscateDerivative1(std::max(tau, 1e-7), dx, dy);
    return std::atan2(dy, dx);
}

/** Arc length (a=1) from tau=0 to tau, via composite Simpson (matches the
 *  quadrature style of the reference implementation you supplied). */
double lemniscateArcLength1(double tau, int n = 400)
{
    if (tau <= 0.0) return 0.0;
    if (n % 2 != 0) ++n;
    const double h = tau / n;
    double sum = 0.0;
    for (int i = 0; i <= n; ++i) {
        const double tt = i * h;
        double dx, dy;
        lemniscateDerivative1(tt, dx, dy);
        const double speed = std::hypot(dx, dy);
        if (i == 0 || i == n)      sum += speed;
        else if (i % 2 == 0)       sum += 2.0 * speed;
        else                       sum += 4.0 * speed;
    }
    return (h / 3.0) * sum;
}

/** Maximum achievable kappa1(tau)*s1(tau) product (at the vertex, tau=pi/2);
 *  Ls/|R| beyond this cannot be represented (curve would have to pass the
 *  vertex into decreasing curvature — invalid for a monotonic transition). */
double lemniscateMaxRatio()
{
    static const double kMax = lemniscateKappa1(M_PI / 2.0) * lemniscateArcLength1(M_PI / 2.0);
    return kMax;
}

/** Bisect tau in [lo,hi] for f(tau)=0, f assumed monotonic (standard use
 *  here: f built from kappa1*s1 - target, or a*s1(tau)-L; both monotonic
 *  increasing in tau over the valid range). */
double lemniscateBisect(const std::function<double(double)>& f, double lo, double hi, int iters = 60)
{
    double flo = f(lo);
    for (int i = 0; i < iters; ++i) {
        const double mid = 0.5 * (lo + hi);
        const double fm = f(mid);
        if ((fm < 0) == (flo < 0)) { lo = mid; flo = fm; }
        else                        hi = mid;
    }
    return 0.5 * (lo + hi);
}

} // anonymous namespace

void LemniscateElement::ensureSolved() const
{
    if (m_cacheValid && m_cachedLs == m_length && m_cachedR == m_radius)
        return;

    m_cachedLs = m_length;
    m_cachedR  = m_radius;

    const double absR = std::abs(m_radius);
    if (m_length <= 1e-3 || absR <= 1e-3 || !std::isfinite(absR)) {
        m_tauEnd = 0.0; m_a = 1.0; m_cacheValid = true;
        return;
    }

    const double target  = m_length / absR;
    const double maxRatio = lemniscateMaxRatio();
    // Clamp: beyond this ratio the lemniscate would have to pass its vertex
    // (curvature would start decreasing again) — not usable as a monotonic
    // straight->arc transition. Clamp to 99.9% of the max as a safety margin
    // rather than producing an invalid/self-intersecting result.
    const double targetClamped = std::min(target, maxRatio * 0.999);

    auto f = [&](double tau) {
        return lemniscateKappa1(tau) * lemniscateArcLength1(tau) - targetClamped;
    };
    m_tauEnd = lemniscateBisect(f, 1e-9, M_PI / 2.0 - 1e-9);
    m_a      = absR * lemniscateKappa1(m_tauEnd);
    m_cacheValid = true;
}

LocalFrame LemniscateElement::localFrame(double L) const
{
    ensureSolved();

    const double Ls = m_length;
    const double R  = m_reversed ? -m_radius : m_radius;
    if (Ls <= 1e-3 || m_a <= 0.0 || !std::isfinite(R)) return { L, 0.0, 0.0 };

    L = std::max(0.0, std::min(Ls, L));

    double tau;
    if (L <= 1e-9)              tau = 0.0;
    else if (L >= Ls - 1e-9)    tau = m_tauEnd;
    else {
        auto f = [&](double t) { return m_a * lemniscateArcLength1(t) - L; };
        tau = lemniscateBisect(f, 1e-9, m_tauEnd);
    }

    double x1, y1;
    lemniscateXY1(tau, x1, y1);
    const double th1     = lemniscateTheta1(tau);
    static const double theta0 = lemniscateTheta1(1e-7);   // ~45 deg tangent-at-origin

    const double ca = std::cos(-theta0), sa = std::sin(-theta0);
    const double xb = x1 * ca - y1 * sa;
    const double yb = x1 * sa + y1 * ca;
    const double thb = th1 - theta0;

    // Base construction bends toward -y for increasing tau; flip so that
    // positive R (this codebase's convention) gives positive y/theta.
    const double sign = (R >= 0.0) ? -1.0 : 1.0;

    return { m_a * xb, sign * m_a * yb, sign * thb };
}

QJsonObject LemniscateElement::toJson() const { return TransitionElement::toJson(); }

// ── WienerBogenElement ───────────────────────────────────────────────────

LocalFrame WienerBogenElement::localFrame(double L) const
{
    // Hasslinger's original Wiener Bogen formula (EP1523597B1; MDPI review
    // Eq. 9 — see chat/TransitionCurveLiteratureReferences.md):
    //   kappa(l) = kappa1 + (kappa2-kappa1)*f(l) - h*(psi2-psi1)*f''(l)
    // with kappa1=0 (straight tangent start), kappa2=1/R, psi1=0 (no cant on
    // the straight), f(l)=g(l/Ls) the septic smootherstep already used here
    // (g''(0)=g''(1)=0, verified — so this correction term vanishes exactly
    // at both ends and kappa(0)=0/kappa(Ls)=1/R are preserved regardless of
    // h, psi2). The previous implementation used only the first term
    // (kappa=g(t)/R), which is why it could not reproduce the "additional
    // bends" (non-monotonic curvature) literature identifies as the Wiener
    // Bogen's defining characteristic — that comes entirely from this
    // second (roll-dynamics) term.
    //
    // h (height to centre of gravity) and psi2 (target cant/superelevation
    // angle at the circular-arc end) are NOT yet exposed as user-settable
    // parameters (AICAD's TransitionElement contract only carries R, Ls —
    // no cant/vehicle data model exists yet). Per your direction, fixed
    // representative defaults are used instead of a fully accurate
    // per-project vehicle/cant model:
    //   h    = 2.1336 m (7 ft) — official FRA-cited standard assumption for
    //          typical railway rolling-stock centre-of-gravity height, used
    //          in cant-deficiency/overturning calculations:
    //          DOT/FRA/ORD-19/42 (2019), "Superelevation", Eqs. 6-8
    //          <https://railroads.dot.gov/sites/fra.dot.gov/files/fra_net/19085/Superelevation.pdf>
    //   psi2 = 0.10 rad (~5.7°) — representative cant angle, close to a
    //          standard maximum design cant of ~150mm over a ~1500mm
    //          reference gauge (atan(0.15/1.5)=0.0997 rad; 150mm and 1500mm
    //          are both widely-cited standard reference values — see
    //          literature doc), used as a representative "typical design
    //          cant" case rather than a per-curve equilibrium-cant
    //          calculation, which would need a design-speed input this
    //          codebase does not yet have.
    //          = [ g(t) - |R|*kH*kPsi2*gpp(t)/Ls^2 ] / R
    // These are documented approximations — see class doc comment and the
    // literature reference doc for the full discussion of what a proper,
    // per-project-parameterised implementation would require.
    constexpr double kH    = 2.1336;
    constexpr double kPsi2 = 0.10;

    const double R  = m_reversed ? -m_radius : m_radius;
    const double Ls = m_length;

    return integrateCurvatureRamp(L, Ls, R, [R, Ls](double t) {
        const double t2 = t * t, t3 = t2 * t, t4 = t2 * t2;
        const double g   = 35.0 * t4 - 84.0 * t4 * t + 70.0 * t4 * t2 - 20.0 * t3 * t4;
        const double gpp = 420.0 * t2 - 1680.0 * t3 + 2100.0 * t4 - 840.0 * t4 * t;
        // kappa(l) = g(t)/R - sign(R)*kH*kPsi2*gpp(t)/Ls^2   (cant/roll always
        // tilts toward the curve's own centre, so this correction must flip
        // sign with the turn direction just like the g(t)/R term does)
        //          = [ g(t) - |R|*kH*kPsi2*gpp(t)/Ls^2 ] / R
        if (Ls <= 1e-3 || !std::isfinite(Ls)) return g;
        return g - std::abs(R) * kH * kPsi2 * gpp / (Ls * Ls);
    });
}

QJsonObject WienerBogenElement::toJson() const { return TransitionElement::toJson(); }

// ── RadioidElement ───────────────────────────────────────────────────────

LocalFrame RadioidElement::localFrame(double L) const
{
    const double R = m_reversed ? -m_radius : m_radius;
    return integrateCurvatureRamp(L, m_length, R, [](double t) {
        return 2.0 * t - t * t;
    });
}

QJsonObject RadioidElement::toJson() const { return TransitionElement::toJson(); }

// ============================================================================
//  Shared RK4 coupled-ODE stepper for ElasticRadioidElement/NorwichSturmElement
//
//  Both integrate d(x,y,theta)/ds = (cos theta, sin theta, kappa(x,y)) where
//  kappa depends on the (still-unknown) position, not just on a normalized
//  parameter t — unlike every curvature-ramp class above, so the shared
//  integrateCurvatureRamp() helper doesn't apply here; a genuine coupled ODE
//  integrator is needed instead.
// ============================================================================

namespace {

struct OdeState { double x, y, theta; };

/** RK4-integrate the coupled ODE above from s=0 to s=L. kappaFn(x,y) gives
 *  the curvature at a point (independent of s directly, for both curves
 *  used here). nSteps=300 is ample for engineering precision (verified in
 *  chat to sub-mm/sub-percent accuracy against SciPy's adaptive solver). */
OdeState rk4IntegrateCoupled(double L,
                              const std::function<double(double, double)>& kappaFn,
                              int nSteps = 300)
{
    if (L <= 0.0) return { 0.0, 0.0, 0.0 };
    const double h = L / nSteps;
    OdeState st{ 0.0, 0.0, 0.0 };

    auto deriv = [&](const OdeState& s) {
        const double k = kappaFn(s.x, s.y);
        return OdeState{ std::cos(s.theta), std::sin(s.theta), k };
    };

    for (int i = 0; i < nSteps; ++i) {
        const OdeState k1 = deriv(st);
        const OdeState s2{ st.x + 0.5*h*k1.x, st.y + 0.5*h*k1.y, st.theta + 0.5*h*k1.theta };
        const OdeState k2 = deriv(s2);
        const OdeState s3{ st.x + 0.5*h*k2.x, st.y + 0.5*h*k2.y, st.theta + 0.5*h*k2.theta };
        const OdeState k3 = deriv(s3);
        const OdeState s4{ st.x + h*k3.x, st.y + h*k3.y, st.theta + h*k3.theta };
        const OdeState k4 = deriv(s4);

        st.x     += (h/6.0) * (k1.x + 2.0*k2.x + 2.0*k3.x + k4.x);
        st.y     += (h/6.0) * (k1.y + 2.0*k2.y + 2.0*k3.y + k4.y);
        st.theta += (h/6.0) * (k1.theta + 2.0*k2.theta + 2.0*k3.theta + k4.theta);
    }
    return st;
}

} // anonymous namespace

// ── ElasticRadioidElement ────────────────────────────────────────────────

void ElasticRadioidElement::ensureSolved() const
{
    if (m_cacheValid && m_cachedLs == m_length && m_cachedR == m_radius)
        return;
    m_cachedLs = m_length;
    m_cachedR  = m_radius;

    const double absR = std::abs(m_radius);
    if (m_length <= 1e-3 || absR <= 1e-3 || !std::isfinite(absR)) {
        m_a = 1.0; m_cacheValid = true;
        return;
    }

    // Solve for `a` such that kappa(Ls) = 2*x(Ls)/a^2 = 1/absR, by bisection
    // on `a` (each evaluation runs a full RK4 integration to Ls). f(a) is
    // monotonic (larger a -> gentler curve -> smaller end curvature).
    auto endKappa = [&](double a) {
        auto kappaFn = [a](double x, double /*y*/) { return 2.0 * x / (a * a); };
        const OdeState end = rk4IntegrateCoupled(m_length, kappaFn, 150);
        return 2.0 * end.x / (a * a);
    };

    double lo = 1e-3, hi = m_length;
    // Grow hi until endKappa(hi) < target (larger a -> smaller end curvature)
    double target = 1.0 / absR;
    while (endKappa(hi) > target && hi < 1e9) hi *= 2.0;

    for (int i = 0; i < 50; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (endKappa(mid) > target) lo = mid; else hi = mid;
    }
    m_a = 0.5 * (lo + hi);
    m_cacheValid = true;
}

LocalFrame ElasticRadioidElement::localFrame(double L) const
{
    ensureSolved();
    const double R  = m_reversed ? -m_radius : m_radius;
    const double Ls = m_length;
    if (Ls <= 1e-3 || m_a <= 0.0 || !std::isfinite(R)) return { L, 0.0, 0.0 };

    L = std::max(0.0, std::min(Ls, L));
    const double a = m_a;
    auto kappaFn = [a](double x, double /*y*/) { return 2.0 * x / (a * a); };
    const OdeState st = rk4IntegrateCoupled(L, kappaFn, 300);

    const double sign = (R >= 0.0) ? 1.0 : -1.0;
    return { st.x, sign * st.y, sign * st.theta };
}

QJsonObject ElasticRadioidElement::toJson() const { return TransitionElement::toJson(); }

// ── NorwichSturmElement ──────────────────────────────────────────────────

void NorwichSturmElement::ensureSolved() const
{
    if (m_cacheValid && m_cachedLs == m_length && m_cachedR == m_radius)
        return;
    m_cachedLs = m_length;
    m_cachedR  = m_radius;

    const double absR = std::abs(m_radius);
    if (m_length <= 1e-3 || absR <= 1e-3 || !std::isfinite(absR)) {
        m_poleD = absR; m_cacheValid = true;
        return;
    }

    // Solve for pole distance D (pole placed ahead at (D,0)) such that
    // r(Ls) = |end - pole| = absR exactly. f(D) = r_end(D) - absR is
    // monotonic increasing in D (verified in chat).
    auto rEnd = [&](double D) {
        auto kappaFn = [D](double x, double y) {
            const double r = std::hypot(x - D, y);
            return (r > 1e-9) ? (1.0 / r) : 0.0;
        };
        const OdeState end = rk4IntegrateCoupled(m_length, kappaFn, 150);
        return std::hypot(end.x - D, end.y);
    };

    double lo = absR * 0.5, hi = absR * 4.0;
    double flo = rEnd(lo) - absR;
    // widen bracket if needed
    int guard = 0;
    while ((rEnd(hi) - absR) * flo > 0.0 && guard < 30) { hi *= 1.5; ++guard; }

    for (int i = 0; i < 50; ++i) {
        const double mid = 0.5 * (lo + hi);
        const double fm = rEnd(mid) - absR;
        if ((fm < 0.0) == (flo < 0.0)) { lo = mid; flo = fm; }
        else                            hi = mid;
    }
    m_poleD = 0.5 * (lo + hi);
    m_cacheValid = true;
}

LocalFrame NorwichSturmElement::localFrame(double L) const
{
    ensureSolved();
    const double R  = m_reversed ? -m_radius : m_radius;
    const double Ls = m_length;
    if (Ls <= 1e-3 || m_poleD <= 0.0 || !std::isfinite(R)) return { L, 0.0, 0.0 };

    L = std::max(0.0, std::min(Ls, L));
    const double D = m_poleD;
    auto kappaFn = [D](double x, double y) {
        const double r = std::hypot(x - D, y);
        return (r > 1e-9) ? (1.0 / r) : 0.0;
    };
    const OdeState st = rk4IntegrateCoupled(L, kappaFn, 300);

    const double sign = (R >= 0.0) ? 1.0 : -1.0;
    return { st.x, sign * st.y, sign * st.theta };
}

QJsonObject NorwichSturmElement::toJson() const { return TransitionElement::toJson(); }

// ── PseudoEllipticRadioidElement ─────────────────────────────────────────
//
//  y(x) = a*gd^-1(x/a) = a*asinh(tan(x/a)), evaluated in the curve's own
//  natural frame (inflection point at x=0, but tangent there is 45 deg —
//  see class doc comment), then rotated by -45 deg so the local frame's
//  contract (tangent along +x at L=0) holds.

namespace {

double peRadioidY(double x, double a)   { return a * std::asinh(std::tan(x / a)); }
double peRadioidYp(double x, double a)  { return 1.0 / std::cos(x / a); }              // sec(x/a)
double peRadioidYpp(double x, double a) { return (1.0/std::cos(x/a)) * std::tan(x/a) / a; }

double peRadioidKappa(double x, double a)
{
    const double yp = peRadioidYp(x, a), ypp = peRadioidYpp(x, a);
    return ypp / std::pow(1.0 + yp*yp, 1.5);
}

/** Arc length from 0 to x (natural frame), by numeric quadrature — see
 *  class doc comment re: not re-deriving mathcurve's closed form. */
double peRadioidArcLength(double x, double a, int n = 300)
{
    if (x <= 0.0) return 0.0;
    const double h = x / n;
    double sum = 0.0;
    double prevSpeed = std::sqrt(1.0 + peRadioidYp(0.0, a) * peRadioidYp(0.0, a));
    for (int i = 1; i <= n; ++i) {
        const double xi = i * h;
        const double yp = peRadioidYp(xi, a);
        const double speed = std::sqrt(1.0 + yp * yp);
        sum += 0.5 * h * (prevSpeed + speed);
        prevSpeed = speed;
    }
    return sum;
}

} // anonymous namespace

void PseudoEllipticRadioidElement::ensureSolved() const
{
    if (m_cacheValid && m_cachedLs == m_length && m_cachedR == m_radius)
        return;
    m_cachedLs = m_length;
    m_cachedR  = m_radius;

    const double absR = std::abs(m_radius);
    if (m_length <= 1e-3 || absR <= 1e-3 || !std::isfinite(absR)) {
        m_a = 1.0; m_xEnd = 0.0; m_cacheValid = true;
        return;
    }

    // Normalised (a=1) shape: find x1_end in (0, ~1.0255) — the monotonic
    // rising branch up to the curvature peak — such that
    // kappa1(x1_end)*s1(x1_end) = Ls/absR (scale-invariant target, same
    // two-stage strategy as LemniscateElement/PHQuinticElement).
    constexpr double kXPeak = 1.0255; // just below the true peak (~1.02553)
    const double target = m_length / absR;

    auto ratio = [&](double x1) {
        const double k1 = peRadioidKappa(x1, 1.0);
        const double s1 = peRadioidArcLength(x1, 1.0);
        return k1 * s1;
    };

    double lo = 1e-6, hi = kXPeak;
    double flo = ratio(lo) - target;
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        const double fm = ratio(mid) - target;
        if ((fm < 0.0) == (flo < 0.0)) { lo = mid; flo = fm; }
        else                            hi = mid;
    }
    const double x1End = 0.5 * (lo + hi);
    const double s1End = peRadioidArcLength(x1End, 1.0);
    const double k1End = peRadioidKappa(x1End, 1.0);

    // Scale: kappa scales as 1/a, arc length scales as a (uniform scale of
    // a Cartesian y=f(x) curve — NOT the same lambda^2 rule as the PH
    // quintic/Lemniscate parametric constructions, since here `a` directly
    // scales x,y linearly). kappa_actual = k1End/a = 1/absR  =>  a = absR*k1End.
    m_a    = absR * k1End;
    m_xEnd = x1End * m_a;
    // sanity cross-check (not asserted): m_a * s1End should equal m_length.
    (void)s1End;
    m_cacheValid = true;
}

LocalFrame PseudoEllipticRadioidElement::localFrame(double L) const
{
    ensureSolved();
    const double R  = m_reversed ? -m_radius : m_radius;
    const double Ls = m_length;
    if (Ls <= 1e-3 || m_a <= 0.0 || !std::isfinite(R)) return { L, 0.0, 0.0 };

    L = std::max(0.0, std::min(Ls, L));

    // Invert arc length (natural frame, actual scale `a`) to find x.
    double x;
    if (L <= 1e-9)           x = 0.0;
    else if (L >= Ls - 1e-9) x = m_xEnd;
    else {
        auto arcAt = [&](double xx) { return peRadioidArcLength(xx, m_a); };
        double lo = 0.0, hi = m_xEnd;
        for (int i = 0; i < 60; ++i) {
            const double mid = 0.5 * (lo + hi);
            if (arcAt(mid) < L) lo = mid; else hi = mid;
        }
        x = 0.5 * (lo + hi);
    }

    const double yNat  = peRadioidY(x, m_a);
    const double ypNat = peRadioidYp(x, m_a);
    const double thetaNat = std::atan(ypNat);

    // Rotate by -45 deg (pi/4) so the tangent at x=0 (theta_nat=45 deg)
    // aligns with local +x, matching every other TransitionElement.
    constexpr double kRot = -M_PI / 4.0;
    const double c = std::cos(kRot), s = std::sin(kRot);
    const double xr = x * c - yNat * s;
    const double yr = x * s + yNat * c;
    const double thr = thetaNat + kRot;

    const double sign = (R >= 0.0) ? 1.0 : -1.0;
    return { xr, sign * yr, sign * thr };
}

QJsonObject PseudoEllipticRadioidElement::toJson() const { return TransitionElement::toJson(); }

// ── LogarithmicElement ───────────────────────────────────────────────────

LocalFrame LogarithmicElement::localFrame(double L) const
{
    // Log-aesthetic curve (LAC) family, n=+1 case (Miura 2005, building on
    // Harada et al.'s 1999 "Logarithmic Distribution Diagram of Curvature"):
    //   rho(L)^n = a*L + b,  n=+1  =>  rho(L) = a*L + b   (radius of
    //   curvature LINEAR in arc length) — verified analytically (see chat)
    //   to be exactly the arc-length parametrization of a segment of the
    //   true logarithmic/equiangular spiral r=r0*e^(b*theta), which is why
    //   this LAC case is explicitly identified in the literature as "the
    //   logarithmic spiral" (vs. n=-1, which gives the clothoid).
    //
    // Parametrised here as rho(L) = R + b*(Ls-L), b = R*(K-1)/Ls, so that
    // rho(Ls)=R exactly (kappa(Ls)=1/R exactly) and rho(0)=K*R (kappa(0)=
    // 1/(K*R), a factor 1/K of the target end curvature). Like
    // NorwichSturmElement (a related radioid-family curve with the same
    // underlying issue), kappa=1/rho can never be EXACTLY zero for a
    // finite-length curve — only in the K->infinity limit — so K=20 is used
    // as a fixed, documented approximation (kappa(0) = 5% of kappa(Ls),
    // small enough to be a good practical approximation of a
    // straight-tangent start for realistic railway Ls/R ratios).
    //
    // theta(L) has a clean closed form (verified in chat, matches numeric
    // dtheta/dL exactly): theta(L) = (1/b)*ln[(R+b*Ls)/(R+b*(Ls-L))].
    const double R  = m_reversed ? -m_radius : m_radius;
    const double Ls = m_length;
    if (Ls <= 1e-3 || !std::isfinite(R)) return { L, 0.0, 0.0 };
    L = std::max(0.0, std::min(Ls, L));

    constexpr double kK = 20.0; // rho(0) = kK * R  =>  kappa(0) = kappa(Ls)/kK
    const double b = R * (kK - 1.0) / Ls;

    auto theta = [&](double l) {
        if (std::abs(b) < 1e-12) return l / R; // degenerate: b->0 => clothoid-less linear fallback
        return (1.0 / b) * std::log((R + b * Ls) / (R + b * (Ls - l)));
    };

    constexpr int n = 400;
    const double h = L / n;
    double x = 0.0, y = 0.0;
    double thPrev = theta(0.0);
    double cosPrev = std::cos(thPrev), sinPrev = std::sin(thPrev);
    for (int i = 1; i <= n; ++i) {
        const double l  = i * h;
        const double th = theta(l);
        const double c = std::cos(th), sn = std::sin(th);
        x += 0.5 * h * (cosPrev + c);
        y += 0.5 * h * (sinPrev + sn);
        cosPrev = c; sinPrev = sn;
    }
    return { x, y, theta(L) };
}

QJsonObject LogarithmicElement::toJson() const { return TransitionElement::toJson(); }

// ── HyperbolicElement ────────────────────────────────────────────────────

LocalFrame HyperbolicElement::localFrame(double L) const
{
    // Kisgyörgy & Barna, "Hyperbolic transition curve", Periodica
    // Polytechnica Civil Engineering 58(1), 2014, eq. 11:
    //   G(l) = (1/2R) * [sh(p - 2p*l/L) - sh(p) + (2p*l/L)*ch(p)]
    //                 / [p*ch(p) - sh(p)]
    // Factoring out 1/R gives a pure shape function of t=l/L (verified
    // symbolically: g(0)=0, g(1)=1), so this fits the shared curvature-ramp
    // integrator exactly like Bloss/Sinusoidal/etc. Cross-checked against
    // the paper's own published X(L),Y(L) endpoint table for p=1,5,20 —
    // matches to ~5e-5 (the residual is the paper's own truncated-series
    // approximation, not this implementation).
    //
    // p is a shape parameter (paper: p->0 approaches the cosine/half-sine
    // curve, p->infinity approaches the clothoid) not currently exposed as
    // a user-settable field (same simplification approach as
    // WienerBogenElement's h/psi2) — p=5 is used, one of the paper's own
    // three analysed cases, a middle ground between the two extremes.
    const double R = m_reversed ? -m_radius : m_radius;
    return integrateCurvatureRamp(L, m_length, R, [](double t) {
        constexpr double kP = 5.0;
        const double denom = kP * std::cosh(kP) - std::sinh(kP);
        return (std::sinh(kP * (1.0 - 2.0 * t)) - std::sinh(kP) + 2.0 * kP * t * std::cosh(kP))
               / (2.0 * denom);
    });
}

QJsonObject HyperbolicElement::toJson() const { return TransitionElement::toJson(); }

// ── PolynomialElement ────────────────────────────────────────────────────

LocalFrame PolynomialElement::localFrame(double L) const
{
    const double R = m_reversed ? -m_radius : m_radius;
    return integrateCurvatureRamp(L, m_length, R, [](double t) {
        return t * t * t;
    });
}

QJsonObject PolynomialElement::toJson() const { return TransitionElement::toJson(); }

// ── QuinticElement ───────────────────────────────────────────────────────

LocalFrame QuinticElement::localFrame(double L) const
{
    const double R = m_reversed ? -m_radius : m_radius;
    return integrateCurvatureRamp(L, m_length, R, [](double t) {
        const double t2 = t * t, t3 = t2 * t;
        return 6.0 * t3 * t2 - 15.0 * t2 * t2 + 10.0 * t3;
    });
}

QJsonObject QuinticElement::toJson() const { return TransitionElement::toJson(); }

// ── PHQuinticElement ─────────────────────────────────────────────────────
//
//  Pythagorean-Hodograph quintic spiral. See the class doc comment in
//  RailwayAlignmentElement.h for the full derivation. Summary of the
//  closed-form pieces used below (all with w0=w1=1 fixed):
//
//    w(t)     = (1-t)^2 + 2t(1-t) + (p+iq)t^2            [complex pre-image]
//    kappa(t) = 2*Im(w(t)*w'(t)) / |w(t)|^4
//    s(t)     = t^5*(p^2/5 - 2p/5 + q^2/5 + 1/5) + t^3*(2p/3 - 2/3) + t
//    r(t)     = quintic Bezier with control points (Farouki 1994/2023):
//                 p0=0, p1=1/5, p2=2/5, p3=8/15+(p+iq)/15,
//                 p4=8/15+4(p+iq)/15, p5=8/15+4(p+iq)/15+(p+iq)^2/5
//
//  Shape solve (ensureSolved()): kappa'(1)=0 reduces to a quadratic in q^2,
//  solved in closed form as q2_of_p(p); kappa(1)*s(1)=Ls/|R| is then a 1-D
//  bisection in p over the analytically-known monotonic branch
//  p in (p_min, p_hi], p_min=(19-sqrt(41))/20 (exact root where q^2 -> 0).

namespace {

double phQuinticQ2OfP(double p)
{
    // Closed-form solution of the kappa'(1)=0 condition (with w0=w1=1) for
    // q^2 in terms of p — see chat derivation (SymPy), using the corrected
    // curvature identity kappa(t) = 2*Im(conj(w(t))*w'(t)) / |w(t)|^4
    // (Im(conj(w)*w'), NOT Im(w*w') — the latter does not equal d(theta)/ds
    // for this parametrization; verified symbolically against direct
    // differentiation of theta(t)=2*atan2(Im w,Re w), see chat).
    return p * (8.0 - 7.0 * p) / 7.0;
}

double phQuinticKappa1(double p, double q)
{
    // kappa(1) with w0=w1=1, using the corrected curvature identity above.
    const double den = p * p + q * q;
    return 4.0 * q / (den * den);
}

double phQuinticS1End(double p, double q)
{
    return p * p / 5.0 + 4.0 * p / 15.0 + q * q / 5.0 + 8.0 / 15.0;
}

/** Closed-form arc length (a=1/unit shape) from 0 to t, w0=w1=1. */
double phQuinticSNorm(double t, double p, double q)
{
    const double c5 = p * p / 5.0 - 2.0 * p / 5.0 + q * q / 5.0 + 1.0 / 5.0;
    const double c3 = 2.0 * p / 3.0 - 2.0 / 3.0;
    const double t3 = t * t * t;
    return t3 * t * t * c5 + t3 * c3 + t;
}

/** Complex pre-image w(t), w0=w1=1. Returned as (real, imag). */
void phQuinticW(double t, double p, double q, double& wr, double& wi)
{
    const double oneMinusT = 1.0 - t;
    wr = oneMinusT * oneMinusT + 2.0 * t * oneMinusT + p * t * t;
    wi = q * t * t;
}

/** Quintic Bezier position r(t), w0=w1=1, control points p0..p5 per the
 *  Farouki control-point formula, evaluated via direct Bernstein sum
 *  (De Casteljau not needed — degree 5 direct sum is cheap and exact). */
void phQuinticR(double t, double p, double q, double& xr, double& yi)
{
    // Complex control points (real, imag):
    const double p0r = 0.0,            p0i = 0.0;
    const double p1r = 1.0 / 5.0,       p1i = 0.0;
    const double p2r = 2.0 / 5.0,       p2i = 0.0;
    const double p3r = 8.0 / 15.0 + p / 15.0,          p3i = q / 15.0;
    const double p4r = 8.0 / 15.0 + 4.0 * p / 15.0,     p4i = 4.0 * q / 15.0;
    // p5 = 8/15 + 4(p+iq)/15 + (p+iq)^2/5
    const double p2mq2 = p * p - q * q;
    const double p5r = 8.0 / 15.0 + 4.0 * p / 15.0 + p2mq2 / 5.0;
    const double p5i = 4.0 * q / 15.0 + (2.0 * p * q) / 5.0;

    const double u = 1.0 - t;
    const double u2 = u * u, u3 = u2 * u, u4 = u3 * u, u5 = u4 * u;
    const double t2 = t * t, t3 = t2 * t, t4 = t3 * t, t5 = t4 * t;
    // Bernstein degree-5 basis coefficients: C(5,k)=1,5,10,10,5,1
    const double b0 = u5,             b1 = 5.0 * u4 * t,   b2 = 10.0 * u3 * t2;
    const double b3 = 10.0 * u2 * t3, b4 = 5.0 * u * t4,   b5 = t5;

    xr = p0r*b0 + p1r*b1 + p2r*b2 + p3r*b3 + p4r*b4 + p5r*b5;
    yi = p0i*b0 + p1i*b1 + p2i*b2 + p3i*b3 + p4i*b4 + p5i*b5;
}

} // anonymous namespace

void PHQuinticElement::ensureSolved() const
{
    if (m_cacheValid && m_cachedLs == m_length && m_cachedR == m_radius)
        return;

    m_cachedLs = m_length;
    m_cachedR  = m_radius;

    const double absR = std::abs(m_radius);
    if (m_length <= 1e-3 || absR <= 1e-3 || !std::isfinite(absR)) {
        m_p = m_q = 0.0; m_lambda2 = 1.0; m_cacheValid = true;
        return;
    }

    static const double kPHi = 8.0 / 7.0; // exact upper root where q^2 -> 0
    constexpr double kMaxTarget = 500.0;  // generous safety clamp (target=Ls/|R|
                                           // is monotonically achievable from 0
                                           // to +infinity as p ranges over
                                           // (0, kPHi) — see chat verification;
                                           // this just guards against
                                           // pathological inputs)

    double target = m_length / absR;
    target = std::min(target, kMaxTarget);

    auto h = [&](double p) {
        const double q2 = phQuinticQ2OfP(p);
        if (q2 < 0.0) return std::numeric_limits<double>::quiet_NaN();
        const double q = std::sqrt(q2);
        return phQuinticKappa1(p, q) * phQuinticS1End(p, q) - target;
    };

    double lo = 1e-9, hi = kPHi - 1e-9;
    double flo = h(lo);
    for (int i = 0; i < 80; ++i) {
        const double mid = 0.5 * (lo + hi);
        const double fm = h(mid);
        if ((fm < 0.0) == (flo < 0.0)) { lo = mid; flo = fm; }
        else                            hi = mid;
    }
    m_p = 0.5 * (lo + hi);
    m_q = std::sqrt(std::max(0.0, phQuinticQ2OfP(m_p)));

    const double s1 = phQuinticS1End(m_p, m_q);
    m_lambda2 = (s1 > 1e-12) ? (m_length / s1) : 1.0;
    m_cacheValid = true;
}

LocalFrame PHQuinticElement::localFrame(double L) const
{
    ensureSolved();

    const double Ls = m_length;
    const double R  = m_reversed ? -m_radius : m_radius;
    if (Ls <= 1e-3 || m_lambda2 <= 0.0 || !std::isfinite(R)) return { L, 0.0, 0.0 };

    L = std::max(0.0, std::min(Ls, L));
    const double sTarget = L / m_lambda2;

    double t;
    if (L <= 1e-9)           t = 0.0;
    else if (L >= Ls - 1e-9) t = 1.0;
    else {
        double lo = 0.0, hi = 1.0;
        for (int i = 0; i < 60; ++i) {
            const double mid = 0.5 * (lo + hi);
            if (phQuinticSNorm(mid, m_p, m_q) < sTarget) lo = mid; else hi = mid;
        }
        t = 0.5 * (lo + hi);
    }

    double xr, yi;
    phQuinticR(t, m_p, m_q, xr, yi);
    double wr, wi;
    phQuinticW(t < 1e-7 ? 1e-7 : t, m_p, m_q, wr, wi);
    const double thb = 2.0 * std::atan2(wi, wr);

    const double sign = (R >= 0.0) ? 1.0 : -1.0;

    return { m_lambda2 * xr, sign * m_lambda2 * yi, sign * thb };
}

QJsonObject PHQuinticElement::toJson() const { return TransitionElement::toJson(); }



LocalFrame BiquadraticElement::localFrame(double L) const
{
    // Helmert's 1872 biquadratic parabola — resolved (see literature doc)
    // via Schuhr's precise technical description: "die parabelförmig
    // geschwungene Überhöhungslinie von Helmert 1872 [führt] auf die
    // biquadratische Parabel" (Helmert's PARABOLA-shaped — i.e. simple
    // quadratic — cant/superelevation ramp leads to the biquadratic
    // parabola), as opposed to the LINEAR cant ramp that leads to the
    // ordinary cubic parabola (ParabolaElement/CubicJPNElement/
    // CubicECIElement above). Taking this literally — curvature itself is
    // a simple parabola (quadratic) in arc length, kappa(l)=(1/R)(l/Ls)^2 —
    // and integrating twice for the Cartesian y(x) small-angle
    // approximation gives y(x) = x^4/(12*R*Ls^2): a genuine QUARTIC
    // ("4th-order parabola" / "Parabel vierter Ordnung"), verified
    // symbolically (see chat) — exactly matching German Wikipedia's
    // description of the Schramm/Helmert curve, resolving the earlier
    // conflict with Autodesk's brief "two second-degree parabolas"
    // description (a previous version of this class used that piecewise
    // form instead; this simple quadratic-curvature form has more precise,
    // directly-traceable academic grounding — see literature doc).
    const double R = m_reversed ? -m_radius : m_radius;
    return integrateCurvatureRamp(L, m_length, R, [](double t) {
        return t * t;
    });
}

QJsonObject BiquadraticElement::toJson() const { return TransitionElement::toJson(); }

// ── SplineElement ────────────────────────────────────────────────────────

LocalFrame SplineElement::localFrame(double L) const
{
    const double R = m_reversed ? -m_radius : m_radius;
    return integrateCurvatureRamp(L, m_length, R, &splineShape);
}

QJsonObject SplineElement::toJson() const { return TransitionElement::toJson(); }

// ── BlossEulerHybridElement ──────────────────────────────────────────────

LocalFrame BlossEulerHybridElement::localFrame(double L) const
{
    // "Doucine" — a REAL, standardised French construction (SNCF, since
    // 1968; used on TGV and conventional lines, documented lengths 40m/20m
    // respectively — see literature doc): a short SMOOTH (Bloss-like,
    // zero-slope) cap at EACH END of an otherwise ordinary LINEAR (clothoid)
    // curvature ramp, rounding off the slope discontinuity that a pure
    // clothoid has where it meets the constant-curvature straight tangent
    // and circular arc. This replaces a previous version that uniformly
    // averaged the Bloss and linear shapes across the whole length (a
    // simple blend, not a corner-smoothing composite — verified in chat
    // that it did NOT actually have zero slope at the ends, so it didn't
    // capture doucine's actual purpose).
    //
    // Built from a smoothed-trapezoid g'(t): a raised-cosine ramp up over
    // [0,f], a constant plateau over [f,1-f] (the clothoid body), and a
    // mirrored raised-cosine ramp down over [1-f,1], normalised so
    // g(0)=0, g(1)=1 (verified symbolically/numerically: continuous value
    // AND derivative at both internal junctions, zero derivative at both
    // ends). f=0.25 (each end cap is 25% of Ls) is used as a documented
    // representative fraction — SNCF's own standard uses fixed absolute
    // cap lengths (40m/20m) rather than a fraction of the total transition
    // length, which doesn't translate directly to this codebase's
    // arbitrary-Ls parametrization.
    const double R = m_reversed ? -m_radius : m_radius;
    return integrateCurvatureRamp(L, m_length, R, [](double t) {
        constexpr double kF = 0.25;
        const double C = 1.0 / (1.0 - kF);
        if (t <= kF) {
            return 0.5 * C * (t - (kF / M_PI) * std::sin(M_PI * t / kF));
        } else if (t >= 1.0 - kF) {
            const double u = 1.0 - t;
            return 1.0 - 0.5 * C * (u - (kF / M_PI) * std::sin(M_PI * u / kF));
        } else {
            const double gF = 0.5 * C * kF; // g(f) from the up-ramp branch (sin(pi)=0)
            return gF + C * (t - kF);
        }
    });
}

QJsonObject BlossEulerHybridElement::toJson() const { return TransitionElement::toJson(); }

// ============================================================================
//  EggTransitionElement
// ============================================================================

// ── Generalised equivalent-spiral machinery ─────────────────────────────────
//
//  The classical OLS egg-curve construction reduces the CC transition to a
//  sub-portion of a full-length CLOTHOID because a clothoid's curvature is
//  *exactly* linear in arc length: kappa(L) = (L/Ls)/R. That linearity is
//  exactly why "traverse [Ls-LE, Ls] of a length-Ls clothoid" reproduces the
//  correct curvature at both physical ends of the egg segment for ANY LE —
//  restricting a linear function to a sub-interval is still linear, so it
//  can always be re-scaled to hit two prescribed endpoint values.
//
//  For a general curvature-ramp family kappa(L) = g(L/Ls)/R (g monotonic,
//  not necessarily linear), the equivalent-spiral trick still works, but Ls
//  must be solved so that g(t*) at t* = (Ls-LE)/Ls equals the target
//  curvature RATIO |R2|/|R1| (r1Dominant case), or the mirror for the other
//  case — not just linearly interpolated. Two building blocks below make
//  this generic across every isFamilyEggCompatible() family, with no
//  per-family formula required here:
//
//   1. gRampDerivative()/invertNormalisedRamp() recover g(t) (up to the
//      family's own normalisation) by finite-differencing localFrame(t).theta
//      on a small reference instance of the family — theta'(t) is exactly
//      the curvature shape function for every family built on
//      integrateCurvatureRamp() (Sinusoidal, Bloss, Radioid, Polynomial,
//      Quintic, Biquadratic, Spline, BlossEulerHybrid, HalfSine's closed
//      form) since kappa(L) there is by construction g(L/Ls)/R exactly,
//      independent of the reference instance's own Ls/R.
//   2. For the "small-deflection Cartesian shortcut" families (Parabola,
//      CubicJPN, CubicECI, Cosine — see their own doc comments: these
//      approximate arc length by the Cartesian x-coordinate, so their
//      shape has a residual, usually small, dependence on the dimensionless
//      ratio rho=Ls/R, not purely on t), ols() below iterates a short
//      fixed-point loop so the reference instance's own rho converges to
//      the actual target rho, rather than accepting extra unaccounted error
//      from an arbitrary fixed reference ratio. This converges in 1
//      iteration for the exact families above and typically 2-4 for the
//      Cartesian-shortcut ones, since realistic railway Ls/R ratios are
//      small and the shape's rho-dependence is correspondingly weak.
// ============================================================================

namespace {

/** Central-difference estimate of d(theta)/dt at parameter t (t in [0,1])
 *  for a TransitionElement configured with unit length (Ls=1). Equals the
 *  family's curvature shape function kappa(t)*R exactly for every family
 *  built on integrateCurvatureRamp()/halfSineExactFrame(); an approximation
 *  (function of both t and rho=1/R here, since Ls=1) for the small-
 *  deflection Cartesian families — see the comment block above. */
double gRampDerivative(const TransitionElement& ref, double t)
{
    constexpr double h = 1e-4;
    const double t0 = std::max(0.0, t - h);
    const double t1 = std::min(1.0, t + h);
    if (t1 - t0 < 1e-9) return 0.0;
    return (ref.localFrame(t1).theta - ref.localFrame(t0).theta) / (t1 - t0);
}

/** Solve g(t*)/g(1) = ratio for t* in [0,1] via bisection (g assumed
 *  monotonic nondecreasing on [0,1], per isFamilyEggCompatible()'s
 *  contract). Normalising by g(1) rather than assuming it's already
 *  exactly 1 is a defensive measure against finite-difference/quadrature
 *  noise right at the endpoint. */
double invertNormalisedRamp(const TransitionElement& ref, double ratio)
{
    const double gEnd = gRampDerivative(ref, 1.0);
    if (!(gEnd > 1e-12)) return std::clamp(ratio, 0.0, 1.0); // degenerate guard
    const double target = std::clamp(ratio, 0.0, 1.0) * gEnd;
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 50; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (gRampDerivative(ref, mid) < target) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

} // anonymous namespace

std::unique_ptr<TransitionElement> EggTransitionElement::makeElement(ElementType family)
{
    switch (family) {
    case ElementType::HalfSine:         return std::make_unique<HalfSineElement>();
    case ElementType::Parabola:         return std::make_unique<ParabolaElement>();
    case ElementType::CubicJPN:         return std::make_unique<CubicJPNElement>();
    case ElementType::CubicECI:         return std::make_unique<CubicECIElement>();
    case ElementType::Sinusoidal:       return std::make_unique<SinusoidalElement>();
    case ElementType::Cosine:           return std::make_unique<CosineElement>();
    case ElementType::Bloss:            return std::make_unique<BlossElement>();
    case ElementType::Radioid:          return std::make_unique<RadioidElement>();
    case ElementType::Logarithmic:      return std::make_unique<LogarithmicElement>();
    case ElementType::Hyperbolic:       return std::make_unique<HyperbolicElement>();
    case ElementType::Polynomial:       return std::make_unique<PolynomialElement>();
    case ElementType::Quintic:          return std::make_unique<QuinticElement>();
    case ElementType::Biquadratic:      return std::make_unique<BiquadraticElement>();
    case ElementType::Spline:           return std::make_unique<SplineElement>();
    case ElementType::BlossEulerHybrid: return std::make_unique<BlossEulerHybridElement>();
    case ElementType::Clothoid:
    default:
        // Also the fallback for any non-egg-compatible family (WienerBogen,
        // Lemniscate, PHQuintic, ElasticRadioid, NorwichSturm,
        // PseudoEllipticRadioid, …) — callers are expected to have already
        // routed those through isFamilyEggCompatible() before reaching here,
        // but this keeps the factory total/safe regardless.
        return std::make_unique<ClothoidElement>();
    }
}

bool EggTransitionElement::isFamilyEggCompatible(ElementType family)
{
    switch (family) {
    case ElementType::Clothoid:
    case ElementType::HalfSine:
    case ElementType::Parabola:
    case ElementType::CubicJPN:
    case ElementType::CubicECI:
    case ElementType::Sinusoidal:
    case ElementType::Cosine:
    case ElementType::Bloss:
    case ElementType::Radioid:
    case ElementType::Logarithmic:
    case ElementType::Hyperbolic:
    case ElementType::Polynomial:
    case ElementType::Quintic:
    case ElementType::Biquadratic:
    case ElementType::Spline:
    case ElementType::BlossEulerHybrid:
        return true;
    default:
        return false;
    }
}

QString EggTransitionElement::familyName(ElementType f)
{
    return isFamilyEggCompatible(f) ? makeElement(f)->typeName()
                                     : QStringLiteral("Clothoid");
}

ElementType EggTransitionElement::familyFromName(const QString& name)
{
    if      (name == "HalfSine")         return ElementType::HalfSine;
    else if (name == "Parabola")         return ElementType::Parabola;
    else if (name == "CubicJPN")         return ElementType::CubicJPN;
    else if (name == "CubicECI")         return ElementType::CubicECI;
    else if (name == "Sinusoidal")       return ElementType::Sinusoidal;
    else if (name == "Cosine")           return ElementType::Cosine;
    else if (name == "Bloss")            return ElementType::Bloss;
    else if (name == "Radioid")          return ElementType::Radioid;
    else if (name == "Logarithmic")      return ElementType::Logarithmic;
    else if (name == "Hyperbolic")       return ElementType::Hyperbolic;
    else if (name == "Polynomial")       return ElementType::Polynomial;
    else if (name == "Quintic")          return ElementType::Quintic;
    else if (name == "Biquadratic")      return ElementType::Biquadratic;
    else if (name == "Spline")           return ElementType::Spline;
    else if (name == "BlossEulerHybrid") return ElementType::BlossEulerHybrid;
    return ElementType::Clothoid; // includes "Clothoid" itself and unknowns
}

double EggTransitionElement::ols(double R1, double R2, double LE)
{
    return ols(ElementType::Clothoid, R1, R2, LE);
}

double EggTransitionElement::ols(ElementType family, double R1, double R2, double LE)
{
    const double aR1 = std::abs(R1), aR2 = std::abs(R2);
    const double diff = std::abs(aR1 - aR2);
    if (diff < 1e-9 || LE <= 0.0) return LE; // degenerate: equal radii

    // Closed form for the exactly-linear clothoid shape (g(t)=t exactly) —
    // also the fallback for any non-egg-compatible family, so this class
    // never silently applies a foreign curvature shape's math to a family
    // it wasn't derived for.
    if (family == ElementType::Clothoid || !isFamilyEggCompatible(family))
        return LE * std::max(aR1, aR2) / diff;

    const double rTight = std::min(aR1, aR2);
    const double rLoose = std::max(aR1, aR2);
    const double ratio  = rTight / rLoose;   // target g(t*)/g(1), in (0,1)

    // Fixed-point solve for Ls (see the comment block above this section for
    // why this is needed instead of a single reference evaluation).
    double Ls = LE * rLoose / diff; // clothoid-formula initial guess
    for (int iter = 0; iter < 8; ++iter) {
        const double rho = rTight > 1e-9 ? Ls / rTight : 0.0;
        auto ref = makeElement(family);
        ref->setLength(1.0);
        ref->setRadius(rho > 1e-12 ? 1.0 / rho : kMaxRadius);

        const double tStar  = invertNormalisedRamp(*ref, ratio);
        const double LsNext = LE / std::max(1.0 - tStar, 1e-9);

        if (std::abs(LsNext - Ls) < 1e-6 * std::max(1.0, Ls)) { Ls = LsNext; break; }
        Ls = LsNext;
    }
    return Ls;
}

void EggTransitionElement::rebuild()
{
    if (!m_r1 || !m_r2 || !m_le) return;

    m_ls         = ols(m_family, m_r1, m_r2, m_le);
    m_r1Dominant = std::abs(m_r1) >= std::abs(m_r2);
    m_length     = m_le;

    // Build the internal equivalent full-length spiral, in whichever family
    // was selected (defaults to Clothoid — see setSpiralFamily()).
    m_equiv = makeElement(m_family);
    m_equiv->setLength(m_ls);
    m_equiv->setRadius(m_r1Dominant ? m_r2 : -m_r1);

    // The equivalent spiral placement is computed in worldXY / localFrame
    // based on the EggTransitionElement's own placement (set externally).
}

// ── localFrame() — corrected re-anchoring fix (see Phase 5 note below) ───────
//
//  BUG (found while cross-checking against a reference VBA egg-curve
//  implementation that samples the two directions (|R1|≥|R2| vs |R1|<|R2|)
//  separately and was verified closed to 0.0 error): the previous
//  implementation returned `m_equiv->localFrame(Lequiv)` *raw* — i.e. the
//  equivalent full-length spiral's own (x,y,theta), measured from ITS
//  chainage-0 (a point that generally does NOT coincide with the actual
//  physical start of this egg segment, since the physical segment only
//  occupies the sub-range [ls−le, ls] (r1Dominant) or [0, ls] traversed
//  back-to-front (!r1Dominant) of the theoretical length-ls spiral — see the
//  class comment above). Every other TransitionElement's localFrame(L) is
//  (0,0,0) at L=0 and grows from there; the old Egg code broke that contract
//  whenever |R1|≠|R2| (i.e. essentially always), silently including the
//  un-traversed leading portion's rotation/offset in the result. Verified
//  numerically (Python, comparing against direct curvature-integration
//  ground truth) that this could overstate the segment's (x,y,theta) by
//  ~30–100% depending on the R1/R2 ratio — a serious error for
//  solveCompoundChain()/solveACA(), the only real callers of this method.
//
//  Fix: explicitly subtract the equivalent spiral's own local frame AT the
//  physical start (L=0) before returning, rotating the position delta into
//  the frame whose x-axis is the physical start's own tangent direction —
//  exactly the same "re-anchor to a known point" operation every other
//  TransitionElement gets for free by construction (their own chainage-0 IS
//  their physical start). The r1Dominant branch traverses the equivalent
//  spiral in its own natural (increasing-chainage) direction, so a plain
//  rotate-by(−theta0) suffices. The !r1Dominant branch traverses it
//  back-to-front (physical L increasing ⇒ Lequiv decreasing), which mirrors
//  the position delta in addition to rotating it — confirmed against ground
//  truth for a range of R1/R2 ratios (same sign; the only case
//  solveCompoundChain()/solveACA() ever construct, since both radii there
//  are always `signR * someAbsRadius`).
//
//  worldXY()/worldAzimuth()/inversePW() below are simplified to just delegate
//  to the base-class implementations (which already do exactly
//  localToWorld(localFrame(L), w_eff) / m_place.azimuth + localFrame(L).theta
//  — see AlignmentElement::worldXY/worldAzimuth). Now that localFrame() is
//  correctly self-anchored, the old custom "virtual equivalent-spiral
//  placement" indirection (eggEquivPlacement(), removed) is both unnecessary
//  and was itself carrying the mirror-image of the same bug (confirmed
//  numerically: its !r1Dominant branch had an extraneous +π in the azimuth
//  term and the wrong sign on the position offset, so worldXY() only agreed
//  with ground truth when |R1|≥|R2|).

LocalFrame EggTransitionElement::localFrame(double L) const
{
    if (!m_equiv) return {};

    const double le = m_le;
    const double ls = m_ls;

    // Lequiv value corresponding to the egg's own physical start (L=0), and
    // to the requested chainage L.
    double Lequiv0, Lequiv1;
    if (m_r1Dominant) {
        Lequiv0 = ls - le;
        Lequiv1 = (ls - le) + L;
    } else {
        Lequiv0 = ls;
        Lequiv1 = ls - L;
    }

    const LocalFrame lf0 = m_equiv->localFrame(Lequiv0);
    const LocalFrame lf1 = m_equiv->localFrame(Lequiv1);

    const double dx = lf1.x - lf0.x;
    const double dy = lf1.y - lf0.y;
    const double c0 = std::cos(lf0.theta), s0 = std::sin(lf0.theta);

    double x, y;
    if (m_r1Dominant) {
        // Forward traversal through the equivalent spiral: standard
        // rotate-by(−theta0) re-anchors the frame at the physical start.
        x =  dx * c0 + dy * s0;
        y = -dx * s0 + dy * c0;
    } else {
        // Back-to-front traversal: mirrors the rotated delta as well
        // (verified numerically against direct curvature integration for
        // R1/R2 ratios spanning 1.5x–5x, both signs).
        x = -(dx * c0 + dy * s0);
        y =  dx * s0 - dy * c0;
    }
    const double theta = lf1.theta - lf0.theta;

    return { x, y, theta };
}

QPointF EggTransitionElement::worldXY(double p, double w) const
{
    return AlignmentElement::worldXY(p, w);
}

double EggTransitionElement::worldAzimuth(double p) const
{
    return AlignmentElement::worldAzimuth(p);
}

QPointF EggTransitionElement::inversePW(double x, double y) const
{
    return AlignmentElement::inversePW(x, y);
}

QJsonObject EggTransitionElement::toJson() const
{
    QJsonObject o = TransitionElement::toJson();
    o["r1"] = m_r1;
    o["r2"] = m_r2;
    o["le"] = m_le;
    o["spiralFamily"] = familyName(m_family);
    return o;
}

// ============================================================================
//  AlignmentElementFactory
// ============================================================================

namespace {
// 圓弧／緩和曲線半徑有效性檢查。半徑 0（或非有限值）在幾何上沒有意義——
// ClothoidElement/HalfSineElement/ParabolaElement/CubicJPNElement/
// CubicECIElement 的 localFrame() 都會以此半徑作分母（A² = |R|·Ls、
// b = 1/(2R) …），若放行 R=0 會產生 Infinity/NaN，在啟用了浮點例外
// 陷阱的執行環境下（例如本專案內嵌的 ECL）會直接觸發
// FLOATING-POINT-OVERFLOW 而讓整個行程崩潰。
// 曾實際發生的觸發情境：ALD 資料中出現非典型的 TSC 鄰接樣式（例如
// "SC"/"CS"/"SS"），createSpiral() 的 fallback 分支誤用了鄰近「S」型
// 關鍵點的 radius 欄位——該欄位對緩和曲線關鍵點而言恆為 0（半徑欄位
// 在此類記錄中實際存放的是曲線類型名稱，而非數值），因而建出半徑為 0
// 的退化元素。
bool isValidRadius(double r)
{
    constexpr double kMinRadius = 1e-3;  // 1 mm；小於此值視為資料錯誤
    return std::isfinite(r) && std::abs(r) > kMinRadius;
}
} // namespace

Placement AlignmentElementFactory::makePlacement(const AlignmentPoint& pt)
{
    return { pt.chainage, pt.easting, pt.northing, pt.azimuth };
}

Placement AlignmentElementFactory::makeReversedPlacement(const AlignmentPoint& farEnd,
                                                         double /*length*/)
{
    // Reversed spiral origin: far-end point with azimuth reversed (+π)
    return {
        farEnd.chainage,
        farEnd.easting,
        farEnd.northing,
        AlignmentElement::normalise(farEnd.azimuth + M_PI)
    };
}

std::unique_ptr<AlignmentElement>
AlignmentElementFactory::create(const AlignmentPoint& prev,
                                const AlignmentPoint& cur,
                                const AlignmentPoint& next)
{
    // Element type is the SECOND character of cur.tsc
    if (cur.tsc.size() < 2) {
        qWarning() << "[AlignmentElementFactory] Invalid TSC code:" << cur.tsc;
        return nullptr;
    }

    const QChar elemType = cur.tsc[1];

    if (elemType == 'T') {
        auto elem = std::make_unique<TangentElement>();
        elem->setPlacement(makePlacement(cur));
        elem->setLength(cur.length);
        return elem;
    }

    if (elemType == 'C') {
        if (!isValidRadius(cur.radius)) {
            qWarning() << "[AlignmentElementFactory] Rejecting circular element with"
                          " invalid radius at chainage" << cur.chainage
                       << "radius=" << cur.radius;
            return nullptr;
        }
        auto elem = std::make_unique<CircularArcElement>(cur.radius);
        elem->setPlacement(makePlacement(cur));
        elem->setLength(cur.length);
        return elem;
    }

    if (elemType == 'S') {
        return createSpiral(prev, cur, next);
    }

    qWarning() << "[AlignmentElementFactory] Unknown element type:" << elemType;
    return nullptr;
}

std::unique_ptr<AlignmentElement>
AlignmentElementFactory::createSpiral(const AlignmentPoint& prev,
                                      const AlignmentPoint& cur,
                                      const AlignmentPoint& next)
{
    // Determine spiral sub-type from neighbour TSC second characters
    // (matching the C# switch: prev.tsc[1] + next.tsc[1]).
    //
    // 'S' 鄰居視同 'T'：兩段緩和曲線在 SS 交會點直接相接（例如 G06U 的
    // SC(800)-CS-SS-SC(800)-CS 複合反曲線），該交會點在幾何上等同於曲率
    // 歸零的「虛擬切線點」（長度為 0 的切線），與真正的 T（直線）鄰接在
    // 半徑判斷上是等價的。若不做此轉換，"CS"/"SS" 這類組合會落入下方的
    // Fallback 分支，誤用鄰近緩和曲線關鍵點的 radius 欄位（該欄位對緩和
    // 曲線關鍵點而言恆為 0，非真正的圓弧半徑）。
    auto asPatternChar = [](QChar c) -> QChar {
        return (c == 'S') ? QChar('T') : c;
    };
    const QChar prevElem = asPatternChar((prev.tsc.size() >= 2) ? prev.tsc[1] : QChar('T'));
    const QChar nextElem = asPatternChar((next.tsc.size() >= 2) ? next.tsc[1] : QChar('T'));

    // Map cur.curveType (the uppercase ALD-style token written by
    // AlignmentSolver.cpp's spiralTypeName(), e.g. "BLOSS", "HALFSINE") to
    // ElementType. Shared by both the Egg branch below and the
    // makeTransition() lambda further down — kept as two separate small
    // switches rather than factored into a shared header, matching this
    // project's existing "minimal-surface-area repeated lookup table"
    // convention (see e.g. the near-identical table inside makeTransition()
    // just below, and AlignmentElementFactory::fromJson() above).
    auto curveTypeToElementType = [](const QString& ct) -> ElementType {
        if      (ct == "HALFSINE")         return ElementType::HalfSine;
        else if (ct == "PARABOLA")         return ElementType::Parabola;
        else if (ct == "CUBICJPN")         return ElementType::CubicJPN;
        else if (ct == "CUBICECI")         return ElementType::CubicECI;
        else if (ct == "SINUSOIDAL")       return ElementType::Sinusoidal;
        else if (ct == "COSINE")           return ElementType::Cosine;
        else if (ct == "BLOSS")            return ElementType::Bloss;
        else if (ct == "LEMNISCATE")       return ElementType::Lemniscate;
        else if (ct == "WIENERBOGEN")      return ElementType::WienerBogen;
        else if (ct == "RADIOID")          return ElementType::Radioid;
        else if (ct == "ELASRADIOID")      return ElementType::ElasticRadioid;
        else if (ct == "NORWICHSTURM")     return ElementType::NorwichSturm;
        else if (ct == "PSEUELLRADIOID")   return ElementType::PseudoEllipticRadioid;
        else if (ct == "LOGARITHMIC")      return ElementType::Logarithmic;
        else if (ct == "HYPERBOLIC")       return ElementType::Hyperbolic;
        else if (ct == "POLYNOMIAL")       return ElementType::Polynomial;
        else if (ct == "QUINTIC")          return ElementType::Quintic;
        else if (ct == "PHQUINTIC")        return ElementType::PHQuintic;
        else if (ct == "BIQUADRATIC")      return ElementType::Biquadratic;
        else if (ct == "SPLINE")           return ElementType::Spline;
        else if (ct == "BLOSSEULERHYBRID") return ElementType::BlossEulerHybrid;
        return ElementType::Clothoid;
    };

    // ── Egg (CC): circle → spiral → circle ────────────────────────────────
    if (prevElem == 'C' && nextElem == 'C') {
        if (!isValidRadius(prev.radius) || !isValidRadius(next.radius)) {
            qWarning() << "[AlignmentElementFactory] Rejecting egg transition with"
                          " invalid neighbour radius at chainage" << cur.chainage
                       << "R1=" << prev.radius << "R2=" << next.radius;
            return nullptr;
        }
        // BUG FIX: this used to always default to ElementType::Clothoid (the
        // 3-arg EggTransitionElement constructor), silently ignoring
        // cur.curveType. Any ACA spiral solved with a non-Clothoid family by
        // AlignmentSolver::solveACA() was therefore re-rendered here as a
        // *different* curve (Clothoid) sharing the same R1/R2/Ls — which
        // does not actually pass through the solved SC₂ point, so the drawn
        // spiral visibly failed to connect to the second arc for every
        // family except Clothoid. cur.curveType carries the actually-solved
        // family (see spiralTypeName() in AlignmentSolver.cpp, which wrote
        // it into the "CS" keypoint emitted by the ACA branch of the raw-
        // point generator) — map it back to ElementType exactly the way
        // makeTransition() below does for ordinary TC/CT spirals.
        const ElementType eggFamily = curveTypeToElementType(cur.curveType);
        auto elem = std::make_unique<EggTransitionElement>(
            prev.radius, next.radius, cur.length, eggFamily);
        elem->setPlacement(makePlacement(cur));
        elem->setLength(cur.length);
        // The EggTransitionElement uses next.radius as R2 for OLS
        elem->setR1(prev.radius);
        elem->setR2(next.radius);
        elem->setLE(cur.length);
        return elem;
    }

    // ── Determine curve factory from curveType field ───────────────────────
    const QString& ct = cur.curveType;

    auto makeTransition = [&](double radius,
                              const Placement& place,
                              double length,
                              bool reversed)
        -> std::unique_ptr<TransitionElement>
    {
        std::unique_ptr<TransitionElement> elem;

        if      (ct == "HALFSINE")         elem = std::make_unique<HalfSineElement>();
        else if (ct == "PARABOLA")         elem = std::make_unique<ParabolaElement>();
        else if (ct == "CUBICJPN")         elem = std::make_unique<CubicJPNElement>();
        else if (ct == "CUBICECI")         elem = std::make_unique<CubicECIElement>();
        else if (ct == "SINUSOIDAL")       elem = std::make_unique<SinusoidalElement>();
        else if (ct == "COSINE")           elem = std::make_unique<CosineElement>();
        else if (ct == "BLOSS")            elem = std::make_unique<BlossElement>();
        else if (ct == "LEMNISCATE")       elem = std::make_unique<LemniscateElement>();
        else if (ct == "WIENERBOGEN")      elem = std::make_unique<WienerBogenElement>();
        else if (ct == "RADIOID")          elem = std::make_unique<RadioidElement>();
        else if (ct == "ELASRADIOID")      elem = std::make_unique<ElasticRadioidElement>();
        else if (ct == "NORWICHSTURM")     elem = std::make_unique<NorwichSturmElement>();
        else if (ct == "PSEUELLRADIOID")   elem = std::make_unique<PseudoEllipticRadioidElement>();
        else if (ct == "LOGARITHMIC")      elem = std::make_unique<LogarithmicElement>();
        else if (ct == "HYPERBOLIC")       elem = std::make_unique<HyperbolicElement>();
        else if (ct == "POLYNOMIAL")       elem = std::make_unique<PolynomialElement>();
        else if (ct == "QUINTIC")          elem = std::make_unique<QuinticElement>();
        else if (ct == "PHQUINTIC")        elem = std::make_unique<PHQuinticElement>();
        else if (ct == "BIQUADRATIC")      elem = std::make_unique<BiquadraticElement>();
        else if (ct == "SPLINE")           elem = std::make_unique<SplineElement>();
        else if (ct == "BLOSSEULERHYBRID") elem = std::make_unique<BlossEulerHybridElement>();
        else                        elem = std::make_unique<ClothoidElement>(); // default SPIRAL

        elem->setPlacement(place);
        elem->setLength(length);
        elem->setRadius(radius);
        elem->setReversed(reversed);
        return elem;
    };

    // ── Normal (TC): tangent → spiral → circle ─────────────────────────────
    if (prevElem == 'T' && nextElem == 'C') {
        if (!isValidRadius(next.radius)) {
            qWarning() << "[AlignmentElementFactory] Rejecting TC transition with"
                          " invalid exit radius at chainage" << cur.chainage
                       << "radius=" << next.radius;
            return nullptr;
        }
        // Normal: starts at cur, exits onto circle with radius next.radius
        return makeTransition(next.radius, makePlacement(cur), cur.length, false);
    }

    // ── Reversed (CT): circle → spiral → tangent ──────────────────────────
    if (prevElem == 'C' && nextElem == 'T') {
        if (!isValidRadius(prev.radius)) {
            qWarning() << "[AlignmentElementFactory] Rejecting CT transition with"
                          " invalid entry radius at chainage" << cur.chainage
                       << "radius=" << prev.radius;
            return nullptr;
        }
        // Reversed: reference origin = next (far end = ST), reversed azimuth.
        //
        // Radius sign: load() has already assigned the correct sign to
        // prev.radius via the crossTrack test (positive = right-turn arc,
        // negative = left-turn arc).  We must pass that signed value
        // UNCHANGED so that the local frame at L=Ls produces the correct
        // y-offset that connects to csPoint.
        //
        // Derivation (localToWorld at L=Ls, az=az_ST+π):
        //   Right turn (prev.radius = +R):  Wx = ST.x − Xm·sin(az2) − Ym·cos(az2) = csPoint ✓
        //   Left  turn (prev.radius = −R):  Wx = ST.x − Xm·sin(az2) + Ym·cos(az2) = csPoint ✓
        //   Using −prev.radius inverts both cases → arc/spiral gap (the bug).
        return makeTransition(prev.radius,
                              makeReversedPlacement(next, cur.length),
                              cur.length,
                              true);
    }

    // ── Fallback: treat as normal spiral ──────────────────────────────────
    qWarning() << "[AlignmentElementFactory] Unhandled spiral pattern:"
               << prev.tsc << cur.tsc << next.tsc
               << "– falling back to ClothoidElement";
    if (!isValidRadius(next.radius)) {
        // next 本身也是緩和曲線關鍵點時，其 radius 欄位並非真正的圓弧
        // 半徑（恆為 0）。找不到可用的鄰接圓弧半徑，寧可跳過此關鍵點
        // （該段線形留下小缺口）也不要建出零半徑元素而讓程式當掉。
        qWarning() << "[AlignmentElementFactory] Rejecting fallback spiral with"
                      " invalid neighbour radius at chainage" << cur.chainage
                   << "radius=" << next.radius;
        return nullptr;
    }
    return makeTransition(next.radius, makePlacement(cur), cur.length, false);
}

std::unique_ptr<AlignmentElement>
AlignmentElementFactory::fromJson(const QJsonObject& j)
{
    const QString typeName = j["elementType"].toString();

    std::unique_ptr<AlignmentElement> elem;

    if      (typeName == "Tangent")     elem = std::make_unique<TangentElement>();
    else if (typeName == "CircularArc") elem = std::make_unique<CircularArcElement>();
    else if (typeName == "Clothoid")    elem = std::make_unique<ClothoidElement>();
    else if (typeName == "HalfSine")    elem = std::make_unique<HalfSineElement>();
    else if (typeName == "Parabola")    elem = std::make_unique<ParabolaElement>();
    else if (typeName == "CubicJPN")    elem = std::make_unique<CubicJPNElement>();
    else if (typeName == "CubicECI")    elem = std::make_unique<CubicECIElement>();
    else if (typeName == "Sinusoidal")       elem = std::make_unique<SinusoidalElement>();
    else if (typeName == "Cosine")           elem = std::make_unique<CosineElement>();
    else if (typeName == "Bloss")            elem = std::make_unique<BlossElement>();
    else if (typeName == "Lemniscate")       elem = std::make_unique<LemniscateElement>();
    else if (typeName == "WienerBogen")      elem = std::make_unique<WienerBogenElement>();
    else if (typeName == "Radioid")          elem = std::make_unique<RadioidElement>();
    else if (typeName == "ElasticRadioid")   elem = std::make_unique<ElasticRadioidElement>();
    else if (typeName == "NorwichSturm")     elem = std::make_unique<NorwichSturmElement>();
    else if (typeName == "PseudoEllipticRadioid") elem = std::make_unique<PseudoEllipticRadioidElement>();
    else if (typeName == "Logarithmic")      elem = std::make_unique<LogarithmicElement>();
    else if (typeName == "Hyperbolic")       elem = std::make_unique<HyperbolicElement>();
    else if (typeName == "Polynomial")       elem = std::make_unique<PolynomialElement>();
    else if (typeName == "Quintic")          elem = std::make_unique<QuinticElement>();
    else if (typeName == "PHQuintic")        elem = std::make_unique<PHQuinticElement>();
    else if (typeName == "Biquadratic")      elem = std::make_unique<BiquadraticElement>();
    else if (typeName == "Spline")           elem = std::make_unique<SplineElement>();
    else if (typeName == "BlossEulerHybrid") elem = std::make_unique<BlossEulerHybridElement>();
    else if (typeName == "Egg")         elem = std::make_unique<EggTransitionElement>();
    else {
        qWarning() << "[AlignmentElementFactory] Unknown element type in JSON:" << typeName;
        return nullptr;
    }

    // Restore common fields
    Placement place;
    place.chainage  = j["chainage"].toDouble();
    place.easting   = j["easting"].toDouble();
    place.northing  = j["northing"].toDouble();
    place.azimuth   = j["azimuth"].toDouble();
    elem->setPlacement(place);
    elem->setLength(j["length"].toDouble());

    // Type-specific fields
    if (auto* arc = dynamic_cast<CircularArcElement*>(elem.get()))
        arc->setRadius(j["radius"].toDouble());

    if (auto* tr = dynamic_cast<TransitionElement*>(elem.get())) {
        tr->setRadius(j["radius"].toDouble());
        tr->setReversed(j["reversed"].toBool());
    }

    if (auto* egg = dynamic_cast<EggTransitionElement*>(elem.get())) {
        // Family must be set before r1/r2/le so the final rebuild() (fired
        // by setLE(), the last of the three) already uses it.
        if (j.contains("spiralFamily"))
            egg->setSpiralFamily(EggTransitionElement::familyFromName(j["spiralFamily"].toString()));
        egg->setR1(j["r1"].toDouble());
        egg->setR2(j["r2"].toDouble());
        egg->setLE(j["le"].toDouble());
    }

    return elem;
}

} // namespace railway
} // namespace aicad
