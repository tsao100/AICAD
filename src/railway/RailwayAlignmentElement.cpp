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
    const double Ls = m_length;
     const double R  = m_reversed ? - m_radius :  m_radius;
    const double b  = 1.0 / (2.0 * R);
    const double la = M_PI / Ls;
    const double Ba = la * L;

    // θ = (1/2R)·(L − Ls/π·sin(πL/Ls))
    const double theta = b * (L - Ls / M_PI * std::sin(L / Ls * M_PI));

    // x (cubic approximation including b² corrections)
    const double x = L - b * b *
                             (2.0 * std::pow(Ba,3) - 12.0 * std::sin(Ba)
                              + 12.0 * Ba * std::cos(Ba)
                              - 3.0 * std::cos(Ba) * std::sin(Ba)
                              + 3.0 * Ba)
                             / (12.0 * std::pow(la, 3));

    // y
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

double CubicECIElement::solveCECI(double Ls, double R)
{
    // Bisection on φ ∈ [0,1]: Ls/(2R) = (tan(φ)/pow(1+tan²,1.5)) · poly(tan²)
    auto F = [&](double aa) {
        const double p  = std::tan(aa);
        const double p2 = p * p;
        return Ls / (2.0 * R)
               - (p / std::pow(1.0 + p2, 1.5))
                     * (1.0 + p2 / 10.0 - p2 * p2 / 72.0 + p2 * p2 * p2 / 208.0);
    };
    double a = 0.0, b = 1.0;
    while (std::abs(a - b) > 1e-10) {
        double c = 0.5 * (a + b);
        (F(a) * F(c) > 0.0) ? (a = c) : (b = c);
    }
    return 0.5 * (a + b);
}

void CubicECIElement::ensureCache() const
{
    if (m_bigFp >= 0.0) return;

    const double absR = std::abs(m_radius);
    const double Ls   = m_length;

    m_bigFp = solveCECI(Ls, absR);
    const double p  = std::tan(m_bigFp);
    const double p2 = p * p;
    m_bigX  = Ls / (1.0 + p2 / 10.0 - p2 * p2 / 72.0 + p2 * p2 * p2 / 208.0);
    m_bigA  = m_bigX * m_bigX / (2.0 * p);
}

double CubicECIElement::bigFp() const { ensureCache(); return m_bigFp; }
double CubicECIElement::bigX()  const { ensureCache(); return m_bigX;  }
double CubicECIElement::bigA()  const { ensureCache(); return m_bigA;  }

LocalFrame CubicECIElement::localFrame(double L) const
{
    const double absR = std::abs(m_radius);
    const double R = m_reversed ? - m_radius :  m_radius;
    // θ at L: re-solve CECI for partial length L (same equation, different Ls)
    const double theta_abs = solveCECI(L, absR);
    const double theta     = theta_abs * std::copysign(1.0, R);

    const double BA  = bigA();
    const double p   = std::tan(theta_abs);
    const double p2  = p * p;
    const double x   = L / (1.0 + p2 / 10.0 - p2 * p2 / 72.0 + p2 * p2 * p2 / 208.0);
    const double y   = (x * x * x) / (6.0 * BA) * std::copysign(1.0, R);

    return { x, y, theta };
}

QJsonObject CubicECIElement::toJson() const
{
    return TransitionElement::toJson();
}

// ============================================================================
//  EggTransitionElement
// ============================================================================

double EggTransitionElement::ols(double R1, double R2, double LE)
{
    const double aR1 = std::abs(R1), aR2 = std::abs(R2);
    const double diff = std::abs(aR1 - aR2);
    if (diff < 1e-9) return LE; // degenerate: equal radii
    return LE * std::max(aR1, aR2) / diff;
}

void EggTransitionElement::rebuild()
{
    if (!m_r1 || !m_r2 || !m_le) return;

    m_ls         = ols(m_r1, m_r2, m_le);
    m_r1Dominant = std::abs(m_r1) >= std::abs(m_r2);
    m_length     = m_le;

    // Build an internal ClothoidElement for the equivalent full spiral
    m_equiv = std::make_unique<ClothoidElement>();
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

    // ── Egg (CC): circle → spiral → circle ────────────────────────────────
    if (prevElem == 'C' && nextElem == 'C') {
        if (!isValidRadius(prev.radius) || !isValidRadius(next.radius)) {
            qWarning() << "[AlignmentElementFactory] Rejecting egg transition with"
                          " invalid neighbour radius at chainage" << cur.chainage
                       << "R1=" << prev.radius << "R2=" << next.radius;
            return nullptr;
        }
        auto elem = std::make_unique<EggTransitionElement>(
            prev.radius, next.radius, cur.length);
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

        if      (ct == "HALFSINE") elem = std::make_unique<HalfSineElement>();
        else if (ct == "PARABOLA") elem = std::make_unique<ParabolaElement>();
        else if (ct == "CUBICJPN") elem = std::make_unique<CubicJPNElement>();
        else if (ct == "CUBICECI") elem = std::make_unique<CubicECIElement>();
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
        egg->setR1(j["r1"].toDouble());
        egg->setR2(j["r2"].toDouble());
        egg->setLE(j["le"].toDouble());
    }

    return elem;
}

} // namespace railway
} // namespace aicad
