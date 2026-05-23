/**
 * @file AlignmentSolver.cpp
 * @brief AlignmentSolver — Fixed + Floating Curve solver implementation.
 *
 * Implements:
 *   Pass 1 – Fixed elements (Tangent direct copy; CircularArc foot-of-perp).
 *   Pass 2 – Floating CircularArc via Appendix A simple-curve formula.
 *   Pass 3 – Build AlignmentPoint sequence and call HorizontalAlignment::load().
 *
 * @see AlignmentSolver.h for formula derivation notes.
 */

#include "AlignmentDocument.h"
#include "AlignmentSolver.h"
#include "RailwayAlignmentElement.h"

#include <QJsonArray>
#include <QLineF>
#include <QtDebug>
#include <QtMath>
#include <cmath>

namespace aicad {
namespace railway {

// ============================================================================
//  Private static helpers
// ============================================================================

double AlignmentSolver::azimuthOf(QPointF from, QPointF to)
{
    // CW from North:  atan2(ΔEast, ΔNorth)
    return std::atan2(to.x() - from.x(), to.y() - from.y());
}

QPointF AlignmentSolver::footOfPerp(QPointF A, QPointF B, QPointF P)
{
    QPointF ab  = B - A;
    double  ab2 = QPointF::dotProduct(ab, ab);
    if (ab2 < 1e-18) return A;
    double t = QPointF::dotProduct(P - A, ab) / ab2;
    return A + t * ab;
}

double AlignmentSolver::normaliseAngle(double a)
{
    // Fold to (-π, π]
    while (a >  M_PI) a -= 2.0 * M_PI;
    while (a <= -M_PI) a += 2.0 * M_PI;
    return a;
}

QPointF AlignmentSolver::lineIntersect(QPointF A, double azA,
                                       QPointF B, double azB)
{
    // Solve:  A + t·(sinA, cosA) = B + s·(sinB, cosB)
    //
    // Matrix form:
    //   [ sinA  -sinB ] [t]   [B.x - A.x]
    //   [ cosA  -cosB ] [s] = [B.y - A.y]
    //
    // det = sinA·(-cosB) - (-sinB)·cosA = sinB·cosA - sinA·cosB = sin(azB−azA)

    const double sinA = std::sin(azA),  cosA = std::cos(azA);
    const double sinB = std::sin(azB),  cosB = std::cos(azB);

    const double det = sinB * cosA - sinA * cosB;   // = sin(azB - azA)

    if (std::abs(det) < 1e-9) {
        // Lines are parallel — return midpoint as a safe fallback
        qWarning() << "[AlignmentSolver] lineIntersect: parallel tangents"
                   << "(azA=" << qRadiansToDegrees(azA)
                   << "azB=" << qRadiansToDegrees(azB) << "), using midpoint";
        return (A + B) * 0.5;
    }

    const double dx = B.x() - A.x();
    const double dy = B.y() - A.y();

    // Cramer: t = |dx -sinB| / det   →   (dx*(-cosB) - (-sinB)*dy) / det
    //             |dy -cosB|
    const double t = (-dx * cosB + dy * sinB) / det;

    return QPointF(A.x() + t * sinA, A.y() + t * cosA);
}

// ============================================================================
//  solveFloatingCurve  (public static — Appendix A formula)
// ============================================================================

SolvedCurve AlignmentSolver::solveFloatingCurve(
    const EditableElement& cur,
    const QPointF& tanStartPrev, const QPointF& tanEndPrev,
    const QPointF& tanStartNext, const QPointF& tanEndNext)
{
    SolvedCurve result;

    const double R = std::abs(cur.radius);
    if (R < 1e-9) {
        qWarning() << "[AlignmentSolver] solveFloatingCurve: radius ≈ 0";
        return result;
    }

    // ── Step 1: tangent azimuths ─────────────────────────────────────────────
    const double az1 = azimuthOf(tanStartPrev, tanEndPrev);  // incoming α₁
    const double az2 = azimuthOf(tanStartNext, tanEndNext);  // outgoing α₂

    // ── Step 2: intersection point PI ───────────────────────────────────────
    // Use one representative point on each tangent line plus its azimuth.
    const QPointF pi = lineIntersect(tanStartPrev, az1, tanStartNext, az2);

    // ── Step 3: turning angle Δ = α₂ − α₁ ───────────────────────────────────
    const double delta = normaliseAngle(az2 - az1);
    const double absDelta = std::abs(delta);

    if (absDelta < 1e-9) {
        qWarning() << "[AlignmentSolver] solveFloatingCurve: Δ ≈ 0 "
                      "(tangents are parallel — curve degenerates to a point)";
        return result;
    }

    // ── Step 4: tangent length T ─────────────────────────────────────────────
    //   T = R × tan(|Δ|/2)
    const double T = R * std::tan(absDelta * 0.5);

    // ── Step 5: PC and PT ────────────────────────────────────────────────────
    //   PC = PI − T × unit(α₁)
    //   PT = PI + T × unit(α₂)
    //
    //   unit(α) = (sin α, cos α)  in (Easting, Northing) frame

    const QPointF unit1(std::sin(az1), std::cos(az1));
    const QPointF unit2(std::sin(az2), std::cos(az2));

    result.pc  = pi - T * unit1;
    result.pt  = pi + T * unit2;

    // ── Step 6: arc length L = R × |Δ| ──────────────────────────────────────
    result.arcLen = R * absDelta;

    // ── Step 7: azimuth at PC = α₁ ──────────────────────────────────────────
    result.azPC  = az1;
    result.delta = delta;
    result.valid = true;

    return result;
}


// ============================================================================
//  makeTransitionElement  (file-local helper)
//
//  Instantiate the correct TransitionElement subclass for a given SpiralType,
//  configure its length and radius, and return it.  The element is used only
//  to evaluate localFrame(Ls) so placement is left at defaults.
// ============================================================================

namespace {

std::unique_ptr<TransitionElement>
makeTransitionElement(SpiralType type, double Ls, double signedR)
{
    std::unique_ptr<TransitionElement> elem;
    switch (type) {
    case SpiralType::HalfSine: elem = std::make_unique<HalfSineElement>(); break;
    case SpiralType::Parabola: elem = std::make_unique<ParabolaElement>(); break;
    case SpiralType::CubicJPN: elem = std::make_unique<CubicJPNElement>(); break;
    case SpiralType::CubicECI: elem = std::make_unique<CubicECIElement>(); break;
    case SpiralType::Clothoid:
    default:                   elem = std::make_unique<ClothoidElement>();  break;
    }
    elem->setLength(Ls);
    elem->setRadius(signedR);
    return elem;
}

} // anonymous namespace

// ============================================================================
//  solveSCS  (public static — VBA TryToFit sliding algorithm)
//
//  Algorithm ported from VBA Sub TryToFit():
//
//  Pass 1 — Anchor TS at tanStartPrev, propagate the SCS block forward
//            (entry spiral → circular arc → exit spiral reversed) to obtain a
//            preliminary ST position.
//
//  Fit    — Measure the cross-track gap (tw) from preliminary ST to the exit
//            tangent line, then compute the along-tangent correction d5:
//
//              d5 = tw(tanStartNext, az2, stInit) / sin(|Δ|) × signR
//
//            Derivation: sliding TS by d5 along az1 translates the entire SCS
//            block by d5·unit(az1), which changes ST's cross-track by
//            −d5·sin(Δ).  Setting the corrected gap to zero gives d5 exactly.
//
//  Pass 2 — Rebuild all four key-points from the corrected TS.
//
//  Xm / Ym / thetaS for each spiral are obtained from the corresponding
//  TransitionElement subclass via localFrame(Ls) rather than the Clothoid-only
//  Fresnel series, so all five spiral families are handled correctly.
// ============================================================================

SolvedSCS AlignmentSolver::solveSCS(
    double radius, double spiralLength1, double spiralLength2,
    SpiralType type1, SpiralType type2,
    const QPointF& tanStartPrev, const QPointF& tanEndPrev,
    const QPointF& tanStartNext, const QPointF& tanEndNext)
{
    SolvedSCS result;

    const double Rabs = std::abs(radius);
    const double L1   = std::abs(spiralLength1);
    const double L2   = std::abs(spiralLength2);

    if (Rabs < 1e-9) {
        qWarning() << "[AlignmentSolver] solveSCS: radius ≈ 0";
        return result;
    }
    if (L1 < 1e-9 && L2 < 1e-9) {
        qWarning() << "[AlignmentSolver] solveSCS: both spiral lengths ≈ 0";
        return result;
    }

    // ── Step 1: tangent azimuths  (VBA: Amz1, Amz2) ─────────────────────────
    const double az1 = azimuthOf(tanStartPrev, tanEndPrev);
    const double az2 = azimuthOf(tanStartNext, tanEndNext);

    // ── Step 2: turning angle and hand-of-curve ───────────────────────────────
    // VBA: a1 = AngleOfTwoAmz(Amz1, Amz2)
    //      R1 = R1 * LeftRightOfTwoAmz(Amz1, Amz2)
    const double delta    = normaliseAngle(az2 - az1);
    const double absDelta = std::abs(delta);
    if (absDelta < 1e-9) {
        qWarning() << "[AlignmentSolver] solveSCS: Δ ≈ 0 (parallel tangents)";
        return result;
    }
    const int    signR = (delta >= 0.0) ? 1 : -1;  // +1 = right-hand curve
    const double R     = signR * Rabs;              // signed radius (VBA: R1)

    // ── Step 3: spiral end-frame via TransitionElement::localFrame(Ls) ─────────
    //
    //  Each spiral is instantiated with its full length Ls and the signed
    //  radius R (signR * Rabs).  localFrame(Ls) returns the local coordinates
    //  at the far end of the spiral (the TC→SC or CS→ST tangent point):
    //    lf.x     = Xm  – along-tangent reach from the spiral start
    //    lf.y     = Ym  – cross-track offset (positive = right, sign follows R)
    //    lf.theta = θs  – total tangent deflection (positive = right turn)
    //
    //  This replaces the Clothoid-only Fresnel approximation so that all five
    //  spiral families (Clothoid, HalfSine, Parabola, CubicJPN, CubicECI)
    //  are handled with their own exact formulae.

    double Xm1 = 0.0, Ym1 = 0.0, thetaS1 = 0.0;
    if (L1 > 1e-9) {
        const auto elem1 = makeTransitionElement(type1, L1, R);  // R is signed
        const LocalFrame lf1 = elem1->localFrame(L1);
        Xm1     = lf1.x;
        Ym1     = lf1.y;      // already carries sign from R
        thetaS1 = lf1.theta;  // already carries sign from R
    }

    double Xm2 = 0.0, Ym2 = 0.0, thetaS2 = 0.0;
    if (L2 > 1e-9) {
        const auto elem2 = makeTransitionElement(type2, L2, R);  // R is signed
        const LocalFrame lf2 = elem2->localFrame(L2);
        Xm2     = lf2.x;
        Ym2     = lf2.y;
        thetaS2 = lf2.theta;
    }

    // ── Step 4: arc deflection and length ────────────────────────────────────
    //   arcAngle = |Δ| − |θs1| − |θs2|
    const double arcAngle = absDelta - std::abs(thetaS1) - std::abs(thetaS2);
    if (arcAngle < -1e-9) {
        qWarning() << "[AlignmentSolver] solveSCS: spirals overlap (Δ < Θ1+Θ2). Δ="
                   << qRadiansToDegrees(absDelta)
                   << "Θ1+Θ2=" << qRadiansToDegrees(std::abs(thetaS1) + std::abs(thetaS2));
        return result;
    }
    const double arcLen = Rabs * std::max(0.0, arcAngle);

    // ── Step 5: azimuths at SC and CS ────────────────────────────────────────
    //   thetaS already carries the turn sign, so no separate signR multiply.
    const double azSC = az1 + thetaS1;
    const double azCS = az2 - thetaS2;

    // ── Step 6: pre-compute trig ──────────────────────────────────────────────
    const double sA1 = std::sin(az1), cA1 = std::cos(az1);
    const double sA2 = std::sin(az2), cA2 = std::cos(az2);

    // ── Step 7: rigid offsets within the SCS block ───────────────────────────
    //
    //  (a) Entry spiral TS → SC
    //      Xm1 is along az1; Ym1 is along right_perp(az1) = (cos az1, −sin az1).
    //      Ym1 is already signed, so no extra signR factor needed.
    const double scDx  =  Xm1 * sA1 + Ym1 * cA1;   // ΔEast  TS → SC
    const double scDy  =  Xm1 * cA1 - Ym1 * sA1;   // ΔNorth TS → SC

    //  (b) Circular arc SC → CS
    //      R is signed; this computes the chord CS − SC correctly for both hands.
    const double csDx  =  R * (std::cos(azSC) - std::cos(azCS));   // ΔEast  SC → CS
    const double csDy  =  R * (std::sin(azCS) - std::sin(azSC));   // ΔNorth SC → CS

    //  (c) Exit spiral reversed CS → ST
    //      The exit spiral runs CT (decreasing curvature toward ST).
    //      In the local frame of az2: Xm2 along az2, Ym2 rightward.
    //      ST = CS + Xm2·unit(az2) − Ym2·right_perp(az2)
    //      (subtract because CT mirror of TC; Ym2 already signed).
    const double stDx  =  Xm2 * sA2 - Ym2 * cA2;   // ΔEast  CS → ST
    const double stDy  =  Xm2 * cA2 + Ym2 * sA2;   // ΔNorth CS → ST

    // ── Step 8: pass 1 — anchor TS at tanStartPrev, propagate to ST ──────────
    //  VBA: initial TS = ed[0].StartNE = (cox1, coy1)
    //       SC  = sx/sy(cox1,coy1, …)
    //       CS  = CX/CY(SC, azSC, R1, cl1)
    //       ST  = tx(CS, Amz2, 0, d4, −d3)          [cox6, coy6]
    const QPointF stInit(
        tanStartPrev.x() + scDx + csDx + stDx,
        tanStartPrev.y() + scDy + csDy + stDy);

    // ── Step 9: cross-track gap from preliminary ST to exit tangent ──────────
    //  VBA: tw(cox2, coy2, Amz2, 0, cox6, coy6)
    //  tw = (ST − ref) · right_perp(az2)
    //     = (ST.x − ref.x)·cos(az2) − (ST.y − ref.y)·sin(az2)
    //  Positive → ST is to the right of the exit tangent line.
    const double twDist = (stInit.x() - tanStartNext.x()) * cA2
                        - (stInit.y() - tanStartNext.y()) * sA2;

    // ── Step 10: along-tangent correction d5  (VBA: d5 = tw/sin(a1)*signR) ──
    //  Sliding TS by d5 along az1 translates the whole SCS block by d5·unit(az1).
    //  Cross-track change of ST = d5 · sin(az1−az2) = −d5 · sin(Δ).
    //  Setting corrected gap = 0:
    //    twDist − d5·sin(Δ) = 0
    //    d5 = twDist / sin(Δ) = twDist / (signR·sin(|Δ|))
    //       = twDist / sin(|Δ|) · signR          [VBA form, valid since signR = ±1]
    const double d5 = twDist / std::sin(absDelta) * signR;

    // ── Step 11: corrected TS  (VBA: cox7 = tx(cox1,coy1, Amz1, 0, d5, 0)) ──
    const QPointF tsPoint(tanStartPrev.x() + d5 * sA1,
                          tanStartPrev.y() + d5 * cA1);

    // ── Step 12: pass 2 — final geometry from corrected TS ───────────────────
    //  VBA second block: recompute cox4/coy4 (SC), cox5/coy5 (CS), cox6/coy6 (ST)
    //  from cox7/coy7 (corrected TS). Because the offsets are rigid, this is
    //  a simple translation of the preliminary block by d5·unit(az1).
    const QPointF scPoint(tsPoint.x() + scDx, tsPoint.y() + scDy);
    const QPointF csPoint(scPoint.x() + csDx, scPoint.y() + csDy);
    const QPointF stPoint(csPoint.x() + stDx, csPoint.y() + stDy);

    // ── Populate result ───────────────────────────────────────────────────────
    result.valid    = true;
    result.tsPoint  = tsPoint;
    result.scPoint  = scPoint;
    result.csPoint  = csPoint;
    result.stPoint  = stPoint;
    result.azTS     = az1;
    result.azSC     = azSC;
    result.azCS     = azCS;
    result.azST     = az2;
    result.arcLen   = arcLen;
    result.Ls1      = L1;
    result.Ls2      = L2;
    result.R        = Rabs;
    result.delta    = delta;
    result.thetaS1  = thetaS1;
    result.thetaS2  = thetaS2;

    return result;
}

// ============================================================================
//  solve()  — main entry point
// ============================================================================

std::unique_ptr<HorizontalAlignment>
AlignmentSolver::solve(const QVector<EditableElement>& elems)
{
    auto result = std::make_unique<HorizontalAlignment>();

    const int n = elems.size();
    if (n == 0) return result;

    // ── Working copies of tangent endpoints (mutated during pass 1 & 2) ──────
    QVector<QPointF> tanStart(n), tanEnd(n);
    for (int i = 0; i < n; ++i) {
        tanStart[i] = elems[i].startPI;
        tanEnd[i]   = elems[i].endPI;
    }

    // ── Per-element solved-curve data ─────────────────────────────────────────
    struct ArcData {
        bool    valid   = false;
        QPointF pc;
        QPointF pt;
        double  azPC    = 0.0;
        double  arcLen  = 0.0;
    };
    QVector<ArcData> arcData(n);

    // SCS group data — stored per SpiralIn element index
    struct SCSData {
        bool      valid    = false;
        SolvedSCS scs;
        int       arcIdx   = -1;  ///< index of the CircularArc element
        int       spiralOutIdx = -1; ///< index of the SpiralOut element
    };
    QVector<SCSData> scsData(n);

    // ════════════════════════════════════════════════════════════════════════
    //  Pass 1 – Fixed CircularArc: foot-of-perpendicular T1 / T2
    // ════════════════════════════════════════════════════════════════════════
    for (int i = 0; i < n; ++i) {
        const auto& e = elems[i];
        if (e.type != EditableElementType::CircularArc) continue;
        if (e.mode != ConstraintMode::Fixed)            continue;

        const QPointF center = e.arcCenter;   // Fixed arc: circle centre stored separately
        const double  R      = std::abs(e.radius);
        if (R < 1e-9) {
            qWarning() << "[AlignmentSolver] Pass1 Fixed arc radius ≈ 0 at idx" << i;
            continue;
        }

        ArcData arc;

        // ── PC and PT are stored directly in startPI / endPI ──────────────────
        //    (set by addFixedCurve from the 3 user-clicked points).
        //    Do NOT recompute via foot-of-perpendicular; that would move the
        //    arc away from the user's 3 points.
        arc.pc = e.startPI;
        arc.pt = e.endPI;

        // ── Tangent azimuth at PC: perpendicular to radius (centre → PC) ──────
        //    cross(r1, r2) > 0  →  CCW (left turn)   →  tangent = (-r1.y,  r1.x)
        //    cross(r1, r2) < 0  →  CW  (right turn)  →  tangent = ( r1.y, -r1.x)
        //    In (Easting, Northing) frame:
        //      azimuth = atan2(dEast, dNorth)
        {
            const QPointF r1 = arc.pc - center;
            const QPointF r2 = arc.pt - center;
            const double  crossVal = r1.x() * r2.y() - r1.y() * r2.x();
            const double  sign     = (crossVal >= 0.0) ? 1.0 : -1.0;
            // tangent (dE, dN) = sign * (-r1.y, r1.x)
            arc.azPC = std::atan2(sign * (-r1.y()), sign * r1.x());

            // ── Arc length from included angle ────────────────────────────────
            const double dot   = r1.x() * r2.x() + r1.y() * r2.y();
            const double delta = std::abs(std::atan2(crossVal, dot));
            arc.arcLen = R * delta;
        }

        if (arc.arcLen < 1e-9) {
            qWarning() << "[AlignmentSolver] Pass1 Fixed arc length ≈ 0 at idx" << i;
            continue;
        }

        // ── Fixed arc: do NOT trim adjacent tangents ──────────────────────────
        //    A Fixed arc's PC/PT are exactly the user-clicked points, fully
        //    independent of any neighbouring tangent.  Trimming would move the
        //    tangent's endpoint to match the arc, which is wrong.
        //    (Only Floating arcs auto-connect to adjacent tangents — handled in
        //     Pass 2 via solveFloatingCurve.)

        arc.valid    = true;
        arcData[i]   = arc;
    }

    // ════════════════════════════════════════════════════════════════════════
    //  Pass 2 – Floating CircularArc: Appendix A formula
    //
    //  Elements are processed in index order (Fixed-first guarantee: the task
    //  spec requires that Fixed tangents bounding a Floating curve are already
    //  fully determined before Pass 2 runs).
    // ════════════════════════════════════════════════════════════════════════
    for (int i = 0; i < n; ++i) {
        const auto& e = elems[i];
        if (e.type != EditableElementType::CircularArc) continue;
        if (e.mode != ConstraintMode::Floating)         continue;

        const int tb = e.tangentIdxBefore;
        const int ta = e.tangentIdxAfter;

        // Validate references
        if (tb < 0 || tb >= n || elems[tb].type != EditableElementType::Tangent) {
            qWarning() << "[AlignmentSolver] Pass2 Floating arc idx" << i
                       << ": invalid tangentIdxBefore =" << tb;
            continue;
        }
        if (ta < 0 || ta >= n || elems[ta].type != EditableElementType::Tangent) {
            qWarning() << "[AlignmentSolver] Pass2 Floating arc idx" << i
                       << ": invalid tangentIdxAfter =" << ta;
            continue;
        }

        // Solve using (possibly already-trimmed) working tangent endpoints
        const SolvedCurve sc = solveFloatingCurve(
            e,
            tanStart[tb], tanEnd[tb],
            tanStart[ta], tanEnd[ta]);

        if (!sc.valid) continue;

        // Store solved geometry
        ArcData arc;
        arc.valid  = true;
        arc.pc     = sc.pc;
        arc.pt     = sc.pt;
        arc.azPC   = sc.azPC;
        arc.arcLen = sc.arcLen;
        arcData[i] = arc;

        // Trim adjacent tangents to the solved cut-points
        tanEnd[tb]   = sc.pc;   // incoming tangent ends at PC
        tanStart[ta] = sc.pt;   // outgoing tangent starts at PT
    }

    // ════════════════════════════════════════════════════════════════════════
    //  Pass 2b — SCS group (SpiralIn + CircularArc + SpiralOut)
    //
    //  An SCS group is identified by:
    //    elems[i]   : SpiralIn   (Floating) with tangentIdxBefore / After
    //    elems[i+1] : CircularArc (Floating), same tangent references
    //    elems[i+2] : SpiralOut  (Floating), same tangent references
    //
    //  The entire group is solved as a unit using solveSCS().
    // ════════════════════════════════════════════════════════════════════════
    for (int i = 0; i < n - 2; ++i) {
        if (elems[i].type != EditableElementType::SpiralIn)  continue;
        if (elems[i].mode != ConstraintMode::Floating)        continue;

        // Expect the next two elements to be CircularArc and SpiralOut
        if (i + 2 >= n)                                        continue;
        if (elems[i+1].type != EditableElementType::CircularArc) continue;
        if (elems[i+2].type != EditableElementType::SpiralOut)   continue;

        const int tb = elems[i].tangentIdxBefore;
        const int ta = elems[i].tangentIdxAfter;

        if (tb < 0 || tb >= n || elems[tb].type != EditableElementType::Tangent) {
            qWarning() << "[AlignmentSolver] Pass2b SCS idx" << i
                       << ": invalid tangentIdxBefore =" << tb;
            continue;
        }
        if (ta < 0 || ta >= n || elems[ta].type != EditableElementType::Tangent) {
            qWarning() << "[AlignmentSolver] Pass2b SCS idx" << i
                       << ": invalid tangentIdxAfter =" << ta;
            continue;
        }

        const double R  = std::abs(elems[i+1].radius);
        const double L1 = elems[i].length;    // entry spiral length
        const double L2 = elems[i+2].length;  // exit  spiral length

        // Spiral types: SpiralIn element (index i) carries both type1 and type2.
        const SpiralType type1 = elems[i].spiralType1;
        const SpiralType type2 = elems[i].spiralType2;

        const SolvedSCS scs = solveSCS(
            R, L1, L2,
            type1, type2,
            tanStart[tb], tanEnd[tb],
            tanStart[ta], tanEnd[ta]);

        if (!scs.valid) continue;

        // Store SCS data against the SpiralIn index
        SCSData sd;
        sd.valid        = true;
        sd.scs          = scs;
        sd.arcIdx       = i + 1;
        sd.spiralOutIdx = i + 2;
        scsData[i] = sd;

        // Trim adjacent tangents
        tanEnd[tb]   = scs.tsPoint;  // incoming tangent ends at TS
        tanStart[ta] = scs.stPoint;  // outgoing tangent starts at ST

        // Skip past arc and spiral-out in the outer loop
        // (i++ in the for-loop body makes it i+1 next, but we need i+3)
        // We tag them as handled so Pass 3 knows to skip them.
        arcData[i+1].valid = false; // will be rendered via scsData
        arcData[i+2].valid = false;

        i += 2; // skip arc and SpiralOut
    }

    // ════════════════════════════════════════════════════════════════════════
    //  Pass 3 — Free elements: derive geometry from solved neighbours
    //
    //  Strategy: iterative residual minimisation.
    //    - A Free Tangent adjusts its endpoints to connect its left and right
    //      neighbours' solved endpoints.
    //    - A Free CircularArc: treat as Floating using the updated working
    //      tangent endpoints from nearest non-Free neighbours.
    //  We iterate until residual < 0.5 mm or max 20 iterations.
    // ════════════════════════════════════════════════════════════════════════
    {
        const int maxIter = 20;
        const double tol  = 0.0005;   // 0.5 mm

        for (int iter = 0; iter < maxIter; ++iter) {
            double maxResidual = 0.0;

            for (int i = 0; i < n; ++i) {
                if (elems[i].mode != ConstraintMode::Free) continue;

                if (elems[i].type == EditableElementType::Tangent) {
                    // Find nearest solved endpoints on either side
                    QPointF leftPt  = tanStart[i];
                    QPointF rightPt = tanEnd[i];

                    // Left: end-point of the element to the left
                    for (int j = i - 1; j >= 0; --j) {
                        if (elems[j].type == EditableElementType::Tangent) {
                            leftPt = tanEnd[j];
                            break;
                        } else if (elems[j].type == EditableElementType::CircularArc
                                   && arcData[j].valid) {
                            leftPt = arcData[j].pt;
                            break;
                        } else if (elems[j].type == EditableElementType::SpiralOut
                                   && j > 0 && scsData[j-2].valid) {
                            leftPt = scsData[j-2].scs.stPoint;
                            break;
                        }
                    }

                    // Right: start-point of the element to the right
                    for (int j = i + 1; j < n; ++j) {
                        if (elems[j].type == EditableElementType::Tangent) {
                            rightPt = tanStart[j];
                            break;
                        } else if (elems[j].type == EditableElementType::CircularArc
                                   && arcData[j].valid) {
                            rightPt = arcData[j].pc;
                            break;
                        } else if (elems[j].type == EditableElementType::SpiralIn
                                   && scsData[j].valid) {
                            rightPt = scsData[j].scs.tsPoint;
                            break;
                        }
                    }

                    const double r1 = QLineF(tanStart[i], leftPt).length();
                    const double r2 = QLineF(tanEnd[i],  rightPt).length();
                    maxResidual = std::max(maxResidual, std::max(r1, r2));

                    tanStart[i] = leftPt;
                    tanEnd[i]   = rightPt;

                } else if (elems[i].type == EditableElementType::CircularArc) {
                    // Treat as floating: re-solve using current working tangents
                    const int tb = elems[i].tangentIdxBefore;
                    const int ta = elems[i].tangentIdxAfter;
                    if (tb < 0 || ta < 0 || tb >= n || ta >= n) continue;

                    const SolvedCurve sc = solveFloatingCurve(
                        elems[i],
                        tanStart[tb], tanEnd[tb],
                        tanStart[ta], tanEnd[ta]);

                    if (!sc.valid) continue;

                    const double r = QLineF(arcData[i].pc, sc.pc).length();
                    maxResidual = std::max(maxResidual, r);

                    arcData[i].valid  = true;
                    arcData[i].pc     = sc.pc;
                    arcData[i].pt     = sc.pt;
                    arcData[i].azPC   = sc.azPC;
                    arcData[i].arcLen = sc.arcLen;

                    tanEnd[tb]   = sc.pc;
                    tanStart[ta] = sc.pt;
                }
            }

            if (maxResidual < tol) break;
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    //  Build AlignmentPoint sequence and load HorizontalAlignment
    // ════════════════════════════════════════════════════════════════════════
    QVector<AlignmentPoint> pts;
    pts.reserve(n * 2);
    double chainage = 0.0;

    // Track which indices are part of an SCS group (to avoid double-emitting).
    //
    // CRITICAL: the exit tangent (tangentIdxAfter) of every SCS group must also
    // be suppressed from the main loop.  addSCS() always appends SCS elements
    // AFTER the existing tangents, so the exit tangent index is always LOWER
    // than the SpiralIn index.  Emitting it in index order inserts a TT
    // keypoint into pts[] BEFORE the TS/SC/CS keypoints (wrong chainage order).
    //
    // Instead, the SCS emission block emits the trimmed exit tangent TT
    // immediately after the CS keypoint — correct order, correct position.
    QVector<bool> handledBySCS(n, false);
    for (int i = 0; i < n; ++i) {
        if (scsData[i].valid) {
            handledBySCS[i]                       = true;  // SpiralIn
            handledBySCS[scsData[i].arcIdx]       = true;  // CircularArc
            handledBySCS[scsData[i].spiralOutIdx] = true;  // SpiralOut

            // Suppress exit tangent — emitted inline by the SCS block (after CS).
            const int ta = elems[i].tangentIdxAfter;
            if (ta >= 0 && ta < n && ta < i)
                handledBySCS[ta] = true;
        }
    }

    for (int i = 0; i < n; ++i) {
        const auto& e = elems[i];
        AlignmentPoint pt;

        // ── SCS group: SpiralIn drives emission of all three sub-elements ──
        if (e.type == EditableElementType::SpiralIn && scsData[i].valid) {
            const SolvedSCS& scs = scsData[i].scs;
            const double R   = scs.R;
            const double Ls1 = scs.Ls1;   // entry spiral length
            const double Ls2 = scs.Ls2;   // exit  spiral length (may differ from Ls1)

            // Map SpiralType enum → AlignmentPoint curveType string
            // SpiralIn element (index i) carries both type1 (entry) and type2 (exit)
            auto spiralTypeName = [](SpiralType t) -> QString {
                switch (t) {
                case SpiralType::HalfSine: return QStringLiteral("HALFSINE");
                case SpiralType::Parabola: return QStringLiteral("PARABOLA");
                case SpiralType::CubicJPN: return QStringLiteral("CUBICJPN");
                case SpiralType::CubicECI: return QStringLiteral("CUBICECI");
                case SpiralType::Clothoid: // fall-through — default
                default:                   return QStringLiteral("SPIRAL");
                }
            };

            const QString curveTypeIn  = spiralTypeName(e.spiralType1);
            const QString curveTypeOut = spiralTypeName(e.spiralType2);

            // Emit TS point (entry spiral start): tsc = "TS"
            AlignmentPoint tspt;
            tspt.tsc       = QStringLiteral("TS");
            tspt.curveType = curveTypeIn;
            tspt.easting   = scs.tsPoint.x();
            tspt.northing  = scs.tsPoint.y();
            tspt.azimuth   = scs.azTS;
            tspt.length    = Ls1;
            tspt.radius    = R;
            tspt.chainage  = chainage;
            chainage      += Ls1;
            pts.append(tspt);

            // Emit SC point (arc start): tsc = "SC"
            AlignmentPoint scpt;
            scpt.tsc       = QStringLiteral("SC");
            scpt.curveType = QStringLiteral("ARC");
            scpt.easting   = scs.scPoint.x();
            scpt.northing  = scs.scPoint.y();
            scpt.azimuth   = scs.azSC;
            scpt.length    = scs.arcLen;
            scpt.radius    = R;
            scpt.chainage  = chainage;
            chainage      += scs.arcLen;
            pts.append(scpt);

            // Emit CS point (arc end / spiral-out start): tsc = "CS"
            AlignmentPoint cspt;
            cspt.tsc       = QStringLiteral("CS");
            cspt.curveType = curveTypeOut;
            cspt.easting   = scs.csPoint.x();
            cspt.northing  = scs.csPoint.y();
            cspt.azimuth   = scs.azCS;
            cspt.length    = Ls2;
            cspt.radius    = R;
            cspt.chainage  = chainage;
            chainage      += Ls2;
            pts.append(cspt);

            // Emit the trimmed exit tangent TT immediately after CS, in
            // correct chainage order.  This point serves two purposes:
            //   1. It is the "next" seen by makeReversedPlacement() for the
            //      spiral-out element — its easting/northing/azimuth give the
            //      ST anchor so the reversed spiral draws from the right place.
            //   2. It becomes a rendered TangentElement (ST → tanEnd[ta]),
            //      i.e. the trimmed exit tangent is visible on screen.
            //
            // tanStart[ta] was set to scs.stPoint by Pass 2b, so it already
            // holds the correct trimmed start.  tanEnd[ta] is the unchanged
            // far end of the exit tangent.
            {
                const int ta = e.tangentIdxAfter;
                if (ta >= 0 && ta < n
                    && elems[ta].type == EditableElementType::Tangent) {

                    const QPointF& stPt  = tanStart[ta];  // = scs.stPoint
                    const QPointF& endPt = tanEnd[ta];    // far end (unchanged)
                    const double   tlen  = QLineF(stPt, endPt).length();

                    AlignmentPoint stTT;
                    stTT.tsc      = QStringLiteral("TT");
                    stTT.easting  = stPt.x();
                    stTT.northing = stPt.y();
                    stTT.azimuth  = scs.azST;          // forward direction at ST
                    stTT.length   = tlen;
                    stTT.chainage = chainage;
                    chainage     += tlen;
                    pts.append(stTT);
                }
            }
            continue;
        }

        // ── Skip elements already emitted as part of an SCS group ─────────
        if (handledBySCS[i]) continue;

        if (e.type == EditableElementType::Tangent) {
            const double len = QLineF(tanStart[i], tanEnd[i]).length();
            if (len < 1e-9) continue;

            pt.tsc      = QStringLiteral("TT");
            pt.easting  = tanStart[i].x();
            pt.northing = tanStart[i].y();
            pt.azimuth  = azimuthOf(tanStart[i], tanEnd[i]);
            pt.length   = len;
            pt.chainage = chainage;
            chainage   += len;
            pts.append(pt);

        } else if (e.type == EditableElementType::CircularArc) {
            if (!arcData[i].valid) continue;
            const ArcData& arc = arcData[i];

            pt.tsc      = QStringLiteral("CC");
            pt.easting  = arc.pc.x();
            pt.northing = arc.pc.y();
            pt.azimuth  = arc.azPC;
            pt.radius   = e.radius;   // absolute value; load() assigns sign
            pt.length   = arc.arcLen;
            pt.chainage = chainage;
            chainage   += arc.arcLen;
            pts.append(pt);

            // ── PT waypoint: anchor the arc's endpoint in pts ─────────────────
            // The arc's PT (end-point) must always appear in the pts array so
            // that (a) PI grips are drawn there, and (b) it is never lost when
            // a subsequent element is appended and the sentinel moves.
            //
            // Exception: if the very next non-SCS element is a Tangent, its
            // own easting/northing in pts already anchors this point (via
            // tanStart[nextTangent] which Pass 2 sets to arc.pt for a Floating
            // arc, or which is already at the right position for a Fixed arc
            // followed by a tangent).  In that case we skip the extra point to
            // avoid a zero-length degenerate element.
            {
                // Look ahead: find the next emittable element in m_elems
                bool nextIsTangent = false;
                for (int j = i + 1; j < n; ++j) {
                    if (handledBySCS[j]) continue;  // part of SCS, skip
                    if (elems[j].type == EditableElementType::SpiralIn
                        && scsData[j].valid) break;  // SCS group — not a Tangent
                    if (elems[j].type == EditableElementType::Tangent) {
                        nextIsTangent = true;
                    }
                    break;  // only examine the immediate next emittable element
                }

                if (!nextIsTangent) {
                    // Compute azimuth at PT (tangent direction at arc end).
                    // The arc sweeps by delta = arcLen / R from azPC.
                    // Sign follows e.radius: positive = right turn (CW), negative = left.
                    const double R           = std::abs(e.radius);
                    const double delta       = (R > 1e-9) ? (arc.arcLen / R) : 0.0;
                    const double signedDelta = (e.radius >= 0.0) ? delta : -delta;
                    const double azPT        = arc.azPC + signedDelta;

                    AlignmentPoint ptWaypoint;
                    ptWaypoint.tsc      = QStringLiteral("TT");
                    ptWaypoint.easting  = arc.pt.x();
                    ptWaypoint.northing = arc.pt.y();
                    ptWaypoint.azimuth  = azPT;
                    ptWaypoint.length   = 0.0;
                    ptWaypoint.chainage = chainage;
                    pts.append(ptWaypoint);
                }
            }
        }
    }

    // ── Sentinel end-point (length = 0) ──────────────────────────────────────
    if (!pts.isEmpty()) {
        AlignmentPoint sentinel;
        sentinel.tsc      = QStringLiteral("TT");
        sentinel.length   = 0.0;
        sentinel.chainage = chainage;

        // Walk backward to find the last element with valid geometry
        for (int i = n - 1; i >= 0; --i) {
            if (elems[i].type == EditableElementType::SpiralOut
                && i >= 2 && scsData[i - 2].valid) {
                // The trimmed exit tangent TT (starting at scs.stPoint) was
                // already emitted directly after the CS keypoint.  The sentinel
                // must therefore point to the FAR END of the exit tangent so it
                // doesn't duplicate the TT point and provides the correct
                // end-of-alignment anchor.
                const int ta = elems[i - 2].tangentIdxAfter;  // SpiralIn is i-2
                if (ta >= 0 && ta < n
                    && elems[ta].type == EditableElementType::Tangent) {
                    sentinel.easting  = tanEnd[ta].x();
                    sentinel.northing = tanEnd[ta].y();
                    sentinel.azimuth  = azimuthOf(tanStart[ta], tanEnd[ta]);
                } else {
                    // Fallback: no exit tangent found, anchor at ST
                    const SolvedSCS& scs = scsData[i - 2].scs;
                    sentinel.easting  = scs.stPoint.x();
                    sentinel.northing = scs.stPoint.y();
                    sentinel.azimuth  = scs.azST;
                }
                break;
            } else if (elems[i].type == EditableElementType::Tangent) {
                const double len = QLineF(tanStart[i], tanEnd[i]).length();
                if (len < 1e-9) continue;
                sentinel.easting  = tanEnd[i].x();
                sentinel.northing = tanEnd[i].y();
                sentinel.azimuth  = azimuthOf(tanStart[i], tanEnd[i]);
                break;
            } else if (elems[i].type == EditableElementType::CircularArc
                       && arcData[i].valid) {
                sentinel.easting  = arcData[i].pt.x();
                sentinel.northing = arcData[i].pt.y();
                // Azimuth at PT = azimuth at PC ± (arc angle).
                // delta = arcLen / R; sign follows e.radius sign.
                {
                    const double R    = std::abs(elems[i].radius);
                    const double arc_delta = (R > 1e-9)
                                            ? (arcData[i].arcLen / R)
                                            : 0.0;
                    const double signedDelta = (elems[i].radius >= 0.0)
                                              ? arc_delta : -arc_delta;
                    sentinel.azimuth = arcData[i].azPC + signedDelta;
                }
                break;
            }
        }
        pts.append(sentinel);
    }

    result->load(pts);
    return result;
}

} // namespace railway
} // namespace aicad