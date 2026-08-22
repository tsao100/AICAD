/**
 * @file RailwayAlignmentElement.h
 * @brief Polymorphic element class hierarchy for railway horizontal alignment.
 *
 * Class hierarchy
 * ───────────────
 *   AlignmentElement          (abstract – geometry contract + world transform)
 *   ├── TangentElement        (straight T element)
 *   ├── CircularArcElement    (circular C element)
 *   └── TransitionElement     (abstract – adds spiral-specific interface)
 *       ├── ClothoidElement   (Euler / Clothoid spiral – "SPIRAL")
 *       ├── HalfSineElement   (half-sine spiral        – "HALFSINE")
 *       ├── ParabolaElement   (cubic parabola          – "PARABOLA")
 *       ├── CubicJPNElement   (Japanese cubic parabola – "CUBICJPN")
 *       ├── CubicECIElement   (CECI cubic parabola     – "CUBICECI")
 *       └── EggTransitionElement  (bi-quadratic / compound spiral – CC)
 *
 * Coordinate conventions
 * ──────────────────────
 *   Chainage p  [m]   – distance along centreline from alignment start
 *   Offset   w  [m]   – perpendicular; POSITIVE = LEFT of direction of travel
 *   Azimuth  a  [rad] – clockwise from geographic North
 *   Radius   R  [m]   – signed; POSITIVE = RIGHT-hand curve
 *
 * Local frame (origin at element start, x along initial tangent, y rightward)
 * ────────────────────────────────────────────────────────────────────────────
 *   x      [m]   along-track local coordinate
 *   y      [m]   cross-track local coordinate (positive = RIGHT)
 *   theta  [rad] cumulative tangent rotation (positive = right turn)
 *
 * World transform (derived once in the base class)
 * ────────────────────────────────────────────────
 *   Wx = ox + X1·sin(az) + Y1·cos(az)
 *   Wy = oy + X1·cos(az) − Y1·sin(az)
 *
 *   where   X1 = x − w·sin(θ)      (w sign-flipped: public API left+, internal right+)
 *           Y1 = y + w·cos(θ)
 *
 * @author AICAD Team
 * @date   2025-01
 */

#pragma once

#include <QString>
#include <QPointF>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>
#include <cmath>
#include <limits>

namespace aicad {
namespace railway {

// ============================================================================
//  Forward declarations
// ============================================================================

struct AlignmentPoint;   // defined in RailwayAlignment.h

// ============================================================================
//  Supporting types
// ============================================================================

/** Element category. */
enum class ElementType {
    Tangent,
    CircularArc,
    Clothoid,
    HalfSine,
    Parabola,
    CubicJPN,
    CubicECI,
    Sinusoidal,        ///< Sine-ramp curvature profile ("Klein" style)
    Cosine,            ///< Raised-cosine curvature ramp
    Bloss,             ///< Bloss cubic (smoothstep) curvature ramp
    Lemniscate,        ///< Lemniscate-style convex curvature ramp
    WienerBogen,       ///< Wiener Bogen (Vienna curve) septic curvature ramp
    Radioid,           ///< Radioid concave curvature ramp
    ElasticRadioid,        ///< Elastic curve (elastica): kappa(x)=2x/a^2
    NorwichSturm,          ///< Norwich/Sturm spiral: kappa=1/r (r=dist to pole)
    PseudoEllipticRadioid, ///< Pseudo-elliptic radioid: y=a*gd^-1(x/a)
    Logarithmic,       ///< Logarithmic curvature ramp
    Hyperbolic,        ///< Hyperbolic-tangent curvature ramp
    Polynomial,        ///< Plain cubic-power curvature ramp
    Quintic,           ///< Quintic (5th-order) smoothstep curvature ramp
    PHQuintic,         ///< Pythagorean-Hodograph quintic spiral (Walton & Meek)
    Biquadratic,       ///< Quartic-power curvature ramp
    Spline,            ///< Piecewise cubic-Hermite curvature ramp
    BlossEulerHybrid,  ///< 50/50 blend of Bloss and linear (Euler) ramps
    Egg          ///< Bi-quadratic (compound) spiral between two circles
};

/**
 * @brief Local-frame coordinates at a distance L from element start.
 *
 * The local frame has its origin at the element's start point, its x-axis
 * along the initial tangent, and its y-axis 90° clockwise (rightward).
 */
struct LocalFrame {
    double x;      ///< Along-tangent coordinate [m]
    double y;      ///< Cross-track coordinate, positive = right [m]
    double theta;  ///< Cumulative tangent angle (positive = right turn) [rad]
};

/**
 * @brief World placement of an element's reference point.
 *
 * For normal elements: the reference is the start.
 * For reversed (CT) spirals: the reference is the far end, azimuth+π.
 */
struct Placement {
    double chainage  = 0.0;  ///< Chainage of the reference point [m]
    double easting   = 0.0;  ///< Easting of the reference point [m]
    double northing  = 0.0;  ///< Northing of the reference point [m]
    double azimuth   = 0.0;  ///< Tangent azimuth at the reference point [rad]
};

// ============================================================================
//  AlignmentElement – abstract base
// ============================================================================

/**
 * @brief Abstract base class for all horizontal alignment elements.
 *
 * Concrete subclasses implement @c localFrame(L) which returns the
 * element's geometry in a normalised local coordinate system.  The base
 * class provides the world-space transform, azimuth query, JSON
 * serialisation skeleton, and a default iterative inverse solver.
 */
class AlignmentElement
{
public:
    virtual ~AlignmentElement() = default;

    // ── Identity ──────────────────────────────────────────────────────────────

    virtual ElementType type()     const = 0;
    virtual QString     typeName() const = 0;

    // ── Parameters ────────────────────────────────────────────────────────────

    const Placement& placement() const { return m_place; }
    void setPlacement(const Placement& p) { m_place = p; }

    double length()         const { return m_length; }
    void   setLength(double l)    { m_length = l; }

    /** Chainage at the start (smallest chainage) of this element. */
    double startChainage() const;
    /** Chainage at the end   (largest  chainage) of this element. */
    double endChainage()   const;

    /** True if this element covers chainage p. */
    bool   contains(double p) const;

    // ── Core geometry: implement in each subclass ─────────────────────────────

    /**
     * @brief Local-frame coordinates at distance @p L from the reference point.
     * @param L  Arc-length from the reference (always positive, [0, length()]).
     */
    virtual LocalFrame localFrame(double L) const = 0;

