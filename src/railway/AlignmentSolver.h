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

private:

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