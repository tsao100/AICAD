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
    return p >= startChainage() - kTol && p <= endChainage() + kTol;
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
    // For reversed elements, the traversal is "backwards", so negate theta
    double dAz = m_reversed ? -localTheta : localTheta;
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
        double tangAz = normalise(az + (m_reversed ? -lf.theta : lf.theta));
        // Signed perpendicular: positive = left
        double dx = x - pt.x(), dy = y - pt.y();
        // Rotate (dx,dy) into local frame of this tangent
        //   left component = dx * (-cos(az)) + dy * sin(az)  [for tangent along az]
        double leftComp = -dx * std::cos(tangAz) + dy * std::sin(tangAz);
        return leftComp;
    };

    // Along-track component (used to seed the iteration)
    auto along = [&](double L) -> double {
        LocalFrame lf = localFrame(L);
        QPointF pt    = localToWorld(lf, 0.0);
        double tangAz = normalise(az + (m_reversed ? -lf.theta : lf.theta));
        double dx = x - pt.x(), dy = y - pt.y();
        return dx * std::sin(tangAz) + dy * std::cos(tangAz);
    };

    // Estimate starting L from along-track projection at the two ends
    double pA = perp(0.0),      pB = perp(m_length);
    double aA = along(0.0),     aB = along(m_length);
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
    double tangAz = normalise(az + (m_reversed ? -lf.theta : lf.theta));
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
    const double R  = m_radius;

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
    const double R  = m_radius;
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
    const double R   = m_radius;
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
    const double R  = m_radius;

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
    // θ at L: re-solve CECI for partial length L (same equation, different Ls)
    const double theta_abs = solveCECI(L, absR);
    const double theta     = theta_abs * std::copysign(1.0, m_radius);

    const double BA  = bigA();
    const double p   = std::tan(theta_abs);
    const double p2  = p * p;
    const double x   = L / (1.0 + p2 / 10.0 - p2 * p2 / 72.0 + p2 * p2 * p2 / 208.0);
    const double y   = (x * x * x) / (6.0 * BA) * std::copysign(1.0, m_radius);

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

// ── Derive the equivalent spiral's placement from the egg element's own ──────

/**
 * The original C# BEXY computes the "virtual origin" of the equivalent spiral
 * by walking backwards (LS−LE) along the equivalent spiral from the egg start.
 *
 * We replicate this here using the ClothoidElement's localFrame method.
 */
static Placement eggEquivPlacement(const EggTransitionElement& egg,
                                   const ClothoidElement& equiv)
{
    const double ls   = egg.ls();
    const double le   = egg.le();
    const bool  r1dom = egg.r1Dominant();
    const double R1   = egg.r1(), R2 = egg.r2();

    const Placement& ep = egg.placement();
    const double az0    = ep.azimuth;

    Placement result;

    if (r1dom) {
        // Equivalent spiral runs from virtual origin to SC (current end + (ls−le) extra)
        // Origin azimuth = ep.az − (ls−le)²/(2·ls·R2)
        const double dAz = std::pow(ls - le, 2) / (2.0 * ls * R2);
        result.azimuth   = az0 - dAz;

        // Position: go back (ls−le) along equivalent spiral to find origin
        LocalFrame lfBack = equiv.localFrame(ls - le);
        // The egg element's start (ep) is at local position (lfBack.x, lfBack.y)
        // in the equivalent-spiral frame.  Invert to find the equiv origin.
        // world_origin = ep - rotate(lfBack, az0−dAz)
        const double azR = result.azimuth;
        result.easting   = ep.easting
                         - (lfBack.x * std::sin(azR) + lfBack.y * std::cos(azR));
        result.northing  = ep.northing
                          - (lfBack.x * std::cos(azR) - lfBack.y * std::sin(azR));
        result.chainage  = ep.chainage - (ls - le);   // virtual start chainage

    } else {
        // R2 dominant: equivalent spiral runs from egg end backwards
        const double dAz = std::pow(ls, 2) / (2.0 * ls * R1) + M_PI;
        result.azimuth   = az0 + dAz;

        LocalFrame lfFull = equiv.localFrame(ls);
        const double azR  = result.azimuth;
        result.easting    = ep.easting
                         + (lfFull.x * std::sin(azR) + lfFull.y * std::cos(azR));
        result.northing   = ep.northing
                          + (lfFull.x * std::cos(azR) - lfFull.y * std::sin(azR));
        result.chainage   = ep.chainage - ls;
    }
    return result;
}