    // ── World-space queries (implemented in base class) ───────────────────────

    /**
     * @brief World (Easting, Northing) at chainage @p p, offset @p w.
     * @param p  Chainage [m]
     * @param w  Lateral offset [m]; POSITIVE = LEFT of direction of travel.
     *
     * Virtual: kept virtual so EggTransitionElement can override it (see
     * that class). Historically the override had to recompute an internal
     * equivalent-spiral placement (eggEquivPlacement()) because
     * EggTransitionElement::localFrame() was not self-anchored at its own
     * physical start; that bug is fixed as of the Phase 5 localFrame() fix
     * (see RailwayAlignmentElement.cpp), so EggTransitionElement::worldXY()
     * now just delegates straight back to this base implementation. Kept
     * `virtual` regardless so a future subclass with a genuine placement
     * quirk isn't silently bypassed when held through a base
     * AlignmentElement& (elementAt(), TrackCenterLine's element list, etc.).
     */
    virtual QPointF worldXY(double p, double w = 0.0) const;

    /**
     * @brief Tangent azimuth [rad, CW from N] at chainage @p p.
     *
     * Virtual for the same reason as worldXY() above. EggTransitionElement's
     * override now simply delegates here too (see that class and the
     * Phase 5 localFrame() fix note in RailwayAlignmentElement.cpp).
     */
    virtual double worldAzimuth(double p) const;

    // ── Inverse solver ────────────────────────────────────────────────────────

    /**
     * @brief Find (chainage, offset) for world point (@p x, @p y).
     *
     * Default implementation: iterative Newton bisection on the
     * perpendicular-distance condition.  O(n_iter) with typically < 30 steps.
     * Override for analytic solutions (T and C elements).
     *
     * @return QPointF(p, w) where w > 0 means the point is to the LEFT.
     */
    virtual QPointF inversePW(double x, double y) const;

    // ── Serialisation ─────────────────────────────────────────────────────────

    virtual QJsonObject toJson() const;

    // ── Utilities ─────────────────────────────────────────────────────────────

    static double normalise(double a);  ///< Wrap angle to [0, 2π)

protected:
    Placement m_place;
    double    m_length = 0.0;

    /** True when traversal is "backwards" (CT reversed spiral). */
    bool      m_reversed = false;

    /**
     * @brief Resolve chainage @p p to a local L and effective offset w_eff.
     *
     * Accounts for reversed elements: reversed elements have their reference
     * at the far (high-chainage) end and are traversed with decreasing L.
     */
    void resolvePW(double p, double w,
                   double& L_out, double& w_eff_out) const;

    /**
     * @brief Convert local L and w_eff back to (p, w) accounting for reversal.
     */
    QPointF unresolveLocalPW(double L_local, double w_local) const;

    /** Convenience: convert local frame to world. */
    QPointF localToWorld(const LocalFrame& lf, double w_eff) const;

    static constexpr double kTol    = 1e-9;
    static constexpr double kAngTol = 1e-7;
    static constexpr double kInf    = std::numeric_limits<double>::infinity();

    /** 樁號邊界比對容差（非浮點運算誤差容差，用途不同於 kTol）。
     *  舊系統 ALD 資料的樁號 (chainage) 與長度 (length) 各自獨立以
     *  ASCII 文字四捨五入寫入（通常到公釐等級），相鄰兩元素之間可能因此
     *  產生次公釐等級（實測 GM01/YT01 樣本資料最大約 0.12 mm）的銜接
     *  落差，遠大於 kTol (1e-9)。contains() 採用 1 mm 作為邊界容差，
     *  避免查詢端點樁號時落在此落差區間而得到「找不到元素」的結果。 */
    static constexpr double kChainageTol = 1e-3;

    /** 曲線幾何公式下限半徑（低於此值一律視為退化為 0，予以修正）。
     *  1 mm 已遠小於任何實際鐵路曲線半徑，僅用於偵測資料異常。 */
    static constexpr double kMinRadius = 1e-3;
    /** 修正退化半徑時代入的極大值（視為近似直線，曲率≈0）。 */
    static constexpr double kMaxRadius = 1.0e9;

    /**
     * @brief 修正退化（零或極小）半徑，避免 CircularArcElement／
     *        TransitionElement 各家公式中 1/R、L/R 等除以半徑的運算
     *        產生 Infinity/NaN。
     *
     * 目前唯一已知會產生 0 半徑的情況：AlignmentElementFactory 在無法
     * 依 TC/CT/CC 規則判斷緩和曲線鄰接型態時（例如連續多段緩和曲線的
     * 複合／反向曲線，鄰近關鍵點本身也是緩和曲線而非圓弧），退回使用
     * 鄰近關鍵點的 radius 欄位，但該欄位對緩和曲線關鍵點而言並非真正
     * 的圓弧半徑（值為 0）。此處在寫入 m_radius 前一律做防呆修正，讓
     * 這類資料異常退化為近似直線，而不是讓程式因浮點例外而當掉。
     */
    static double clampRadius(double r);
};

// ============================================================================
//  TangentElement
// ============================================================================

/**
 * @brief Straight-line (T) element.
 *
 * Local frame: x = L, y = 0, theta = 0.  Trivially exact.
 * Inverse: analytic via perpendicular projection.
 */
class TangentElement : public AlignmentElement
{
public:
    TangentElement() = default;

    ElementType type()     const override { return ElementType::Tangent; }
    QString     typeName() const override { return QStringLiteral("Tangent"); }

    LocalFrame  localFrame(double L)           const override;
    QPointF     inversePW (double x, double y) const override;
    QJsonObject toJson()                        const override;
};

// ============================================================================
//  CircularArcElement
// ============================================================================

/**
 * @brief Constant-radius circular arc (C) element.
 *
 * Local frame:
 *   x     = R · sin(L/R)
 *   y     = R · (1 − cos(L/R))
 *   theta = L/R
 *
 * Sign convention: R > 0 = right-hand curve, R < 0 = left-hand curve.
 * Inverse: analytic – projects world point onto the arc via the circle centre.
 */
class CircularArcElement : public AlignmentElement
{
public:
    CircularArcElement() = default;
    explicit CircularArcElement(double radius) : m_radius(clampRadius(radius)) {}

