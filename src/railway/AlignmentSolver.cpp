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
#include <limits>

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
//  solveSSJunction  —  消去 SS 交會點兩側 SCS 群組間的殘留短切線
//  （演算法推導見 AlignmentSolver.h 的方法註解）
// ============================================================================

SolvedSSJunction AlignmentSolver::solveSSJunction(
    const QPointF& ssPoint, double initialAzimuth,
    double radius1, double spiralLen1In, double spiralLen1Out,
    SpiralType type1In, SpiralType type1Out,
    const QPointF& tanStartPrev1, const QPointF& tanEndPrev1,
    double radius2, double spiralLen2In, double spiralLen2Out,
    SpiralType type2In, SpiralType type2Out,
    const QPointF& tanStartNext2, const QPointF& tanEndNext2,
    double tol, int maxIter)
{
    SolvedSSJunction result;

    // g(theta)：以候選方位角 theta 重解兩側 SCS 群組，回傳沿線帶號殘差。
    // 順帶把最近一次成功求解的 group1/group2 存進 result，讓呼叫端在
    // 「未收斂但已是目前最佳估計」的情況下仍能取得對應幾何。
    auto evaluate = [&](double theta) -> double {
        const QPointF lineEnd = ssPoint + QPointF(std::sin(theta), std::cos(theta));

        const SolvedSCS g1 = solveSCS(
            radius1, spiralLen1In, spiralLen1Out, type1In, type1Out,
            tanStartPrev1, tanEndPrev1, ssPoint, lineEnd);
        const SolvedSCS g2 = solveSCS(
            radius2, spiralLen2In, spiralLen2Out, type2In, type2Out,
            ssPoint, lineEnd, tanStartNext2, tanEndNext2);

        if (!g1.valid || !g2.valid)
            return std::numeric_limits<double>::quiet_NaN();

        result.group1 = g1;
        result.group2 = g2;

        // 兩端點依構造都落在方位角 theta 的直線上，取沿線分量即為
        // 帶號殘留間距（正負代表 group1 端相對 group2 端偏前或偏後）。
        const QPointF d = g1.stPoint - g2.tsPoint;
        return d.x() * std::sin(theta) + d.y() * std::cos(theta);
    };

    double theta0 = initialAzimuth;
    double g0     = evaluate(theta0);
    if (!std::isfinite(g0)) {
        qWarning() << "[AlignmentSolver] solveSSJunction: initial geometry invalid"
                      " (Δ≈0 or spirals overlap) -- cannot solve";
        return result;
    }
    if (std::abs(g0) < tol) {
        result.converged = true;
        result.azimuth   = theta0;
        result.gap       = g0;
        return result;
    }

    // 正割法：以極小擾動（1e-5 rad）建立第二個取樣點後疊代逼近 g(theta)=0。
    double theta1 = theta0 + 1.0e-5;
    double g1v    = evaluate(theta1);

    for (int iter = 0; iter < maxIter; ++iter) {
        result.iterations = iter + 1;
        if (!std::isfinite(g1v)) break;

        const double denom = g1v - g0;
        if (std::abs(denom) < 1e-15) break;   // 斜率退化，避免除以極小值

        const double theta2 = theta1 - g1v * (theta1 - theta0) / denom;
        const double g2v    = evaluate(theta2);

        theta0 = theta1; g0 = g1v;
        theta1 = theta2; g1v = g2v;

        if (std::isfinite(g1v) && std::abs(g1v) < tol) {
            result.converged = true;
            result.azimuth   = theta1;
            result.gap       = g1v;
            return result;
        }
    }

    qWarning() << "[AlignmentSolver] solveSSJunction: did not converge below"
               << tol << "m after" << result.iterations << "iterations"
               << "-- residual gap =" << g1v << "m";
    result.azimuth = theta1;
    result.gap      = g1v;
    return result;
}

// ============================================================================
//  solveLC  —  Line → Clothoid → Arc  (unknown Ls)
//
//  Derivation of the cross-track gap f(Ls)
//  ─────────────────────────────────────────
//  Let az1 = incoming tangent azimuth (fixed).
//  Let signR = +1 if arc turns right, −1 if left (determined by which side of
//              az1 the arc centre lies).
//
//  For a given trial Ls, obtain from localFrame(Ls):
//    thetaS  — spiral deflection angle (signed, carries turn direction)
//    Xm      — along-tangent reach from TS to SC
//    Ym      — cross-track offset from TS to SC (signed: positive = right)
//
//  azSC = az1 + thetaS
//
//  Condition (c): SC tangent is tangent to the arc.
//  ⟹ The centre is exactly one radius to the right of the forward direction
//    at SC (for a right-hand arc):
//        arcCenter = SC + signR · R · right_perp(azSC)
//    ⟹ SC_on_arc = arcCenter − signR · R · right_perp(azSC)
//               = arcCenter + (−signR·R·cos azSC,  +signR·R·sin azSC)
//
//  Condition (a): TS slides freely along az1:
//    TS = tanEnd − t · (sin az1, cos az1)   for some t
//    SC = TS + (scDx, scDy)                 [scDx/scDy from Xm, Ym, az1]
//
//  Cross-track equation  (project onto right-perp n1 = (cos az1, −sin az1)):
//    SC · n1 = SC_on_arc · n1          (t drops out because TS · n1 is independent of t)
//
//  ⟹ f(Ls) = (tanEnd + (scDx,scDy) − SC_on_arc) · n1 = 0
//
//  After finding Ls, TS is recovered from the along-track projection (h = 0):
//    h = (tanEnd + (scDx,scDy) − SC_on_arc) · u1
//    TS = tanEnd − h · u1
//  which gives exactly  TS = SC_on_arc − (scDx, scDy).
// ============================================================================

SolvedLC AlignmentSolver::solveLC(
    QPointF arcCenter, double arcRadius,
    QPointF arcEnd,    double arcAzEnd,
    const QPointF& tanStart, const QPointF& tanEnd,
    SpiralType spiralType)
{
    SolvedLC result;

    const double R = std::abs(arcRadius);
    if (R < 1e-9) {
        qWarning() << "[AlignmentSolver] solveLC: arcRadius ≈ 0";
        return result;
    }

    // ── Step 1: incoming tangent azimuth ─────────────────────────────────────
    const double az1 = azimuthOf(tanStart, tanEnd);
    const double sA1 = std::sin(az1), cA1 = std::cos(az1);
    // right-perpendicular of az1: n1 = (cos az1, −sin az1)
    const double n1x = cA1, n1y = -sA1;

    // ── Step 2: hand-of-curve ────────────────────────────────────────────────
    // If the arc centre lies to the right of the tangent direction, it's a
    // right-hand (CW) curve: signR = +1; left-hand: signR = −1.
    const double sideVal = (arcCenter.x() - tanEnd.x()) * n1x
                         + (arcCenter.y() - tanEnd.y()) * n1y;
    const int signR = (sideVal >= 0.0) ? 1 : -1;

    // ── Step 3: bisection on f(Ls) ───────────────────────────────────────────
    // SC_on_arc at trial azSC:
    //   SC_on_arc = (arcCenter.x − signR·R·cos azSC,
    //                arcCenter.y + signR·R·sin azSC)
    // f(Ls) = (tanEnd.x + scDx − SC_on_arc.x)·n1x
    //       + (tanEnd.y + scDy − SC_on_arc.y)·n1y

    auto evalF = [&](double Ls) -> double {
        const auto elem = makeTransitionElement(spiralType, Ls, signR * R);
        const LocalFrame lf = elem->localFrame(Ls);
        const double Xm     = lf.x;
        const double Ym     = lf.y;
        const double thetaS = lf.theta;

        const double azSC = az1 + thetaS;
        // Offset from TS to SC in world frame
        const double scDx = Xm * sA1 + Ym * cA1;
        const double scDy = Xm * cA1 - Ym * sA1;

        // Point on arc whose forward tangent is azSC
        const double sc_arc_x = arcCenter.x() - signR * R * std::cos(azSC);
        const double sc_arc_y = arcCenter.y() + signR * R * std::sin(azSC);

        const double bx = tanEnd.x() + scDx - sc_arc_x;
        const double by = tanEnd.y() + scDy - sc_arc_y;
        return bx * n1x + by * n1y;
    };

    // Search bounds:
    // Lower bound: near-zero (degenerate spiral).
    // Upper bound: thetaS cannot exceed π/2 (spiral would fold back), so
    //   Ls_max ≤ π·R (Clothoid: thetaS = Ls²/(2RL) but we use localFrame).
    // Also clamp to a generous multiple of the geometry size.
    const double geomDist = std::hypot(arcEnd.x() - tanEnd.x(),
                                       arcEnd.y() - tanEnd.y())
                          + std::hypot(arcEnd.x() - arcCenter.x(),
                                       arcEnd.y() - arcCenter.y()); // ≈ chord + arc
    const double Ls_max = std::min(geomDist * 2.0, M_PI * R * 0.95);

    // Scan for sign change
    const int    nScan = 400;
    double Ls_lo = 1e-3;
    double f_lo  = evalF(Ls_lo);
    double Ls_hi = -1.0;

    for (int k = 1; k <= nScan; ++k) {
        const double Ls_k = Ls_max * k / static_cast<double>(nScan);
        const double f_k  = evalF(Ls_k);
        if (f_lo * f_k < 0.0) { Ls_hi = Ls_k; break; }
        f_lo = f_k;
        Ls_lo = Ls_k;
    }

    if (Ls_hi < 0.0) {
        qWarning() << "[AlignmentSolver] solveLC: f(Ls) has no sign change."
                   << "az1=" << qRadiansToDegrees(az1)
                   << "R="   << R
                   << "Ls_max=" << Ls_max;
        return result;
    }

    // Bisect to 0.1 mm
    for (int iter = 0; iter < 80; ++iter) {
        const double Ls_mid = 0.5 * (Ls_lo + Ls_hi);
        if (std::abs(Ls_hi - Ls_lo) < 1e-4) { Ls_lo = Ls_mid; break; }
        if (evalF(Ls_lo) * evalF(Ls_mid) <= 0.0)
            Ls_hi = Ls_mid;
        else
            Ls_lo = Ls_mid;
    }
    const double Ls = Ls_lo;

    // ── Step 4: final geometry ────────────────────────────────────────────────
    const auto   elemF  = makeTransitionElement(spiralType, Ls, signR * R);
    const LocalFrame lf = elemF->localFrame(Ls);
    const double Xm     = lf.x;
    const double Ym     = lf.y;
    const double thetaS = lf.theta;

    const double azSC = az1 + thetaS;
    const double scDx = Xm * sA1 + Ym * cA1;
    const double scDy = Xm * cA1 - Ym * sA1;

    // SC pinned to arc (tangency condition)
    const QPointF scPoint(arcCenter.x() - signR * R * std::cos(azSC),
                          arcCenter.y() + signR * R * std::sin(azSC));
    // TS derived from SC and the rigid spiral offset
    const QPointF tsPoint(scPoint.x() - scDx, scPoint.y() - scDy);

    // ── Step 5: trimmed arc  SC → arcEnd ─────────────────────────────────────
    const QPointF r_sc  = scPoint - arcCenter;
    const QPointF r_end = arcEnd  - arcCenter;
    const double  crossV = r_sc.x() * r_end.y() - r_sc.y() * r_end.x();
    const double  dotV   = r_sc.x() * r_end.x() + r_sc.y() * r_end.y();
    double dPhi = std::atan2(std::abs(crossV), dotV);
    // Ensure arc goes in the correct travel direction:
    //   right-turn (signR=+1): crossV should be ≤ 0 (clockwise sweep SC→end)
    if (signR > 0 && crossV > 0.0) dPhi = 2.0 * M_PI - dPhi;
    if (signR < 0 && crossV < 0.0) dPhi = 2.0 * M_PI - dPhi;

    result.valid       = true;
    result.Ls          = Ls;
    result.R           = R;
    result.thetaS      = thetaS;
    result.tsPoint     = tsPoint;
    result.scPoint     = scPoint;
    result.azTS        = az1;
    result.azSC        = azSC;
    result.arcEndPoint = arcEnd;
    result.arcLen      = R * dPhi;
    result.azArcEnd    = arcAzEnd;

    return result;
}

// ============================================================================
//  solveCA  —  Arc → Clothoid → Line  (unknown Ls)
//
//  Exact mirror of solveLC with the spiral running CT (curvature decreasing).
//
//  f(Ls) for CA:
//    azCS = az2 − thetaS(Ls)                         [exit from arc into spiral]
//    CS_on_arc = arcCenter + (−signR·R·cos azCS,  +signR·R·sin azCS)
//    stDx = Xm·sin(az2) + Ym·cos(az2)               [CT: same local frame as TC]
//    stDy = Xm·cos(az2) − Ym·sin(az2)
//    f(Ls) = (tanStart − (stDx,stDy) − CS_on_arc) · n2   [n2 = right-perp of az2]
// ============================================================================

