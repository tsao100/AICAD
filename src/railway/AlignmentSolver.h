#pragma once

/**
 * @file AlignmentSolver.h
 * @brief Fixed + Floating Curve geometry solver for HorizontalAlignmentEdit.
 *
 * AlignmentSolver converts a QVector<EditableElement> into a fully solved
 * HorizontalAlignment, handling both Fixed and Floating constraint modes.
 *
 * Pass 1  — Lock all Fixed elements (Tangents already carry their startPI /
 *            endPI; Fixed CircularArcs compute T1/T2 via foot-of-perpendicular).
 *
 * Pass 2  — For every Floating CircularArc, call solveFloatingCurve(), which
 *            applies the standard simple-curve formula (Appendix A):
 *
 *              Δ  = α₂ − α₁
 *              T  = R × tan(|Δ|/2)
 *              PC = PI₁ − T × unit(α₁)      ← back along incoming tangent
 *              PT = PI₂ + T × unit(α₂)      ← forward along outgoing tangent
 *              L  = R × |Δ|
 *
 *            where PI is the intersection of the two tangent lines.
 *
 * Pass 3  — Build AlignmentPoint sequence and call HorizontalAlignment::load().
 *
 * Coordinate conventions (matches the rest of AICAD):
 *   QPointF::x()  = Easting   [m]
 *   QPointF::y()  = Northing  [m]
 *   Azimuth       = CW from North [rad]   (atan2(ΔEast, ΔNorth))
 *
 * @author AICAD Team
 * @date   2026-05
 */

#include "AlignmentDocument.h"
#include "RailwayAlignment.h"
#include "RailwayAlignmentElement.h"

#include <QPointF>
#include <QVector>
#include <memory>

namespace aicad {
namespace railway {

// ============================================================================
//  SolvedCurve  — internal result of solveFloatingCurve()
// ============================================================================

struct SolvedCurve
{
    bool    valid   = false;
    QPointF pc;             ///< Point of Curvature  (arc start = T1 cut point)
    QPointF pt;             ///< Point of Tangency   (arc end   = T2 cut point)
    double  azPC    = 0.0;  ///< Tangent azimuth at PC [rad]
    double  arcLen  = 0.0;  ///< Arc length L = R·|Δ| [m]
    double  delta   = 0.0;  ///< Signed turning angle Δ = α₂−α₁ [rad]
};

// ============================================================================
//  SolvedSCS  — internal result of solveSCS()
// ============================================================================

/**
 * @brief Geometry solution for one SCS group (SpiralIn + Arc + SpiralOut).
 *
 * Coordinate/azimuth conventions follow SolvedCurve above.
 * All three elements share the same PI₁ and PI₂; the arc is bounded by SC/CS
 * cut-points; each spiral runs from a tangent cut-point to an arc cut-point.
 */
struct SolvedSCS
{
    bool    valid    = false;

    // SpiralIn (TS → SC)
    QPointF tsPoint;     ///< Start of entry spiral (on incoming tangent)
    QPointF scPoint;     ///< End of entry spiral = start of circular arc
    double  azTS    = 0; ///< Azimuth at TS (= incoming tangent azimuth)
    double  azSC    = 0; ///< Azimuth at SC

    // Circular arc (SC → CS)
    QPointF csPoint;     ///< End of circular arc = start of exit spiral
    double  azCS    = 0; ///< Azimuth at CS
    double  arcLen  = 0; ///< Arc length between SC and CS [m]

    // SpiralOut (CS → ST)
    QPointF stPoint;     ///< End of exit spiral (on outgoing tangent)
    double  azST    = 0; ///< Azimuth at ST (= outgoing tangent azimuth)

