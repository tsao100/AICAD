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
     */
    QPointF worldXY     (double p, double w = 0.0) const;

    /**
     * @brief Tangent azimuth [rad, CW from N] at chainage @p p.
     */
    double  worldAzimuth(double p)             const;

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
    explicit CircularArcElement(double radius) : m_radius(radius) {}

    ElementType type()     const override { return ElementType::CircularArc; }
    QString     typeName() const override { return QStringLiteral("CircularArc"); }

    double radius() const { return m_radius; }
    void   setRadius(double r) { m_radius = r; }

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
    void   setRadius(double r) { m_radius = r; }

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
//  CubicECIElement  ("CUBICECI" – CECI cubic parabola)
// ============================================================================

/**
 * @brief CECI cubic parabola.  Curvature satisfies a parametric equation
 *        solved via bisection on the half-total-angle φ.
 *
 *   BigFp = φ  (half-angle, from bisection BIGFPCECI)
 *   BigX  = Ls / (1 + p²/10 − p⁴/72 + p⁶/208)   with p = tan(φ)
 *   BigA  = BigX² / (2·tan(φ))
 *   x(L)  = BIGFXCECI(L)   [another bisection]
 *   y(L)  = x³/(6·BigA) · sign(R)
 *   θ(L)  = BIGFPCECI(L) · sign(R)
 */
class CubicECIElement : public TransitionElement
{
public:
    CubicECIElement() = default;

    ElementType type()     const override { return ElementType::CubicECI; }
    QString     typeName() const override { return QStringLiteral("CubicECI"); }

    LocalFrame  localFrame(double L) const override;
    QJsonObject toJson()              const override;

private:
    /** Bisection shared by BigFp, BigX and BigA computations. */
    static double solveCECI(double Ls, double R);

    double bigFp() const;
    double bigX () const;
    double bigA () const;

    mutable double m_bigA = -1.0;
    mutable double m_bigX = -1.0;
    mutable double m_bigFp= -1.0;
    void ensureCache() const;
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
        : m_r1(r1), m_r2(r2), m_le(le) { rebuild(); }

    ElementType type()     const override { return ElementType::Egg; }
    QString     typeName() const override { return QStringLiteral("Egg"); }

    double r1() const { return m_r1; }          ///< Entering circle radius (signed)
    double r2() const { return m_r2; }          ///< Exiting  circle radius (signed)
    double le() const { return m_le; }          ///< Actual element length [m]
    double ls() const { return m_ls; }          ///< Equivalent spiral length [m]
    bool   r1Dominant() const { return m_r1Dominant; } ///< True when |R1| ≥ |R2|

    void setR1(double r) { m_r1 = r; rebuild(); }
    void setR2(double r) { m_r2 = r; rebuild(); }
    void setLE(double l) { m_le = l; rebuild(); }

    LocalFrame  localFrame(double L) const override;
    QPointF     worldXY   (double p, double w = 0.0) const;
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