SolvedCA AlignmentSolver::solveCA(
    QPointF arcCenter, double arcRadius,
    QPointF arcStart,  double /*arcAzStart*/,
    const QPointF& tanStart, const QPointF& tanEnd,
    SpiralType spiralType)
{
    SolvedCA result;

    const double R = std::abs(arcRadius);
    if (R < 1e-9) {
        qWarning() << "[AlignmentSolver] solveCA: arcRadius ≈ 0";
        return result;
    }

    // ── Step 1: outgoing tangent azimuth ─────────────────────────────────────
    const double az2 = azimuthOf(tanStart, tanEnd);
    const double sA2 = std::sin(az2), cA2 = std::cos(az2);
    const double n2x = cA2, n2y = -sA2;

    // ── Step 2: hand-of-curve ────────────────────────────────────────────────
    const double sideVal = (arcCenter.x() - tanStart.x()) * n2x
                         + (arcCenter.y() - tanStart.y()) * n2y;
    const int signR = (sideVal >= 0.0) ? 1 : -1;

    // ── Step 3: bisection on f(Ls) ───────────────────────────────────────────
    auto evalF = [&](double Ls) -> double {
        const auto elem = makeTransitionElement(spiralType, Ls, signR * R);
        const LocalFrame lf = elem->localFrame(Ls);
        const double Xm     = lf.x;
        const double Ym     = lf.y;
        const double thetaS = lf.theta;

        const double azCS = az2 - thetaS;
        // CT spiral: TS→SC in local frame, mirrored for CS→ST direction
        const double stDx = Xm * sA2 + Ym * cA2;
        const double stDy = Xm * cA2 - Ym * sA2;

        const double cs_arc_x = arcCenter.x() - signR * R * std::cos(azCS);
        const double cs_arc_y = arcCenter.y() + signR * R * std::sin(azCS);

        const double bx = tanStart.x() - stDx - cs_arc_x;
        const double by = tanStart.y() - stDy - cs_arc_y;
        return bx * n2x + by * n2y;
    };

    const double geomDist = std::hypot(arcStart.x() - tanStart.x(),
                                       arcStart.y() - tanStart.y())
                          + std::hypot(arcStart.x() - arcCenter.x(),
                                       arcStart.y() - arcCenter.y());
    const double Ls_max = std::min(geomDist * 2.0, M_PI * R * 0.95);

    const int    nScan = 400;
    double Ls_lo = 1e-3;
    double f_lo  = evalF(Ls_lo);
    double Ls_hi = -1.0;

    for (int k = 1; k <= nScan; ++k) {
        const double Ls_k = Ls_max * k / static_cast<double>(nScan);
        const double f_k  = evalF(Ls_k);
        if (f_lo * f_k < 0.0) { Ls_hi = Ls_k; break; }
        f_lo = f_k;
        Ls_lo = Ls_k;
    }

    if (Ls_hi < 0.0) {
        qWarning() << "[AlignmentSolver] solveCA: f(Ls) has no sign change."
                   << "az2=" << qRadiansToDegrees(az2)
                   << "R="   << R
                   << "Ls_max=" << Ls_max;
        return result;
    }

    for (int iter = 0; iter < 80; ++iter) {
        const double Ls_mid = 0.5 * (Ls_lo + Ls_hi);
        if (std::abs(Ls_hi - Ls_lo) < 1e-4) { Ls_lo = Ls_mid; break; }
        if (evalF(Ls_lo) * evalF(Ls_mid) <= 0.0)
            Ls_hi = Ls_mid;
        else
            Ls_lo = Ls_mid;
    }
    const double Ls = Ls_lo;

    // ── Step 4: final geometry ────────────────────────────────────────────────
    const auto   elemF  = makeTransitionElement(spiralType, Ls, signR * R);
    const LocalFrame lf = elemF->localFrame(Ls);
    const double Xm     = lf.x;
    const double Ym     = lf.y;
    const double thetaS = lf.theta;

    const double azCS = az2 - thetaS;
    const double stDx = Xm * sA2 + Ym * cA2;
    const double stDy = Xm * cA2 - Ym * sA2;

    const QPointF csPoint(arcCenter.x() - signR * R * std::cos(azCS),
                          arcCenter.y() + signR * R * std::sin(azCS));
    const QPointF stPoint(csPoint.x() + stDx, csPoint.y() + stDy);

    // ── Step 5: trimmed arc  arcStart → CS ───────────────────────────────────
    const QPointF r_start = arcStart - arcCenter;
    const QPointF r_cs    = csPoint  - arcCenter;
    const double  crossV  = r_start.x() * r_cs.y() - r_start.y() * r_cs.x();
    const double  dotV    = r_start.x() * r_cs.x() + r_start.y() * r_cs.y();
    double dPhi = std::atan2(std::abs(crossV), dotV);
    if (signR > 0 && crossV > 0.0) dPhi = 2.0 * M_PI - dPhi;
    if (signR < 0 && crossV < 0.0) dPhi = 2.0 * M_PI - dPhi;

    result.valid          = true;
    result.arcStartPoint  = arcStart;
    result.csPoint        = csPoint;
    result.arcLen         = R * dPhi;
    result.azCS           = azCS;
    result.Ls             = Ls;
    result.R              = R;
    result.thetaS         = thetaS;
    result.stPoint        = stPoint;
    result.azST           = az2;

    return result;
}

// ============================================================================
//  solveACA  —  Arc₁ → Clothoid → Arc₂  (unknown Ls)
//
//  Problem:
//    Two Fixed circular arcs with absolute radii R₁ and R₂ separated by an
//    unknown-length clothoid.  The clothoid must be tangent to both arcs
//    (entry curvature κ₁ = 1/R₁, exit curvature κ₂ = 1/R₂, linearly varying).
//
//  Representation:
//    This is exactly an Egg (bi-quadratic) transition spanning from the exit
//    point of Arc₁ to the entry point of Arc₂.  For a trial length Ls the
//    equivalent full-clothoid length is:
//        Ls_eq = Ls · max(R₁,R₂) / |R₁ − R₂|
//    and the traversal starts at arc-length  Ls_start = Ls_eq − Ls  along the
//    equivalent clothoid (from the larger-radius end).
//
//  Gap function  f(Ls):
//    Given Ls, the EggTransitionElement provides the local-frame offset of
//    SC₂ from SC₁.  We compare the expected world position of SC₂ (derived
//    from the tangency condition on Arc₂) with the computed world position and
//    project the difference onto the cross-track direction of Arc₂ at SC₂.
//    Bisection drives f(Ls) → 0.
//
//  After solving, both arcs are trimmed:
//    Arc₁ loses its tail (its new PT = SC₁).
//    Arc₂ loses its head (its new PC = SC₂).
// ============================================================================