    // Shared
    double  Ls1     = 0; ///< Entry spiral length L1 [m]
    double  Ls2     = 0; ///< Exit  spiral length L2 [m]
    double  R       = 0; ///< Circular radius [m] (absolute)
    double  delta   = 0; ///< Total turning angle Δ [rad]
    double  thetaS1 = 0; ///< Entry spiral angle Θ1 = L1/(2R) [rad]
    double  thetaS2 = 0; ///< Exit  spiral angle Θ2 = L2/(2R) [rad]
};

// ============================================================================
//  SolvedLC  — unknown-length Clothoid between Fixed Tangent → Fixed Arc
// ============================================================================

/**
 * @brief Result of solveLC(): Line → Clothoid → Arc.
 *
 * The spiral's entry tangent lies on the incoming Fixed Tangent (TS slides
 * along the tangent).  The spiral's exit point SC lies on the Fixed Arc and
 * its tangent is tangent to the arc.  The arc is trimmed: its new PC' = scPoint;
 * its original end (PT) is unchanged.
 *
 * Coordinate / azimuth conventions identical to SolvedCurve.
 */
struct SolvedLC
{
    bool    valid    = false;

    // Spiral  TS → SC
    double  Ls       = 0.0;   ///< Solved Clothoid length [m]
    double  R        = 0.0;   ///< Arc radius (absolute) [m]
    double  thetaS   = 0.0;   ///< Spiral angle at SC = Ls/(2R) for Clothoid [rad]
    QPointF tsPoint;           ///< Start of spiral (on incoming tangent)
    QPointF scPoint;           ///< End of spiral = trimmed arc new-PC
    double  azTS     = 0.0;   ///< Azimuth at TS = incoming tangent azimuth [rad]
    double  azSC     = 0.0;   ///< Azimuth at SC = azTS + thetaS [rad]

    // Trimmed arc  SC → original arc-end (PT)
    QPointF arcEndPoint;       ///< Original arc end (PT), unchanged
    double  arcLen   = 0.0;   ///< Trimmed arc length  SC → PT [m]
    double  azArcEnd = 0.0;   ///< Forward azimuth at PT [rad]
};

// ============================================================================
//  SolvedCA  — unknown-length Clothoid between Fixed Arc → Fixed Tangent
// ============================================================================

/**
 * @brief Result of solveCA(): Arc → Clothoid → Line.
 *
 * Mirror image of SolvedLC: the arc is trimmed at its exit end; the spiral
 * runs from the new CS point (on the arc) to ST (on the outgoing Fixed Tangent).
 */
struct SolvedCA
{
    bool    valid    = false;

    // Trimmed arc  original arc-start (PC) → CS
    QPointF arcStartPoint;     ///< Original arc start (PC), unchanged
    QPointF csPoint;           ///< Trimmed arc new-PT = start of spiral
    double  arcLen   = 0.0;   ///< Trimmed arc length  PC → CS [m]
    double  azCS     = 0.0;   ///< Forward azimuth at CS [rad]

    // Spiral  CS → ST
    double  Ls       = 0.0;   ///< Solved Clothoid length [m]
    double  R        = 0.0;   ///< Arc radius (absolute) [m]
    double  thetaS   = 0.0;   ///< Spiral angle at CS [rad]
    QPointF stPoint;           ///< End of spiral (on outgoing tangent)
    double  azST     = 0.0;   ///< Azimuth at ST = outgoing tangent azimuth [rad]
};

// ============================================================================
//  SolvedACA  — unknown-length Clothoid between Fixed Arc → Fixed Arc
// ============================================================================

/**
 * @brief Result of solveACA(): Arc₁ → Clothoid → Arc₂.
 *
 * The Clothoid has curvature κ₁ = 1/R₁ at its entry (SC₁, tangent to Arc₁)
 * and curvature κ₂ = 1/R₂ at its exit (SC₂, tangent to Arc₂).  Because
 * curvature varies linearly with arc length, this is equivalent to a segment
 * of a longer clothoid whose parameter A satisfies  A² = Ls · Req  where the
 * equivalent radius is  Req = |R₁·R₂| / |R₁ − R₂|  (i.e. an EggTransition).
 *
 * Both arcs are trimmed: Arc₁ new-PT = sc1Point; Arc₂ new-PC = sc2Point.
 * The arc lengths stored here are the trimmed lengths.
 *
 * Coordinate / azimuth conventions identical to SolvedLC / SolvedCA.
 */
struct SolvedACA
{
    bool    valid    = false;