    ElementType type()     const override { return ElementType::CircularArc; }
    QString     typeName() const override { return QStringLiteral("CircularArc"); }

    double radius() const { return m_radius; }
    void   setRadius(double r) { m_radius = clampRadius(r); }

    LocalFrame  localFrame(double L)           const override;
    QPointF     inversePW (double x, double y) const override;
    QJsonObject toJson()                        const override;

    /**
     * @brief AutoCAD LWPOLYLINE bulge factor for an arc of length @p arcLen.
     *
     * bulge = −tan(Δ/4) where Δ = arcLen/|R|.
     */
    double bulge(double arcLen) const;

    /** World position of the arc centre. */
    QPointF centreXY() const;

private:
    double m_radius = 0.0;   ///< Signed radius [m]
};

// ============================================================================
//  TransitionElement – abstract base for all spiral families
// ============================================================================

/**
 * @brief Abstract base for all transition-curve (spiral) element types.
 *
 * Subclasses implement @c localFrame(L) using their respective mathematical
 * formulae.  The traversal direction (normal TC vs reversed CT) is encoded
 * in the base-class @c m_reversed flag; reversed elements negate L and w
 * before calling @c localFrame and adjust the returned (p, w) accordingly.
 *
 * @par Normal (TC) spiral:
 *   Reference origin = start point; m_reversed = false.
 *   L increases from 0 at TC to length() at SC.
 *
 * @par Reversed (CT) spiral:
 *   Reference origin = far (SC→ST) end; m_reversed = true.
 *   The base-class resolvePW() computes L = reference_chainage − p so that
 *   the spiral formula is evaluated in the direction of decreasing curvature.
 */
class TransitionElement : public AlignmentElement
{
public:
    TransitionElement() = default;

    /** Equivalent exit radius.  |radius| → ∞ means exiting onto a tangent. */
    double radius() const { return m_radius; }
    void   setRadius(double r) { m_radius = clampRadius(r); }

    bool   isReversed() const { return m_reversed; }
    void   setReversed(bool r) { m_reversed = r; }

    QJsonObject toJson() const override;

protected:
    double m_radius = 0.0;   ///< Signed exit radius [m] (∞ on CT spirals)
};

// ============================================================================
//  ClothoidElement  (Euler spiral / Cornu spiral – "SPIRAL")
// ============================================================================

/**
 * @brief Euler–Cornu clothoid.  Curvature increases linearly with arc length.
 *
 *   θ(L)  = L² / (2·A²)     where A² = |R|·Ls
 *   x(L)  = L · Σ (−1)ⁿ θⁿ / (4n+1) / (2n)!       (power-series in θ)
 *   y(L)  = L · θ · Σ (−1)ⁿ θⁿ / (4n+3) / (2n+1)!
 *
 * Series truncated at θ⁶ (sufficient for |L/R| ≤ π, typical in practice).
 */
class ClothoidElement : public TransitionElement
{
public:
    ClothoidElement() = default;

    ElementType type()     const override { return ElementType::Clothoid; }
    QString     typeName() const override { return QStringLiteral("Clothoid"); }

    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;
};

// ============================================================================
//  HalfSineElement  ("HALFSINE")
// ============================================================================

/**
 * @brief Half-sine (cosine) transition spiral.  Curvature follows a half-cycle
 *        of a raised-cosine function:  κ(L) = (1/2R)·(1 − cos(πL/Ls)).
 */
class HalfSineElement : public TransitionElement
{
public:
    HalfSineElement() = default;

    ElementType type()     const override { return ElementType::HalfSine; }
    QString     typeName() const override { return QStringLiteral("HalfSine"); }

    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;
};

// ============================================================================
//  ParabolaElement  ("PARABOLA")
// ============================================================================

/**
 * @brief Cubic parabola (approximation of Euler spiral in track coordinates).
 *
 *   x(L) ≈ L − L⁵ / (40·R²·Ls²)
 *   y(L)  = x³ / (6·R·XB)    where XB = Ls − Ls³/(40R²)
 */
class ParabolaElement : public TransitionElement
{
public:
    ParabolaElement() = default;

    ElementType type()     const override { return ElementType::Parabola; }
    QString     typeName() const override { return QStringLiteral("Parabola"); }

    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;

private:
    /** x-coordinate at the arc end (BigX equivalent). */
    double xEnd() const;
};

// ============================================================================
//  CubicJPNElement  ("CUBICJPN" – Japanese cubic parabola)
// ============================================================================

/**
 * @brief Japanese cubic parabola (JIS E 1301).
 *
 * The "BigX" anchor value satisfies a transcendental equation solved via
 * bisection (BIGFXJPN in the original C#).  Then for any L:
 *   x(L) solved by another bisection (FXJPN)
 *   y(L) = x(L)³ / (6·R·BigX)
 *   θ(L) = atan(x²/(2·R·BigX))
 */
class CubicJPNElement : public TransitionElement
{
public:
    CubicJPNElement() = default;

    ElementType type()     const override { return ElementType::CubicJPN; }
    QString     typeName() const override { return QStringLiteral("CubicJPN"); }

    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;

private:
    /** BigX (computed from Ls and |R|). Cached lazily. */
    mutable double m_bigX = -1.0;
    double bigX() const;

    static double solveBigX(double Ls, double R);
    static double solveFx  (double BigX, double L, double R);
};

// ============================================================================
//  CubicECIElement  ("CUBICECI" – CECI Engineering Consultants, Inc. cubic
//  parabola — 中華顧問工程司／台灣世曦工程顧問, per the theoretical formula
//  you sourced)
// ============================================================================

/**
 * @brief CECI cubic parabola — the classic cubic-parabola transition curve:
 *
 *          y = x³ / (6·R·Ls)
 *
 *        with x found by inverting the arc-length series (per CECI's
 *        published 4-term approximation), NOT by taking x=L directly:
 *
 *          l(x) = x·[ 1 + x⁴/(40R²Ls²) − x⁸/(1152R⁴Ls⁴) + x¹²/(13312R⁶Ls⁶) ]
 *
 *        derived here by expanding l(x) = ∫₀ˣ√(1+y'(u)²) du as a Taylor
 *        series in x and factoring out the leading term (matches the
 *        "x(1+...four terms)" form you described from the CECI reference —
 *        verified in chat against direct numerical arc-length integration:
 *        max error ~1e-8 vs. a naive single-term correction's ~1e-3 to 1e-4
 *        at typical railway curvature). x(L) is solved from this by simple
 *        fixed-point iteration (5 iterations; converges in ~2–3 for
 *        realistic Ls/R since the correction terms are small).
 *
 *        θ(L) = atan(x²/(2·R·Ls)), the exact slope of y=f(x) above.
 */
class CubicECIElement : public TransitionElement
{
public:
    CubicECIElement() = default;