SolvedACA AlignmentSolver::solveACA(
    QPointF arc1Center, double arc1Radius,
    QPointF arc1Start,  double arc1AzStart,
    QPointF arc2Center, double arc2Radius,
    QPointF arc2End,    double arc2AzEnd,
    SpiralType /*spiralType*/)
{
    SolvedACA result;

    const double R1 = std::abs(arc1Radius);
    const double R2 = std::abs(arc2Radius);
    if (R1 < 1e-9 || R2 < 1e-9) {
        qWarning() << "[AlignmentSolver] solveACA: arc radius ≈ 0";
        return result;
    }
    if (std::abs(R1 - R2) < 1e-6) {
        qWarning() << "[AlignmentSolver] solveACA: R1 ≈ R2 — degenerate (concentric arcs)";
        return result;
    }

    // ── Determine turn signs from arc centres relative to an initial guess ──
    //   signR1 = +1 if Arc₁ turns right, -1 if left.
    //   signR2 = +1 if Arc₂ turns right, -1 if left.
    //   We derive signR from the cross-product of the radius vector at the
    //   arc's entry point and the arc's travel direction.
    //
    //   For Arc₁, we compute at arc1Start (PC₁):
    //     r1 = arc1Start − arc1Center
    //     tangent1 direction (dE, dN) derived from arc1AzStart
    //     signR1 = +1 if centre is to the RIGHT of tangent.
    //
    //   For Arc₂, we compute at arc2End (PT₂):
    //     The centre must be to the RIGHT for a right-hand arc.
    {
        const double sA1 = std::sin(arc1AzStart), cA1 = std::cos(arc1AzStart);
        const double n1x = cA1, n1y = -sA1;   // right-perp of az1
        const double side1 = (arc1Center.x() - arc1Start.x()) * n1x
                           + (arc1Center.y() - arc1Start.y()) * n1y;
        // side1 > 0 means centre is to the right → right-hand curve
        (void)side1; // used only in sign computation below
    }

    // Signed radii used internally:
    //   Derive signR1 from (arc1Center relative to the travel direction at arc1Start)
    //   Derive signR2 from (arc2Center relative to the travel direction at arc2End)
    auto computeSignR = [](QPointF centre, QPointF refPt, double az) -> int {
        const double n_x = std::cos(az), n_y = -std::sin(az);   // right-perp
        const double side = (centre.x() - refPt.x()) * n_x
                          + (centre.y() - refPt.y()) * n_y;
        return (side >= 0.0) ? 1 : -1;
    };

    const int signR1 = computeSignR(arc1Center, arc1Start, arc1AzStart);
    const int signR2 = computeSignR(arc2Center, arc2End,   arc2AzEnd - M_PI);
    // Note: at PT₂ we travel in the forward direction az2, so the right-perp
    // check uses az2AzEnd.  But arc2AzEnd is the forward azimuth at PT₂,
    // so the travel direction at PT₂ is arc2AzEnd.  The centre should be on
    // the same side at both entry and exit.
    const int signR2b = computeSignR(arc2Center, arc2End, arc2AzEnd);
    (void)signR2; // resolve below

    // Use signR2b (evaluated at PT₂ with forward-travel direction) for Arc₂.
    const double signedR1 = signR1 * R1;
    const double signedR2 = signR2b * R2;

    // ── Gap-function evaluation ───────────────────────────────────────────────
    //
    //  For trial Ls, the spiral sweeps from curvature 1/R1 to curvature 1/R2.
    //  We use an EggTransitionElement internally to get the local-frame geometry.
    //
    //  SC₁ on Arc₁: The spiral enters Arc₁ with azimuth azSC1.  The tangency
    //  condition pins azSC1 to a specific point on Arc₁:
    //      SC₁ = arc1Center + (-signR1·R1·cos azSC1,  +signR1·R1·sin azSC1)
    //
    //  SC₂ on Arc₂: Similarly
    //      SC₂_expected = arc2Center + (-signR2·R2·cos azSC2, +signR2·R2·sin azSC2)
    //
    //  The EggTransitionElement provides (Xm, Ym, thetaTotal) from SC₁ to SC₂.
    //  Given azSC1 we can compute the world position of SC₂:
    //      SC₂_computed = SC₁ + (Xm·sin(azSC1) + Ym·cos(azSC1),
    //                             Xm·cos(azSC1) − Ym·sin(azSC1))
    //
    //  The error is projected onto the cross-track direction at SC₂ (n_SC2):
    //      f(Ls) = (SC₂_computed − SC₂_expected) · n_SC2
    //
    //  This makes the gap function insensitive to along-track position of SC₁
    //  (which slides freely along Arc₁).

    auto evalF = [&](double Ls) -> double {
        // Build EggTransitionElement for this Ls
        EggTransitionElement egg(signedR1, signedR2, Ls);

        // Local frame at full Ls from SC₁
        const LocalFrame lf = egg.localFrame(Ls);
        const double Xm      = lf.x;
        const double Ym      = lf.y;
        const double thetaS  = lf.theta;   // accumulated rotation from SC₁ to SC₂

        // azSC1 is unknown but determined by: exit azimuth azSC2 = azSC1 + thetaS
        // And SC₂ must satisfy tangency with Arc₂:
        //   azSC2 is the forward tangent of Arc₂ at SC₂.
        //
        // We iterate: guess azSC1, compute SC₁ and SC₂_computed, compare with
        // SC₂_expected.  But to keep a simple 1-D bisection we pick the degree
        // of freedom as Ls; azSC1 is recovered from azSC2.
        //
        // Strategy: use the direction from arc1Center to arc2Center as a seed
        // to estimate azSC1.  For the bisection we only need the SIGN of f(Ls),
        // not its exact value, so an inner Newton loop on azSC1 is fast.
        //
        // Simpler consistent approach: for a given Ls, solve for azSC1 such
        // that |SC₂_computed − arc2Center| = R₂  (point on Arc₂ circle).
        // Then check tangency: (SC₂_computed − arc2Center) should be perpendicular
        // to the spiral tangent direction at SC₂.
        //
        // Cross-track gap: project (SC₂_computed − SC₂_expected) onto the
        // cross-track direction of arc2.
        //
        // We solve the inner equation  |SC₂_computed(azSC1) − arc2Center|² = R₂²
        // analytically.
        //
        //  SC₂_computed(azSC1) = SC₁(azSC1) + T·(sin azSC1, cos azSC1) + C
        //  where T = Xm, and the cross-track term Ym is rotated by azSC1.
        //
        //  Let  SC₁(azSC1) = arc1Center + (-signR1·R1·cos azSC1, +signR1·R1·sin azSC1)
        //  Then:
        //    SC₂_computed.x = arc1Center.x - signR1·R1·cos(azSC1)
        //                      + Xm·sin(azSC1) + Ym·cos(azSC1)
        //    SC₂_computed.y = arc1Center.y + signR1·R1·sin(azSC1)
        //                      + Xm·cos(azSC1) - Ym·sin(azSC1)
        //
        //  |SC₂_computed − arc2Center|² = R₂²  is a transcendental equation
        //  in azSC1.  We solve it by a fast bisection on azSC1 as an inner loop.
        //
        //  For the OUTER bisection (over Ls), we evaluate the signed gap after
        //  finding the inner azSC1.

        // Inner bisection: find azSC1 such that SC₂ lies on Arc₂ circle
        // Search range for azSC1: full circle, but start near initial estimate
        const double azSC1_init = std::atan2(arc2Center.x() - arc1Center.x(),
                                             arc2Center.y() - arc1Center.y());

        auto sc2FromAzSC1 = [&](double azSC1) -> QPointF {
            const double sc1x = arc1Center.x() - signR1 * R1 * std::cos(azSC1);
            const double sc1y = arc1Center.y() + signR1 * R1 * std::sin(azSC1);
            const double dx   = Xm * std::sin(azSC1) + Ym * std::cos(azSC1);
            const double dy   = Xm * std::cos(azSC1) - Ym * std::sin(azSC1);
            return QPointF(sc1x + dx, sc1y + dy);
        };

        auto radialError = [&](double azSC1) -> double {
            const QPointF sc2 = sc2FromAzSC1(azSC1);
            const double dr2 = (sc2.x() - arc2Center.x()) * (sc2.x() - arc2Center.x())
                             + (sc2.y() - arc2Center.y()) * (sc2.y() - arc2Center.y());
            return dr2 - R2 * R2;
        };

        // Scan for a bracket near the estimate
        double az_lo = azSC1_init - M_PI;
        double az_hi = azSC1_init + M_PI;
        double f_lo  = radialError(az_lo);
        double az_br = -999.0;

        {
            const int nInner = 360;
            double prev_f = f_lo;
            for (int k = 1; k <= nInner; ++k) {
                const double az_k = az_lo + 2.0 * M_PI * k / nInner;
                const double f_k  = radialError(az_k);
                if (prev_f * f_k < 0.0) { az_br = az_k; az_hi = az_k; az_lo = az_lo + 2.0 * M_PI * (k-1) / nInner; break; }
                prev_f = f_k;
            }
        }

        if (az_br < -900.0) {
            // No intersection: spiral can't reach Arc₂ at this Ls
            // Return a large positive error (gap too big)
            return 1e9;
        }

        // Bisect inner loop to find azSC1
        for (int iter = 0; iter < 60; ++iter) {
            const double az_mid = 0.5 * (az_lo + az_hi);
            if (std::abs(az_hi - az_lo) < 1e-9) { az_lo = az_mid; break; }
            if (radialError(az_lo) * radialError(az_mid) <= 0.0)
                az_hi = az_mid;
            else
                az_lo = az_mid;
        }
        const double azSC1 = az_lo;

        // azSC2 = azSC1 + thetaS
        const double azSC2 = azSC1 + thetaS;

        // Expected SC₂ from tangency condition on Arc₂
        const double sc2_exp_x = arc2Center.x() - signR2b * R2 * std::cos(azSC2);
        const double sc2_exp_y = arc2Center.y() + signR2b * R2 * std::sin(azSC2);

        // Computed SC₂
        const QPointF sc2_comp = sc2FromAzSC1(azSC1);

        // Cross-track gap: project difference onto right-perp of azSC2
        const double nx = std::cos(azSC2), ny = -std::sin(azSC2);
        return (sc2_comp.x() - sc2_exp_x) * nx
             + (sc2_comp.y() - sc2_exp_y) * ny;
    };

    // ── Outer bisection over Ls ───────────────────────────────────────────────
    const double geomDist = std::hypot(arc2End.x() - arc1Start.x(),
                                       arc2End.y() - arc1Start.y())
                          + R1 + R2;
    const double Req    = (R1 * R2) / std::abs(R1 - R2);   // equivalent radius
    const double Ls_max = std::min(geomDist * 2.0, M_PI * Req * 0.95);

    const int    nScan = 400;
    double Ls_lo = 1e-3;
    double f_lo  = evalF(Ls_lo);
    double Ls_hi = -1.0;

    for (int k = 1; k <= nScan; ++k) {
        const double Ls_k = Ls_max * k / static_cast<double>(nScan);
        const double f_k  = evalF(Ls_k);
        if (f_lo * f_k < 0.0) { Ls_hi = Ls_k; break; }
        f_lo = f_k;
        Ls_lo = Ls_k;
    }

    if (Ls_hi < 0.0) {
        qWarning() << "[AlignmentSolver] solveACA: f(Ls) has no sign change."
                   << "R1=" << R1 << "R2=" << R2 << "Ls_max=" << Ls_max;
        return result;
    }

    for (int iter = 0; iter < 80; ++iter) {
        const double Ls_mid = 0.5 * (Ls_lo + Ls_hi);
        if (std::abs(Ls_hi - Ls_lo) < 1e-4) { Ls_lo = Ls_mid; break; }
        if (evalF(Ls_lo) * evalF(Ls_mid) <= 0.0)
            Ls_hi = Ls_mid;
        else
            Ls_lo = Ls_mid;
    }
    const double Ls = Ls_lo;

    // ── Final geometry ────────────────────────────────────────────────────────
    EggTransitionElement eggF(signedR1, signedR2, Ls);
    const LocalFrame lfF = eggF.localFrame(Ls);
    const double XmF      = lfF.x;
    const double YmF      = lfF.y;
    const double thetaSF  = lfF.theta;

    // Recover azSC1 with the same inner bisection logic
    const double azSC1_init = std::atan2(arc2Center.x() - arc1Center.x(),
                                         arc2Center.y() - arc1Center.y());

    auto sc2FromAzSC1_F = [&](double azSC1) -> QPointF {
        const double sc1x = arc1Center.x() - signR1 * R1 * std::cos(azSC1);
        const double sc1y = arc1Center.y() + signR1 * R1 * std::sin(azSC1);
        const double dx   = XmF * std::sin(azSC1) + YmF * std::cos(azSC1);
        const double dy   = XmF * std::cos(azSC1) - YmF * std::sin(azSC1);
        return QPointF(sc1x + dx, sc1y + dy);
    };
    auto radialError_F = [&](double azSC1) -> double {
        const QPointF sc2 = sc2FromAzSC1_F(azSC1);
        const double dr2 = (sc2.x() - arc2Center.x()) * (sc2.x() - arc2Center.x())
                         + (sc2.y() - arc2Center.y()) * (sc2.y() - arc2Center.y());
        return dr2 - R2 * R2;
    };

    double az_lo_F = azSC1_init - M_PI, az_hi_F = azSC1_init + M_PI;
    {
        const int nI = 360;
        double prev_f = radialError_F(az_lo_F);
        for (int k = 1; k <= nI; ++k) {
            const double az_k = (azSC1_init - M_PI) + 2.0 * M_PI * k / nI;
            const double f_k  = radialError_F(az_k);
            if (prev_f * f_k < 0.0) {
                az_hi_F = az_k;
                az_lo_F = (azSC1_init - M_PI) + 2.0 * M_PI * (k-1) / nI;
                break;
            }
            prev_f = f_k;
        }
    }
    for (int iter = 0; iter < 60; ++iter) {
        const double az_mid = 0.5 * (az_lo_F + az_hi_F);
        if (std::abs(az_hi_F - az_lo_F) < 1e-9) { az_lo_F = az_mid; break; }
        if (radialError_F(az_lo_F) * radialError_F(az_mid) <= 0.0)
            az_hi_F = az_mid;
        else
            az_lo_F = az_mid;
    }
    const double azSC1 = az_lo_F;
    const double azSC2 = azSC1 + thetaSF;

    // SC₁ and SC₂ world positions
    const QPointF sc1Point(arc1Center.x() - signR1 * R1 * std::cos(azSC1),
                           arc1Center.y() + signR1 * R1 * std::sin(azSC1));
    const QPointF sc2Point(arc2Center.x() - signR2b * R2 * std::cos(azSC2),
                           arc2Center.y() + signR2b * R2 * std::sin(azSC2));

    // ── Trimmed arc lengths ───────────────────────────────────────────────────
    auto arcAngle = [](QPointF from, QPointF to, QPointF centre, int signR) -> double {
        const QPointF rf = from - centre;
        const QPointF rt = to   - centre;
        const double cross = rf.x() * rt.y() - rf.y() * rt.x();
        const double dot   = rf.x() * rt.x() + rf.y() * rt.y();
        double phi = std::atan2(std::abs(cross), dot);
        if (signR > 0 && cross > 0.0) phi = 2.0 * M_PI - phi;
        if (signR < 0 && cross < 0.0) phi = 2.0 * M_PI - phi;
        return phi;
    };

    const double phi1 = arcAngle(arc1Start, sc1Point, arc1Center, signR1);
    const double phi2 = arcAngle(sc2Point,  arc2End,  arc2Center, signR2b);

    result.valid         = true;
    result.arc1StartPoint = arc1Start;
    result.sc1Point       = sc1Point;
    result.arc1Len        = R1 * phi1;
    result.azSC1          = azSC1;
    result.Ls             = Ls;
    result.R1             = R1;
    result.R2             = R2;
    result.thetaS         = thetaSF;
    result.sc2Point       = sc2Point;
    result.arc2EndPoint   = arc2End;
    result.arc2Len        = R2 * phi2;
    result.azSC2          = azSC2;

    return result;
}

// ============================================================================
//  solveCompoundChain  — S0 C0 S1 C1 ... Sn（N≥2 個圓弧），封閉解
//
//  結構上是 solveSCS 的直接推廣：
//    Step 1  逐段計算每段緩和曲線的 (Xm, Ym, θs)。邊界兩段（index 0、N）
//            用既有 makeTransitionElement()（曲率 0 ↔ 1/R，與 solveSCS
//            entry/exit spiral 相同）；中段（index 1..N-1，介於兩個不同
//            半徑的圓弧之間）用 EggTransitionElement(r1, r2, Ls) ——它的
//            localFrame(Ls) 已經是「從入端切線量起的局部座標」，用法與
//            TransitionElement::localFrame() 完全一致（見 solveACA 的
//            evalF() 內對同一個 API 的用法），可以直接沿用同一套疊加公式。
//  Step 2  Σthetas = 所有緩和曲線轉角總和；leftover = Δ − Σthetas；
//          每段圓弧心角 = leftover / N（平分規則，見 AlignmentDocument.h
//          CompoundChainSpec 的說明）。
//  Step 3  以未修正的 TS = tanStartPrev 為起點，依序疊加緩和曲線／圓弧的
//          局部偏移，走到初步 ST。
//  Step 4  比照 solveSCS 的 d5：量測初步 ST 相對出口切線的橫向間隙，解析
//          解出 TS 應沿入口切線平移的距離，修正後即可精確閉合。
// ============================================================================