LocalFrame EggTransitionElement::localFrame(double L) const
{
    // Delegate to the internal equivalent spiral, evaluated at the right offset.
    if (!m_equiv) return {};

    const double le  = m_le;
    const double ls  = m_ls;

    double Lequiv;
    if (m_r1Dominant) {
        // Normal direction: local L in egg maps to (ls−le+L) in equiv spiral
        Lequiv = (ls - le) + L;
    } else {
        // Reversed direction: local L in egg maps to (ls−L) in equiv spiral, w negated
        Lequiv = ls - L;
    }
    return m_equiv->localFrame(Lequiv);
}

QPointF EggTransitionElement::worldXY(double p, double w) const
{
    if (!m_equiv) return {};

    const double le  = m_le;
    const double ls  = m_ls;

    // Compute placement for the equivalent spiral
    Placement equivPlace = eggEquivPlacement(*this, *m_equiv);
    m_equiv->setPlacement(equivPlace);

    double L_local = p - startChainage();

    double Lequiv;
    double w_eff;
    if (m_r1Dominant) {
        Lequiv = (ls - le) + L_local;
        w_eff  = w;
    } else {
        Lequiv = ls - L_local;
        w_eff  = -w;   // Reversed traverse → negate offset
    }

    // Use equiv element's own worldXY from its adjusted placement
    double p_equiv = equivPlace.chainage + Lequiv;
    return m_equiv->worldXY(p_equiv, w_eff);
}

QPointF EggTransitionElement::inversePW(double x, double y) const
{
    if (!m_equiv) return {};

    Placement equivPlace = eggEquivPlacement(*this, *m_equiv);
    m_equiv->setPlacement(equivPlace);

    // Let the equivalent spiral solve the inverse
    QPointF pw_equiv = m_equiv->AlignmentElement::inversePW(x, y);

    const double le = m_le, ls = m_ls;
    double L_local;
    double w_out;

    if (m_r1Dominant) {
        L_local = pw_equiv.x() - equivPlace.chainage - (ls - le);
        w_out   = pw_equiv.y();
    } else {
        L_local = ls - (pw_equiv.x() - equivPlace.chainage);
        w_out   = -pw_equiv.y();
    }

    return { startChainage() + L_local, w_out };
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
    // (matching the C# switch: prev.tsc[1] + next.tsc[1])
    const QChar prevElem = (prev.tsc.size() >= 2) ? prev.tsc[1] : QChar('T');
    const QChar nextElem = (next.tsc.size() >= 2) ? next.tsc[1] : QChar('T');

    // ── Egg (CC): circle → spiral → circle ────────────────────────────────
    if (prevElem == 'C' && nextElem == 'C') {
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
        // Normal: starts at cur, exits onto circle with radius next.radius
        return makeTransition(next.radius, makePlacement(cur), cur.length, false);
    }

    // ── Reversed (CT): circle → spiral → tangent ──────────────────────────
    if (prevElem == 'C' && nextElem == 'T') {
        // Reversed: reference origin = next (far end), reversed azimuth
        // Radius = −prev.radius (negated for reversed traverse; C# SREVxy)
        return makeTransition(-prev.radius,
                              makeReversedPlacement(next, cur.length),
                              cur.length,
                              true);
    }

    // ── Fallback: treat as normal spiral ──────────────────────────────────
    qWarning() << "[AlignmentElementFactory] Unhandled spiral pattern:"
               << prev.tsc << cur.tsc << next.tsc
               << "– falling back to ClothoidElement";
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