    // Trimmed Arc₁  (original arc1-start → SC₁)
    QPointF arc1StartPoint;    ///< Original arc1 start (PC₁), unchanged
    QPointF sc1Point;          ///< Trimmed arc1 new-PT = start of spiral
    double  arc1Len  = 0.0;   ///< Trimmed arc1 length  PC₁ → SC₁ [m]
    double  azSC1    = 0.0;   ///< Forward azimuth at SC₁ [rad]

    // Spiral  SC₁ → SC₂
    double  Ls       = 0.0;   ///< Solved Clothoid length [m]
    double  R1       = 0.0;   ///< Arc₁ radius (absolute) [m]
    double  R2       = 0.0;   ///< Arc₂ radius (absolute) [m]
    double  thetaS   = 0.0;   ///< Total spiral angle = Ls·(1/R₂ − 1/R₁) / 2  [rad]
                               ///<   (accumulated tangent rotation from SC₁ to SC₂)

    // Trimmed Arc₂  (SC₂ → original arc2-end)
    QPointF sc2Point;          ///< Trimmed arc2 new-PC = end of spiral
    QPointF arc2EndPoint;      ///< Original arc2 end (PT₂), unchanged
    double  arc2Len  = 0.0;   ///< Trimmed arc2 length  SC₂ → PT₂ [m]
    double  azSC2    = 0.0;   ///< Forward azimuth at SC₂ [rad]
};

// ============================================================================
//  AlignmentSolver
// ============================================================================

class AlignmentSolver
{
public:
    AlignmentSolver() = default;

    /**
     * @brief Solve the alignment and return a fully constructed
     *        HorizontalAlignment.
     *
     * @param elems  Element list from HorizontalAlignmentEdit (may contain
     *               Fixed and Floating entries in any order).
     * @return       Newly allocated HorizontalAlignment (never nullptr).
     */
    std::unique_ptr<HorizontalAlignment>
    solve(const QVector<EditableElement>& elems);

    // ── Public so unit tests can call it directly ────────────────────────────

    /**
     * @brief Compute PC / PT for a Floating CircularArc given its two bounding
     *        tangent elements.  Implements Appendix A formula.
     *
     * @param cur   The Floating CircularArc element (provides radius).
     * @param prev  Preceding Fixed Tangent (possibly with trimmed endpoints).
     * @param next  Following Fixed Tangent (possibly with trimmed endpoints).
     * @param tanStartPrev  Effective start of incoming tangent (may differ from
     *                      prev.startPI after earlier trim operations).
     * @param tanEndPrev    Effective end   of incoming tangent.
     * @param tanStartNext  Effective start of outgoing tangent.
     * @param tanEndNext    Effective end   of outgoing tangent.
     * @return SolvedCurve with valid==true on success.
     */
    static SolvedCurve solveFloatingCurve(
        const EditableElement& cur,
        const QPointF& tanStartPrev, const QPointF& tanEndPrev,
        const QPointF& tanStartNext, const QPointF& tanEndNext);

    /**
     * @brief Compute full SCS geometry using Appendix B formulae (asymmetric).
     *
     * Xm / Ym / thetaS for each spiral are derived by calling the appropriate
     * TransitionElement subclass's localFrame(Ls) rather than using the
     * Clothoid-only Fresnel approximation, so all five spiral families
     * (Clothoid, HalfSine, Parabola, CubicJPN, CubicECI) are handled exactly.
     *
     * @param radius         Circular radius R [m] (absolute).
     * @param spiralLength1  Entry spiral length L1 [m].
     * @param spiralLength2  Exit  spiral length L2 [m].
     * @param type1          Entry spiral family (default: Clothoid).
     * @param type2          Exit  spiral family (default: Clothoid).
     * @param tanStartPrev   Effective start of incoming tangent.
     * @param tanEndPrev     Effective end   of incoming tangent.
     * @param tanStartNext   Effective start of outgoing tangent.
     * @param tanEndNext     Effective end   of outgoing tangent.
     * @return SolvedSCS with valid==true on success.
     */
    static SolvedSCS solveSCS(
        double radius, double spiralLength1, double spiralLength2,
        SpiralType type1, SpiralType type2,
        const QPointF& tanStartPrev, const QPointF& tanEndPrev,
        const QPointF& tanStartNext, const QPointF& tanEndNext);