    ElementType type()     const override { return ElementType::CubicECI; }
    QString     typeName() const override { return QStringLiteral("CubicECI"); }

    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;
};

// ============================================================================
//  Curvature-ramp transition families ("SINUSOIDAL" … "BLOSSEULERHYBRID")
// ============================================================================
//
//  All thirteen classes below share one mathematical shape: curvature grows
//  monotonically from 0 (tangent end) to 1/R (circular-arc end) following a
//  normalised profile g(t), t = L/Ls ∈ [0,1], g(0)=0, g(1)=1 — i.e.
//
//      κ(L) = g(L/Ls) / R
//
//  Unlike ClothoidElement/HalfSineElement/etc. (which use hand-derived
//  closed-form or power-series formulae specific to each curve family),
//  these classes are evaluated via a single shared numerical integrator
//  (integrateCurvatureRamp(), file-local to RailwayAlignmentElement.cpp)
//  that integrates κ(L) directly to obtain (x, y, theta). Each subclass only
//  supplies its g(t) shape function in localFrame(); see that function's
//  doc comment in the .cpp for the exact formula and its rationale.
//
//  This keeps every new family self-anchored at L=0 (per the localFrame()
//  contract documented on AlignmentElement::localFrame), exact at both
//  endpoints (κ(0)=0, κ(Ls)=1/R, matching the adjoining tangent/arc), and
//  numerically accurate to well below chainage/coordinate tolerances used
//  elsewhere in this file (kTol, kChainageTol) for any realistic transition
//  length. As with the other TransitionElement subclasses, m_radius is the
//  *signed exit* radius and m_reversed flips its sign for CT spirals before
//  the shape function is evaluated.
// ============================================================================

/** @brief Sine-ramp curvature profile: g(t) = t − sin(2πt)/(2π). Zero slope
 *         at both ends (jerk-free at both the tangent and the arc). */
class SinusoidalElement : public TransitionElement
{
public:
    SinusoidalElement() = default;
    ElementType type()     const override { return ElementType::Sinusoidal; }
    QString     typeName() const override { return QStringLiteral("Sinusoidal"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;
};

/** @brief MÁV (Hungarian State Railways) "Cosine Transition Curve" —
 *         Cartesian y=f(x) approximation (x treated as arc length):
 *
 *         y(x) = x²/(4R) − Ls²/(2π²R)·(1 − cos(πx/Ls))
 *
 *         Genuinely distinct from HalfSineElement despite sharing the same
 *         underlying curvature law (y''(x) is identical to HalfSine's κ(s));
 *         the two differ because this class uses the classical small-
 *         deflection Cartesian shortcut (same family as ParabolaElement/
 *         CubicJPNElement/CubicECIElement above) rather than exact
 *         arc-length parametric integration. Independently corroborated by
 *         a reported real-world case reconstructing a MÁV alignment in
 *         Civil 3D, where the closest available type ("Sine Half-Wavelength
 *         Diminishing Tangent" = HalfSine) measurably diverged from the true
 *         Cosine geometry (~5mm within the curve body, ~112mm at the
 *         endpoints) — see chat discussion for the source. */
class CosineElement : public TransitionElement
{
public:
    CosineElement() = default;
    ElementType type()     const override { return ElementType::Cosine; }
    QString     typeName() const override { return QStringLiteral("Cosine"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;
};

/** @brief Classic Bloss cubic (smoothstep) curvature ramp:
 *         g(t) = 3t² − 2t³. Zero slope at both ends. */
class BlossElement : public TransitionElement
{
public:
    BlossElement() = default;
    ElementType type()     const override { return ElementType::Bloss; }
    QString     typeName() const override { return QStringLiteral("Bloss"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;
};

/** @brief Lemniscate transition curve — the true Bernoulli lemniscate
 *         geometry (rational parametrization, arc-length inverted via
 *         bisection), per your supplied reference implementation, with two
 *         corrections verified in chat:
 *          1. The true origin (κ=0) is at parameter t=±π/2, not t=±π/4 as
 *             commented in the reference code — verified: (x,y) at t=−π/4
 *             is (6.667,−4.714) for a=10, not the origin.
 *          2. The reference code's curvature formula κ=3r/a² is off by a
 *             factor of 2 — cross-checked against the fundamental formula
 *             κ=(x'y″−y'x″)/(x'²+y'²)^1.5, the correct closed form for this
 *             parametrization is κ=1.5·r/a².
 *
 *         Rational parametrization (radius a, parameter t):
 *           x(t) = a√2·cos(t) / (1+sin²t),  y(t) = a√2·sin(t)·cos(t) / (1+sin²t)
 *         with curvature κ = 1.5r/a² where r = √(x²+y²) (see correction #2
 *         above). The true origin (r=0) is at t=±π/2 (see correction #1).
 *         This class starts at t=π/2 (the real origin, κ=0) and moves toward
 *         the vertex t=0 (maximum curvature 3√2/a), which is the portion
 *         classical transition-curve practice actually uses.
 *
 *         Unlike every other TransitionElement in this file, the lemniscate
 *         has no simple κ(s)=g(s/Ls)/R scaling — the free shape parameter
 *         `a` must itself be solved for per (Ls,R) pair, since κ(Ls)·Ls/R is
 *         a function of the reduced parameter τ alone (scale-invariant).
 *         localFrame() therefore does a one-time root-find for `a` and the
 *         end parameter on first use (cached in mutable members, matching
 *         Ls/R signature), then a per-query bisection for τ(L) — mirroring
 *         the two-stage bisection structure of your reference code, but with
 *         an outer solve added so κ(0)=0 and κ(Ls)=1/R hold exactly (the
 *         TransitionElement contract every other class in this file follows).
 *         The classical Bernoulli-lemniscate property that the tangent at
 *         the origin makes a 45° angle (verified numerically) is used to
 *         align the local frame so θ(0)=0 as usual. ensureSolved() clamps
 *         the achievable Ls/|R| to just under the curve's own maximum
 *         κ(τ)·s(τ) product (reached at the vertex, τ=π/2) rather than a
 *         fixed angular limit — the tangent sweeps roughly 135° from origin
 *         to vertex in total (45°→−90°, not the 45° this comment originally
 *         assumed; the vertex tangent is vertical, not horizontal — see
 *         chat verification), so realistic railway Ls/R ratios are nowhere
 *         near this bound. */
class LemniscateElement : public TransitionElement
{
public:
    LemniscateElement() = default;
    ElementType type()     const override { return ElementType::Lemniscate; }
    QString     typeName() const override { return QStringLiteral("Lemniscate"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;

private:
    // Lazily-computed, (Ls,R)-dependent cache for the outer "solve for a and
    // tau_end" step (expensive; independent of the query point L, so this is
    // computed once per distinct (Ls,R) pair rather than on every call).
    mutable bool   m_cacheValid = false;
    mutable double m_cachedLs   = 0.0;
    mutable double m_cachedR    = 0.0;
    mutable double m_a          = 0.0;   // solved lemniscate scale parameter
    mutable double m_tauEnd     = 0.0;   // solved end parameter (0..pi/2)
    void ensureSolved() const;           // (re)populates the cache above
};

/** @brief Wiener Bogen (Vienna curve) — Hasslinger's original formula
 *         (patent EP1523597B1; see TransitionCurveLiteratureReferences.md):
 *
 *           κ(l) = κ₁ + (κ₂−κ₁)·f(l) − h·(ψ₂−ψ₁)·f″(l)
 *
 *         where f(l)=g(l/Ls) is the septic smootherstep
 *         g(t)=35t⁴−84t⁵+70t⁶−20t⁷ (zero 1st AND 2nd derivative at both
 *         ends — the "most common" f() per the patent), and the second term
 *         is a roll-dynamics correction (h=vehicle centre-of-gravity
 *         height, ψ=cant/superelevation) that is entirely absent from a
 *         plain curvature-ramp — it is this term that produces the
 *         "additional bends" (locally non-monotonic curvature) literature
 *         identifies as the Wiener Bogen's defining, unique characteristic
 *         among transition curves.
 *
 *         h and ψ₂ are NOT yet exposed as user-settable parameters — this
 *         codebase's TransitionElement contract only carries R and Ls, with
 *         no cant/vehicle-dynamics data model. Fixed representative
 *         defaults are used instead (h=2.0m, ψ₂=0.10rad≈150mm/1435mm
 *         gauge) — see the .cpp for the full discussion. This is therefore
 *         a documented approximation of the true, per-project-parameterised
 *         curve, not an exact implementation. */
class WienerBogenElement : public TransitionElement
{
public:
    WienerBogenElement() = default;
    ElementType type()     const override { return ElementType::WienerBogen; }
    QString     typeName() const override { return QStringLiteral("WienerBogen"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;
};

/** @brief Radioid concave curvature ramp: g(t) = 2t − t² = 1 − (1−t)².
 *         Mirror image of LemniscateElement — rapid initial curvature
 *         growth that eases into the circular arc. */
class RadioidElement : public TransitionElement
{
public:
    RadioidElement() = default;
    ElementType type()     const override { return ElementType::Radioid; }
    QString     typeName() const override { return QStringLiteral("Radioid"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;
};

// ============================================================================
//  Three specific named members of the "radioid" family (mathcurve.com,
//  Nördling 1867 / Fargue 1868 / Turrière 1939) — genuinely distinct curves
//  from the generic RadioidElement above, verified against
//  <https://mathcurve.com/courbes2d.gb/radioide/radioide.shtml>.
// ============================================================================

/** @brief Elastic curve (elastica) — the radioid with curvature
 *         proportional to the CARTESIAN ABSCISSA x (not arc length):
 *
 *           κ(x) = 2x/a²
 *
 *         Unlike every other TransitionElement in this file, κ here is not
 *         a pure function of the normalized arc-length fraction t=L/Ls — it
 *         depends on the (as yet unknown) x-coordinate itself, giving the
 *         coupled ODE dθ/ds=κ(x), dx/ds=cosθ, dy/ds=sinθ. Differentiating
 *         once more gives d²θ/ds²=(2/a²)cosθ, the classical pendulum-type
 *         elastica equation whose solution is expressed via Jacobi elliptic
 *         functions (Euler 1744; see literature doc) — this class instead
 *         integrates the coupled ODE directly via RK4 (mathematically
 *         equivalent, avoids deriving the Legendre-form elliptic-integral
 *         reduction). As with LemniscateElement/PHQuinticElement, the
 *         curve's free shape parameter `a` must be solved for per (Ls,R)
 *         pair (cached; expensive, independent of the query point). Verified
 *         numerically: κ(0)=0 exactly (curve starts at x=0), κ(Ls)=1/R to
 *         solver tolerance, monotonic curvature. */
class ElasticRadioidElement : public TransitionElement
{
public:
    ElasticRadioidElement() = default;
    ElementType type()     const override { return ElementType::ElasticRadioid; }
    QString     typeName() const override { return QStringLiteral("ElasticRadioid"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;

private:
    mutable bool   m_cacheValid = false;
    mutable double m_cachedLs   = 0.0;
    mutable double m_cachedR    = 0.0;
    mutable double m_a          = 0.0;   // solved elastica shape parameter
    void ensureSolved() const;
};

/** @brief Norwich spiral (a.k.a. Sturm's curve) — the radioid with
 *         curvature EQUAL to the reciprocal of the distance r to a fixed
 *         pole point:
 *
 *           κ(s) = 1/r(s),   r(s) = |P(s) − pole|
 *
 *         ⚠️ IMPORTANT, verified mathematically (see literature doc): unlike
 *         every other transition curve in this file, κ=1/r can never be
 *         EXACTLY zero for any finite pole distance — only in the trivial
 *         limit r→∞ (which degenerates to a straight line). This means this
 *         curve family CANNOT satisfy the usual "κ(0)=0, tangent-aligned
 *         start" contract exactly. Given (Ls,R), this class places the pole
 *         ahead of the start along the initial tangent and solves for the
 *         pole distance D so that r(Ls)=R exactly (i.e. the far/SC end is
 *         geometrically exact) — but κ(0)=1/D comes out non-zero in
 *         general (verified: roughly 70-90% of the target end curvature
 *         1/R for typical railway Ls/R ratios — NOT a small correction).
 *         Practically, this means a small curvature discontinuity remains
 *         where this element joins a preceding straight tangent; it is
 *         mathematically the best this curve family can do for a finite Ls.
 *         Uses the same coupled-ODE + two-stage-solve architecture as
 *         ElasticRadioidElement above. */
class NorwichSturmElement : public TransitionElement
{
public:
    NorwichSturmElement() = default;
    ElementType type()     const override { return ElementType::NorwichSturm; }
    QString     typeName() const override { return QStringLiteral("NorwichSturm"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;

private:
    mutable bool   m_cacheValid = false;
    mutable double m_cachedLs   = 0.0;
    mutable double m_cachedR    = 0.0;
    mutable double m_poleD      = 0.0;   // solved pole distance ahead of start
    void ensureSolved() const;
};

/** @brief Pseudo-elliptic radioid — Cartesian y=f(x) curve, the plot of the
 *         inverse Gudermannian function:
 *
 *           y = a·gd⁻¹(x/a) = a·arcsinh(tan(x/a))
 *
 *         Per mathcurve.com, this "looks like the tangentoid, but ... its
 *         curvilinear abscissa can be calculated with usual functions, not
 *         elliptic functions" (hence "pseudo-elliptic" — it merely
 *         resembles a curve that would need elliptic integrals). Verified:
 *         y'(x)=sec(x/a), y''(x)=sec(x/a)tan(x/a)/a; at x=0 the CURVATURE is
 *         exactly zero (an inflection point) even though the SLOPE there is
 *         1 (45°) — not 0. This class therefore evaluates the curve in its
 *         own natural frame and rotates the whole thing by −45° so the
 *         local tangent at the inflection point (mapped to L=0) points
 *         along +x, matching every other TransitionElement's contract
 *         (mirrors how LemniscateElement handles its own 45°
 *         tangent-at-origin property). Verified numerically: κ(x) rises
 *         from 0 at x=0 to a MAXIMUM around x/a≈1.0255 before falling back
 *         toward 0 as x/a→π/2 (a vertical asymptote) — so only the
 *         monotonic-rising portion x/a∈[0, ~1.0255] is usable; ensureSolved()
 *         clamps to this range. Arc length is evaluated by direct numerical
 *         quadrature of √(1+y'(x)²) (robust; the "usual functions" closed
 *         form mathcurve.com references was not re-derived here). */
class PseudoEllipticRadioidElement : public TransitionElement
{
public:
    PseudoEllipticRadioidElement() = default;
    ElementType type()     const override { return ElementType::PseudoEllipticRadioid; }
    QString     typeName() const override { return QStringLiteral("PseudoEllipticRadioid"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;

private:
    mutable bool   m_cacheValid = false;
    mutable double m_cachedLs   = 0.0;
    mutable double m_cachedR    = 0.0;
    mutable double m_a          = 0.0;   // solved shape/scale parameter
    mutable double m_xEnd       = 0.0;   // solved end x (in the curve's own frame)
    void ensureSolved() const;
};

/** @brief Logarithmic transition curve — a segment of the true
 *         logarithmic/equiangular spiral, per the n=+1 case of the
 *         Log-Aesthetic Curve (LAC) family (Miura 2005; Harada et al. 1999
 *         "Logarithmic Distribution Diagram of Curvature"): ρ(L)ⁿ=aL+b,
 *         n=+1, i.e. radius of curvature ρ LINEAR in arc length — verified
 *         analytically (see literature doc) to be exactly the arc-length
 *         parametrization of r=r₀e^(bθ) (n=−1 gives the clothoid instead).
 *         θ(L) has a clean closed form. Like NorwichSturmElement (a related
 *         radioid-family curve with the same underlying issue), κ=1/ρ can
 *         never be EXACTLY zero for a finite-length curve — this class uses
 *         a fixed κ(0)=κ(Ls)/20 approximation (documented in the .cpp) as
 *         a practical compromise. Genuinely distinct from the log-shaped
 *         g(t) curvature-ramp previously used here — replaced because that
 *         formula didn't correspond to any specific named curve in the
 *         literature (see literature doc for the full discussion). */
class LogarithmicElement : public TransitionElement
{
public:
    LogarithmicElement() = default;
    ElementType type()     const override { return ElementType::Logarithmic; }
    QString     typeName() const override { return QStringLiteral("Logarithmic"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;
};

/** @brief Hyperbolic transition curve — Kisgyörgy & Barna (2014),
 *         "Hyperbolic transition curve", Periodica Polytechnica Civil
 *         Engineering 58(1):63-69, eq. 11:
 *
 *           G(l) = (1/2R)·[sh(p−2pl/L)−sh(p)+(2pl/L)ch(p)] / [p·ch(p)−sh(p)]
 *
 *         Built by the paper from sh(x) (Fig. 1), shifted so its derivative
 *         has two zero-tangent points (Fig. 2-3), then fitted so the local
 *         maximum equals the target 1/R (Fig. 4) — a genuine railway
 *         transition curve, not merely a tanh-shaped ramp (the previous
 *         implementation here used g(t)=tanh(kt)/tanh(k), which doesn't
 *         correspond to this or any other specific named curve in the
 *         literature). Verified against the paper's own published endpoint
 *         coordinate table for p=1,5,20 — matches to ~5e-5 (the paper's own
 *         truncated 2-term series approximation, not this implementation,
 *         accounts for that residual). p is a shape parameter — the paper
 *         shows p→0 approaches the cosine/half-sine curve and p→∞
 *         approaches the clothoid; not yet exposed as a user-settable
 *         field (same simplification as WienerBogenElement's h/ψ₂), fixed
 *         at p=5 (one of the paper's own three analysed cases). */
class HyperbolicElement : public TransitionElement
{
public:
    HyperbolicElement() = default;
    ElementType type()     const override { return ElementType::Hyperbolic; }
    QString     typeName() const override { return QStringLiteral("Hyperbolic"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;
};

/** @brief Plain cubic-power curvature ramp: g(t) = t³. A simple monotonic
 *         polynomial ramp (no smoothstep shaping at the ends). */
class PolynomialElement : public TransitionElement
{
public:
    PolynomialElement() = default;
    ElementType type()     const override { return ElementType::Polynomial; }
    QString     typeName() const override { return QStringLiteral("Polynomial"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;
};

/** @brief Quintic (5th-order) smoothstep curvature ramp:
 *         g(t) = 6t⁵ − 15t⁴ + 10t³. Zero first AND second derivative at
 *         both ends (the standard "smootherstep" polynomial).
 *
 *         IMPORTANT (reviewed against literature — see
 *         TransitionCurveLiteratureReferences.md): "quintic transition
 *         curve" in railway engineering literature (e.g. Zboinski & Woznica,
 *         and the MDPI curvature-smoothness study) refers to a *family* —
 *         5th-degree curvature polynomials whose coefficients are typically
 *         solved per-project against a vehicle-dynamics/comfort objective —
 *         not one single fixed industry-standard formula the way Bloss or
 *         Sinusoidal are. The literature explicitly notes that the
 *         optimal quintic shape sometimes has smooth (zero-derivative)
 *         curvature at the endpoints and sometimes deliberately does not,
 *         depending on the optimization criteria used. g(t) above is this
 *         codebase's specific, well-defined, mathematically standard choice
 *         within that family (the symmetric C² "smootherstep") — a
 *         reasonable member of the family, not a verified match to any one
 *         cited industry-standard "the quintic railway curve" formula,
 *         because no single such canonical formula was found to exist.
 *
 *         Do not confuse with PHQuinticElement (planned, not yet
 *         implemented) — the Pythagorean-Hodograph quintic spiral from the
 *         CAGD literature (Farouki et al.), a fundamentally different
 *         construction (its hodograph satisfies a Pythagorean condition,
 *         giving a polynomial — not numerically-integrated — arc length)
 *         used for G² transitions between circles/lines. That is a distinct
 *         curve family, not another coefficient choice for this class. */
class QuinticElement : public TransitionElement
{
public:
    QuinticElement() = default;
    ElementType type()     const override { return ElementType::Quintic; }
    QString     typeName() const override { return QStringLiteral("Quintic"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;
};

/** @brief PH Quintic (Pythagorean-Hodograph quintic) spiral — Farouki,
 *         Pelosi & Sampoli (2023) construction, following the original
 *         Walton & Meek (1996) "PH quintic spiral" concept of a G²
 *         straight-to-circle transition with polynomial (not numerically
 *         integrated) arc length.
 *
 *         Genuinely distinct curve family from QuinticElement above — NOT
 *         another coefficient choice within the same curvature-ramp
 *         architecture. A PH quintic is built from a complex quadratic
 *         "pre-image" polynomial w(t) = w0(1−t)² + 2w1t(1−t) + w2t² via
 *         r'(t) = w(t)², so the hodograph satisfies the Pythagorean
 *         condition and r(t) itself is an ordinary (degree-5) polynomial
 *         curve. This gives:
 *           κ(t) = 2·Im(w(t)·w'(t)) / |w(t)|⁴
 *           s(t) = ∫₀ᵗ |w(u)|² du               (closed-form polynomial!)
 *         Setting w0=w1=1 (real — necessary and sufficient for κ(0)=0 with
 *         the initial tangent along +x, i.e. the required straight-tangent
 *         start) leaves w2=p+iq as the only remaining shape freedom, fixed
 *         here by two conditions: κ'(1)=0 (smooth, C² entry into the
 *         circular arc — no curvature kink at the SC point) and
 *         κ(1)·s(1)=Ls/|R| (matching the target endpoint curvature and
 *         total length simultaneously, since this product is scale-
 *         invariant — same two-stage solve strategy as LemniscateElement).
 *         The first condition reduces to a quadratic in q² solvable in
 *         closed form; the second is then a 1-D root in p, found by
 *         bisection over the analytically-known monotonic branch
 *         p ∈ ((19−√41)/20, ~0.837] (where the target ratio rises smoothly
 *         from 0 to its maximum ≈1.32 — comfortably covering realistic
 *         railway Ls/R ratios; see chat verification). Both this outer
 *         shape solve and the per-query arc-length inversion (needed to map
 *         L to the curve parameter t) are bisections on a closed-form
 *         polynomial (no numerical quadrature anywhere), verified against
 *         direct numerical arc-length integration to ~1e-6 agreement.
 *         Monotonic curvature confirmed numerically across realistic and
 *         even fairly extreme Ls/R ratios. */
class PHQuinticElement : public TransitionElement
{
public:
    PHQuinticElement() = default;
    ElementType type()     const override { return ElementType::PHQuintic; }
    QString     typeName() const override { return QStringLiteral("PHQuintic"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;

private:
    // Lazily-computed (Ls,R)-dependent cache for the outer shape solve
    // (expensive; independent of the query point L).
    mutable bool   m_cacheValid = false;
    mutable double m_cachedLs   = 0.0;
    mutable double m_cachedR    = 0.0;
    mutable double m_p          = 0.0;   // solved pre-image shape parameter
    mutable double m_q          = 0.0;   // solved pre-image shape parameter
    mutable double m_lambda2    = 1.0;   // solved scale factor (position/arc-length)
    void ensureSolved() const;
};

/** @brief Helmert's (1872) biquadratic parabola — g(t)=t², i.e. curvature
 *         itself is a simple parabola (quadratic) in arc length. Resolved
 *         (see literature doc) via Schuhr's precise technical description:
 *         a LINEAR cant/superelevation ramp gives the ordinary cubic
 *         parabola (ParabolaElement/CubicJPNElement/CubicECIElement), while
 *         Helmert's PARABOLA-shaped (quadratic) cant ramp gives this
 *         biquadratic curve — verified symbolically that integrating
 *         κ(l)=(1/R)(l/Ls)² twice gives a genuine quartic Cartesian y(x),
 *         matching "Parabel vierter Ordnung" (4th-order parabola), the
 *         term used for this curve in German railway-engineering
 *         literature. g'(0)=0 (smooth tangent start) but g'(1)=2≠0 (a
 *         curvature-derivative discontinuity remains where this curve
 *         meets the circular arc — unlike the C¹-at-both-ends piecewise
 *         form a previous version of this class used, based on a briefer,
 *         less precisely-sourced Autodesk description; this quadratic form
 *         has more directly-traceable academic grounding, see literature
 *         doc). */
class BiquadraticElement : public TransitionElement
{
public:
    BiquadraticElement() = default;
    ElementType type()     const override { return ElementType::Biquadratic; }
    QString     typeName() const override { return QStringLiteral("Biquadratic"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;
};

/** @brief Piecewise cubic-Hermite ("spline") curvature ramp through the
 *         three knots (0,0)→(0.5,0.5)→(1,1), with prescribed end/mid
 *         slopes (0, 1, 0 respectively). Distinct in construction (two
 *         Hermite segments) from the single-formula ramps above. */
class SplineElement : public TransitionElement
{
public:
    SplineElement() = default;
    ElementType type()     const override { return ElementType::Spline; }
    QString     typeName() const override { return QStringLiteral("Spline"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;
};

/** @brief "Doucine" — a REAL, standardised French construction (SNCF,
 *         since 1968; used on TGV and conventional-speed lines, documented
 *         cap lengths 40m/20m respectively — see literature doc): a short
 *         smooth (zero-slope) Bloss-like cap at EACH END of an otherwise
 *         ordinary LINEAR (clothoid) curvature ramp, rounding off the
 *         slope discontinuity a pure clothoid has where it meets the
 *         constant-curvature straight tangent and circular arc. Built from
 *         a smoothed-trapezoid dg/dt (raised-cosine ramp up over [0,f],
 *         constant plateau — the clothoid body — over [f,1-f], mirrored
 *         raised-cosine ramp down over [1-f,1]); f=0.25 is a documented
 *         representative fraction (SNCF's own standard uses fixed absolute
 *         lengths, which don't translate directly to this codebase's
 *         arbitrary-Ls parametrization). Replaces a previous version that
 *         uniformly averaged Bloss and linear shapes across the whole
 *         length — verified that construction did NOT actually have zero
 *         slope at the ends, so it didn't capture doucine's actual
 *         corner-smoothing purpose. */
class BlossEulerHybridElement : public TransitionElement
{
public:
    BlossEulerHybridElement() = default;
    ElementType type()     const override { return ElementType::BlossEulerHybrid; }
    QString     typeName() const override { return QStringLiteral("BlossEulerHybrid"); }
    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;
};

// ============================================================================
//  EggTransitionElement  ("EGG" – compound / bi-quadratic spiral between C–C)
// ============================================================================

/**
 * @brief Bi-quadratic compound spiral connecting two circles of different radii.
 *
 * Used for CC transitions (circle–spiral–circle without an intervening
 * tangent).  The element is reduced to an equivalent clothoid of length
 *   Ls = LE · max(|R1|,|R2|) / ||R1|−|R2||
 * starting at a computed origin, then traversed partially (LS−LE … LS).
 *
 * The equivalent sub-spiral is built dynamically as a @c ClothoidElement
 * with adjusted placement.
 */
class EggTransitionElement : public TransitionElement
{
public:
    EggTransitionElement() = default;
    EggTransitionElement(double r1, double r2, double le)
        : m_r1(clampRadius(r1)), m_r2(clampRadius(r2)), m_le(le) { rebuild(); }

    ElementType type()     const override { return ElementType::Egg; }
    QString     typeName() const override { return QStringLiteral("Egg"); }

    double r1() const { return m_r1; }          ///< Entering circle radius (signed)
    double r2() const { return m_r2; }          ///< Exiting  circle radius (signed)
    double le() const { return m_le; }          ///< Actual element length [m]
    double ls() const { return m_ls; }          ///< Equivalent spiral length [m]
    bool   r1Dominant() const { return m_r1Dominant; } ///< True when |R1| ≥ |R2|

    void setR1(double r) { m_r1 = clampRadius(r); rebuild(); }
    void setR2(double r) { m_r2 = clampRadius(r); rebuild(); }
    void setLE(double l) { m_le = l; rebuild(); }

    LocalFrame  localFrame(double L) const override;
    QPointF     worldXY   (double p, double w = 0.0) const override;
    double      worldAzimuth(double p) const override;
    QPointF     inversePW (double x, double y)        const override;
    QJsonObject toJson()                               const override;

    /** OLS: equivalent spiral length = LE · max(|R|) / ||R1|−|R2|| */
    static double ols(double R1, double R2, double LE);

private:
    double m_r1 = 0.0;
    double m_r2 = 0.0;
    double m_le = 0.0;

    /** Equivalent clothoid (owned, rebuilt when R1/R2/LE change). */
    std::unique_ptr<ClothoidElement> m_equiv;

    /** True when |R1| ≥ |R2|: traversal from the R2 side. */
    bool m_r1Dominant = true;

    /** Equivalent spiral length Ls. */
    double m_ls = 0.0;

    /**
     * @brief Rebuild the internal equivalent ClothoidElement.
     * Called whenever R1, R2 or LE change.
     */
    void rebuild();
};

// ============================================================================
//  AlignmentElementFactory
// ============================================================================

/**
 * @brief Creates concrete AlignmentElement objects from AlignmentPoint data.
 *
 * The factory requires three consecutive @c AlignmentPoint records to resolve
 * spiral sub-types (TC / CT / CC) from neighbour context.
 *
 * @code
 *   // Build element at index i (element spans pts[i] → pts[i+1])
 *   auto elem = AlignmentElementFactory::create(pts[i-1], pts[i], pts[i+1]);
 * @endcode
 */
class AlignmentElementFactory
{
public:
    AlignmentElementFactory() = delete;

    /**
     * @brief Create the element that spans from @p cur to @p next.
     *
     * @param prev  Point before @p cur (for spiral neighbour context).
     *              Pass a default AlignmentPoint with tsc="TT" when not available.
     * @param cur   Start keypoint of the element to create.
     * @param next  End   keypoint of the element.
     * @return      Newly allocated element (caller takes ownership).
     */
    static std::unique_ptr<AlignmentElement> create(
        const AlignmentPoint& prev,
        const AlignmentPoint& cur,
        const AlignmentPoint& next);

    /** Create from a JSON object (type-dispatched via "elementType" field). */
    static std::unique_ptr<AlignmentElement> fromJson(const QJsonObject& j);

private:
    static std::unique_ptr<AlignmentElement> createSpiral(
        const AlignmentPoint& prev,
        const AlignmentPoint& cur,
        const AlignmentPoint& next);

    static Placement makePlacement(const AlignmentPoint& pt);

    static Placement makeReversedPlacement(const AlignmentPoint& farEnd,
                                           double length);
};

} // namespace railway
} // namespace aicad