SolvedCompoundChain AlignmentSolver::solveCompoundChain(
    const QVector<double>&     arcRadii,
    const QVector<double>&     spiralLengths,
    const QVector<SpiralType>& spiralTypes,
    const QPointF& tanStartPrev, const QPointF& tanEndPrev,
    const QPointF& tanStartNext, const QPointF& tanEndNext,
    const QVector<double>&     givenArcAngles,
    const CompoundChainUnknown& unknown)
{
    SolvedCompoundChain result;

    const int N = arcRadii.size();
    if (N < 2) {
        qWarning() << "[AlignmentSolver] solveCompoundChain: needs N>=2 arcs, got" << N;
        return result;
    }
    if (spiralLengths.size() != N + 1 || spiralTypes.size() != N + 1) {
        qWarning() << "[AlignmentSolver] solveCompoundChain: spiralLengths/spiralTypes size mismatch"
                   << "(expected" << (N + 1) << ")";
        return result;
    }
    if (!givenArcAngles.isEmpty() && givenArcAngles.size() != N) {
        qWarning() << "[AlignmentSolver] solveCompoundChain: givenArcAngles size mismatch"
                   << "(expected" << N << "or empty)";
        return result;
    }

    // ── Phase 4：反解模式的前提驗證 ───────────────────────────────────────
    //  見 CompoundChainUnknown 的說明：只有 1 條 Δθ 方程式可用，所以指定
    //  未知數時，所有圓弧心角都必須是釘死的固定值（不可再混用自動平分，
    //  否則 Δθ 會被平分吸收、變成恒成立、沒有方程式可反解）。
    if (unknown.kind != CompoundChainUnknownKind::None) {
        if (givenArcAngles.size() != N) {
            qWarning() << "[AlignmentSolver] solveCompoundChain: unknown.kind != None requires"
                          " givenArcAngles fully specified (size==" << N << "), got size="
                       << givenArcAngles.size();
            return result;
        }
        for (int k = 0; k < N; ++k) {
            if (std::abs(givenArcAngles[k]) <= 1e-12) {
                qWarning() << "[AlignmentSolver] solveCompoundChain: unknown.kind != None requires"
                              " EVERY arc central angle to be pinned (non-zero); arc" << k
                           << "is left to auto-split, which absorbs Δθ and leaves no equation"
                              " to solve the unknown from.";
                return result;
            }
        }
        if (unknown.kind == CompoundChainUnknownKind::SpiralLength
            && (unknown.index < 0 || unknown.index > N)) {
            qWarning() << "[AlignmentSolver] solveCompoundChain: unknown spiral index out of range"
                       << unknown.index;
            return result;
        }
        if (unknown.kind == CompoundChainUnknownKind::ArcRadius
            && (unknown.index < 0 || unknown.index >= N)) {
            qWarning() << "[AlignmentSolver] solveCompoundChain: unknown arc index out of range"
                       << unknown.index;
            return result;
        }
    }

    for (int k = 0; k < N; ++k) {
        if (unknown.kind == CompoundChainUnknownKind::ArcRadius && k == unknown.index) continue;
        if (std::abs(arcRadii[k]) < 1e-9) {
            qWarning() << "[AlignmentSolver] solveCompoundChain: arcRadii[" << k << "] ≈ 0";
            return result;
        }
    }

    // ── Step 0: boundary tangent azimuths / turning angle ────────────────────
    const double az1 = azimuthOf(tanStartPrev, tanEndPrev);
    const double az2 = azimuthOf(tanStartNext, tanEndNext);
    const double delta    = normaliseAngle(az2 - az1);
    const double absDelta = std::abs(delta);
    if (absDelta < 1e-9) {
        qWarning() << "[AlignmentSolver] solveCompoundChain: Δ ≈ 0 (parallel tangents)";
        return result;
    }
    const int    signR = (delta >= 0.0) ? 1 : -1;

    // ── Phase 4：若指定了未知數，先用「掃描找變號區間 + 二分法」反解 ────────
    //  殘差函式 f(x) = Δ − Σ緩和曲線轉角(x) − Σ已釘死的圓弧心角。
    //  x 是 spiralLengths[unknown.index] 或 arcRadii[unknown.index] 的試值。
    QVector<double> resolvedArcRadii     = arcRadii;
    QVector<double> resolvedSpiralLengths = spiralLengths;
    int    solveIterations  = 0;
    double solveResidual    = 0.0;

    if (unknown.kind != CompoundChainUnknownKind::None) {
        double sumPinnedFull = 0.0;
        for (int k = 0; k < N; ++k) sumPinnedFull += signR * std::abs(givenArcAngles[k]);

        // 輕量版 Step 1：只算 Σθ，不需要 Xm/Ym（二分法搜尋階段用，避免
        // 重複建構完整節點序列）。
        auto sumAllThetas = [&](const QVector<double>& radiiTry,
                                 const QVector<double>& lensTry) -> double {
            double sum = 0.0;
            for (int k = 0; k <= N; ++k) {
                const double Ls = lensTry[k];
                if (Ls < 1e-9) continue;
                double th = 0.0;
                if (k == 0) {
                    const auto elem = makeTransitionElement(spiralTypes[0], Ls, signR * radiiTry[0]);
                    th = elem->localFrame(Ls).theta;
                } else if (k == N) {
                    const auto elem = makeTransitionElement(spiralTypes[N], Ls, signR * radiiTry[N - 1]);
                    th = elem->localFrame(Ls).theta;
                } else {
                    EggTransitionElement egg(signR * radiiTry[k - 1], signR * radiiTry[k], Ls);
                    th = egg.localFrame(Ls).theta;
                }
                sum += th;
            }
            return sum;
        };

        auto evalResidual = [&](double x) -> double {
            QVector<double> radiiTry = arcRadii;
            QVector<double> lensTry  = spiralLengths;
            if (unknown.kind == CompoundChainUnknownKind::ArcRadius) radiiTry[unknown.index] = x;
            else                                                     lensTry[unknown.index]  = x;
            return delta - sumAllThetas(radiiTry, lensTry) - sumPinnedFull;
        };

        // 搜尋範圍：緩和曲線長度用「切線間距」量級估計上限（與 solveLC/CA
        // 既有的 Ls_max 啟發式同精神）；圓弧半徑用「其餘已知半徑」量級估計。
        double xLo, xHi;
        if (unknown.kind == CompoundChainUnknownKind::SpiralLength) {
            const double geomDist = std::hypot(tanStartNext.x() - tanStartPrev.x(),
                                               tanStartNext.y() - tanStartPrev.y());
            xLo = 1e-3;
            xHi = std::max(500.0, geomDist);
        } else {
            double otherRadiiMax = 0.0;
            for (int k = 0; k < N; ++k) {
                if (k == unknown.index) continue;
                otherRadiiMax = std::max(otherRadiiMax, std::abs(arcRadii[k]));
            }
            xLo = 1.0;
            xHi = std::max(100000.0, otherRadiiMax * 50.0);
        }

        const int nScan = 400;
        double x_lo = xLo, f_lo = evalResidual(xLo);
        double x_hi = -1.0;
        for (int k = 1; k <= nScan; ++k) {
            const double x_k = xLo + (xHi - xLo) * k / static_cast<double>(nScan);
            const double f_k = evalResidual(x_k);
            if (f_lo * f_k < 0.0) { x_hi = x_k; break; }
            f_lo = f_k;
            x_lo = x_k;
        }

        if (x_hi < 0.0) {
            qWarning() << "[AlignmentSolver] solveCompoundChain: no sign change found for the"
                          " unknown (kind=" << static_cast<int>(unknown.kind)
                       << ", index=" << unknown.index << ") over [" << xLo << "," << xHi << "]"
                       << "-- pinned arc angles may be inconsistent with Δ="
                       << qRadiansToDegrees(absDelta) << "deg";
            return result;
        }

        double x_bisect_lo = x_lo, x_bisect_hi = x_hi;
        int iter = 0;
        for (; iter < 80; ++iter) {
            const double x_mid = 0.5 * (x_bisect_lo + x_bisect_hi);
            if (std::abs(x_bisect_hi - x_bisect_lo) < 1e-6) { x_bisect_lo = x_mid; break; }
            if (evalResidual(x_bisect_lo) * evalResidual(x_mid) <= 0.0)
                x_bisect_hi = x_mid;
            else
                x_bisect_lo = x_mid;
        }
        const double xSolved = x_bisect_lo;
        solveIterations = iter + 1;
        solveResidual   = std::abs(evalResidual(xSolved));

        if (unknown.kind == CompoundChainUnknownKind::ArcRadius) {
            resolvedArcRadii[unknown.index] = xSolved;
        } else {
            resolvedSpiralLengths[unknown.index] = xSolved;
        }
    }

    // ── Step 1: per-spiral (Xm, Ym, theta), boundary via TransitionElement, ──
    //            interior via EggTransitionElement (two-curvature segment).
    //            使用 resolved 陣列：unknown.kind==None 時與 arcRadii/
    //            spiralLengths 完全相同（零回歸）；否則已含反解結果。
    QVector<double> Xm(N + 1, 0.0), Ym(N + 1, 0.0), theta(N + 1, 0.0);
    for (int k = 0; k <= N; ++k) {
        const double Ls = resolvedSpiralLengths[k];
        if (Ls < 1e-9) continue;   // 省略此段緩和曲線：貢獻 0

        if (k == 0) {
            const auto elem = makeTransitionElement(spiralTypes[0], Ls, signR * resolvedArcRadii[0]);
            const LocalFrame lf = elem->localFrame(Ls);
            Xm[0] = lf.x; Ym[0] = lf.y; theta[0] = lf.theta;
        } else if (k == N) {
            const auto elem = makeTransitionElement(spiralTypes[N], Ls, signR * resolvedArcRadii[N - 1]);
            const LocalFrame lf = elem->localFrame(Ls);
            Xm[N] = lf.x; Ym[N] = lf.y; theta[N] = lf.theta;
        } else {
            EggTransitionElement egg(signR * resolvedArcRadii[k - 1], signR * resolvedArcRadii[k], Ls);
            const LocalFrame lf = egg.localFrame(Ls);
            Xm[k] = lf.x; Ym[k] = lf.y; theta[k] = lf.theta;
        }
    }

    // ── Step 2: fixed (pinned) arc angles are used as-is; the turning angle
    //            left over after subtracting all spiral thetas AND all pinned
    //            arc angles is split equally among the remaining (auto) arcs.
    //            With no givenArcAngles at all, this reduces to the original
    //            "equal split over all N arcs" rule. When unknown.kind !=
    //            None, autoCount is guaranteed 0 by the validation above, so
    //            arcAngleEach is unused (every arc takes its pinned value).
    double sumThetas = 0.0;
    for (int k = 0; k <= N; ++k) sumThetas += theta[k];

    QVector<double> pinnedAngle(N, 0.0);   // signed, 0 = not pinned
    double sumPinned = 0.0;
    int    autoCount = N;
    if (!givenArcAngles.isEmpty()) {
        for (int k = 0; k < N; ++k) {
            if (std::abs(givenArcAngles[k]) > 1e-12) {
                pinnedAngle[k] = signR * std::abs(givenArcAngles[k]);
                sumPinned += pinnedAngle[k];
                --autoCount;
            }
        }
    }

    const double leftover     = delta - sumThetas - sumPinned;
    const double arcAngleEach = (autoCount > 0) ? (leftover / static_cast<double>(autoCount)) : 0.0;

    if (autoCount > 0 && arcAngleEach * signR < -1e-9) {
        qWarning() << "[AlignmentSolver] solveCompoundChain: spirals + pinned arcs overlap"
                   << "(Δ=" << qRadiansToDegrees(absDelta)
                   << "deg, Σθ+Σpinned=" << qRadiansToDegrees(std::abs(sumThetas + sumPinned)) << "deg)";
        return result;
    }

    // ── Step 3: forward walk from the uncorrected TS = tanStartPrev ─────────
    //  Nodes are appended inline (rather than reconstructed afterward from
    //  index parity) so that omitted spirals — resolvedSpiralLengths[k] <
    //  1e-9, which contribute no node of their own, exactly mirroring
    //  solveSCS's L1=0/L2=0 degenerate handling — never desynchronise node
    //  bookkeeping from segment bookkeeping.
    struct RawNode { QPointF pt; double az; bool isArcStart; double segLength; double radius; };
    QVector<RawNode> raw;
    QVector<double>  arcAngles(N, 0.0);
    QVector<double>  arcLens(N, 0.0);

    QPointF pos = tanStartPrev;
    double  az  = az1;

    // TS: next segment is spiral 0 (or, if omitted, arc 0 directly).
    raw.append({ pos, az, false, resolvedSpiralLengths[0], resolvedArcRadii[0] });

    if (resolvedSpiralLengths[0] >= 1e-9) {
        const double dx =  Xm[0] * std::sin(az1) + Ym[0] * std::cos(az1);
        const double dy =  Xm[0] * std::cos(az1) - Ym[0] * std::sin(az1);
        pos += QPointF(dx, dy);
        az  += theta[0];
        // SC0: next segment is arc 0.
        raw.append({ pos, az, true, 0.0 /*filled below*/, resolvedArcRadii[0] });
    } else {
        // Entry spiral omitted: TS itself is where arc 0 begins.
        raw.last().isArcStart = true;
    }

    for (int k = 0; k < N; ++k) {
        const double thisArcAngle = (std::abs(pinnedAngle[k]) > 1e-12) ? pinnedAngle[k] : arcAngleEach;
        const double azStart = az;
        const double azEnd   = az + thisArcAngle;
        const double R       = signR * resolvedArcRadii[k];

        const double dx = R * (std::cos(azStart) - std::cos(azEnd));
        const double dy = R * (std::sin(azEnd)   - std::sin(azStart));
        pos += QPointF(dx, dy);
        az   = azEnd;

        arcAngles[k] = thisArcAngle;
        arcLens[k]   = resolvedArcRadii[k] * std::abs(thisArcAngle);
        if (!raw.isEmpty() && raw.last().isArcStart) raw.last().segLength = arcLens[k];

        const bool isLastArc = (k == N - 1);
        if (isLastArc) {
            // CS(N-1): next segment is the exit spiral (or ST directly if omitted).
            raw.append({ pos, az, false, resolvedSpiralLengths[N], resolvedArcRadii[N - 1] });
        } else if (resolvedSpiralLengths[k + 1] >= 1e-9) {
            // CS_k: next segment is interior spiral (k+1).
            raw.append({ pos, az, false, resolvedSpiralLengths[k + 1], resolvedArcRadii[k] });

            const double dxs =  Xm[k + 1] * std::sin(az) + Ym[k + 1] * std::cos(az);
            const double dys =  Xm[k + 1] * std::cos(az) - Ym[k + 1] * std::sin(az);
            pos += QPointF(dxs, dys);
            az  += theta[k + 1];
            // SC_{k+1}: next segment is arc (k+1).
            raw.append({ pos, az, true, 0.0 /*filled at next arc iter*/, resolvedArcRadii[k + 1] });
        }
        // else: interior spiral (k+1) omitted — arc k+1 starts immediately at
        // this same CS_k point; the arc-start node for arc(k+1) is simply the
        // CS_k node just appended above with isArcStart still false. Retag it:
        else {
            raw.last().isArcStart = true;
            raw.last().radius     = resolvedArcRadii[k + 1];
        }
    }

    // Final (exit) spiral: mirrored formula referenced at the KNOWN target
    // azimuth az2 (identical role to solveSCS Step 7c / solveCA's stDx/stDy).
    QPointF stRaw = pos;
    if (resolvedSpiralLengths[N] >= 1e-9) {
        const double stDx =  Xm[N] * std::sin(az2) + Ym[N] * std::cos(az2);
        const double stDy =  Xm[N] * std::cos(az2) - Ym[N] * std::sin(az2);
        stRaw = pos + QPointF(stDx, stDy);
    }
    // ST: terminal node, no following segment.
    raw.append({ stRaw, az2, false, 0.0, resolvedArcRadii[N - 1] });

    // ── Step 4: cross-track gap → analytic along-tangent slide correction ───
    const double sA2 = std::sin(az2), cA2 = std::cos(az2);
    const double twDist = (stRaw.x() - tanStartNext.x()) * cA2
                        - (stRaw.y() - tanStartNext.y()) * sA2;
    const double d5 = twDist / std::sin(absDelta) * signR;

    const QPointF shift(d5 * std::sin(az1), d5 * std::cos(az1));

    // ── Populate result: nodes = raw + shift ─────────────────────────────────
    result.valid = true;
    result.spiralLengths = resolvedSpiralLengths;
    result.spiralTypes   = spiralTypes;
    result.arcRadii       = resolvedArcRadii;
    result.arcAngles     = arcAngles;
    result.arcLengths    = arcLens;
    result.iterations    = solveIterations;
    result.residualNorm  = solveResidual;

    result.nodes.reserve(raw.size());
    for (const RawNode& rn : raw) {
        CompoundChainNode node;
        node.pt         = rn.pt + shift;
        node.az         = rn.az;
        node.isArcStart = rn.isArcStart;
        node.segLength  = rn.segLength;
        node.radius     = rn.radius;
        result.nodes.append(node);
    }

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

    // ── SS 交會虛擬切線：快取原始（未受任何裁切污染的）方位角 ─────────
    //
    //  SS 交會點的 Fixed Tangent 只有 kSSEpsilon（約 1 微米）長，純粹是
    //  方向載體。下面 Pass 2b 在裁切它其中一端（tanStart 或 tanEnd）時，
    //  裁切位移量通常是毫米級 —— 遠大於 kSSEpsilon —— 若不連動修正另一
    //  端，(tanEnd − tanStart) 這個方向向量會被裁切位移淹沒，
    //  azimuthOf() 算出的方向形同雜訊，導致下一個依附這條切線的 SCS
    //  群組解出錯誤位置的端點（外顯症狀：兩段緩和曲線間出現一小段方向
    //  不合理的殘留短切線）。這裡先把原始方向存下來，供 Pass 2b 裁切後
    //  重新外插使用。
    QVector<double> ssOrigAzimuth(n, 0.0);
    for (int i = 0; i < n; ++i) {
        if (elems[i].isSSJunction)
            ssOrigAzimuth[i] = azimuthOf(tanStart[i], tanEnd[i]);
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

    // Compound chain group data (N>=2 arcs) — stored per SpiralIn element
    // index, alongside (and mutually exclusive with) scsData.
    struct CompoundData {
        bool                 valid   = false;
        SolvedCompoundChain  chain;
        int                  lastIdx = -1;  ///< index of the final SpiralOut element
    };
    QVector<CompoundData> compoundData(n);

    // LC group data — stored per SpiralIn element index
    // (SpiralIn sits between Fixed Tangent and Fixed Arc)
    struct LCData {
        bool     valid   = false;
        SolvedLC lc;
        int      arcIdx  = -1;   ///< index of the Fixed CircularArc element
        int      tanIdx  = -1;   ///< index of the Fixed Tangent element (before)
    };
    QVector<LCData> lcData(n);

    // CA group data — stored per SpiralOut element index
    // (SpiralOut sits between Fixed Arc and Fixed Tangent)
    struct CAData {
        bool     valid   = false;
        SolvedCA ca;
        int      arcIdx  = -1;   ///< index of the Fixed CircularArc element
        int      tanIdx  = -1;   ///< index of the Fixed Tangent element (after)
    };
    QVector<CAData> caData(n);

    // ACA group data — stored per SpiralIn element index
    // (SpiralIn sits between Fixed Arc₁ and Fixed Arc₂)
    struct ACAData {
        bool      valid    = false;
        SolvedACA aca;
        int       arc1Idx  = -1;   ///< index of the Fixed Arc₁ element (before spiral)
        int       arc2Idx  = -1;   ///< index of the Fixed Arc₂ element (after spiral)
    };
    QVector<ACAData> acaData(n);

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
    //  Pass 2b — SCS group (SpiralIn + CircularArc + SpiralOut), extended to
    //            S0 C0 S1 C1 ... Sn compound chains (N≥2 arcs).
    //
    //  A group is identified by:
    //    elems[i]     : SpiralIn   (Floating) with tangentIdxBefore / After
    //    elems[i+1]   : CircularArc (Floating), same tangent references
    //    elems[i+2]   : SpiralOut  (Floating), same tangent references
    //    elems[i+3]   : CircularArc (Floating), same tangent references  ┐
    //    elems[i+4]   : SpiralOut  (Floating), same tangent references  ┘ repeat…
    //
    //  Exactly one (CircularArc, SpiralOut) pair after the initial SpiralIn
    //  is the pre-existing SCS case, solved unchanged via solveSCS() (zero
    //  regression risk — see AlignmentDocument.h addCompoundChain() doc).
    //  Two or more pairs is a compound chain (built by addCompoundChain()),
    //  solved via solveCompoundChain() instead.
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

        // ── Count how many (CircularArc, SpiralOut) pairs belong to this
        //    group: keep consuming pairs sharing the same tb/ta boundary. ──
        int arcCount = 1;
        while (true) {
            const int nextArcIdx = i + 1 + 2 * arcCount;
            const int nextSprIdx = i + 2 + 2 * arcCount;
            if (nextSprIdx >= n) break;
            if (elems[nextArcIdx].type != EditableElementType::CircularArc) break;
            if (elems[nextArcIdx].mode  != ConstraintMode::Floating)        break;
            if (elems[nextArcIdx].tangentIdxBefore != tb || elems[nextArcIdx].tangentIdxAfter != ta) break;
            if (elems[nextSprIdx].type != EditableElementType::SpiralOut) break;
            if (elems[nextSprIdx].mode != ConstraintMode::Floating)       break;
            if (elems[nextSprIdx].tangentIdxBefore != tb || elems[nextSprIdx].tangentIdxAfter != ta) break;
            ++arcCount;
        }

        if (arcCount == 1) {
            // ── Original single-arc SCS path (unchanged) ─────────────────
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

            // SS 交會虛擬切線方向保護：見本函式開頭 ssOrigAzimuth 的說明。
            // tanStart[ta] 剛被搬到本群組（group1）解出的 ST 點，若 ta 是
            // SS 虛擬切線，立刻依原始方位角重新外插 tanEnd[ta]，確保稍後
            // 共用這條切線的下一個群組（group2，其 tangentIdxBefore==ta）
            // 讀到的仍是正確方向，而不是被裁切位移淹沒的雜訊方向。
            if (elems[ta].isSSJunction) {
                constexpr double kSSEpsilon = 1.0e-6;
                tanEnd[ta] = tanStart[ta]
                        + kSSEpsilon * QPointF(std::sin(ssOrigAzimuth[ta]),
                                                std::cos(ssOrigAzimuth[ta]));
            }

            // Skip past arc and spiral-out in the outer loop
            // (i++ in the for-loop body makes it i+1 next, but we need i+3)
            // We tag them as handled so Pass 3 knows to skip them.
            arcData[i+1].valid = false; // will be rendered via scsData
            arcData[i+2].valid = false;

            i += 2; // skip arc and SpiralOut
            continue;
        }

        // ── Compound chain path (arcCount >= 2) ──────────────────────────
        const int N = arcCount;
        QVector<double>     arcRadii(N);
        QVector<double>     spiralLens(N + 1);
        QVector<SpiralType> spiralTys(N + 1);
        QVector<double>     arcAngles(N, 0.0);   // 0 = 交給 solveCompoundChain 自動平分
        bool                anyPinned = false;

        // Phase 4：偵測群組內是否有元素標記 isCompoundUnknown（最多 1 個，
        // 由 addCompoundChain() 的驗證保證）。
        CompoundChainUnknown unknown;

        spiralLens[0] = elems[i].length;
        spiralTys[0]  = elems[i].spiralType1;
        if (elems[i].isCompoundUnknown) {
            unknown.kind  = CompoundChainUnknownKind::SpiralLength;
            unknown.index = 0;
        }
        for (int k = 0; k < N; ++k) {
            const int arcIdx = i + 1 + 2 * k;
            const int sprIdx = i + 2 + 2 * k;
            arcRadii[k]       = std::abs(elems[arcIdx].radius);
            arcAngles[k]      = elems[arcIdx].centralAngle;   // 0 = 未釘死
            if (std::abs(arcAngles[k]) > 1e-12) anyPinned = true;
            if (elems[arcIdx].isCompoundUnknown) {
                unknown.kind  = CompoundChainUnknownKind::ArcRadius;
                unknown.index = k;
            }
            spiralLens[k + 1] = elems[sprIdx].length;
            spiralTys[k + 1]  = elems[sprIdx].spiralType2;   // SpiralOut 慣例：type 存於 spiralType2（見 setSpiralType()）
            if (elems[sprIdx].isCompoundUnknown) {
                unknown.kind  = CompoundChainUnknownKind::SpiralLength;
                unknown.index = k + 1;
            }
        }

        const SolvedCompoundChain chain = solveCompoundChain(
            arcRadii, spiralLens, spiralTys,
            tanStart[tb], tanEnd[tb],
            tanStart[ta], tanEnd[ta],
            anyPinned ? arcAngles : QVector<double>(),
            unknown);

        if (!chain.valid) {
            qWarning() << "[AlignmentSolver] Pass2b compound chain idx" << i
                       << ": solveCompoundChain() failed (N=" << N << ", unknown.kind="
                       << static_cast<int>(unknown.kind) << ")";
            continue;
        }

        CompoundData cd;
        cd.valid    = true;
        cd.chain    = chain;
        cd.lastIdx  = i + 2 * N;   // index of the final SpiralOut element
        compoundData[i] = cd;

        // Trim adjacent tangents
        tanEnd[tb]   = chain.nodes.first().pt;  // incoming tangent ends at TS
        tanStart[ta] = chain.nodes.last().pt;   // outgoing tangent starts at ST

        // Suppress every consumed element from the generic Pass 3 emission
        // (mirrors the arcData[..].valid=false pattern used for plain SCS).
        for (int k = 1; k <= 2 * N; ++k) {
            if (i + k < n) arcData[i + k].valid = false;
        }

        i += 2 * N; // skip all consumed arcs/spirals (i++ in for-loop lands one past lastIdx)
    }

    // ════════════════════════════════════════════════════════════════════════
    //  Pass 2b-SS — 消去 SS 交會點兩側 SCS 群組間的殘留短切線
    //
    //  Pass 2b 已用「原始（ALD 記錄）方位角」各自解過 group1（進入 SS）
    //  與 group2（離開 SS），且已修正了裁切造成的方向污染（見上方
    //  ssOrigAzimuth 說明）。但兩群組仍是獨立求解，端點通常不會恰好
    //  重合（ALD 原始資料獨立四捨五入所致），殘留一小段短切線。
    //
    //  這裡用 solveSSJunction()（使用者提出的方法：SS 點固定＋方位角
    //  微調求根）重新聯立求解，把這條虛擬切線的方位角當作待解未知
    //  數，驅動殘留間距收斂到 < 1e-6 m。收斂後，兩群組的端點已重合
    //  到遠低於量測與繪圖精度，短切線可視為不存在（Pass 3 沿用既有
    //  的 ST/TS 兩點表示法即可，其間距已 < 1e-6 m，等同直接 SS 相接）。
    //
    //  僅處理「兩側都是完整 SCS 群組」的 SS 交會點（G06U 等常見樣式）；
    //  若某一側是裸露緩和曲線、LC/CA/ACA 群組或線形起訖邊界，不在此
    //  求解範圍內，維持 Pass 2b 的結果（僅保留原始方向修正）。
    // ════════════════════════════════════════════════════════════════════════
    for (int t = 0; t < n; ++t) {
        if (!elems[t].isSSJunction) continue;

        int i0 = -1, i1 = -1;   // group1 (→SS) 與 group2 (SS→) 的 SpiralIn index
        for (int i = 0; i < n; ++i) {
            if (elems[i].type != EditableElementType::SpiralIn) continue;
            if (!scsData[i].valid) continue;
            if (elems[i].tangentIdxAfter  == t) i0 = i;
            if (elems[i].tangentIdxBefore == t) i1 = i;
        }
        if (i0 < 0 || i1 < 0) continue;   // 至少一側不是完整 SCS 群組，跳過

        const int tb1 = elems[i0].tangentIdxBefore;   // group1 上游穩定切線
        const int ta2 = elems[i1].tangentIdxAfter;    // group2 下游穩定切線
        if (tb1 < 0 || tb1 >= n || ta2 < 0 || ta2 >= n) continue;

        const double R1  = std::abs(elems[i0 + 1].radius);
        const double L1a = elems[i0].length;       // group1 入螺旋長
        const double L1b = elems[i0 + 2].length;    // group1 出螺旋長（接 SS）
        const double R2  = std::abs(elems[i1 + 1].radius);
        const double L2a = elems[i1].length;        // group2 入螺旋長（接 SS）
        const double L2b = elems[i1 + 2].length;    // group2 出螺旋長

        const SolvedSSJunction fit = solveSSJunction(
            /*ssPoint*/ elems[t].startPI, /*initialAzimuth*/ ssOrigAzimuth[t],
            R1, L1a, L1b, elems[i0].spiralType1, elems[i0].spiralType2,
            tanStart[tb1], tanEnd[tb1],
            R2, L2a, L2b, elems[i1].spiralType1, elems[i1].spiralType2,
            tanStart[ta2], tanEnd[ta2]);

        if (!fit.converged) {
            qWarning() << "[AlignmentSolver] Pass2b-SS: SS junction at tangent idx"
                       << t << "did not converge below tolerance (residual gap ="
                       << fit.gap << "m) -- keeping Pass 2b result";
            continue;
        }

        // 採用收斂後的解，取代 Pass 2b 用初始方位角算出的結果
        scsData[i0].scs = fit.group1;
        scsData[i1].scs = fit.group2;

        tanEnd[tb1]   = fit.group1.tsPoint;
        tanStart[t]   = fit.group1.stPoint;
        tanEnd[t]     = fit.group2.tsPoint;   // 收斂後與 tanStart[t] 幾乎重合（< 1e-6 m）
        tanStart[ta2] = fit.group2.stPoint;
    }

    // ════════════════════════════════════════════════════════════════════════
    //  Pass 2c — LC group  (Fixed Tangent → SpiralIn → Fixed CircularArc)
    //
    //  Identification: element i is a SpiralIn with mode==Floating and
    //    tangentIdxBefore pointing to a Fixed Tangent, while the very next
    //    emittable element (i+1) is a Fixed CircularArc.
    //
    //  The Clothoid length Ls is unknown; solveLC() finds it iteratively.
    //  The Fixed Arc's PC (startPI) is then updated to the solved SC point
    //  so that subsequent rendering shows the trimmed arc.
    // ════════════════════════════════════════════════════════════════════════
    for (int i = 0; i < n - 1; ++i) {
        if (elems[i].type != EditableElementType::SpiralIn) continue;
        if (elems[i].mode != ConstraintMode::Floating)      continue;

        const int arcI = i + 1;
        if (arcI >= n) continue;
        if (elems[arcI].type != EditableElementType::CircularArc) continue;
        if (elems[arcI].mode != ConstraintMode::Fixed)            continue;

        // Already handled by Pass 2b as part of an SCS group?
        if (scsData[i].valid) continue;

        const int tb = elems[i].tangentIdxBefore;
        if (tb < 0 || tb >= n) continue;
        if (elems[tb].type != EditableElementType::Tangent) continue;
        if (elems[tb].mode != ConstraintMode::Fixed)        continue;

        const SpiralType stype = elems[i].spiralType1;
        const double     Rarc  = std::abs(elems[arcI].radius);

        // arcData[arcI] was populated in Pass 1 (Fixed arc)
        if (!arcData[arcI].valid) {
            qWarning() << "[AlignmentSolver] Pass2c LC idx" << i
                       << ": Fixed arc at idx" << arcI << " not solved in Pass 1";
            continue;
        }

        const SolvedLC lc = solveLC(
            elems[arcI].arcCenter, Rarc,
            arcData[arcI].pt,   // arcEnd = original PT (unchanged)
            arcData[arcI].azPC + arcData[arcI].arcLen / Rarc, // azArcEnd
            tanStart[tb], tanEnd[tb],
            stype);

        if (!lc.valid) {
            qWarning() << "[AlignmentSolver] Pass2c LC idx" << i << ": solveLC failed";
            continue;
        }

        // Store result
        LCData ld;
        ld.valid  = true;
        ld.lc     = lc;
        ld.arcIdx = arcI;
        ld.tanIdx = tb;
        lcData[i] = ld;

        // Write solved Ls back to the element so it survives save/load
        // (the element's length field is serialised in toJson)
        // We need a mutable reference — elems is const, so we cast via the
        // caller's QVector.  Pass the solved length through lcData; the
        // AlignmentDocument::solve() wrapper (which owns m_elems) should do
        // this.  Here we use a const_cast as a pragmatic in-solver fix:
        const_cast<EditableElement&>(elems[i]).length = lc.Ls;

        // Trim incoming tangent: its end becomes TS
        tanEnd[tb] = lc.tsPoint;

        // Update the Fixed Arc's PC to the solved SC (trim the arc's start)
        arcData[arcI].pc     = lc.scPoint;
        arcData[arcI].azPC   = lc.azSC;
        arcData[arcI].arcLen = lc.arcLen;
        // arcData[arcI].pt and .valid remain as set by Pass 1
    }

    // ════════════════════════════════════════════════════════════════════════
    //  Pass 2d — CA group  (Fixed CircularArc → SpiralOut → Fixed Tangent)
    //
    //  Identification: element i is a SpiralOut with mode==Floating and
    //    tangentIdxAfter pointing to a Fixed Tangent, while the element
    //    immediately before (i−1) is a Fixed CircularArc.
    // ════════════════════════════════════════════════════════════════════════
    for (int i = 1; i < n; ++i) {
        if (elems[i].type != EditableElementType::SpiralOut) continue;
        if (elems[i].mode != ConstraintMode::Floating)       continue;

        const int arcI = i - 1;
        if (elems[arcI].type != EditableElementType::CircularArc) continue;
        if (elems[arcI].mode != ConstraintMode::Fixed)            continue;

        // Already handled by Pass 2b or 2c?
        if (scsData[arcI - 1 >= 0 ? arcI - 1 : 0].valid && arcI > 0) {
            // Check if arcI was part of an SCS at arcI-1
            if (arcI > 0 && scsData[arcI - 1].valid &&
                scsData[arcI - 1].arcIdx == arcI) continue;
        }

        const int ta = elems[i].tangentIdxAfter;
        if (ta < 0 || ta >= n) continue;
        if (elems[ta].type != EditableElementType::Tangent) continue;
        if (elems[ta].mode != ConstraintMode::Fixed)        continue;

        const SpiralType stype = elems[i].spiralType2;
        const double     Rarc  = std::abs(elems[arcI].radius);

        if (!arcData[arcI].valid) {
            qWarning() << "[AlignmentSolver] Pass2d CA idx" << i
                       << ": Fixed arc at idx" << arcI << " not solved in Pass 1";
            continue;
        }

        // If Pass 2c already trimmed the arc's PC, use arcData (already updated)
        const double azArcStart = arcData[arcI].azPC;

        const SolvedCA ca = solveCA(
            elems[arcI].arcCenter, Rarc,
            arcData[arcI].pc,   // arcStart (may already be trimmed by Pass 2c)
            azArcStart,
            tanStart[ta], tanEnd[ta],
            stype);

        if (!ca.valid) {
            qWarning() << "[AlignmentSolver] Pass2d CA idx" << i << ": solveCA failed";
            continue;
        }

        // Store result
        CAData cd;
        cd.valid  = true;
        cd.ca     = ca;
        cd.arcIdx = arcI;
        cd.tanIdx = ta;
        caData[i] = cd;

        // Write solved Ls back so it survives serialisation
        const_cast<EditableElement&>(elems[i]).length = ca.Ls;

        // Trim outgoing tangent: its start becomes ST
        tanStart[ta] = ca.stPoint;

        // Update the Fixed Arc's PT to the solved CS (trim the arc's end)
        arcData[arcI].arcLen = ca.arcLen;
        arcData[arcI].pt     = ca.csPoint;
        // arcData[arcI].pc and .azPC remain (set by Pass 1 or Pass 2c)
    }

    // ════════════════════════════════════════════════════════════════════════
    //  Pass 2e — ACA group  (Fixed Arc₁ → SpiralIn → Fixed Arc₂)
    //
    //  Identification: element i is a SpiralIn with mode==Floating,
    //    tangentIdxBefore == -1  (no bounding tangent — it references Arc₁),
    //    and is bracketed by Fixed CircularArc elements at (i-1) and (i+1).
    //
    //  The spiral element carries arc1Idx / arc2Idx in its tangentIdxBefore /
    //  tangentIdxAfter fields (overloaded: -1 means "not a tangent reference";
    //  in addACA() we set tangentIdxBefore = arc1Idx, tangentIdxAfter = arc2Idx,
    //  but both point to CircularArc elements, not Tangents).
    //
    //  The Clothoid length Ls is unknown; solveACA() finds it iteratively.
    //  Both fixed arcs are trimmed at their new cut-points.
    // ════════════════════════════════════════════════════════════════════════
    for (int i = 0; i < n; ++i) {
        if (elems[i].type != EditableElementType::SpiralIn) continue;
        if (elems[i].mode != ConstraintMode::Floating)      continue;

        // ACA spirals store arc1Idx in tangentIdxBefore, arc2Idx in tangentIdxAfter,
        // but BOTH reference CircularArc elements (not Tangents).
        const int arc1I = elems[i].tangentIdxBefore;
        const int arc2I = elems[i].tangentIdxAfter;

        if (arc1I < 0 || arc1I >= n) continue;
        if (arc2I < 0 || arc2I >= n) continue;
        if (elems[arc1I].type != EditableElementType::CircularArc) continue;
        if (elems[arc2I].type != EditableElementType::CircularArc) continue;
        if (elems[arc1I].mode != ConstraintMode::Fixed)            continue;
        if (elems[arc2I].mode != ConstraintMode::Fixed)            continue;

        // Skip if already handled as SCS or LC or CA
        if (scsData[i].valid || lcData[i].valid) continue;
        // Also: LC uses tangentIdxBefore pointing to a Tangent,
        // ACA uses it pointing to a CircularArc — so the above checks already
        // distinguish them.  But guard against a spurious SCS match as well.

        if (!arcData[arc1I].valid) {
            qWarning() << "[AlignmentSolver] Pass2e ACA idx" << i
                       << ": Fixed arc1 at idx" << arc1I << " not solved in Pass 1";
            continue;
        }
        if (!arcData[arc2I].valid) {
            qWarning() << "[AlignmentSolver] Pass2e ACA idx" << i
                       << ": Fixed arc2 at idx" << arc2I << " not solved in Pass 1";
            continue;
        }

        const SpiralType stype = elems[i].spiralType1;
        const double R1 = std::abs(elems[arc1I].radius);
        const double R2 = std::abs(elems[arc2I].radius);

        // arc1 azimuth at its start = arcData[arc1I].azPC
        const double az1Start = arcData[arc1I].azPC;
        // arc2 azimuth at its end = azPC + arcLen / R (signed delta)
        const double signedDelta2 = (elems[arc2I].radius >= 0.0)
                                    ? (arcData[arc2I].arcLen / R2)
                                    : -(arcData[arc2I].arcLen / R2);
        const double az2End = arcData[arc2I].azPC + signedDelta2;

        const SolvedACA aca = solveACA(
            elems[arc1I].arcCenter, R1,
            arcData[arc1I].pc,  az1Start,
            elems[arc2I].arcCenter, R2,
            arcData[arc2I].pt,  az2End,
            stype);

        if (!aca.valid) {
            qWarning() << "[AlignmentSolver] Pass2e ACA idx" << i << ": solveACA failed";
            continue;
        }

        // Store result
        ACAData ad;
        ad.valid   = true;
        ad.aca     = aca;
        ad.arc1Idx = arc1I;
        ad.arc2Idx = arc2I;
        acaData[i] = ad;

        // Write solved Ls back so it survives serialisation
        const_cast<EditableElement&>(elems[i]).length = aca.Ls;

        // Trim Arc₁ tail: its new PT = SC₁
        arcData[arc1I].arcLen = aca.arc1Len;
        arcData[arc1I].pt     = aca.sc1Point;

        // Trim Arc₂ head: its new PC = SC₂
        arcData[arc2I].pc     = aca.sc2Point;
        arcData[arc2I].azPC   = aca.azSC2;
        arcData[arc2I].arcLen = aca.arc2Len;
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
    // The exit tangent (tangentIdxAfter) of every SCS group must also be
    // suppressed from the main loop, because the SCS emission block below
    // ALWAYS emits the trimmed exit tangent TT inline, immediately after the
    // CS keypoint (correct chainage order), regardless of whether the exit
    // tangent's own array index happens to be lower or higher than the
    // SpiralIn's index.
    //
    // BUG FIX (was: `&& ta < i`): the previous guard assumed addSCS() always
    // appends SCS elements AFTER the existing tangents, so the exit tangent
    // index would always be LOWER than the SpiralIn index. That assumption
    // does not always hold — e.g. when a floating curve/SCS group is
    // inserted between two pre-existing tangents, the exit tangent's index
    // can end up HIGHER than the SpiralIn's index. In that case the guard
    // failed to suppress the exit tangent, so it was emitted twice: once
    // here (inline, with the full correct length) and again later when the
    // main loop reached that Tangent element in normal index order — the
    // second copy repeated the SAME start point and length, i.e. exactly
    // the "折返" (fold-back) duplicate-station artifact seen downstream in
    // 3D Alignment sampling and elsewhere. Suppression must apply whenever
    // the inline emission happens, independent of index order.
    QVector<bool> handledBySCS(n, false);
    for (int i = 0; i < n; ++i) {
        if (scsData[i].valid) {
            handledBySCS[i]                       = true;  // SpiralIn
            handledBySCS[scsData[i].arcIdx]       = true;  // CircularArc
            handledBySCS[scsData[i].spiralOutIdx] = true;  // SpiralOut

            // Suppress exit tangent — emitted inline by the SCS block (after CS).
            // Guard must mirror the emission-side check below exactly, so an
            // element is only suppressed when it was actually pre-emitted.
            const int ta = elems[i].tangentIdxAfter;
            if (ta >= 0 && ta < n && elems[ta].type == EditableElementType::Tangent)
                handledBySCS[ta] = true;
        }
        // LC group: SpiralIn is emitted inline with the Fixed Arc → suppress SpiralIn
        if (lcData[i].valid) {
            handledBySCS[i] = true;  // SpiralIn handled inside arc emission
        }
        // CA group: SpiralOut is emitted inline with the Fixed Arc → suppress SpiralOut
        if (caData[i].valid) {
            handledBySCS[i] = true;  // SpiralOut handled inside arc emission
        }
        // ACA group: SpiralIn is emitted inline between the two arcs → suppress SpiralIn
        if (acaData[i].valid) {
            handledBySCS[i] = true;  // SpiralIn handled inside arc emission
        }
        // Compound chain group (N>=2 arcs): every consumed element (SpiralIn
        // + all interior CircularArc/SpiralOut pairs) plus the exit tangent
        // is emitted inline by the compound-chain block, mirroring the SCS
        // suppression above.
        if (compoundData[i].valid) {
            for (int k = i; k <= compoundData[i].lastIdx && k < n; ++k)
                handledBySCS[k] = true;

            const int ta = elems[i].tangentIdxAfter;
            if (ta >= 0 && ta < n && elems[ta].type == EditableElementType::Tangent)
                handledBySCS[ta] = true;
        }
    }

    // ── 邊界建構線判定（供 Tangent 發點區塊使用）───────────────────────────
    //  seedFromRawPoints() 建立的 Fixed Tangent 只要 isConstructionLine==true
    //  就不是真正量測到的直線，但這個 flag 同時涵蓋了兩種完全不同的情況：
    //    1. 線形起訖點的邊界虛擬 Tangent —— 只被「一側」的 Floating 群組
    //       參照（起點只當某群組的 tangentIdxBefore；終點只當某群組的
    //       tangentIdxAfter），另一側落到資料範圍外，沒有對應的群組。
    //    2. 兩段緩和曲線直接相接的 SS 交會點 —— 兩側都各自被一個 Floating
    //       群組參照（同時是前一群組的 tangentIdxAfter、也是後一群組的
    //       tangentIdxBefore），是線形「中間」真實存在的一個點，不是邊界。
    //  平面線形資料表只需要隱藏第 1 種（純屬求解用的邊界佔位線，起訖點
    //  本身的資訊已由 sentinel 收尾列涵蓋），第 2 種 SS 交會點仍要正常顯示
    //  （其長度已由 seedFromRawPoints() 的 SS 精度修正釘在近乎 0，會正常
    //  顯示成一個長度~0 的路徑點）。用「是否同時被兩側的 Floating 群組
    //  參照」來分辨，不需要額外欄位。
    QVector<bool> refAsTangentBefore(n, false);  // 這個 Tangent 是某群組的 tangentIdxBefore（其後緊接著一個群組）
    QVector<bool> refAsTangentAfter(n, false);   // 這個 Tangent 是某群組的 tangentIdxAfter（其前緊接著一個群組）
    for (int i = 0; i < n; ++i) {
        if (elems[i].mode != ConstraintMode::Floating) continue;
        const int tb = elems[i].tangentIdxBefore;
        const int ta = elems[i].tangentIdxAfter;
        if (tb >= 0 && tb < n && elems[tb].type == EditableElementType::Tangent)
            refAsTangentBefore[tb] = true;
        if (ta >= 0 && ta < n && elems[ta].type == EditableElementType::Tangent)
            refAsTangentAfter[ta] = true;
    }
    QVector<bool> isBoundaryConstruction(n, false);
    for (int i = 0; i < n; ++i) {
        if (elems[i].type != EditableElementType::Tangent) continue;
        if (!elems[i].isConstructionLine) continue;
        isBoundaryConstruction[i] = !(refAsTangentBefore[i] && refAsTangentAfter[i]);
    }
    // 找出與某個邊界建構線緊鄰的真正量測曲線半徑（供 constructionIsArc==true
    // 時顯示用；該建構線本身沒有半徑，半徑來自它所銜接的那段 Floating
    // CircularArc / SCS 群組）。
    auto adjacentArcRadius = [&](int tangentIdx) -> double {
        for (int j = 0; j < n; ++j) {
            if (elems[j].mode != ConstraintMode::Floating) continue;
            if (elems[j].tangentIdxBefore != tangentIdx && elems[j].tangentIdxAfter != tangentIdx)
                continue;
            if (elems[j].type == EditableElementType::CircularArc)
                return elems[j].radius;
            if (elems[j].type == EditableElementType::SpiralIn
                && j + 1 < n && elems[j + 1].type == EditableElementType::CircularArc)
                return elems[j + 1].radius;
        }
        return 0.0;
    };

    for (int i = 0; i < n; ++i) {
        const auto& e = elems[i];
        AlignmentPoint pt;

        // ── Compound chain group (N≥2 arcs): SpiralIn drives emission of ──
        //    the entire node sequence. Generalizes the SCS block below to
        //    an arbitrary number of arcs/spirals (see solveCompoundChain()).
        if (e.type == EditableElementType::SpiralIn && compoundData[i].valid) {
            const SolvedCompoundChain& chain = compoundData[i].chain;

            auto spiralTypeName = [](SpiralType t) -> QString {
                switch (t) {
                case SpiralType::HalfSine: return QStringLiteral("HALFSINE");
                case SpiralType::Parabola: return QStringLiteral("PARABOLA");
                case SpiralType::CubicJPN: return QStringLiteral("CUBICJPN");
                case SpiralType::CubicECI: return QStringLiteral("CUBICECI");
                case SpiralType::Clothoid: // fall-through — default
                default:                    return QStringLiteral("SPIRAL");
                }
            };

            const int nodeCount = chain.nodes.size();
            int spiralIdx = 0;   // index into chain.spiralTypes (0..N)
            for (int ni = 0; ni < nodeCount - 1; ++ni) {
                const CompoundChainNode& node = chain.nodes[ni];
                AlignmentPoint np;
                if (ni == 0) {
                    np.tsc = QStringLiteral("TS");
                } else if (node.isArcStart) {
                    np.tsc = QStringLiteral("SC");
                } else {
                    np.tsc = QStringLiteral("CS");
                }
                np.curveType = node.isArcStart
                    ? QStringLiteral("ARC")
                    : spiralTypeName(spiralIdx < chain.spiralTypes.size()
                                      ? chain.spiralTypes[spiralIdx] : SpiralType::Clothoid);
                np.easting  = node.pt.x();
                np.northing = node.pt.y();
                np.azimuth  = node.az;
                np.length   = node.segLength;
                np.radius   = node.radius;
                np.chainage = chainage;
                chainage   += node.segLength;
                pts.append(np);

                if (!node.isArcStart) ++spiralIdx;  // consumed one spiral segment
            }

            // Emit trimmed exit tangent (ST → tanEnd[ta]) inline, mirroring
            // the SCS block's stTT emission immediately below.
            {
                const int ta = e.tangentIdxAfter;
                if (ta >= 0 && ta < n && elems[ta].type == EditableElementType::Tangent) {
                    const QPointF& stPt  = tanStart[ta];   // = chain.nodes.last().pt
                    const QPointF& endPt = tanEnd[ta];
                    const double   tlen  = QLineF(stPt, endPt).length();

                    AlignmentPoint stTT;
                    stTT.tsc      = QStringLiteral("ST");
                    stTT.easting  = stPt.x();
                    stTT.northing = stPt.y();
                    stTT.azimuth  = chain.nodes.last().az;
                    stTT.length   = tlen;
                    stTT.chainage = chainage;
                    chainage     += tlen;
                    pts.append(stTT);
                }
            }
            continue;
        }

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
                    stTT.tsc      = QStringLiteral("ST");
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
            if (isBoundaryConstruction[i]) {
                // 線形起訖點的邊界虛擬 Tangent：純屬求解用的佔位線，不是
                // 實際量測到的直線段，不該出現在平面線形資料表裡（起訖點
                // 本身的座標／里程已由本函式最末端的 sentinel 收尾列涵蓋，
                // 不會遺失）。
                //
                // 若這條邊界建構線的 constructionIsArc==true（規則 2：檔案
                // 資料範圍外的另一側其實是圓弧，代表原始資料在圓弧中途被
                // 截斷，而不是乾淨地在切線上結束），改發一列「虛擬弧」
                // 提示列，讓工程師在資料表上看得出這裡的線形其實還在
                // 彎道上，並帶出緊鄰那段真正量測到的圓弧半徑供參考（檔案
                // 資料範圍外的真實延伸半徑並未記錄，只能以此作為提示）。
                if (e.constructionIsArc) {
                    const double R = adjacentArcRadius(i);
                    AlignmentPoint vapt;
                    vapt.tsc       = QStringLiteral("VA");   // Virtual Arc 提示列，不計入 SC/CC/TC 半徑編輯計數
                    vapt.curveType = QStringLiteral("VIRTUAL_ARC:%1").arg(R, 0, 'f', 3);
                    vapt.easting   = tanStart[i].x();
                    vapt.northing  = tanStart[i].y();
                    vapt.azimuth   = azimuthOf(tanStart[i], tanEnd[i]);
                    vapt.length    = 0.0;
                    vapt.radius    = R;
                    vapt.chainage  = chainage;
                    pts.append(vapt);
                }
                // 不推進 chainage：這條建構線的長度只是求解殘留的極小誤差
                // （通常 < 1cm），不代表真實資料的延伸長度。
                continue;
            }

            const double len = QLineF(tanStart[i], tanEnd[i]).length();
            if (len < 1e-9) continue;

            // 判斷前一個非 SCS-handled 元素的型別，決定此切線起點的 tsc 碼：
            //   前一元素為 CircularArc → 「CT」（弧→切線過渡）
            //   其他                  → 「TT」（切線起點或初始點）
            auto prevType = EditableElementType::Tangent;
            for (int j = i - 1; j >= 0; --j) {
                if (!handledBySCS[j]) { prevType = elems[j].type; break; }
            }
            const bool prevIsArc = (prevType == EditableElementType::CircularArc);

            pt.tsc      = prevIsArc ? QStringLiteral("CT") : QStringLiteral("TT");
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

            // ── Check whether this arc has an LC spiral attached at its start ──
            // An LC group has SpiralIn at index (i-1) → lcData[i-1].arcIdx == i
            const bool hasLC = (i > 0 && lcData[i-1].valid && lcData[i-1].arcIdx == i);
            // A CA group has SpiralOut at index (i+1) → caData[i+1].arcIdx == i
            const bool hasCA = (i + 1 < n && caData[i+1].valid && caData[i+1].arcIdx == i);
            // An ACA group has SpiralIn at index (i+1) where arc1Idx == i (Arc₁ side)
            // and another SpiralIn at index (i-1) where arc2Idx == i (Arc₂ side).
            // We detect: this arc is Arc₁ of an ACA → spiral at i+1 has arc1Idx==i
            const bool hasACA_exit  = (i + 1 < n && acaData[i+1].valid && acaData[i+1].arc1Idx == i);
            // This arc is Arc₂ of an ACA → spiral at i-1 has arc2Idx==i
            const bool hasACA_entry = (i > 0      && acaData[i-1].valid && acaData[i-1].arc2Idx == i);

            // ── Emit LC spiral keypoint (TS) before the arc CC point ──────────
            if (hasLC) {
                const SolvedLC& lc = lcData[i-1].lc;
                auto spiralTypeName = [](SpiralType t) -> QString {
                    switch (t) {
                    case SpiralType::HalfSine: return QStringLiteral("HALFSINE");
                    case SpiralType::Parabola: return QStringLiteral("PARABOLA");
                    case SpiralType::CubicJPN: return QStringLiteral("CUBICJPN");
                    case SpiralType::CubicECI: return QStringLiteral("CUBICECI");
                    default:                   return QStringLiteral("SPIRAL");
                    }
                };
                const QString curveType = spiralTypeName(elems[i-1].spiralType1);

                AlignmentPoint tspt;
                tspt.tsc       = QStringLiteral("TS");
                tspt.curveType = curveType;
                tspt.easting   = lc.tsPoint.x();
                tspt.northing  = lc.tsPoint.y();
                tspt.azimuth   = lc.azTS;
                tspt.length    = lc.Ls;
                tspt.radius    = lc.R;
                tspt.chainage  = chainage;
                chainage      += lc.Ls;
                pts.append(tspt);
            }

            // ── Emit the arc itself ───────────────────────────────────────────
            // tsc 碼依前後元素決定：
            //   LC  / ACA entry → "SC"（來自螺旋）
            //   前一元素為 Tangent → "TC"（切線→圓弧）
            //   前一元素為 CircularArc → "CC"（弧→弧，反向曲線）
            {
                const bool hasLC_entry = (hasLC || hasACA_entry);
                if (hasLC_entry) {
                    pt.tsc = QStringLiteral("SC");
                } else {
                    auto prevTypeArc = EditableElementType::Tangent;
                    for (int j = i - 1; j >= 0; --j) {
                        if (!handledBySCS[j]) { prevTypeArc = elems[j].type; break; }
                    }
                    pt.tsc = (prevTypeArc == EditableElementType::CircularArc)
                             ? QStringLiteral("CC")
                             : QStringLiteral("TC");
                }
            }
            pt.easting  = arc.pc.x();
            pt.northing = arc.pc.y();
            pt.azimuth  = arc.azPC;
            pt.radius   = e.radius;
            pt.length   = arc.arcLen;
            pt.chainage = chainage;
            chainage   += arc.arcLen;
            pts.append(pt);

            // ── Emit ACA spiral keypoint (SC₁ at arc end → SC₂) ─────────────
            // When this arc is Arc₁ of an ACA group, emit the spiral that
            // connects Arc₁ (trimmed at SC₁) to Arc₂ (trimmed at SC₂).
            if (hasACA_exit) {
                const SolvedACA& aca = acaData[i+1].aca;
                auto spiralTypeName = [](SpiralType t) -> QString {
                    switch (t) {
                    case SpiralType::HalfSine: return QStringLiteral("HALFSINE");
                    case SpiralType::Parabola: return QStringLiteral("PARABOLA");
                    case SpiralType::CubicJPN: return QStringLiteral("CUBICJPN");
                    case SpiralType::CubicECI: return QStringLiteral("CUBICECI");
                    default:                   return QStringLiteral("SPIRAL");
                    }
                };
                const QString curveType = spiralTypeName(elems[i+1].spiralType1);

                // CS point = SC₁ (exit of Arc₁ = entry of spiral)
                // We emit it as "CS" (leaving the arc) with the spiral geometry.
                // The spiral's length Ls and equivalent radius are stored in aca.
                const double Req = (aca.R1 * aca.R2) / std::abs(aca.R1 - aca.R2);

                AlignmentPoint cspt;
                cspt.tsc       = QStringLiteral("CS");
                cspt.curveType = curveType;
                cspt.easting   = aca.sc1Point.x();
                cspt.northing  = aca.sc1Point.y();
                cspt.azimuth   = aca.azSC1;
                cspt.length    = aca.Ls;
                cspt.radius    = Req;          // equivalent radius for display
                cspt.chainage  = chainage;
                chainage      += aca.Ls;
                pts.append(cspt);
                // Arc₂ (starting at SC₂) will be emitted in the normal arc flow
                // for element acaData[i+1].arc2Idx, with its updated pc = SC₂.
                continue;  // skip default PT waypoint for Arc₁
            }

            // ── Emit CA spiral keypoint (CS then ST) after the arc ────────────
            if (hasCA) {
                const SolvedCA& ca = caData[i+1].ca;
                auto spiralTypeName = [](SpiralType t) -> QString {
                    switch (t) {
                    case SpiralType::HalfSine: return QStringLiteral("HALFSINE");
                    case SpiralType::Parabola: return QStringLiteral("PARABOLA");
                    case SpiralType::CubicJPN: return QStringLiteral("CUBICJPN");
                    case SpiralType::CubicECI: return QStringLiteral("CUBICECI");
                    default:                   return QStringLiteral("SPIRAL");
                    }
                };
                const QString curveType = spiralTypeName(elems[i+1].spiralType2);

                // CS point (arc-end / spiral-start)
                AlignmentPoint cspt;
                cspt.tsc       = QStringLiteral("CS");
                cspt.curveType = curveType;
                cspt.easting   = ca.csPoint.x();
                cspt.northing  = ca.csPoint.y();
                cspt.azimuth   = ca.azCS;
                cspt.length    = ca.Ls;
                cspt.radius    = ca.R;
                cspt.chainage  = chainage;
                chainage      += ca.Ls;
                pts.append(cspt);

                // ST waypoint = start of the trimmed exit tangent
                AlignmentPoint stpt;
                stpt.tsc      = QStringLiteral("ST");
                stpt.easting  = ca.stPoint.x();
                stpt.northing = ca.stPoint.y();
                stpt.azimuth  = ca.azST;
                stpt.length   = 0.0;
                stpt.chainage = chainage;
                pts.append(stpt);
                // (the trimmed tangent itself will be emitted in normal Tangent flow
                //  since tanStart[ta] was updated to ca.stPoint)
                continue;  // skip the default PT waypoint below
            }

            // ── Default: PT waypoint (no CA spiral) ───────────────────────────
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

                    // tsc 碼：後一個非 SCS-handled 元素若為 CircularArc → "CC"（弧弧相接）
                    //          否則（末端點）→ "TT"
                    QString waypointTsc = QStringLiteral("TT");
                    for (int j = i + 1; j < n; ++j) {
                        if (handledBySCS[j]) continue;
                        if (elems[j].type == EditableElementType::CircularArc)
                            waypointTsc = QStringLiteral("CC");
                        break;
                    }

                    AlignmentPoint ptWaypoint;
                    ptWaypoint.tsc      = waypointTsc;
                    ptWaypoint.easting  = arc.pt.x();
                    ptWaypoint.northing = arc.pt.y();
                    ptWaypoint.azimuth  = azPT;
                    ptWaypoint.length   = 0.0;
                    ptWaypoint.chainage = chainage;
                    pts.append(ptWaypoint);
                }
            }
        } else if ((e.type == EditableElementType::SpiralIn
                    || e.type == EditableElementType::SpiralOut)
                   && e.mode == ConstraintMode::Fixed) {
            // ── 獨立 Fixed 緩和曲線（seedFromRawPoints() 規則 2：線形起訖點
            //    的邊界群組銜接虛擬圓弧，且半徑可從原始資料取得的情況）。
            //    座標／長度／半徑皆直接取自元素自身，不需 solver 推導，也
            //    不屬於任何 SCS/LC/CA/ACA 群組（addFixedSpiral() 建立時
            //    tangentIdxBefore/After 恆為 -1）。
            const double len = e.length;
            if (len < 1e-9) continue;

            auto spiralTypeName = [](SpiralType t) -> QString {
                switch (t) {
                case SpiralType::HalfSine: return QStringLiteral("HALFSINE");
                case SpiralType::Parabola: return QStringLiteral("PARABOLA");
                case SpiralType::CubicJPN: return QStringLiteral("CUBICJPN");
                case SpiralType::CubicECI: return QStringLiteral("CUBICECI");
                default:                   return QStringLiteral("SPIRAL");
                }
            };

            // tsc 碼：SpiralIn（虛擬圓弧在前，即線形起點）→ "CS"（抵達本點
            // 的是檔案資料範圍外的虛擬圓弧）；SpiralOut（虛擬圓弧在後，即
            // 線形終點）→ "TS"（抵達本點的是前一個真正 Tangent／SS）。
            pt.tsc       = (e.type == EditableElementType::SpiralIn)
                           ? QStringLiteral("CS") : QStringLiteral("TS");
            pt.curveType = spiralTypeName(e.spiralType1);
            pt.easting   = e.startPI.x();
            pt.northing  = e.startPI.y();
            pt.azimuth   = azimuthOf(e.startPI, e.endPI);
            pt.length    = len;
            pt.radius    = e.radius;
            pt.chainage  = chainage;
            chainage    += len;
            pts.append(pt);
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
            } else if ((elems[i].type == EditableElementType::SpiralIn
                        || elems[i].type == EditableElementType::SpiralOut)
                       && elems[i].mode == ConstraintMode::Fixed) {
                // 獨立 Fixed 緩和曲線（seedFromRawPoints() 規則 2 邊界群組）
                // 為整條線形最後一個元素：直接以其自身終點作為 sentinel。
                sentinel.easting  = elems[i].endPI.x();
                sentinel.northing = elems[i].endPI.y();
                sentinel.azimuth  = azimuthOf(elems[i].startPI, elems[i].endPI);
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