    /**
     * @brief Solve for an unknown-length Clothoid between a Fixed Tangent
     *        (Line) and a Fixed CircularArc  —  LC group.
     *
     * @par Problem statement
     * Given:
     *   - Fixed Tangent with direction az1 = azimuthOf(tanStart, tanEnd).
     *   - Fixed CircularArc with centre @p arcCenter, absolute radius @p arcRadius,
     *     and fixed far-end @p arcEnd (PT stays where the user placed it).
     *
     * Find: Clothoid length Ls such that
     *   (a) TS lies on the incoming tangent line,
     *   (b) SC lies exactly on the circular arc  (|SC − arcCenter| = R), and
     *   (c) the spiral's tangent at SC is tangent to the arc.
     *
     * @par Algorithm  (cross-track gap f(Ls) = 0)
     * For a given Ls, the spiral end-point SC relative to TS is:
     *   @code
     *   azSC  = az1 + thetaS(Ls)
     *   scDx  = Xm·sin(az1) + Ym·cos(az1)
     *   scDy  = Xm·cos(az1) − Ym·sin(az1)
     *   @endcode
     * Condition (c) pins SC to the specific arc point whose tangent is azSC:
     *   @code
     *   SC_on_arc = arcCenter + R·(−signR·cos(azSC),  signR·sin(azSC))
     *   @endcode
     * Condition (a) means TS slides freely along az1; its along-tangent
     * position cancels out in the cross-track gap:
     *   @code
     *   f(Ls) = (tanEnd + (scDx,scDy) − SC_on_arc) · n1   [n1 = right-perp of az1]
     *   @endcode
     * A bisection solver on f(Ls) finds Ls to 0.1 mm accuracy.
     *
     * After solving, TS = SC_on_arc − (scDx, scDy); the arc is trimmed
     * to SC → arcEnd.
     *
     * @param arcCenter    Centre of the Fixed CircularArc.
     * @param arcRadius    Absolute radius [m].
     * @param arcEnd       Unchanged far-end (PT) of the Fixed Arc.
     * @param arcAzEnd     Forward azimuth at PT [rad].
     * @param tanStart     Start of the Fixed Tangent (far end from the arc).
     * @param tanEnd       End of the Fixed Tangent (close end, near the arc).
     * @param spiralType   Clothoid family (default: Clothoid).
     * @return SolvedLC with valid==true on success.
     */
    static SolvedLC solveLC(
        QPointF arcCenter, double arcRadius,
        QPointF arcEnd,    double arcAzEnd,
        const QPointF& tanStart, const QPointF& tanEnd,
        SpiralType spiralType = SpiralType::Clothoid);

    /**
     * @brief Solve for an unknown-length Clothoid between a Fixed CircularArc
     *        and a Fixed Tangent (Line)  —  CA group.
     *
     * Mirror image of solveLC: the spiral runs CT (curvature decreasing) from
     * CS (on the arc) to ST (on the outgoing Fixed Tangent).
     *
     * @par Gap function
     *   @code
     *   azCS = az2 − thetaS(Ls)
     *   CS_on_arc = arcCenter + R·(−signR·cos(azCS),  signR·sin(azCS))
     *   f(Ls) = (tanStart − (stDx,stDy) − CS_on_arc) · n2   [n2 = right-perp of az2]
     *   @endcode
     *
     * @param arcCenter    Centre of the Fixed CircularArc.
     * @param arcRadius    Absolute radius [m].
     * @param arcStart     Unchanged near-start (PC) of the Fixed Arc.
     * @param arcAzStart   Forward azimuth at PC [rad].
     * @param tanStart     Start of the Fixed Tangent (close end, near the arc).
     * @param tanEnd       End of the Fixed Tangent (far end from the arc).
     * @param spiralType   Clothoid family (default: Clothoid).
     * @return SolvedCA with valid==true on success.
     */
    static SolvedCA solveCA(
        QPointF arcCenter, double arcRadius,
        QPointF arcStart,  double arcAzStart,
        const QPointF& tanStart, const QPointF& tanEnd,
        SpiralType spiralType = SpiralType::Clothoid);

    /**
     * @brief Solve for an unknown-length Clothoid between two Fixed CircularArcs
     *        —  ACA group  (Arc₁ → Clothoid → Arc₂).
     *
     * @par Problem statement
     * Given:
     *   - Fixed Arc₁: centre @p arc1Center, absolute radius @p arc1Radius,
     *     fixed start @p arc1Start (PC₁, unchanged), start azimuth @p arc1AzStart.
     *   - Fixed Arc₂: centre @p arc2Center, absolute radius @p arc2Radius,
     *     fixed end   @p arc2End   (PT₂, unchanged), end azimuth @p arc2AzEnd.
     *
     * Find: Clothoid length Ls such that
     *   (a) SC₁ lies on Arc₁ and the spiral's entry tangent is tangent to Arc₁;
     *   (b) SC₂ lies on Arc₂ and the spiral's exit  tangent is tangent to Arc₂;
     *   (c) The spiral has curvature 1/R₁ at SC₁ and 1/R₂ at SC₂ (linear κ).
     *
     * @par Algorithm  (two-point cross-track gap  f(Ls) = 0)
     * The spiral is modelled as an EggTransition (bi-quadratic clothoid segment)
     * with equivalent full-spiral length  Ls_eq = Ls · max(R₁,R₂)/|R₁ − R₂|.
     *
     * For a given trial Ls, the EggTransitionElement provides SC₁ and SC₂
     * positions in local frame.  The gap function measures the cross-track
     * error of SC₂ relative to its required position on Arc₂ (tangency condition).
     * Bisection drives f(Ls) to zero.
     *
     * @param arc1Center   Centre of Fixed Arc₁.
     * @param arc1Radius   Absolute radius of Arc₁ [m].
     * @param arc1Start    Unchanged start (PC₁) of Fixed Arc₁.
     * @param arc1AzStart  Forward azimuth at PC₁ [rad].
     * @param arc2Center   Centre of Fixed Arc₂.
     * @param arc2Radius   Absolute radius of Arc₂ [m].
     * @param arc2End      Unchanged end (PT₂) of Fixed Arc₂.
     * @param arc2AzEnd    Forward azimuth at PT₂ [rad].
     * @param spiralType   Clothoid family (default: Clothoid).
     * @return SolvedACA with valid==true on success.
     */
    static SolvedACA solveACA(
        QPointF arc1Center, double arc1Radius,
        QPointF arc1Start,  double arc1AzStart,
        QPointF arc2Center, double arc2Radius,
        QPointF arc2End,    double arc2AzEnd,
        SpiralType spiralType = SpiralType::Clothoid);

    // ── Internal helpers ─────────────────────────────────────────────────────

    /** Azimuth of the vector from→to.  CW from North [rad]. */
    static double azimuthOf(QPointF from, QPointF to);

    /**
     * @brief Intersection of two infinite lines in azimuth form.
     *
     * Line A: through pointA in direction azA.
     * Line B: through pointB in direction azB.
     *
     * Returns the midpoint of A and B as a fallback when lines are parallel
     * (|sin(azB − azA)| < 1e-9).
     */
    static QPointF lineIntersect(QPointF pointA, double azA,
                                 QPointF pointB, double azB);

    /**
     * @brief Foot of perpendicular from P onto the line through A→B.
     *        t is unconstrained (perpendicular may lie outside the segment).
     */
    static QPointF footOfPerp(QPointF A, QPointF B, QPointF P);

    /**
     * @brief Normalise angle to (−π, π].
     */
    static double normaliseAngle(double a);
};

} // namespace railway
} // namespace aicad