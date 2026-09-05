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
    case SpiralType::HalfSine:         elem = std::make_unique<HalfSineElement>();         break;
    case SpiralType::Parabola:         elem = std::make_unique<ParabolaElement>();         break;
    case SpiralType::CubicJPN:         elem = std::make_unique<CubicJPNElement>();         break;
    case SpiralType::CubicECI:         elem = std::make_unique<CubicECIElement>();         break;
    case SpiralType::Sinusoidal:       elem = std::make_unique<SinusoidalElement>();       break;
    case SpiralType::Cosine:           elem = std::make_unique<CosineElement>();           break;
    case SpiralType::Bloss:            elem = std::make_unique<BlossElement>();            break;
    case SpiralType::Lemniscate:       elem = std::make_unique<LemniscateElement>();       break;
    case SpiralType::WienerBogen:      elem = std::make_unique<WienerBogenElement>();      break;
    case SpiralType::Radioid:          elem = std::make_unique<RadioidElement>();          break;
    case SpiralType::ElasticRadioid:   elem = std::make_unique<ElasticRadioidElement>();   break;
    case SpiralType::NorwichSturm:     elem = std::make_unique<NorwichSturmElement>();     break;
    case SpiralType::PseudoEllipticRadioid: elem = std::make_unique<PseudoEllipticRadioidElement>(); break;
    case SpiralType::Logarithmic:      elem = std::make_unique<LogarithmicElement>();      break;
    case SpiralType::Hyperbolic:       elem = std::make_unique<HyperbolicElement>();       break;
    case SpiralType::Polynomial:       elem = std::make_unique<PolynomialElement>();       break;
    case SpiralType::Quintic:          elem = std::make_unique<QuinticElement>();          break;
    case SpiralType::PHQuintic:        elem = std::make_unique<PHQuinticElement>();        break;
    case SpiralType::Biquadratic:      elem = std::make_unique<BiquadraticElement>();      break;
    case SpiralType::Spline:           elem = std::make_unique<SplineElement>();           break;
    case SpiralType::BlossEulerHybrid: elem = std::make_unique<BlossEulerHybridElement>(); break;
    case SpiralType::Clothoid:
    default:                           elem = std::make_unique<ClothoidElement>();  break;
    }
    elem->setLength(Ls);
    elem->setRadius(signedR);
    return elem;
}

// ============================================================================
//  spiralTypeToElementType  (file-local helper)
//
//  EggTransitionElement's internal equivalent-spiral (see
//  RailwayAlignmentElement.h/.cpp) is selected via ElementType (Tangent /
//  CircularArc / ... / Egg — the RailwayAlignmentElement.h enum used by the
//  whole element hierarchy), not SpiralType (the AlignmentDocument.h enum
//  used by the solver/edit-command layer for user-facing spiral selection).
//  Same enumerator names, different ordinal values (ElementType inserts
//  Tangent/CircularArc before Clothoid and appends Egg at the end), so a
//  raw static_cast between them is NOT safe — this switch is the one
//  translation point, mirrored 1:1 against makeTransitionElement()'s switch
//  just above so the two never drift apart.
// ============================================================================

ElementType spiralTypeToElementType(SpiralType t)
{
    switch (t) {
    case SpiralType::HalfSine:         return ElementType::HalfSine;
    case SpiralType::Parabola:         return ElementType::Parabola;
    case SpiralType::CubicJPN:         return ElementType::CubicJPN;
    case SpiralType::CubicECI:         return ElementType::CubicECI;
    case SpiralType::Sinusoidal:       return ElementType::Sinusoidal;
    case SpiralType::Cosine:           return ElementType::Cosine;
    case SpiralType::Bloss:            return ElementType::Bloss;
    case SpiralType::Lemniscate:       return ElementType::Lemniscate;
    case SpiralType::WienerBogen:      return ElementType::WienerBogen;
    case SpiralType::Radioid:          return ElementType::Radioid;
    case SpiralType::ElasticRadioid:   return ElementType::ElasticRadioid;
    case SpiralType::NorwichSturm:     return ElementType::NorwichSturm;
    case SpiralType::PseudoEllipticRadioid: return ElementType::PseudoEllipticRadioid;
    case SpiralType::Logarithmic:      return ElementType::Logarithmic;
    case SpiralType::Hyperbolic:       return ElementType::Hyperbolic;
    case SpiralType::Polynomial:       return ElementType::Polynomial;
    case SpiralType::Quintic:          return ElementType::Quintic;
    case SpiralType::PHQuintic:        return ElementType::PHQuintic;
    case SpiralType::Biquadratic:      return ElementType::Biquadratic;
    case SpiralType::Spline:           return ElementType::Spline;
    case SpiralType::BlossEulerHybrid: return ElementType::BlossEulerHybrid;
    case SpiralType::Clothoid:
    default:                           return ElementType::Clothoid;
    }
}

// ============================================================================
//  spiralTypeName  (file-local helper)
//
//  Map SpiralType → AlignmentPoint::curveType token (uppercase, as written to
//  ALD/JSON). Centralised here (used to be duplicated as an identical local
//  lambda in 7 different functions below) so that adding a new SpiralType
//  only requires touching this one switch instead of every call site.
// ============================================================================

QString spiralTypeName(SpiralType t)
{
    switch (t) {
    case SpiralType::HalfSine:         return QStringLiteral("HALFSINE");
    case SpiralType::Parabola:         return QStringLiteral("PARABOLA");
    case SpiralType::CubicJPN:         return QStringLiteral("CUBICJPN");
    case SpiralType::CubicECI:         return QStringLiteral("CUBICECI");
    case SpiralType::Sinusoidal:       return QStringLiteral("SINUSOIDAL");
    case SpiralType::Cosine:           return QStringLiteral("COSINE");
    case SpiralType::Bloss:            return QStringLiteral("BLOSS");
    case SpiralType::Lemniscate:       return QStringLiteral("LEMNISCATE");
    case SpiralType::WienerBogen:      return QStringLiteral("WIENERBOGEN");
    case SpiralType::Radioid:          return QStringLiteral("RADIOID");
    case SpiralType::ElasticRadioid:   return QStringLiteral("ELASRADIOID");
    case SpiralType::NorwichSturm:     return QStringLiteral("NORWICHSTURM");
    case SpiralType::PseudoEllipticRadioid: return QStringLiteral("PSEUELLRADIOID");
    case SpiralType::Logarithmic:      return QStringLiteral("LOGARITHMIC");
    case SpiralType::Hyperbolic:       return QStringLiteral("HYPERBOLIC");
    case SpiralType::Polynomial:       return QStringLiteral("POLYNOMIAL");
    case SpiralType::Quintic:          return QStringLiteral("QUINTIC");
    case SpiralType::PHQuintic:        return QStringLiteral("PHQUINTIC");
    case SpiralType::Biquadratic:      return QStringLiteral("BIQUADRATIC");
    case SpiralType::Spline:           return QStringLiteral("SPLINE");
    case SpiralType::BlossEulerHybrid: return QStringLiteral("BLOSSEULERHYBRID");
    case SpiralType::Clothoid:
    default:                           return QStringLiteral("SPIRAL");
    }
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
//    The CA spiral is walked "backwards" (from ST toward CS) to reuse the
//    same zero-curvature-at-origin clothoid shape function used by LC.
//    Because the walk direction is reversed relative to true travel, the
//    curvature sign fed into the shape function must also be reversed
//    (−signR·R instead of signR·R) — otherwise the reconstructed spiral
//    curves the wrong way and only its cross-track projection happens to
//    hit zero, while the actual ST/CS points end up far from where they
//    should be (Fixed Tangent renders "stretched" and doesn't connect to
//    the arc).
//    azCS = az2 + thetaS(Ls)                         [exit from arc into spiral]
//    CS_on_arc = arcCenter + (−signR·R·cos azCS,  +signR·R·sin azCS)
//    stDx = Xm·sin(az2) + Ym·cos(az2)               [CT: same local frame as TC]
//    stDy = Xm·cos(az2) − Ym·sin(az2)
//    ST = CS + (stDx,stDy)
//    f(Ls) = (tanStart − (stDx,stDy) − CS_on_arc) · n2   [n2 = right-perp of az2]
//
//  (Fixed 2026-07: thetaS/Xm/Ym used to be evaluated with the arc's own
//   signed curvature (signR·R) and azCS = az2 − thetaS. That sign choice
//   for the clothoid shape was wrong — f(Ls) could still be driven to zero
//   near the true Ls by coincidence of the cross-track projection, but the
//   reconstructed CS/ST points did not match the known geometry at all
//   [drawing a SpiralOut that connected to a bogus ST, "stretching" the
//   Fixed Tangent and leaving a visible gap at the arc]. The shape function
//   must use the *reversed* curvature sign (−signR·R) because this spiral
//   is parametrised backwards from ST; azCS then comes out as az2+thetaS.)
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
        // Reversed curvature sign: this clothoid is walked ST→CS, opposite
        // to true travel (CS→ST), so its shape must use −signR·R.
        const auto elem = makeTransitionElement(spiralType, Ls, -signR * R);
        const LocalFrame lf = elem->localFrame(Ls);
        const double Xm     = lf.x;
        const double Ym     = lf.y;
        const double thetaS = lf.theta;

        const double azCS = az2 + thetaS;
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
    const auto   elemF  = makeTransitionElement(spiralType, Ls, -signR * R);
    const LocalFrame lf = elemF->localFrame(Ls);
    const double Xm     = lf.x;
    const double Ym     = lf.y;
    const double thetaS = lf.theta;

    const double azCS = az2 + thetaS;
    const double stDx = Xm * sA2 + Ym * cA2;
    const double stDy = Xm * cA2 - Ym * sA2;

    const QPointF csPoint(arcCenter.x() - signR * R * std::cos(azCS),
                          arcCenter.y() + signR * R * std::sin(azCS));
    // ST = CS + (stDx,stDy). Kept in sync with the corrected sign in evalF() above.
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
    SpiralType spiralType)
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

    // ── Equivalent-spiral family for the internal EggTransitionElement ──────
    //  BUG FIX (previously this parameter was named /*spiralType*/ and
    //  completely unused — every ACA solve silently built a Clothoid-family
    //  EggTransitionElement regardless of what the caller/dialog asked for,
    //  so switching the dialog's spiral-type combo had zero effect on the
    //  ACA result). See spiralTypeToElementType() just above makeTransitionElement()
    //  for the enum-to-enum mapping (SpiralType and ElementType share names
    //  but not ordinal values). isFamilyEggCompatible() families
    //  (Lemniscate/WienerBogen/PHQuintic/ElasticRadioid/NorwichSturm/
    //  PseudoEllipticRadioid) aren't valid equivalent-spiral shapes for the
    //  Egg construction (see that method's doc comment in
    //  RailwayAlignmentElement.h) — EggTransitionElement itself falls back
    //  to Clothoid for those, but we determine that HERE too so
    //  result.actualSpiralType can report it transparently to the caller.
    const ElementType eggFamily = spiralTypeToElementType(spiralType);
    const bool familyCompatible = EggTransitionElement::isFamilyEggCompatible(eggFamily);
    result.actualSpiralType = familyCompatible ? spiralType : SpiralType::Clothoid;

    // ── Determine turn signs from arc centres relative to the travel direction ─
    //   signR1 = +1 if Arc₁ turns right, -1 if left; signR2 likewise for Arc₂.
    //
    //   This is the *unambiguous* determination the VBA reference's second
    //   C-C fix insists on: derive the sign from real, already-known data
    //   (here: the real arc centre vs. the real forward-travel direction at
    //   a real point on the arc) rather than from any fixed convention
    //   (radius magnitude, pick order, ...) that only happens to hold for
    //   one turn direction. It is the caller's job to supply arc1AzStart /
    //   arc2AzEnd as the arc's *true* forward tangent azimuth (see
    //   AlignmentAddSpiralCommand::showSolverPreview()'s arcAzimuthAtPC() /
    //   arcAzimuthAtPT() helpers, which recover this from the arc's real
    //   PC+PT via a cross product — never from a single point).
    auto computeSignR = [](QPointF centre, QPointF refPt, double az) -> int {
        const double n_x = std::cos(az), n_y = -std::sin(az);   // right-perp
        const double side = (centre.x() - refPt.x()) * n_x
                          + (centre.y() - refPt.y()) * n_y;
        return (side >= 0.0) ? 1 : -1;
    };

    const int signR1 = computeSignR(arc1Center, arc1Start, arc1AzStart);
    const int signR2 = computeSignR(arc2Center, arc2End,   arc2AzEnd);

    const double signedR1 = signR1 * R1;
    const double signedR2 = signR2 * R2;

    // ── VBA 蛋形線反算演算法（完全比照使用者提供、已驗證正確的
    //   螺線反算函數.bas / SolveSpiral_CC + LMSolve1D）────────────────────────
    //
    //  舊版做法：外層對 Ls 二分，內層對 azSC1 再做一次二分搜尋「哪個切入
    //  角度會讓 SC₂ 剛好落在 Arc₂ 圓周上」，找不到內層的變號區間就直接把
    //  f(Ls) 設成 1e9（"no intersection"）。這個雙層巢狀二分法在很多幾何
    //  下，內層根本找不到 bracket（尤其 Ls 偏小或偏大時 SC₂ 離 Arc₂ 太遠），
    //  導致 f(Ls) 在絕大部分掃描範圍內都是常數 1e9，外層永遠看不到變號，
    //  回報「no sign change」——這不代表這兩段圓弧真的無法用蛋形線銜接，
    //  只是求解策略本身的中間步驟（內層搜尋）太容易失敗。
    //
    //  VBA 版的作法乾淨得多：蛋形線兩端圓心之間的距離，是「平移旋轉不變
    //  量」，只跟 Ls、R1、R2 有關，跟整體怎麼擺放完全無關。所以只需要
    //  解一個一維方程式：
    //      DistOfLs(Ls) = |Ctrial2(Ls) − Ctrial1(Ls)|  ＝  targetD
    //  （targetD＝實際選取的兩個圓弧、圓心之間的真實距離）
    //  解出 Ls 之後，比較「本地座標系試算出的 O1→O2 方向」跟「實際的
    //  O1→O2 方向」，兩者的夾角差就是 SC1 在圓1上的正確方位角
    //  ALFA1（=azSC1），完全不需要額外一層搜尋。
    //
    //  DistOfLs(Ls) 本身是「圓心距離」這種規則得多的函數，不會像舊版的
    //  cross-track 殘差那樣容易出現「探到很接近零但兩側都不變號」的邊界
    //  情形——但仍保留掃描+二分／黃金分割相切備援，當作跟 VBA 同款的
    //  「不要只相信單一求解策略」的雙重保險。
    auto CircleCenterFromSpiralEnd = [](QPointF pt, double az, double Rsigned) -> QPointF {
        return QPointF(pt.x() + Rsigned * std::cos(az),
                       pt.y() - Rsigned * std::sin(az));
    };
    auto PointOnCircleForAzimuth = [](QPointF centre, double Rsigned, double az) -> QPointF {
        return QPointF(centre.x() - Rsigned * std::cos(az),
                       centre.y() + Rsigned * std::sin(az));
    };
    auto rotateLocal = [](double Xm, double Ym, double az) -> QPointF {
        return QPointF(Xm * std::sin(az) + Ym * std::cos(az),
                       Xm * std::cos(az) - Ym * std::sin(az));
    };

    // DistOfLs：本地座標系（SC1 在原點、方位角 0）試算出的兩端圓心距離。
    // Ls<=0 時的自然邊界值＝兩個純圓弧半徑差的絕對值（比照 VBA DistOfLE）。
    auto DistOfLs = [&](double LsTrial) -> double {
        if (LsTrial <= 1e-9) return std::abs(std::abs(signedR1) - std::abs(signedR2));
        const QPointF ctrial1 = CircleCenterFromSpiralEnd(QPointF(0.0, 0.0), 0.0, signedR1);
        EggTransitionElement egg(signedR1, signedR2, LsTrial, eggFamily);
        const LocalFrame lf = egg.localFrame(LsTrial);
        const QPointF sc2Local = rotateLocal(lf.x, lf.y, 0.0);
        const QPointF ctrial2  = CircleCenterFromSpiralEnd(sc2Local, lf.theta, signedR2);
        return std::hypot(ctrial2.x() - ctrial1.x(), ctrial2.y() - ctrial1.y());
    };

    const double targetD = std::hypot(arc2Center.x() - arc1Center.x(),
                                      arc2Center.y() - arc1Center.y());

    const double geomDist = std::hypot(arc2End.x() - arc1Start.x(),
                                       arc2End.y() - arc1Start.y())
                          + R1 + R2;
    const double Req    = (R1 * R2) / std::abs(R1 - R2);   // equivalent radius
    const double Ls_max = std::min(geomDist * 2.0, M_PI * Req * 0.95);

    // ── 主要求解器：Levenberg-Marquardt（比照 VBA LMSolve1D）────────────────
    //   數值微分近似 J、自適應阻尼 lambda；x 強制維持 >0。收斂後用「相對
    //   殘差」再驗證一次，不直接相信 LM 自己回報的收斂——不夠準就落到下面
    //   的掃描+二分備援（保證收斂，只要解真的存在）。
    auto lmSolveLs = [&](double x0) -> double {
        const int    kMaxIter  = 100;
        const int    kMaxInner = 80;
        double x   = (x0 > 0.0) ? x0 : 0.0001;
        double lam = 0.001;
        const double tolAbs = std::max(targetD * 1e-9, 1e-9);

        double R = DistOfLs(x) - targetD;
        for (int it = 0; it < kMaxIter; ++it) {
            if (std::abs(R) < tolAbs) break;
            double h = std::abs(x) * 1e-7;
            if (h < 1e-8) h = 1e-8;
            double J = (DistOfLs(x + h) - DistOfLs(x - h)) / (2.0 * h);
            if (J == 0.0) J = 1e-12;

            bool accepted = false;
            for (int inner = 0; inner < kMaxInner; ++inner) {
                double delta = -R / (J * (1.0 + lam));
                double xNew  = x + delta;
                if (xNew <= 0.0) xNew = x / 2.0;
                const double rNew = DistOfLs(xNew) - targetD;
                if (std::abs(rNew) < std::abs(R)) {
                    x = xNew; R = rNew;
                    lam = std::max(lam / 10.0, 1e-14);
                    accepted = true;
                    break;
                }
                lam *= 10.0;
                if (lam > 1e14) break;
            }
            if (!accepted) break;
        }

        const double relTol = std::max(targetD * 1e-7, 1e-7);
        if (x <= 0.0 || std::abs(R) > relTol) return -1.0;   // 交給備援
        return x;
    };

    // 初始猜測：跟 VBA 一樣取「圓心實際距離」的一部分，量級通常已經很接近。
    double Ls = lmSolveLs(std::max(1.0, targetD * 0.3));

    if (Ls < 0.0) {
        // ── LM 沒收斂到足夠精度：掃描+二分備援（保證收斂，安全網）───────────
        const int    nScan = 400;
        double Ls_lo = 1e-3;
        double f_lo  = DistOfLs(Ls_lo) - targetD;
        double Ls_hi = -1.0;
        double minAbsF = std::abs(f_lo), minAbsFLs = Ls_lo;

        for (int k = 1; k <= nScan; ++k) {
            const double Ls_k = Ls_max * k / static_cast<double>(nScan);
            const double f_k  = DistOfLs(Ls_k) - targetD;
            if (std::abs(f_k) < minAbsF) { minAbsF = std::abs(f_k); minAbsFLs = Ls_k; }
            if (f_lo * f_k < 0.0) { Ls_hi = Ls_k; break; }
            f_lo = f_k;
            Ls_lo = Ls_k;
        }

        if (Ls_hi < 0.0) {
            // 沒找到變號區間：黃金分割在觀察到的最小值附近再精修一次
            // （相切／重根解的備援，跟前一版邏輯相同，只是換成新的
            // DistOfLs 殘差）；精修後仍明顯偏離零，才真的判定無解。
            const double kLooseTol  = std::max(R1, R2) * 0.01;
            const double kAcceptTol = std::max(R1, R2) * 1e-6;
            bool refined = false;
            double LsTangent = minAbsFLs;
            if (minAbsF < kLooseTol) {
                const double halfWindow = (Ls_max / nScan) * 2.0;
                double lo = std::max(1e-6, minAbsFLs - halfWindow);
                double hi = std::min(Ls_max, minAbsFLs + halfWindow);
                const double gr = (std::sqrt(5.0) - 1.0) / 2.0;
                double c = hi - gr * (hi - lo);
                double d = lo + gr * (hi - lo);
                for (int iter = 0; iter < 80; ++iter) {
                    if (std::abs(DistOfLs(c) - targetD) < std::abs(DistOfLs(d) - targetD)) hi = d; else lo = c;
                    c = hi - gr * (hi - lo);
                    d = lo + gr * (hi - lo);
                }
                LsTangent = 0.5 * (lo + hi);
                if (std::abs(DistOfLs(LsTangent) - targetD) < kAcceptTol) refined = true;
            }
            if (!refined) {
                qWarning() << "[AlignmentSolver] solveACA: DistOfLs(Ls) has no sign"
                              " change and no near-zero minimum was found."
                           << "R1=" << R1 << "R2=" << R2 << "targetD=" << targetD
                           << "Ls_max=" << Ls_max << "min|residual|=" << minAbsF
                           << "at Ls=" << minAbsFLs
                           << "-- these two arcs genuinely cannot be joined by any"
                              " Egg-Transition curve within this Ls range.";
                return result;
            }
            qDebug() << "[AlignmentSolver] solveACA: bisection found no sign change,"
                        " but a tangent-point solution exists at Ls=" << LsTangent
                     << "-- accepting it (golden-section refined).";
            Ls = LsTangent;
        } else {
            for (int iter = 0; iter < 80; ++iter) {
                const double Ls_mid = 0.5 * (Ls_lo + Ls_hi);
                if (std::abs(Ls_hi - Ls_lo) < 1e-6) { Ls_lo = Ls_mid; break; }
                if ((DistOfLs(Ls_lo) - targetD) * (DistOfLs(Ls_mid) - targetD) <= 0.0)
                    Ls_hi = Ls_mid;
                else
                    Ls_lo = Ls_mid;
            }
            Ls = Ls_lo;
        }
    }

    // ── 由 Ls 反推 ALFA1(=azSC1)：比較本地試算方向跟實際 O1→O2 方向 ────────
    const QPointF ctrial1 = CircleCenterFromSpiralEnd(QPointF(0.0, 0.0), 0.0, signedR1);
    EggTransitionElement eggF(signedR1, signedR2, Ls, eggFamily);
    const LocalFrame lfF = eggF.localFrame(Ls);
    const double thetaSF = lfF.theta;
    const QPointF sc2LocalF = rotateLocal(lfF.x, lfF.y, 0.0);
    const QPointF ctrial2   = CircleCenterFromSpiralEnd(sc2LocalF, thetaSF, signedR2);

    const double AmzTrial = std::atan2(ctrial2.x() - ctrial1.x(), ctrial2.y() - ctrial1.y());
    const double AmzReal  = std::atan2(arc2Center.x() - arc1Center.x(),
                                       arc2Center.y() - arc1Center.y());
    double ALFA1 = AmzReal - AmzTrial;
    while (ALFA1 >  M_PI) ALFA1 -= 2.0 * M_PI;
    while (ALFA1 < -M_PI) ALFA1 += 2.0 * M_PI;

    const double azSC1 = ALFA1;
    const double azSC2 = azSC1 + thetaSF;

    // SC₁ and SC₂ world positions
    const QPointF sc1Point = PointOnCircleForAzimuth(arc1Center, signedR1, azSC1);
    const QPointF sc2Point = PointOnCircleForAzimuth(arc2Center, signedR2, azSC2);

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
    const double phi2 = arcAngle(sc2Point,  arc2End,  arc2Center, signR2);

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
//  solveReverseSpiral 系列 — file-local geometry helpers
//
//  核心觀察（見 ReverseSpiral_Command_實作計畫.md 第 2.2/2.3 節，此處補完整
//  推導）：Fixed CircularArc 是「已經釘死在絕對座標中的圓」，沒有 solveLC/
//  solveCA 那種「可沿切線滑動」的自由度；因此「在圓上任選一點當作緩和曲線
//  的接點」這件事，對圓本身而言是**旋轉對稱**的——把接點沿圓周移動角度
//  α，等同於把「圓 + 緩和曲線」這一整組剛體繞圓心旋轉 α。也就是說：
//
//    對固定的 L（緩和曲線長度），當接點在圓上滑動時，緩和曲線另一端
//    （曲率為 0 的那一端，即 S1/S2 的交會點 J）會沿著「以圓心為圓心、
//    半徑 ρ(L) 為半徑」的另一個圓滑動，且 J 的方位角 = (J 相對圓心的
//    方位角) + 一個與接點無關、只與 L 有關的固定偏移量。
//
//  因此，給定 L1、L2，J1 必落在以 arc1Center 為心、ρ1(L1) 為半徑的圓上；
//  J2 必落在以 arc2Center 為心、ρ2(L2) 為半徑的圓上。S1/S2 在 J 相接的
//  「位置」條件（ΔX=ΔY=0，見輸入文件第二節）於是退化為兩個圓的交點
//  （封閉式，circleIntersect()，不需疊代）；「方向」條件（Δθ=0）則是
//  唯一真正需要求根的殘差，且只剩 L1、L2 兩個未知數（不再需要額外的
//  接點角度 φ1/φ2 當作獨立未知數——它們已經被兩圓交點隱式決定）。
//
//  ρ(L) 與「J 相對接點的固定角度偏移」透過 jFromArcCT()/jFromArcTC() 用
//  任意一個已知（位置＋方位角）的圓上參考點直接算出（借用 solveCA／
//  solveLC 已有的 TC/CT 局部座標公式，只是兩者已知/未知的角色對調，見
//  下方個別函式註解），不需要另外疊代。
// ============================================================================

namespace {

/**
 * @brief CT 方向（曲率 R → 0）：已知圓上一點 P（位於 arcCenter/R/signR 所
 *        定義的圓上，含其順行方位角 azP），求緩和曲線另一端（曲率 0 的
 *        J 點）。用於 S1（Arc₁ 出口 → 交會點）。
 *
 *  公式是 solveCA()（已知 ST 反推 CS）的代數反解：solveCA 用
 *  azCS = az2 − thetaS 從已知的 az2（切線端）求 azCS（弧端）；這裡兩者
 *  已知/未知對調，直接令 azJ = azP + thetaS 即可（thetaS 只與 L 本身
 *  有關，與哪一端已知無關），offset 公式（stDx/stDy）維持不變。
 */
void jFromArcCT(QPointF centre, double R, int signR, SpiralType type,
                QPointF P, double azP, double L,
                QPointF& J, double& azJ, double& thetaOut)
{
    if (L < 1e-9) { J = P; azJ = azP; thetaOut = 0.0; return; }
    const auto elem = makeTransitionElement(type, L, signR * R);
    const LocalFrame lf = elem->localFrame(L);
    thetaOut = lf.theta;
    azJ = azP + lf.theta;
    const double sJ = std::sin(azJ), cJ = std::cos(azJ);
    const double dx = lf.x * sJ + lf.y * cJ;
    const double dy = lf.x * cJ - lf.y * sJ;
    J = QPointF(P.x() + dx, P.y() + dy);
}

/**
 * @brief TC 方向（曲率 0 → R）：已知圓上一點 P（同上）求緩和曲線另一端
 *        （曲率 0 的 J 點）。用於 S2（交會點 → Arc₂ 入口）。
 *
 *  solveLC() 用 azSC = az1 + thetaS 從已知的 az1（切線端）求 azSC（弧
 *  端）；這裡對調已知/未知：azJ = azP − thetaS，offset 公式（scDx/scDy）
 *  維持不變，但因為 SC = TS + offset(az1) ⟹ TS = SC − offset(az1)，
 *  offset 從 P 減去而非加上。
 */
void jFromArcTC(QPointF centre, double R, int signR, SpiralType type,
                QPointF P, double azP, double L,
                QPointF& J, double& azJ, double& thetaOut)
{
    if (L < 1e-9) { J = P; azJ = azP; thetaOut = 0.0; return; }
    const auto elem = makeTransitionElement(type, L, signR * R);
    const LocalFrame lf = elem->localFrame(L);
    thetaOut = lf.theta;
    azJ = azP - lf.theta;
    const double sJ = std::sin(azJ), cJ = std::cos(azJ);
    const double dx = lf.x * sJ + lf.y * cJ;
    const double dy = lf.x * cJ - lf.y * sJ;
    J = QPointF(P.x() - dx, P.y() - dy);
}

/**
 * @brief 平面上兩圓（centre1,r1）、（centre2,r2）之交點（標準封閉式）。
 * @return 交點數（0/1/2），存入 out[0..count-1]。
 */
int circleIntersect(QPointF c1, double r1, QPointF c2, double r2, QPointF out[2])
{
    const double dx = c2.x() - c1.x();
    const double dy = c2.y() - c1.y();
    const double d  = std::hypot(dx, dy);
    if (d < 1e-9) return 0;                          // 同心，無（或無限多）交點
    if (d > r1 + r2 + 1e-9) return 0;                 // 太遠
    if (d < std::abs(r1 - r2) - 1e-9) return 0;       // 一圓包住另一圓
    const double a = (r1 * r1 - r2 * r2 + d * d) / (2.0 * d);
    const double h2 = r1 * r1 - a * a;
    const double h  = std::sqrt(std::max(0.0, h2));
    const double mx = c1.x() + a * dx / d;
    const double my = c1.y() + a * dy / d;
    const double ux = -dy / d, uy = dx / d;   // 垂直於 c1→c2 的單位向量
    out[0] = QPointF(mx + h * ux, my + h * uy);
    out[1] = QPointF(mx - h * ux, my - h * uy);
    return (h < 1e-9) ? 1 : 2;   // 相切時只有 1 個真正相異的交點
}

/**
 * @brief 給定 L1、L2，計算交會點與其方向殘差。
 * @return 若兩圓不相交，回傳 NaN；否則回傳 normaliseAngle(azJ1−azJ2)。
 *
 * 分支選擇（branch selection）——修正 Phase 2 影子驗證抓到的不連續 bug
 * ─────────────────────────────────────────────────────────────────────
 * circleIntersect() 在有兩個交點時，兩者恰好相差一個固定的垂直偏移
 * ±h·u，其中 u ⟂ (arc2Center−arc1Center)。**關鍵觀察**：u 本身完全不依賴
 * L1、L2（arc1Center、arc2Center 對整個解族而言是固定的），所以「哪一個
 * 交點才是物理上正確的那一個」這件事，理論上對整個解族只有一個固定答案，
 * 不應該隨 L1、L2 變動而改變。
 *
 * 舊實作在每次呼叫時各自獨立比較兩個候選點的 |residual|、取較小者，這個
 * 「逐次重新比較」的做法會在兩個候選的殘差量級交叉的地方（例如本例
 * L1≈140 附近）產生分支切換——從外部看就是 J 點座標與殘差對 L1、L2 的
 * 依賴關係出現不連續跳躍，導致 bisection 的「單調變號」假設失效、鎖定
 * 假根（見 AlignmentSolver_LM統一升級計畫.md Phase 2 交叉驗證紀錄）。
 *
 * 修正：改成只用「與 L1、L2 無關」的幾何（arc1Center、arc2Center、
 * arc1Start 三點的相對位置）決定一次分支歸屬，讓同一次 solveReverseSpiral()
 * /analyzeReverseSpiralFamily() 呼叫中，所有 L1、L2 組合都套用同一個分支
 * ——保證殘差函式對 (L1,L2) 連續，bisection／LM 才能正確運作。
 *
 * 判定方式：以 arc1Center→arc2Center 為基準線，arc1Start 落在基準線的哪
 * 一側，就選同一側的交點（因為實際線形是從 arc1Start 出發、連續不自我
 * 穿越地走到交會點，正常幾何下交會點理應與 arc1Start 同側）。若
 * arc1Start 幾乎剛好落在基準線上（refCross≈0，無法判斷側別的退化情況），
 * 才退回舊的「逐次取 |residual| 較小者」做法──這個退化情況本身極罕見，
 * 且發生時兩個候選通常本來就很接近，用舊方法也不會產生明顯不連續。
 */
double reverseSpiralResidual(
    QPointF arc1Center, double R1, int signR1, SpiralType type1,
    QPointF arc1Start,  double arc1AzStart, double L1,
    QPointF arc2Center, double R2, int signR2, SpiralType type2,
    QPointF arc2End,    double arc2AzEnd,   double L2,
    QPointF& bestJ, double& bestAzJ1, double& bestAzJ2)
{
    QPointF J1ref, J2ref; double azJ1ref, azJ2ref, th1, th2;
    jFromArcCT(arc1Center, R1, signR1, type1, arc1Start, arc1AzStart, L1, J1ref, azJ1ref, th1);
    jFromArcTC(arc2Center, R2, signR2, type2, arc2End,   arc2AzEnd,   L2, J2ref, azJ2ref, th2);

    const double rho1 = std::hypot(J1ref.x() - arc1Center.x(), J1ref.y() - arc1Center.y());
    const double rho2 = std::hypot(J2ref.x() - arc2Center.x(), J2ref.y() - arc2Center.y());

    QPointF cand[2];
    const int nCand = circleIntersect(arc1Center, rho1, arc2Center, rho2, cand);
    if (nCand == 0) return std::numeric_limits<double>::quiet_NaN();

    auto residualAt = [&](QPointF p, double& az1a, double& az2a) -> double {
        az1a = AlignmentSolver::normaliseAngle(
            azJ1ref + (AlignmentSolver::azimuthOf(arc1Center, p)
                     - AlignmentSolver::azimuthOf(arc1Center, J1ref)));
        az2a = AlignmentSolver::normaliseAngle(
            azJ2ref + (AlignmentSolver::azimuthOf(arc2Center, p)
                     - AlignmentSolver::azimuthOf(arc2Center, J2ref)));
        return AlignmentSolver::normaliseAngle(az1a - az2a);
    };

    // ── deterministic branch selection (L1/L2-independent) ─────────────────
    if (nCand == 2) {
        const double bx = arc2Center.x() - arc1Center.x();
        const double by = arc2Center.y() - arc1Center.y();
        const double refCross = bx * (arc1Start.y() - arc1Center.y())
                               - by * (arc1Start.x() - arc1Center.x());
        if (std::abs(refCross) > 1e-6) {
            // Empirically verified (see diag_branch.cpp / conversation notes):
            // for a REVERSE curve the true junction sits on the OPPOSITE
            // side of the arc1Center→arc2Center baseline from arc1Start —
            // arc1 and arc2 curve in opposite directions by definition,
            // which pushes the junction across the baseline relative to
            // where the alignment enters arc1. Hence the sign flip below
            // (wantSign = −sign(refCross), not +sign(refCross)).
            const double wantSign = (refCross >= 0.0) ? -1.0 : 1.0;
            const double side0 = bx * (cand[0].y() - arc1Center.y())
                                - by * (cand[0].x() - arc1Center.x());
            const int chosen = (side0 * wantSign >= 0.0) ? 0 : 1;
            double az1a, az2a;
            const double res = residualAt(cand[chosen], az1a, az2a);
            bestJ = cand[chosen];
            bestAzJ1 = az1a;
            bestAzJ2 = az2a;
            return res;
        }
        // refCross ≈ 0 (arc1Start essentially on the arc1Center-arc2Center
        // baseline): side test is inconclusive, fall through to the
        // argmin fallback below for just this degenerate case.
    }

    double best = std::numeric_limits<double>::infinity();
    for (int k = 0; k < nCand; ++k) {
        double az1a, az2a;
        const double res = residualAt(cand[k], az1a, az2a);
        if (std::abs(res) < std::abs(best)) {
            best = res;
            bestJ = cand[k];
            bestAzJ1 = az1a;
            bestAzJ2 = az2a;
        }
    }
    return best;
}

} // anonymous namespace

// ============================================================================
//  analyzeReverseSpiralFamily
// ============================================================================

bool AlignmentSolver::analyzeReverseSpiralFamily(
    QPointF arc1Center, double arc1Radius,
    QPointF arc1Start,  double arc1AzStart,
    QPointF arc2Center, double arc2Radius,
    QPointF arc2End,    double arc2AzEnd,
    SpiralType type1, SpiralType type2,
    QVector<ReverseSpiralFamilySample>& outSamples,
    int sampleCount)
{
    outSamples.clear();

    const double R1 = std::abs(arc1Radius);
    const double R2 = std::abs(arc2Radius);
    if (R1 < 1e-9 || R2 < 1e-9) {
        qWarning() << "[AlignmentSolver] analyzeReverseSpiralFamily: arc radius ≈ 0";
        return false;
    }

    auto computeSignR = [](QPointF centre, QPointF refPt, double az) -> int {
        const double n_x = std::cos(az), n_y = -std::sin(az);
        const double side = (centre.x() - refPt.x()) * n_x
                           + (centre.y() - refPt.y()) * n_y;
        return (side >= 0.0) ? 1 : -1;
    };
    const int signR1 = computeSignR(arc1Center, arc1Start, arc1AzStart);
    const int signR2 = computeSignR(arc2Center, arc2End,   arc2AzEnd);

    if (signR1 == signR2) {
        qWarning() << "[AlignmentSolver] analyzeReverseSpiralFamily: arc1 and arc2"
                   << "turn the SAME direction — this is an Egg (ACA) transition,"
                   << "not a reverse spiral. Use solveACA() instead.";
        return false;
    }

    // L 的掃描上限：與 solveLC/solveCA 同款量級（幾何距離的倍數，並用
    // π·R 上限避免緩和曲線轉角超過 90 度造成的自相交退化）。
    const double geomDist = std::hypot(arc2End.x() - arc1Start.x(),
                                       arc2End.y() - arc1Start.y())
                          + R1 + R2;
    const double L1_max = std::min(geomDist, M_PI * R1 * 0.45);

    for (int k = 1; k <= sampleCount; ++k) {
        const double L1_try = L1_max * k / static_cast<double>(sampleCount + 1);

        // 對此 L1，掃描 + 二分找出對應的 L2（見 solveReverseSpiral() 內層
        // solve 的相同邏輯；此處保留獨立實作以維持 analyze/solve 兩者互不
        //依賴，方便個別單元測試）。
        const double L2_max = std::min(geomDist, M_PI * R2 * 0.45);
        const int    nScan  = 60;   // 樣本預覽用，掃描密度可低於正式求解
        QPointF bestJ; double bestAz1, bestAz2;
        double L2_lo = 1e-3;
        double f_lo  = reverseSpiralResidual(arc1Center, R1, signR1, type1, arc1Start, arc1AzStart, L1_try,
                                             arc2Center, R2, signR2, type2, arc2End, arc2AzEnd, L2_lo,
                                             bestJ, bestAz1, bestAz2);
        double L2_hi = -1.0, f_hi = 0.0;
        for (int m = 1; m <= nScan; ++m) {
            const double L2_k = L2_max * m / static_cast<double>(nScan);
            const double f_k  = reverseSpiralResidual(arc1Center, R1, signR1, type1, arc1Start, arc1AzStart, L1_try,
                                                      arc2Center, R2, signR2, type2, arc2End, arc2AzEnd, L2_k,
                                                      bestJ, bestAz1, bestAz2);
            if (std::isfinite(f_lo) && std::isfinite(f_k) && f_lo * f_k < 0.0) {
                L2_hi = L2_k; f_hi = f_k;
                break;
            }
            if (std::isfinite(f_k)) { f_lo = f_k; L2_lo = L2_k; }
        }
        if (L2_hi < 0.0) continue;   // 此 L1 在掃描範圍內找不到匹配的 L2，跳過

        for (int iter = 0; iter < 60; ++iter) {
            const double L2_mid = 0.5 * (L2_lo + L2_hi);
            const double f_mid = reverseSpiralResidual(arc1Center, R1, signR1, type1, arc1Start, arc1AzStart, L1_try,
                                                        arc2Center, R2, signR2, type2, arc2End, arc2AzEnd, L2_mid,
                                                        bestJ, bestAz1, bestAz2);
            if (std::abs(L2_hi - L2_lo) < 1e-4) break;
            if (f_lo * f_mid <= 0.0) { L2_hi = L2_mid; }
            else { L2_lo = L2_mid; f_lo = f_mid; }
        }

        ReverseSpiralFamilySample sample;
        sample.length1  = L1_try;
        sample.length2  = 0.5 * (L2_lo + L2_hi);
        sample.junction = bestJ;
        outSamples.append(sample);
    }

    if (outSamples.isEmpty()) {
        qWarning() << "[AlignmentSolver] analyzeReverseSpiralFamily: no L1 in scan range"
                   << "produced a matching L2 — arcs may be geometrically incompatible"
                   << "(too close / too far apart).";
        return false;
    }
    return true;
}

// ============================================================================
//  solveReverseSpiral
// ============================================================================

SolvedReverseSpiral AlignmentSolver::solveReverseSpiral(
    QPointF arc1Center, double arc1Radius,
    QPointF arc1Start,  double arc1AzStart,
    QPointF arc2Center, double arc2Radius,
    QPointF arc2End,    double arc2AzEnd,
    SpiralType type1, SpiralType type2,
    ReverseSpiralStrategy strategy, double strategyParam,
    QPointF pickedJunctionHint)
{
    SolvedReverseSpiral result;

    const double R1 = std::abs(arc1Radius);
    const double R2 = std::abs(arc2Radius);
    if (R1 < 1e-9 || R2 < 1e-9) {
        qWarning() << "[AlignmentSolver] solveReverseSpiral: arc radius ≈ 0";
        return result;
    }

    auto computeSignR = [](QPointF centre, QPointF refPt, double az) -> int {
        const double n_x = std::cos(az), n_y = -std::sin(az);
        const double side = (centre.x() - refPt.x()) * n_x
                           + (centre.y() - refPt.y()) * n_y;
        return (side >= 0.0) ? 1 : -1;
    };
    const int signR1 = computeSignR(arc1Center, arc1Start, arc1AzStart);
    const int signR2 = computeSignR(arc2Center, arc2End,   arc2AzEnd);

    if (signR1 == signR2) {
        qWarning() << "[AlignmentSolver] solveReverseSpiral: arc1/arc2 turn the same"
                   << "direction — not a reverse spiral (use solveACA()).";
        return result;
    }

    const double geomDist = std::hypot(arc2End.x() - arc1Start.x(),
                                       arc2End.y() - arc1Start.y())
                          + R1 + R2;
    const double L1_max = std::min(geomDist, M_PI * R1 * 0.45);
    const double L2_max = std::min(geomDist, M_PI * R2 * 0.45);

    QPointF bestJ; double bestAz1 = 0.0, bestAz2 = 0.0;

    // ── 內層：給定 L1，二分求出匹配的 L2（g(L1) = L2） ─────────────────────
    auto solveL2FromL1 = [&](double L1, bool& ok) -> double {
        const int nScan = 200;
        double L2_lo = 1e-3;
        double f_lo = reverseSpiralResidual(arc1Center, R1, signR1, type1, arc1Start, arc1AzStart, L1,
                                            arc2Center, R2, signR2, type2, arc2End, arc2AzEnd, L2_lo,
                                            bestJ, bestAz1, bestAz2);
        double L2_hi = -1.0;
        for (int m = 1; m <= nScan; ++m) {
            const double L2_k = L2_max * m / static_cast<double>(nScan);
            const double f_k  = reverseSpiralResidual(arc1Center, R1, signR1, type1, arc1Start, arc1AzStart, L1,
                                                      arc2Center, R2, signR2, type2, arc2End, arc2AzEnd, L2_k,
                                                      bestJ, bestAz1, bestAz2);
            if (std::isfinite(f_lo) && std::isfinite(f_k) && f_lo * f_k < 0.0) { L2_hi = L2_k; break; }
            if (std::isfinite(f_k)) { f_lo = f_k; L2_lo = L2_k; }
        }
        if (L2_hi < 0.0) { ok = false; return 0.0; }
        for (int iter = 0; iter < 80; ++iter) {
            const double L2_mid = 0.5 * (L2_lo + L2_hi);
            const double f_mid = reverseSpiralResidual(arc1Center, R1, signR1, type1, arc1Start, arc1AzStart, L1,
                                                        arc2Center, R2, signR2, type2, arc2End, arc2AzEnd, L2_mid,
                                                        bestJ, bestAz1, bestAz2);
            if (std::abs(L2_hi - L2_lo) < 1e-4) { L2_lo = L2_mid; break; }
            if (f_lo * f_mid <= 0.0) { L2_hi = L2_mid; }
            else { L2_lo = L2_mid; f_lo = f_mid; }
        }
        ok = true;
        return 0.5 * (L2_lo + L2_hi);
    };

    // ── 內層（角色互換）：給定 L2，二分求出匹配的 L1 ────────────────────────
    auto solveL1FromL2 = [&](double L2, bool& ok) -> double {
        const int nScan = 200;
        double L1_lo = 1e-3;
        double f_lo = reverseSpiralResidual(arc1Center, R1, signR1, type1, arc1Start, arc1AzStart, L1_lo,
                                            arc2Center, R2, signR2, type2, arc2End, arc2AzEnd, L2,
                                            bestJ, bestAz1, bestAz2);
        double L1_hi = -1.0;
        for (int m = 1; m <= nScan; ++m) {
            const double L1_k = L1_max * m / static_cast<double>(nScan);
            const double f_k  = reverseSpiralResidual(arc1Center, R1, signR1, type1, arc1Start, arc1AzStart, L1_k,
                                                      arc2Center, R2, signR2, type2, arc2End, arc2AzEnd, L2,
                                                      bestJ, bestAz1, bestAz2);
            if (std::isfinite(f_lo) && std::isfinite(f_k) && f_lo * f_k < 0.0) { L1_hi = L1_k; break; }
            if (std::isfinite(f_k)) { f_lo = f_k; L1_lo = L1_k; }
        }
        if (L1_hi < 0.0) { ok = false; return 0.0; }
        for (int iter = 0; iter < 80; ++iter) {
            const double L1_mid = 0.5 * (L1_lo + L1_hi);
            const double f_mid = reverseSpiralResidual(arc1Center, R1, signR1, type1, arc1Start, arc1AzStart, L1_mid,
                                                        arc2Center, R2, signR2, type2, arc2End, arc2AzEnd, L2,
                                                        bestJ, bestAz1, bestAz2);
            if (std::abs(L1_hi - L1_lo) < 1e-4) { L1_lo = L1_mid; break; }
            if (f_lo * f_mid <= 0.0) { L1_hi = L1_mid; }
            else { L1_lo = L1_mid; f_lo = f_mid; }
        }
        ok = true;
        return 0.5 * (L1_lo + L1_hi);
    };

    double L1 = 0.0, L2 = 0.0;
    bool ok = false;

    switch (strategy) {
    case ReverseSpiralStrategy::FixL1: {
        L1 = std::abs(strategyParam);
        L2 = solveL2FromL1(L1, ok);
        break;
    }
    case ReverseSpiralStrategy::FixL2: {
        L2 = std::abs(strategyParam);
        L1 = solveL1FromL2(L2, ok);
        break;
    }
    case ReverseSpiralStrategy::FixedAValue: {
        const double A = std::abs(strategyParam);
        L1 = A * A / R1;
        L2 = A * A / R2;
        double dummyJ1, dummyJ2;
        const double res = reverseSpiralResidual(arc1Center, R1, signR1, type1, arc1Start, arc1AzStart, L1,
                                                  arc2Center, R2, signR2, type2, arc2End, arc2AzEnd, L2,
                                                  bestJ, bestAz1, bestAz2);
        (void)dummyJ1; (void)dummyJ2;
        ok = std::isfinite(res) && std::abs(res) < 1e-4;
        if (!ok) {
            qWarning() << "[AlignmentSolver] solveReverseSpiral: FixedAValue A=" << A
                       << "does not lie on the reverse-spiral solution family for"
                       << "this arc pair (residual=" << res << "rad).";
        }
        break;
    }
    case ReverseSpiralStrategy::EqualLength:
    case ReverseSpiralStrategy::TotalLength:
    case ReverseSpiralStrategy::EqualAValue: {
        // 外層對 L1 做一次 bisection，驅動 outer(L1) = 0：
        //   EqualLength  : g(L1) − L1
        //   TotalLength  : g(L1) + L1 − strategyParam
        //   EqualAValue  : g(L1)/R2 − L1/R1
        auto outerF = [&](double L1v, bool& innerOk) -> double {
            const double L2v = solveL2FromL1(L1v, innerOk);
            if (!innerOk) return std::numeric_limits<double>::quiet_NaN();
            switch (strategy) {
            case ReverseSpiralStrategy::EqualLength: return L2v - L1v;
            case ReverseSpiralStrategy::TotalLength: return L2v + L1v - strategyParam;
            case ReverseSpiralStrategy::EqualAValue: return L2v / R2 - L1v / R1;
            default: return 0.0;
            }
        };

        const int nScan = 100;
        double L1_lo = 1e-3;
        bool okLo = false;
        double f_lo = outerF(L1_lo, okLo);
        double L1_hi = -1.0, f_hi = 0.0;
        for (int m = 1; m <= nScan; ++m) {
            const double L1_k = L1_max * m / static_cast<double>(nScan);
            bool okK = false;
            const double f_k = outerF(L1_k, okK);
            if (okLo && okK && std::isfinite(f_lo) && std::isfinite(f_k) && f_lo * f_k < 0.0) {
                L1_hi = L1_k; f_hi = f_k; break;
            }
            if (okK) { f_lo = f_k; L1_lo = L1_k; okLo = true; }
        }
        if (L1_hi < 0.0) { ok = false; break; }
        for (int iter = 0; iter < 60; ++iter) {
            const double L1_mid = 0.5 * (L1_lo + L1_hi);
            bool okMid = false;
            const double f_mid = outerF(L1_mid, okMid);
            if (std::abs(L1_hi - L1_lo) < 1e-4) { L1_lo = L1_mid; break; }
            if (!okMid) { L1_hi = L1_mid; continue; }
            if (f_lo * f_mid <= 0.0) { L1_hi = L1_mid; }
            else { L1_lo = L1_mid; f_lo = f_mid; }
        }
        L1 = 0.5 * (L1_lo + L1_hi);
        L2 = solveL2FromL1(L1, ok);
        break;
    }
    case ReverseSpiralStrategy::PickJunction: {
        QVector<ReverseSpiralFamilySample> samples;
        if (!analyzeReverseSpiralFamily(arc1Center, R1, arc1Start, arc1AzStart,
                                        arc2Center, R2, arc2End, arc2AzEnd,
                                        type1, type2, samples, 24)) {
            ok = false;
            break;
        }
        int bestIdx = -1; double bestDist = std::numeric_limits<double>::infinity();
        for (int i = 0; i < samples.size(); ++i) {
            const double d = QLineF(samples[i].junction, pickedJunctionHint).length();
            if (d < bestDist) { bestDist = d; bestIdx = i; }
        }
        if (bestIdx < 0) { ok = false; break; }
        L1 = samples[bestIdx].length1;
        L2 = solveL2FromL1(L1, ok);
        break;
    }
    }

    if (!ok) {
        qWarning() << "[AlignmentSolver] solveReverseSpiral: failed to converge for strategy"
                   << static_cast<int>(strategy);
        return result;
    }

    // ── 由收斂的 L1、L2 反推真正的圓弧裁切點（見檔案開頭大段註解） ───────────
    //   azCS = azJ − thetaS（solveCA() 同款公式，此處 azJ 已知，回推 azCS）。
    double thetaS1 = 0.0, thetaS2 = 0.0;
    {
        const auto elem1 = makeTransitionElement(type1, L1, signR1 * R1);
        thetaS1 = (L1 > 1e-9) ? elem1->localFrame(L1).theta : 0.0;
        const auto elem2 = makeTransitionElement(type2, L2, signR2 * R2);
        thetaS2 = (L2 > 1e-9) ? elem2->localFrame(L2).theta : 0.0;
    }
    const double azArc1Trim = bestAz1 - thetaS1;
    const double azArc2Trim = bestAz2 + thetaS2;

    const QPointF arc1TrimPoint(arc1Center.x() - signR1 * R1 * std::cos(azArc1Trim),
                                arc1Center.y() + signR1 * R1 * std::sin(azArc1Trim));
    const QPointF arc2TrimPoint(arc2Center.x() - signR2 * R2 * std::cos(azArc2Trim),
                                arc2Center.y() + signR2 * R2 * std::sin(azArc2Trim));

    auto arcAngleOf = [](QPointF from, QPointF to, QPointF centre, int signR) -> double {
        const QPointF rf = from - centre;
        const QPointF rt = to   - centre;
        const double cross = rf.x() * rt.y() - rf.y() * rt.x();
        const double dot   = rf.x() * rt.x() + rf.y() * rt.y();
        double phi = std::atan2(std::abs(cross), dot);
        if (signR > 0 && cross > 0.0) phi = 2.0 * M_PI - phi;
        if (signR < 0 && cross < 0.0) phi = 2.0 * M_PI - phi;
        return phi;
    };
    const double phi1 = arcAngleOf(arc1Start, arc1TrimPoint, arc1Center, signR1);
    const double phi2 = arcAngleOf(arc2TrimPoint, arc2End, arc2Center, signR2);

    result.valid          = true;
    result.arc1StartPoint = arc1Start;
    result.arc1TrimPoint  = arc1TrimPoint;
    result.arc1Len        = R1 * phi1;
    result.azArc1Trim     = azArc1Trim;
    result.length1        = L1;
    result.R1             = R1;
    result.junction       = bestJ;
    result.junctionAzimuth = bestAz1;
    result.length2        = L2;
    result.R2             = R2;
    result.arc2TrimPoint  = arc2TrimPoint;
    result.arc2EndPoint   = arc2End;
    result.arc2Len        = R2 * phi2;
    result.azArc2Trim     = azArc2Trim;
    result.residualNorm   = std::abs(AlignmentSolver::normaliseAngle(bestAz1 - bestAz2));

    return result;
}

// ============================================================================
//  solveReverseSpiralLM  —  Phase 2 of AlignmentSolver_LM統一升級計畫.md
//
//  Same inputs/outputs as solveReverseSpiral(), same underlying geometry
//  (reuses reverseSpiralResidual() unchanged — the rotation-symmetry +
//  circle-intersection closed form is not what's being replaced here). The
//  only thing that changes is *how* (L1,L2) is driven to satisfy the
//  residual: instead of solveReverseSpiral()'s nested bisection (outer L1 /
//  inner L2, with EqualLength/TotalLength/EqualAValue wrapping a THIRD outer
//  bisection layer on top of that), this solves the 2-unknown system
//  [L1,L2] jointly in one AlignmentNLSolver::solve() call with 2 residuals:
//
//    r[0] = reverseSpiralResidual(L1, L2)              (the geometric
//           tangency/continuity condition — see the long comment above
//           jFromArcCT()/jFromArcTC() for the derivation)
//    r[1] = strategy-specific second equation (see ReverseSpiralStrategy;
//           mirrors exactly the closed forms already used inside
//           solveReverseSpiral()'s switch statement, just expressed as a
//           residual instead of a bisection target)
//
//  FixL1/FixL2/FixedAValue reduce to a single unknown (the other length is
//  pinned by strategyParam), so those three still use a plain 1-unknown
//  solve (n=1) rather than the full 2-unknown system — there is no nested
//  bisection to eliminate for those cases in the first place, so LM buys
//  nothing there beyond what a single bisection already does; they are
//  included here only so callers can treat all seven strategies uniformly
//  through one function.  PickJunction is not amenable to LM at all — "pick
//  the closest family member to a point the user clicked" is a discrete
//  nearest-neighbour search over analyzeReverseSpiralFamily() samples, not
//  a root-finding problem — so it delegates to analyzeReverseSpiralFamily()
//  exactly as solveReverseSpiral() does, then finishes with the plain
//  1-unknown solve for the resulting fixed L1.
// ============================================================================

SolvedReverseSpiral AlignmentSolver::solveReverseSpiralLM(
    QPointF arc1Center, double arc1Radius,
    QPointF arc1Start,  double arc1AzStart,
    QPointF arc2Center, double arc2Radius,
    QPointF arc2End,    double arc2AzEnd,
    SpiralType type1, SpiralType type2,
    ReverseSpiralStrategy strategy, double strategyParam,
    QPointF pickedJunctionHint)
{
    SolvedReverseSpiral result;

    const double R1 = std::abs(arc1Radius);
    const double R2 = std::abs(arc2Radius);
    if (R1 < 1e-9 || R2 < 1e-9) {
        qWarning() << "[AlignmentSolver] solveReverseSpiralLM: arc radius ≈ 0";
        return result;
    }

    auto computeSignR = [](QPointF centre, QPointF refPt, double az) -> int {
        const double n_x = std::cos(az), n_y = -std::sin(az);
        const double side = (centre.x() - refPt.x()) * n_x
                           + (centre.y() - refPt.y()) * n_y;
        return (side >= 0.0) ? 1 : -1;
    };
    const int signR1 = computeSignR(arc1Center, arc1Start, arc1AzStart);
    const int signR2 = computeSignR(arc2Center, arc2End,   arc2AzEnd);

    if (signR1 == signR2) {
        qWarning() << "[AlignmentSolver] solveReverseSpiralLM: arc1/arc2 turn the same"
                   << "direction — not a reverse spiral (use solveACA()).";
        return result;
    }

    const double geomDist = std::hypot(arc2End.x() - arc1Start.x(),
                                       arc2End.y() - arc1Start.y())
                          + R1 + R2;
    const double L1_max = std::min(geomDist, M_PI * R1 * 0.45);
    const double L2_max = std::min(geomDist, M_PI * R2 * 0.45);

    QPointF bestJ; double bestAz1 = 0.0, bestAz2 = 0.0;

    // Wraps reverseSpiralResidual() with the bestJ/bestAz1/bestAz2 out-params
    // already bound, returning NaN (via reverseSpiralResidual's own contract)
    // when the two circles don't intersect at this (L1,L2) — the residual
    // function below turns that into a large-but-finite penalty so LM never
    // sees a NaN in the Jacobian.
    auto geomResidual = [&](double L1v, double L2v) -> double {
        const double r = reverseSpiralResidual(
            arc1Center, R1, signR1, type1, arc1Start, arc1AzStart, L1v,
            arc2Center, R2, signR2, type2, arc2End,   arc2AzEnd,   L2v,
            bestJ, bestAz1, bestAz2);
        return std::isfinite(r) ? r : 1e3;  // circles don't intersect: steer away
    };

    // ── seed initial guess from the family scan (see plan doc §3: "initial
    //    guess" guidance — LM is sensitive to starting point, and an
    //    arbitrary constant risks landing in the non-intersecting region
    //    where geomResidual() is a flat penalty plateau with zero gradient).
    QVector<ReverseSpiralFamilySample> samples;
    const bool haveSamples = analyzeReverseSpiralFamily(
        arc1Center, R1, arc1Start, arc1AzStart,
        arc2Center, R2, arc2End,   arc2AzEnd,
        type1, type2, samples, 8);
    if (!haveSamples || samples.isEmpty()) {
        qWarning() << "[AlignmentSolver] solveReverseSpiralLM: no solution family"
                   << "(arcs geometrically incompatible — too close/too far apart).";
        return result;
    }
    // Median sample is a reasonable strategy-agnostic seed for all cases
    // except PickJunction (handled separately below).
    const auto& seed = samples[samples.size() / 2];

    double L1 = 0.0, L2 = 0.0;
    bool ok = false;

    // ── PickJunction: identical discrete search to solveReverseSpiral() —
    //    not a root-finding problem, so LM is not applicable; delegate.
    if (strategy == ReverseSpiralStrategy::PickJunction) {
        int bestIdx = -1; double bestDist = std::numeric_limits<double>::infinity();
        for (int i = 0; i < samples.size(); ++i) {
            const double d = QLineF(samples[i].junction, pickedJunctionHint).length();
            if (d < bestDist) { bestDist = d; bestIdx = i; }
        }
        if (bestIdx < 0) {
            qWarning() << "[AlignmentSolver] solveReverseSpiralLM: PickJunction found no sample.";
            return result;
        }
        L1 = samples[bestIdx].length1;
        // Single-unknown LM solve for L2 given fixed L1 (same 1-D case as
        // FixL1 below — see that branch for the shared implementation).
        AlignmentNLSolver::ResidualFn fn = [&](const QVector<double>& x, QVector<double>& r) {
            r.resize(1);
            r[0] = geomResidual(L1, x[0]);
        };
        AlignmentNLSolver solver;
        const auto res = solver.solve({seed.length2}, fn, {L2_max});
        ok = res.converged;
        L2 = res.x[0];
    }
    // ── FixL1 / FixL2 / FixedAValue: genuinely 1 unknown, no nested
    //    bisection existed to eliminate — plain 1-D LM solve for parity.
    else if (strategy == ReverseSpiralStrategy::FixL1) {
        L1 = std::abs(strategyParam);
        AlignmentNLSolver::ResidualFn fn = [&](const QVector<double>& x, QVector<double>& r) {
            r.resize(1);
            r[0] = geomResidual(L1, x[0]);
        };
        AlignmentNLSolver solver;
        const auto res = solver.solve({seed.length2}, fn, {L2_max});
        ok = res.converged;
        L2 = res.x[0];
    }
    else if (strategy == ReverseSpiralStrategy::FixL2) {
        L2 = std::abs(strategyParam);
        AlignmentNLSolver::ResidualFn fn = [&](const QVector<double>& x, QVector<double>& r) {
            r.resize(1);
            r[0] = geomResidual(x[0], L2);
        };
        AlignmentNLSolver solver;
        const auto res = solver.solve({seed.length1}, fn, {L1_max});
        ok = res.converged;
        L1 = res.x[0];
    }
    else if (strategy == ReverseSpiralStrategy::FixedAValue) {
        const double A = std::abs(strategyParam);
        L1 = A * A / R1;
        L2 = A * A / R2;
        const double res = geomResidual(L1, L2);
        ok = std::abs(res) < 1e-4;
        if (!ok) {
            qWarning() << "[AlignmentSolver] solveReverseSpiralLM: FixedAValue A=" << A
                       << "does not lie on the reverse-spiral solution family for"
                       << "this arc pair (residual=" << res << "rad).";
        }
    }
    // ── EqualLength / TotalLength / EqualAValue: the real Phase-2 payoff —
    //    genuine 2-unknown joint solve, replacing solveReverseSpiral()'s
    //    THREE nested bisection layers (outer strategy-loop over L1, inner
    //    loop solving L2 from L1) with one AlignmentNLSolver::solve() call.
    else {
        AlignmentNLSolver::ResidualFn fn = [&](const QVector<double>& x, QVector<double>& r) {
            const double L1v = x[0], L2v = x[1];
            r.resize(2);
            r[0] = geomResidual(L1v, L2v);
            switch (strategy) {
            case ReverseSpiralStrategy::EqualLength: r[1] = L2v - L1v; break;
            case ReverseSpiralStrategy::TotalLength: r[1] = L2v + L1v - strategyParam; break;
            case ReverseSpiralStrategy::EqualAValue: r[1] = L2v / R2 - L1v / R1; break;
            default: r[1] = 0.0; break;
            }
        };
        AlignmentNLSolver::Config cfg;
        cfg.tolerance = 1e-6;   // r[0] is radians (O(1e-2..1)), r[1] is meters
                                 // or meters/meter — both already O(1) scale,
                                 // so the default-scale tolerance guidance in
                                 // AlignmentNLSolver.h applies directly here.
        AlignmentNLSolver solver(cfg);
        QVector<double> x0     = {seed.length1, seed.length2};
        QVector<double> scale  = {L1_max, L2_max};
        const auto res = solver.solve(x0, fn, scale);
        ok = res.converged;
        L1 = res.x[0];
        L2 = res.x[1];
    }

    if (!ok || L1 <= 0.0 || L2 <= 0.0) {
        qWarning() << "[AlignmentSolver] solveReverseSpiralLM: failed to converge for strategy"
                   << static_cast<int>(strategy);
        return result;
    }

    // Re-evaluate once more at the converged (L1,L2) to get final bestJ/
    // bestAz1/bestAz2 (the lambda captures were last written by whichever
    // residual evaluation happened to run last inside the solver, which is
    // already the converged point in practice, but re-evaluating explicitly
    // here removes any dependency on AlignmentNLSolver's internal call order).
    geomResidual(L1, L2);

    // ── final geometry assembly — identical to solveReverseSpiral()'s tail,
    //    duplicated rather than factored out for this shadow-validation
    //    phase so the two implementations stay fully independent (see plan
    //    doc §2: "keep Bisection and LM cross-checkable" — sharing the tail
    //    would make a bug in the tail invisible to cross-validation).
    double thetaS1 = 0.0, thetaS2 = 0.0;
    {
        const auto elem1 = makeTransitionElement(type1, L1, signR1 * R1);
        thetaS1 = (L1 > 1e-9) ? elem1->localFrame(L1).theta : 0.0;
        const auto elem2 = makeTransitionElement(type2, L2, signR2 * R2);
        thetaS2 = (L2 > 1e-9) ? elem2->localFrame(L2).theta : 0.0;
    }
    const double azArc1Trim = bestAz1 - thetaS1;
    const double azArc2Trim = bestAz2 + thetaS2;

    const QPointF arc1TrimPoint(arc1Center.x() - signR1 * R1 * std::cos(azArc1Trim),
                                arc1Center.y() + signR1 * R1 * std::sin(azArc1Trim));
    const QPointF arc2TrimPoint(arc2Center.x() - signR2 * R2 * std::cos(azArc2Trim),
                                arc2Center.y() + signR2 * R2 * std::sin(azArc2Trim));

    auto arcAngleOf = [](QPointF from, QPointF to, QPointF centre, int signR) -> double {
        const QPointF rf = from - centre;
        const QPointF rt = to   - centre;
        const double cross = rf.x() * rt.y() - rf.y() * rt.x();
        const double dot   = rf.x() * rt.x() + rf.y() * rt.y();
        double phi = std::atan2(std::abs(cross), dot);
        if (signR > 0 && cross > 0.0) phi = 2.0 * M_PI - phi;
        if (signR < 0 && cross < 0.0) phi = 2.0 * M_PI - phi;
        return phi;
    };
    const double phi1 = arcAngleOf(arc1Start, arc1TrimPoint, arc1Center, signR1);
    const double phi2 = arcAngleOf(arc2TrimPoint, arc2End, arc2Center, signR2);

    result.valid          = true;
    result.arc1StartPoint = arc1Start;
    result.arc1TrimPoint  = arc1TrimPoint;
    result.arc1Len        = R1 * phi1;
    result.azArc1Trim     = azArc1Trim;
    result.length1        = L1;
    result.R1             = R1;
    result.junction       = bestJ;
    result.junctionAzimuth = bestAz1;
    result.length2        = L2;
    result.R2             = R2;
    result.arc2TrimPoint  = arc2TrimPoint;
    result.arc2EndPoint   = arc2End;
    result.arc2Len        = R2 * phi2;
    result.azArc2Trim     = azArc2Trim;
    result.residualNorm   = std::abs(AlignmentSolver::normaliseAngle(bestAz1 - bestAz2));

    return result;
}

// ============================================================================
//  solveReverseSCS  — Tangent → SpiralIn(L1) → Arc1(R1) → [反向對
//  Lm1/Lm2，EqualLength] → Arc2(R2) → SpiralOut(L2) → Tangent
//
//  見 AlignmentSolver.h 宣告處的分步說明；本函式不做任何聯立迭代，純粹
//  是「兩段封閉式正/反向追跡 + 1 次既有 solveReverseSpiral() 呼叫」，
//  刻意避免對「L1/L2 全部留白」重新推導一套新的聯立方程式（該情境數學上
//  是 1 自由度欠定，已與使用者確認改為 L1/L2 皆為輸入已知量，見
//  AlignmentDocument.h ReverseSCSSpec 註解）。
// ============================================================================

SolvedReverseSCS AlignmentSolver::solveReverseSCS(
    double radius1, double length1, SpiralType type1,
    double radius2, double length2, SpiralType type2,
    SpiralType typeM1, SpiralType typeM2,
    const QPointF& tan1Start, const QPointF& tan1End,
    const QPointF& tan2Start, const QPointF& tan2End)
{
    SolvedReverseSCS result;

    const double R1 = std::abs(radius1);
    const double R2 = std::abs(radius2);
    if (R1 < 1e-9 || R2 < 1e-9) {
        qWarning() << "[AlignmentSolver] solveReverseSCS: radius1/radius2 ≈ 0";
        return result;
    }
    const double L1 = std::max(0.0, length1);
    const double L2 = std::max(0.0, length2);

    // ── Step 1：轉向自動判斷 ────────────────────────────────────────────────
    //    以 tangent1 方向的右垂直分量，判斷 tangent2 的參考點落在哪一側：
    //    落在右側 → Arc1 先右彎（signR1=+1）；落在左側 → Arc1 先左彎
    //    （signR1=-1）。Arc2 轉向恆與 Arc1 相反（反向曲線定義）。
    const double az1 = azimuthOf(tan1Start, tan1End);
    const double sA1 = std::sin(az1), cA1 = std::cos(az1);
    const double n1x = cA1, n1y = -sA1;   // tangent1 的右垂直方向

    const double sideVal = (tan2Start.x() - tan1End.x()) * n1x
                         + (tan2Start.y() - tan1End.y()) * n1y;
    const int signR1 = (sideVal >= 0.0) ? 1 : -1;
    const int signR2 = -signR1;

    // ── Step 2：正向追跡 tangent1 + L1 → Arc1 起點（PC₁）───────────────────
    QPointF arc1Start;
    double  arc1AzStart = 0.0;
    QPointF arc1Center;
    {
        const auto elem = makeTransitionElement(type1, L1, signR1 * R1);
        const LocalFrame lf = elem->localFrame(L1);
        const double Xm = lf.x, Ym = lf.y, thetaS = lf.theta;

        const double worldDx = Xm * sA1 + Ym * cA1;
        const double worldDy = Xm * cA1 - Ym * sA1;
        arc1Start    = tan1End + QPointF(worldDx, worldDy);
        arc1AzStart  = az1 + thetaS;
        arc1Center   = QPointF(
            arc1Start.x() + signR1 * R1 * std::cos(arc1AzStart),
            arc1Start.y() - signR1 * R1 * std::sin(arc1AzStart));
    }

    // ── Step 3：反向追跡 tangent2 + L2 → Arc2 終點（PT₂）───────────────────
    //    「走 ST→CS、訊號取反」，比照 solveCA() Step 4 的既有慣例。
    QPointF arc2End;
    double  arc2AzEnd = 0.0;
    QPointF arc2Center;
    {
        const double az2 = azimuthOf(tan2Start, tan2End);
        const double sA2 = std::sin(az2), cA2 = std::cos(az2);

        const auto elem = makeTransitionElement(type2, L2, -signR2 * R2);
        const LocalFrame lf = elem->localFrame(L2);
        const double Xm = lf.x, Ym = lf.y, thetaS = lf.theta;

        const double azCS  = az2 + thetaS;
        const double stDx  = Xm * sA2 + Ym * cA2;
        const double stDy  = Xm * cA2 - Ym * sA2;

        arc2End    = tan2Start - QPointF(stDx, stDy);
        arc2AzEnd  = azCS;
        arc2Center = QPointF(
            arc2End.x() + signR2 * R2 * std::cos(arc2AzEnd),
            arc2End.y() - signR2 * R2 * std::sin(arc2AzEnd));
    }

    // ── Step 4：中間反向對，固定 EqualLength ───────────────────────────────
    //
    //  Arc₁/Arc₂ 的入口/出口幾何（arc1Pc/arc1Center/arc2Pt/arc2Center 等）
    //  只依賴上面 Step 2／Step 3 算出的值，跟這裡的反向對解不解得出來
    //  無關，所以不論成功或失敗都先填進 result——失敗時這組值就是唯一能
    //  告訴使用者「兩弧實際落在哪裡、為什麼搭不起來」的診斷資訊（見下面
    //  failureReason 組出的訊息，直接用得上這幾個值算出的圓心距離）。
    result.arc1Pc     = arc1Start;
    result.azArc1Pc   = arc1AzStart;
    result.arc1Center = arc1Center;
    result.signR1     = signR1;

    result.arc2Pt     = arc2End;
    result.azArc2Pt   = arc2AzEnd;
    result.arc2Center = arc2Center;
    result.signR2     = signR2;

    result.R1 = R1;
    result.R2 = R2;

    const SolvedReverseSpiral rs = solveReverseSpiral(
        arc1Center, R1, arc1Start, arc1AzStart,
        arc2Center, R2, arc2End,   arc2AzEnd,
        typeM1, typeM2,
        ReverseSpiralStrategy::EqualLength, 0.0);

    if (!rs.valid) {
        // 組出具體數據的失敗說明，而不是只講「失敗」兩個字——這幾個數字
        // 是判斷「為什麼搭不起來」最直接的線索：
        //  - centerDist：兩弧圓心的實際距離。
        //  - R1+R2／|R1-R2|：兩圓「外切」／「內切」的臨界距離，反向曲線
        //    的兩弧圓心距離需要落在合理範圍內（太接近或太遠，緩和曲線
        //    在 π·R·0.45 的掃描上限內都搭不出一個能讓兩段等長的交會點），
        //    給使用者一個具體的參考基準，而不用自己去猜。
        const double centerDist = std::hypot(arc2Center.x() - arc1Center.x(),
                                             arc2Center.y() - arc1Center.y());
        result.failureReason = QStringLiteral(
            "Reverse pair (EqualLength) could not be solved.\n"
            "Arc1 center = (%1, %2),  Arc2 center = (%3, %4)\n"
            "Center distance = %5 m   |   R1+R2 = %6 m   |   |R1-R2| = %7 m\n"
            "The transition-spiral search is capped at pi*R*0.45 per side"
            " (~%8 m for Arc1, ~%9 m for Arc2); if the centers are far outside the"
            " R1+R2/|R1-R2| range, or the required spiral length would exceed that cap,"
            " no EqualLength junction exists for these inputs.\n"
            "Try: larger/smaller R1 or R2, different L1/L2 (they shift where each arc"
            " starts), or re-check which tangents were selected.")
            .arg(arc1Center.x(), 0, 'f', 3).arg(arc1Center.y(), 0, 'f', 3)
            .arg(arc2Center.x(), 0, 'f', 3).arg(arc2Center.y(), 0, 'f', 3)
            .arg(centerDist, 0, 'f', 3)
            .arg(R1 + R2, 0, 'f', 3)
            .arg(std::abs(R1 - R2), 0, 'f', 3)
            .arg(M_PI * R1 * 0.45, 0, 'f', 3)
            .arg(M_PI * R2 * 0.45, 0, 'f', 3);

        qWarning() << "[AlignmentSolver] solveReverseSCS: inner solveReverseSpiral() failed"
                   << "(R1=" << R1 << "L1=" << L1 << "R2=" << R2 << "L2=" << L2
                   << "centerDist=" << centerDist << ")";
        return result;   // result.valid stays false; entry/exit geometry above is kept
    }

    result.valid          = true;
    result.arc1TrimPoint  = rs.arc1TrimPoint;
    result.azArc1Trim     = rs.azArc1Trim;
    result.arc1Len        = rs.arc1Len;

    result.Lm1             = rs.length1;
    result.junction         = rs.junction;
    result.junctionAzimuth  = rs.junctionAzimuth;
    result.Lm2             = rs.length2;

    result.arc2TrimPoint  = rs.arc2TrimPoint;
    result.azArc2Trim     = rs.azArc2Trim;
    result.arc2Len        = rs.arc2Len;

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
            if (resolvedSpiralLengths[N] >= 1e-9) {
                // CS(N-1): next segment is the exit spiral.
                raw.append({ pos, az, false, resolvedSpiralLengths[N], resolvedArcRadii[N - 1] });
            }
            // else：出螺旋省略（長度 0）——圓弧直接接到終點切線，這個位置
            // 跟下面無條件產生的終點節點（見 raw.append(stRaw...)）幾何上
            // 完全重合。對稱於前面「入螺旋省略」時的 retag-not-append 處理
            // （見本函式開頭 "Entry spiral omitted" 註解），這裡刻意不額外
            // 產生節點，否則資料表尾端會多出一列里程相同、內容重複的資料
            // （實測：TC(0)-CS-SC-CT 四列的 ALD，若沒有這段會多出一列
            // 誤標為 "CS" 的重複列，加上原本就有的終點節點，變成六列）。
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
    //
    // ── 修正：先前這裡誤用了跟「入螺旋」forward-walk 相同的旋轉公式
    // （Xm*sin+Ym*cos, Xm*cos−Ym*sin），只適用於「已知起點方位角，往前走」
    // 的情況。但這裡是反過來——已知的是終點方位角 az2，要從 CS（弧終點）
    // 算到 ST，對應的是 solveSCS Step 7c／solveCA 用的「鏡射」公式（見
    // solveSCS：stDx = Xm2*sin(az2) − Ym2*cos(az2)；stDy = Xm2*cos(az2) +
    // Ym2*sin(az2)，等同先把局部 y 取負再依 az2 旋轉）。少了這個鏡射，
    // 等同旋轉角度少轉了整段出螺旋自身的偏轉角 theta[N]，在 theta[N] 不小
    // 的情況下（例如蛋形線一側接續的出螺旋），會讓最後一段出螺旋跟前一個
    // 圓弧之間出現數公尺等級的縱向落差（實測 G02U R109.4 蛋形線案例：
    // 修正前誤差 ~7.6m，修正後 <1mm）。
    QPointF stRaw = pos;
    if (resolvedSpiralLengths[N] >= 1e-9) {
        const double stDx =  Xm[N] * std::sin(az2) - Ym[N] * std::cos(az2);
        const double stDy =  Xm[N] * std::cos(az2) + Ym[N] * std::sin(az2);
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

    // Reverse-spiral group data — stored per SpiralIn element index (SpiralIn
    // and its adjacent SpiralOut both carry isReverseSpiralGroup==true, see
    // ReverseSpiral_Command_實作計畫.md 第 2.4 節). SpiralIn sits between
    // Fixed Arc₁ (before) and Fixed Arc₂ (after), exactly like ACAData, but
    // unlike ACA the "spiral" here is TWO physically distinct elements
    // (SpiralIn=S1, SpiralOut=S2) meeting at a real curvature-zero junction,
    // so both element indices are tracked (spiralOutIdx) for the
    // point-sequence builder below.
    struct RSData {
        bool                 valid        = false;
        SolvedReverseSpiral  rs;
        int                  arc1Idx      = -1;   ///< Fixed Arc₁ element index (before S1)
        int                  arc2Idx      = -1;   ///< Fixed Arc₂ element index (after S2)
        int                  spiralOutIdx = -1;   ///< index of the paired SpiralOut (S2) element
    };
    QVector<RSData> rsData(n);

    // RSCS group data (ALIGNMENTREVSCS/RSCS) — stored per group's leading
    // SpiralIn(L1) element index. See Pass 2g below and the emission-loop
    // hasRSCS_* blocks (mirrors RSData's role, but for the 6-element group
    // built by addReverseSCS(): SpiralIn(L1)/CircularArc(R1)/SpiralOut(Lm1)/
    // SpiralIn(Lm2)/CircularArc(R2)/SpiralOut(L2)).
    struct RSCSData {
        bool              valid   = false;
        SolvedReverseSCS  rscs;
        int               arc1Idx = -1;   ///< index of CircularArc(R1)
        int               mid1Idx = -1;   ///< index of SpiralOut(Lm1), reverse-pair 1st leg
        int               mid2Idx = -1;   ///< index of SpiralIn(Lm2), reverse-pair 2nd leg
        int               arc2Idx = -1;   ///< index of CircularArc(R2)
        int               out2Idx = -1;   ///< index of SpiralOut(L2)
        int               tanAfterIdx = -1; ///< real Tangent(after) index
    };
    QVector<RSCSData> rscsData(n);

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
        // RSCS 群組（見 Pass 2g）自成一套 6 元素辨識與求解路徑，即使
        // tangentIdxBefore/After 恰好也指向兩條真正 Tangent（比照
        // addCompoundChain() 的邊界切線共用慣例），也不可讓 Pass 2b 把它
        // 前 3 個元素誤判為一般 SCS/複合鏈結——RSCS 中間的 SpiralOut
        // （反向對第一段）長度尚未反解（此時為 0），提早被 Pass 2b 當成
        // 一般 SCS 消費會產生錯誤幾何。
        if (elems[i].isReverseSCSGroup)                       continue;

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
        // arc2 azimuth at its end = azPC + arcLen/R2, signed by arc2's
        // ACTUAL turn direction — NOT elems[arc2I].radius's sign. Fixed
        // CircularArc elements (see HorizontalAlignmentEdit::addFixedCurve())
        // always store radius = std::abs(radius), so "elems[arc2I].radius >=
        // 0.0" is true unconditionally and carries no turn-direction
        // information at all: a left-turning Arc₂ was previously always
        // (silently, incorrectly) treated as right-turning here, corrupting
        // az2End — and with it the whole ACA solve — for any ACA group
        // whose second arc turns left. Re-derive the sign the same way
        // Pass 1 did when computing arcData[arc2I].azPC (cross product of
        // Arc₂'s own PC/PT relative to its centre — see the "Pass 1 – Fixed
        // CircularArc" loop above).
        const QPointF arc2R1 = arcData[arc2I].pc - elems[arc2I].arcCenter;
        const QPointF arc2R2 = arcData[arc2I].pt - elems[arc2I].arcCenter;
        const double  arc2CrossVal = arc2R1.x() * arc2R2.y() - arc2R1.y() * arc2R2.x();
        const double  arc2TurnSign = (arc2CrossVal >= 0.0) ? 1.0 : -1.0;
        const double az2End = arcData[arc2I].azPC
                             + arc2TurnSign * (arcData[arc2I].arcLen / R2);

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
    //  Pass 2f — Reverse-spiral group  (Fixed Arc₁ → SpiralIn(S1) →
    //            SpiralOut(S2) → Fixed Arc₂；曲率方向相反，中間過零)
    //
    //  Identification: element i is a SpiralIn with mode==Floating,
    //    isReverseSpiralGroup==true, tangentIdxBefore/After pointing at
    //    Fixed CircularArc elements (overloaded exactly like ACA, see
    //    addReverseSpiral()), and element (i+1) is the paired SpiralOut
    //    (also isReverseSpiralGroup==true). Both S1 and S2 lengths are
    //    unknown; solveReverseSpiral() finds them per the group's stored
    //    ReverseSpiralStrategy/Param (see ReverseSpiral_Command_實作計畫.md
    //    第 2.4 節).
    // ════════════════════════════════════════════════════════════════════════
    for (int i = 0; i < n; ++i) {
        if (elems[i].type != EditableElementType::SpiralIn)  continue;
        if (elems[i].mode != ConstraintMode::Floating)       continue;
        if (!elems[i].isReverseSpiralGroup)                  continue;
        // RSCS 群組（Pass 2g）的中間反向對兩個元素也帶 isReverseSpiralGroup
        // （沿用同一套「反向對」標記），但 tangentIdxBefore/After 存的是
        // RSCS 群組外側的 Tangent index，不是 CircularArc index，必須交給
        // Pass 2g 整組處理，這裡明確跳過（下面的型別檢查其實也會自然
        // 擋掉，這裡加上明確判斷純粹是避免未來改動時的隱性耦合）。
        if (elems[i].isReverseSCSGroup)                      continue;

        const int arc1I = elems[i].tangentIdxBefore;
        const int arc2I = elems[i].tangentIdxAfter;
        const int outI  = i + 1;

        if (arc1I < 0 || arc1I >= n) continue;
        if (arc2I < 0 || arc2I >= n) continue;
        if (elems[arc1I].type != EditableElementType::CircularArc) continue;
        if (elems[arc2I].type != EditableElementType::CircularArc) continue;
        if (elems[arc1I].mode != ConstraintMode::Fixed)            continue;
        if (elems[arc2I].mode != ConstraintMode::Fixed)            continue;
        if (outI >= n || elems[outI].type != EditableElementType::SpiralOut
                       || !elems[outI].isReverseSpiralGroup) {
            qWarning() << "[AlignmentSolver] Pass2f ReverseSpiral idx" << i
                       << ": paired SpiralOut not found immediately after — skipping";
            continue;
        }

        if (!arcData[arc1I].valid) {
            qWarning() << "[AlignmentSolver] Pass2f ReverseSpiral idx" << i
                       << ": Fixed arc1 at idx" << arc1I << " not solved in Pass 1";
            continue;
        }
        if (!arcData[arc2I].valid) {
            qWarning() << "[AlignmentSolver] Pass2f ReverseSpiral idx" << i
                       << ": Fixed arc2 at idx" << arc2I << " not solved in Pass 1";
            continue;
        }

        const double R1 = std::abs(elems[arc1I].radius);
        const double R2 = std::abs(elems[arc2I].radius);

        const double az1Start = arcData[arc1I].azPC;
        const double signedDelta2 = (elems[arc2I].radius >= 0.0)
                                    ? (arcData[arc2I].arcLen / R2)
                                    : -(arcData[arc2I].arcLen / R2);
        const double az2End = arcData[arc2I].azPC + signedDelta2;

        const auto strategy = static_cast<ReverseSpiralStrategy>(elems[i].reverseSpiralStrategy);
        const double strategyParam = elems[i].reverseSpiralParam;

        const SolvedReverseSpiral rs = solveReverseSpiral(
            elems[arc1I].arcCenter, R1,
            arcData[arc1I].pc,  az1Start,
            elems[arc2I].arcCenter, R2,
            arcData[arc2I].pt,  az2End,
            elems[i].spiralType1, elems[outI].spiralType1,
            strategy, strategyParam);

        if (!rs.valid) {
            qWarning() << "[AlignmentSolver] Pass2f ReverseSpiral idx" << i << ": solveReverseSpiral failed";
            continue;
        }

        RSData rd;
        rd.valid        = true;
        rd.rs           = rs;
        rd.arc1Idx      = arc1I;
        rd.arc2Idx      = arc2I;
        rd.spiralOutIdx = outI;
        rsData[i] = rd;

        // Write solved lengths back so they survive serialisation
        const_cast<EditableElement&>(elems[i]).length    = rs.length1;
        const_cast<EditableElement&>(elems[outI]).length = rs.length2;

        // Trim Arc₁ tail: its new PT = S1's arc-side point
        arcData[arc1I].arcLen = rs.arc1Len;
        arcData[arc1I].pt     = rs.arc1TrimPoint;

        // Trim Arc₂ head: its new PC = S2's arc-side point
        arcData[arc2I].pc     = rs.arc2TrimPoint;
        arcData[arc2I].azPC   = rs.azArc2Trim;
        arcData[arc2I].arcLen = rs.arc2Len;
    }

    // ════════════════════════════════════════════════════════════════════════
    //  Pass 2g — RSCS group  (Tangent → SpiralIn(L1) → CircularArc(R1) →
    //            [反向對 Lm1/Lm2] → CircularArc(R2) → SpiralOut(L2) →
    //            Tangent；見 AlignmentDocument.h ReverseSCSSpec、
    //            AlignmentSolver.h solveReverseSCS() 說明）。
    //
    //  Identification: 連續 6 個元素，全部 isReverseSCSGroup==true，
    //    型別依序為 SpiralIn / CircularArc / SpiralOut / SpiralIn /
    //    CircularArc / SpiralOut，tangentIdxBefore/After 皆指向本群組外側
    //    的兩條真正 Tangent（addReverseSCS() 建立時的固定慣例）。
    //
    //  本 Pass 完成 arcData／長度反解寫回後，寫入 rscsData[i]；「Build
    //  AlignmentPoint sequence」區塊的 hasRSCS_asArc1／hasRSCS_asArc2／
    //  hasRSCS_L1／hasRSCS_L2 幾個旗標（在 CircularArc 分支內，緊鄰
    //  hasLC/hasCA/hasRS_exit/hasRS_entry 之後）負責讀取 rscsData 並發出
    //  對應的 AlignmentPoint（TS/CS/ST/SC 等關鍵點）。尚未在真實建置環境
    //  驗證過，見與 Jack 的討論——下一步務必先跑一個具體案例核對座標。
    // ════════════════════════════════════════════════════════════════════════
    for (int i = 0; i + 5 < n; ++i) {
        if (elems[i].type != EditableElementType::SpiralIn) continue;
        if (elems[i].mode != ConstraintMode::Floating)       continue;
        if (!elems[i].isReverseSCSGroup)                     continue;

        const int arc1I = i + 1;
        const int mid1I = i + 2;
        const int mid2I = i + 3;
        const int arc2I = i + 4;
        const int out2I = i + 5;

        if (elems[arc1I].type != EditableElementType::CircularArc || !elems[arc1I].isReverseSCSGroup ||
            elems[mid1I].type != EditableElementType::SpiralOut   || !elems[mid1I].isReverseSCSGroup ||
            elems[mid2I].type != EditableElementType::SpiralIn    || !elems[mid2I].isReverseSCSGroup ||
            elems[arc2I].type != EditableElementType::CircularArc || !elems[arc2I].isReverseSCSGroup ||
            elems[out2I].type != EditableElementType::SpiralOut   || !elems[out2I].isReverseSCSGroup) {
            qWarning() << "[AlignmentSolver] Pass2g RSCS idx" << i
                       << ": malformed 6-element group — skipping";
            continue;
        }

        const int tb = elems[i].tangentIdxBefore;
        const int ta = elems[i].tangentIdxAfter;
        if (tb < 0 || tb >= n || elems[tb].type != EditableElementType::Tangent) {
            qWarning() << "[AlignmentSolver] Pass2g RSCS idx" << i << ": invalid tangentIdxBefore =" << tb;
            continue;
        }
        if (ta < 0 || ta >= n || elems[ta].type != EditableElementType::Tangent) {
            qWarning() << "[AlignmentSolver] Pass2g RSCS idx" << i << ": invalid tangentIdxAfter =" << ta;
            continue;
        }

        const SolvedReverseSCS rscs = solveReverseSCS(
            elems[arc1I].radius, elems[i].length,     elems[i].spiralType1,
            elems[arc2I].radius, elems[out2I].length,  elems[out2I].spiralType1,
            elems[mid1I].spiralType1, elems[mid2I].spiralType1,
            tanStart[tb], tanEnd[tb], tanStart[ta], tanEnd[ta]);

        if (!rscs.valid) {
            qWarning() << "[AlignmentSolver] Pass2g RSCS idx" << i << ": solveReverseSCS() failed";
            continue;
        }

        // Write solved mid-pair lengths back so they survive serialisation.
        const_cast<EditableElement&>(elems[mid1I]).length = rscs.Lm1;
        const_cast<EditableElement&>(elems[mid2I]).length = rscs.Lm2;

        arcData[arc1I].valid  = true;
        arcData[arc1I].pc     = rscs.arc1Pc;
        arcData[arc1I].pt     = rscs.arc1TrimPoint;
        arcData[arc1I].azPC   = rscs.azArc1Pc;
        arcData[arc1I].arcLen = rscs.arc1Len;

        arcData[arc2I].valid  = true;
        arcData[arc2I].pc     = rscs.arc2TrimPoint;
        arcData[arc2I].pt     = rscs.arc2Pt;
        arcData[arc2I].azPC   = rscs.azArc2Trim;
        arcData[arc2I].arcLen = rscs.arc2Len;

        RSCSData rd;
        rd.valid       = true;
        rd.rscs        = rscs;
        rd.arc1Idx     = arc1I;
        rd.mid1Idx     = mid1I;
        rd.mid2Idx     = mid2I;
        rd.arc2Idx     = arc2I;
        rd.out2Idx     = out2I;
        rd.tanAfterIdx = ta;
        rscsData[i] = rd;

        i += 5;   // skip the remaining 5 elements of this group
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

            // Suppress exit tangent too — mirrors the SCS/compound-chain
            // suppression above. Without this, the exit Tangent element
            // (tanIdx) is *also* processed normally by the main loop below,
            // producing a second point at the exact same coordinates as the
            // inline "ST" keypoint emitted here (see the CA emission block
            // further down). The two duplicate points used to be collapsed
            // by a display-layer dedup filter in AlignmentDataTableDialog,
            // which kept the *wrong* one — the inline "ST" point used to
            // hardcode length=0, while the real tangent length only existed
            // on the now-suppressed duplicate, so the data table showed "—"
            // for the last Tangent segment's length. Fixed by (a) suppressing
            // the duplicate here and (b) writing the real length directly
            // onto the inline "ST" point below, matching how the SCS block
            // already does it for its own exit tangent.
            const int ta = caData[i].tanIdx;
            if (ta >= 0 && ta < n && elems[ta].type == EditableElementType::Tangent)
                handledBySCS[ta] = true;
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
        // RSCS group (ALIGNMENTREVSCS/RSCS): the 4 spiral elements
        // (SpiralIn(L1), SpiralOut(Lm1), SpiralIn(Lm2), SpiralOut(L2)) are
        // all emitted inline by the hasRSCS_* blocks below, attached to the
        // two CircularArc elements — suppress just the spirals here (mirrors
        // the hasLC/hasCA suppression pattern), leaving arc1Idx/arc2Idx to
        // flow through the normal CircularArc branch. The exit tangent
        // (tanAfterIdx) is suppressed too, same reasoning as the CA block
        // above: its ST keypoint is emitted inline only when L2 > 0 (see
        // the hasRSCS_asArc2exit block); when L2 == 0 we deliberately leave
        // the exit tangent un-suppressed so it still gets a normal "CT"
        // Tangent row.
        if (rscsData[i].valid) {
            const RSCSData& rd = rscsData[i];
            handledBySCS[i]          = true;   // SpiralIn(L1)
            handledBySCS[rd.mid1Idx] = true;   // SpiralOut(Lm1)
            handledBySCS[rd.mid2Idx] = true;   // SpiralIn(Lm2)
            handledBySCS[rd.out2Idx] = true;   // SpiralOut(L2)

            if (elems[rd.out2Idx].length > 1e-9
                && rd.tanAfterIdx >= 0 && rd.tanAfterIdx < n
                && elems[rd.tanAfterIdx].type == EditableElementType::Tangent) {
                handledBySCS[rd.tanAfterIdx] = true;
            }
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


            const int nodeCount = chain.nodes.size();
            int spiralIdx = 0;   // index into chain.spiralTypes (0..N)
            for (int ni = 0; ni < nodeCount - 1; ++ni) {
                const CompoundChainNode& node = chain.nodes[ni];
                AlignmentPoint np;
                if (ni == 0) {
                    // 節點 0 平常是「切線→入螺旋」的 TS 邊界點；但入螺旋省略
                    // （長度 0）時，solveCompoundChain() 已把它 retag 成
                    // isArcStart=true（見該函式對 "Entry spiral omitted" 的
                    // 處理），代表這裡其實是切線直接接圓弧，應標為 TC，讓
                    // 資料表把它歸進「SC/CC/TC 半徑可編輯」那一支，正確顯示
                    // ／可編輯半徑（原本寫死 "TS" 會落入唯讀的曲線類型欄，
                    // 半徑值就顯示不出來）。
                    np.tsc = node.isArcStart ? QStringLiteral("TC") : QStringLiteral("TS");
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

                    // 出螺旋省略時（見 solveCompoundChain() 對稱於「入螺旋
                    // 省略」的 retag-not-append 處理），這個終點其實是圓弧
                    // 直接接切線，語意上是 "CT" 而不是 "ST"。判斷方式：倒數
                    // 第二個節點（緊接在這個終點之前）若 isArcStart==true，
                    // 代表中間沒有另外產生一個 "CS" 節點——圓弧末端直接落在
                    // 這個終點上，即出螺旋被省略。
                    const bool exitSpiralOmitted =
                        nodeCount >= 2 && chain.nodes[nodeCount - 2].isArcStart;

                    AlignmentPoint stTT;
                    stTT.tsc      = exitSpiralOmitted ? QStringLiteral("CT") : QStringLiteral("ST");
                    stTT.curveType = exitSpiralOmitted ? QStringLiteral("ARC") : QString();
                    stTT.radius   = exitSpiralOmitted ? chain.nodes[nodeCount - 2].radius : 0.0;
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

            // A reverse-spiral group has its SpiralIn(S1)+SpiralOut(S2) pair
            // occupying (arc1Idx+1, arc1Idx+2) — TWO elements, unlike ACA's
            // one — so relative to Arc₁ the SpiralIn sits at (i+1), and
            // relative to Arc₂ the SpiralIn sits at (i-2) (SpiralOut at i-1).
            // This arc is Arc₁ of a reverse-spiral group → SpiralIn at i+1
            const bool hasRS_exit  = (i + 1 < n && rsData[i+1].valid && rsData[i+1].arc1Idx == i);
            // This arc is Arc₂ of a reverse-spiral group → SpiralIn at i-2
            const bool hasRS_entry = (i >= 2     && rsData[i-2].valid && rsData[i-2].arc2Idx == i);

            // ── RSCS group (ALIGNMENTREVSCS/RSCS) ─────────────────────────────
            // Group layout (base index g = the leading SpiralIn(L1)):
            //   g, g+1=arc1Idx, g+2=mid1Idx, g+3=mid2Idx, g+4=arc2Idx, g+5=out2Idx
            // This arc is Arc₁ of an RSCS group → base sits at (i-1)
            const bool hasRSCS_asArc1 = (i > 0 && rscsData[i-1].valid && rscsData[i-1].arc1Idx == i);
            // This arc is Arc₂ of an RSCS group → base sits at (i-4)
            const bool hasRSCS_asArc2 = (i >= 4 && rscsData[i-4].valid && rscsData[i-4].arc2Idx == i);
            // Whether Arc₁'s entry spiral (L1) actually has non-zero length
            // (addReverseSCS() allows L1=0, degenerating this side to a plain
            // "TC" tangent-to-arc join — mirrors solveSCS()'s L1=0 handling).
            const bool hasRSCS_L1 = hasRSCS_asArc1 && elems[i-1].length > 1e-9;
            // Whether Arc₂'s exit spiral (L2) actually has non-zero length.
            const bool hasRSCS_L2 = hasRSCS_asArc2 && elems[rscsData[i-4].out2Idx].length > 1e-9;

            // ── Emit RSCS entry spiral keypoint (TS) before Arc₁'s own point ──
            if (hasRSCS_L1) {
                const SolvedReverseSCS& rscs = rscsData[i-1].rscs;
                const QString curveType = spiralTypeName(elems[i-1].spiralType1);
                const int tb = elems[i-1].tangentIdxBefore;

                AlignmentPoint tspt;
                tspt.tsc       = QStringLiteral("TS");
                tspt.curveType = curveType;
                tspt.easting   = tanEnd[tb].x();
                tspt.northing  = tanEnd[tb].y();
                tspt.azimuth   = azimuthOf(tanStart[tb], tanEnd[tb]);
                tspt.length    = elems[i-1].length;
                tspt.radius    = rscs.R1;
                tspt.chainage  = chainage;
                chainage      += elems[i-1].length;
                pts.append(tspt);
            }

            // ── Emit LC spiral keypoint (TS) before the arc CC point ──────────
            if (hasLC) {
                const SolvedLC& lc = lcData[i-1].lc;
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
                const bool hasLC_entry = (hasLC || hasACA_entry || hasRS_entry || hasRSCS_L1 || hasRSCS_asArc2);
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

            // ── Emit reverse-spiral keypoints (S1: CS→ST, junction, S2: TS) ───
            //  Unlike ACA (one continuous Egg element, emitted as a single
            //  "CS" keypoint), a reverse-spiral group has TWO physically
            //  distinct spirals meeting at a real curvature-zero junction —
            //  so we emit S1's "CS"→"ST" pair (mirroring the hasCA block
            //  below) followed immediately by S2's leading "TS" keypoint at
            //  the SAME location (mirroring the hasLC block above), with
            //  zero chainage gap between ST and TS (matching the existing
            //  isSSJunction convention for "two spirals meeting, no arc
            //  between" elsewhere in this solver).
            if (hasRS_exit) {
                const SolvedReverseSpiral& rs = rsData[i+1].rs;
                const int spiralInIdx  = i + 1;
                const int spiralOutIdx = rsData[i+1].spiralOutIdx;

                // CS point = start of S1 (exit of Arc₁ = entry of S1)
                AlignmentPoint cspt;
                cspt.tsc       = QStringLiteral("CS");
                cspt.curveType = spiralTypeName(elems[spiralInIdx].spiralType1);
                cspt.easting   = rs.arc1TrimPoint.x();
                cspt.northing  = rs.arc1TrimPoint.y();
                cspt.azimuth   = rs.azArc1Trim;
                cspt.length    = rs.length1;
                cspt.radius    = rs.R1;
                cspt.chainage  = chainage;
                chainage      += rs.length1;
                pts.append(cspt);

                // ST point = end of S1 = the curvature-zero junction
                AlignmentPoint stpt;
                stpt.tsc      = QStringLiteral("ST");
                stpt.easting  = rs.junction.x();
                stpt.northing = rs.junction.y();
                stpt.azimuth  = rs.junctionAzimuth;
                stpt.chainage = chainage;
                pts.append(stpt);

                // TS point = start of S2, same location as ST above (zero
                // chainage gap — this is the S1><S2 junction itself).
                AlignmentPoint tspt;
                tspt.tsc       = QStringLiteral("TS");
                tspt.curveType = spiralTypeName(elems[spiralOutIdx].spiralType1);
                tspt.easting   = rs.junction.x();
                tspt.northing  = rs.junction.y();
                tspt.azimuth   = rs.junctionAzimuth;
                tspt.length    = rs.length2;
                tspt.radius    = rs.R2;
                tspt.chainage  = chainage;
                chainage      += rs.length2;
                pts.append(tspt);

                // Arc₂ (starting at rs.arc2TrimPoint) will be emitted in the
                // normal arc flow below for element rsData[i+1].arc2Idx.
                continue;  // skip default PT waypoint for Arc₁
            }

            // ── Emit RSCS reverse-pair keypoints (CS: Arc₁→Lm1, junction,
            //    TS: Lm2→Arc₂) — mirrors the hasRS_exit block above exactly,
            //    sourced from rscsData instead of rsData (see Pass 2g /
            //    solveReverseSCS() — same underlying geometry shape, this
            //    arc's exit spiral is just called Lm1/Lm2 here since it sits
            //    between two Floating arcs traced from raw tangents rather
            //    than two pre-existing Fixed arcs).
            if (hasRSCS_asArc1) {
                const SolvedReverseSCS& rscs = rscsData[i-1].rscs;
                const int mid1Idx = rscsData[i-1].mid1Idx;
                const int mid2Idx = rscsData[i-1].mid2Idx;

                // CS point = start of Lm1 (exit of Arc₁ = entry of reverse pair)
                AlignmentPoint cspt;
                cspt.tsc       = QStringLiteral("CS");
                cspt.curveType = spiralTypeName(elems[mid1Idx].spiralType1);
                cspt.easting   = rscs.arc1TrimPoint.x();
                cspt.northing  = rscs.arc1TrimPoint.y();
                cspt.azimuth   = rscs.azArc1Trim;
                cspt.length    = rscs.Lm1;
                cspt.radius    = rscs.R1;
                cspt.chainage  = chainage;
                chainage      += rscs.Lm1;
                pts.append(cspt);

                // ST point = end of Lm1 = the curvature-zero junction
                AlignmentPoint stpt;
                stpt.tsc      = QStringLiteral("ST");
                stpt.easting  = rscs.junction.x();
                stpt.northing = rscs.junction.y();
                stpt.azimuth  = rscs.junctionAzimuth;
                stpt.chainage = chainage;
                pts.append(stpt);

                // TS point = start of Lm2, same location as ST above (zero
                // chainage gap — the S1><S2 junction itself).
                AlignmentPoint tspt;
                tspt.tsc       = QStringLiteral("TS");
                tspt.curveType = spiralTypeName(elems[mid2Idx].spiralType1);
                tspt.easting   = rscs.junction.x();
                tspt.northing  = rscs.junction.y();
                tspt.azimuth   = rscs.junctionAzimuth;
                tspt.length    = rscs.Lm2;
                tspt.radius    = rscs.R2;
                tspt.chainage  = chainage;
                chainage      += rscs.Lm2;
                pts.append(tspt);

                // Arc₂ (starting at rscs.arc2TrimPoint) will be emitted in
                // the normal arc flow below for element rscsData[i-1].arc2Idx.
                continue;  // skip default PT waypoint for Arc₁
            }

            // ── Emit CA spiral keypoint (CS then ST) after the arc ────────────
            if (hasCA) {
                const SolvedCA& ca = caData[i+1].ca;
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

                // ST waypoint = start of the trimmed exit tangent.
                // Carries the *actual* tangent length directly (mirrors the
                // SCS block's own "stTT.length = tlen" pattern) now that the
                // exit tangent element is suppressed via handledBySCS above
                // -- it will NOT be emitted a second time by the normal
                // Tangent flow, so this is the only row for this segment.
                const int ta = caData[i+1].tanIdx;
                const double tlen = (ta >= 0 && ta < n)
                    ? QLineF(ca.stPoint, tanEnd[ta]).length()
                    : 0.0;
                AlignmentPoint stpt;
                stpt.tsc      = QStringLiteral("ST");
                stpt.easting  = ca.stPoint.x();
                stpt.northing = ca.stPoint.y();
                stpt.azimuth  = ca.azST;
                stpt.length   = tlen;
                stpt.chainage = chainage;
                chainage     += tlen;
                pts.append(stpt);
                continue;  // skip the default PT waypoint below
            }

            // ── Emit RSCS exit spiral keypoint (CS then ST) after Arc₂ ────────
            //  Mirrors the hasCA block above, but L2 is a direct input (see
            //  ReverseSCSSpec) rather than solved via bisection, so the ST
            //  point is simply tan2Start (the anchor used unmodified by
            //  solveReverseSCS() — no additional trimming applied on this
            //  side, see AlignmentSolver.h solveReverseSCS() step 3).
            if (hasRSCS_L2) {
                const RSCSData& rd  = rscsData[i-4];
                const SolvedReverseSCS& rscs = rd.rscs;
                const int out2Idx = rd.out2Idx;
                const int ta      = rd.tanAfterIdx;
                const QString curveType = spiralTypeName(elems[out2Idx].spiralType1);

                // CS point (arc-end / spiral-start)
                AlignmentPoint cspt;
                cspt.tsc       = QStringLiteral("CS");
                cspt.curveType = curveType;
                cspt.easting   = rscs.arc2Pt.x();
                cspt.northing  = rscs.arc2Pt.y();
                cspt.azimuth   = rscs.azArc2Pt;
                cspt.length    = elems[out2Idx].length;
                cspt.radius    = rscs.R2;
                cspt.chainage  = chainage;
                chainage      += elems[out2Idx].length;
                pts.append(cspt);

                // ST waypoint = start of the (un-trimmed) exit tangent.
                const double tlen = (ta >= 0 && ta < n)
                    ? QLineF(tanStart[ta], tanEnd[ta]).length()
                    : 0.0;
                AlignmentPoint stpt;
                stpt.tsc      = QStringLiteral("ST");
                stpt.easting  = tanStart[ta].x();
                stpt.northing = tanStart[ta].y();
                stpt.azimuth  = azimuthOf(tanStart[ta], tanEnd[ta]);
                stpt.length   = tlen;
                stpt.chainage = chainage;
                chainage     += tlen;
                pts.append(stpt);
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

        // 供下方 sentinel 回溯搜尋使用：把每個複合弧鏈（N>=2 弧）群組的
        // 「最後一個元素索引」反查回「這個群組的起點（SpiralIn）索引」，
        // 一次建表，避免在迴圈裡對每個候選 i 都重新掃描一次 compoundData。
        QVector<int> compoundGroupStartByLastIdx(n, -1);
        for (int j = 0; j < n; ++j) {
            if (compoundData[j].valid && compoundData[j].lastIdx >= 0
                && compoundData[j].lastIdx < n) {
                compoundGroupStartByLastIdx[compoundData[j].lastIdx] = j;
            }
        }

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
                    && elems[ta].type == EditableElementType::Tangent
                    && !isBoundaryConstruction[ta]) {
                    sentinel.easting  = tanEnd[ta].x();
                    sentinel.northing = tanEnd[ta].y();
                    sentinel.azimuth  = azimuthOf(tanStart[ta], tanEnd[ta]);
                } else {
                    // Fallback: no real (non-construction) exit tangent found,
                    // anchor at ST -- this also covers the case where ta only
                    // exists as a boundary construction line (see the Tangent
                    // branch below for the analogous CircularArc-ending case).
                    const SolvedSCS& scs = scsData[i - 2].scs;
                    sentinel.easting  = scs.stPoint.x();
                    sentinel.northing = scs.stPoint.y();
                    sentinel.azimuth  = scs.azST;
                }
                break;
            } else if (elems[i].type == EditableElementType::SpiralOut
                       && compoundGroupStartByLastIdx[i] >= 0) {
                // ── 複合弧鏈（N>=2 弧，見 addCompoundChain()）的出螺旋收尾 ──
                // 原本這裡完全沒有對應分支：上面的 SCS 分支要求
                // scsData[i-2].valid，但複合弧鏈是走 compoundData，不會命中；
                // 於是掃到這裡的 elems[i] 會被當成「不符合任何已知類型」，
                // 迴圈繼續往前找，最終落到最前面的邊界建構線 Tangent 被
                // continue 跳過，整個迴圈掃完都沒有 break，sentinel 的座標
                // 就維持預設建構值 (0,0)，資料表尾端因此多出一列座標全 0、
                // 內容無意義的殘影 "TT"（實測：TC(0)-CS-SC-CT 這類含複合
                // 弧鏈的 ALD，匯入後尾端會多出這一列）。
                //
                // 修法比照上面 SCS 分支：往前找出這個出螺旋所屬複合弧鏈
                // 群組的起點（SpiralIn），用該群組自己的 tangentIdxAfter
                // 判斷退出點是否為真正切線。
                const int groupStart = compoundGroupStartByLastIdx[i];
                const int ta = elems[groupStart].tangentIdxAfter;
                if (ta >= 0 && ta < n
                    && elems[ta].type == EditableElementType::Tangent
                    && !isBoundaryConstruction[ta]) {
                    sentinel.easting  = tanEnd[ta].x();
                    sentinel.northing = tanEnd[ta].y();
                    sentinel.azimuth  = azimuthOf(tanStart[ta], tanEnd[ta]);
                } else {
                    // 邊界建構線（或找不到退出切線）：複合弧鏈區塊自己已經
                    // inline 發出正確的收尾關鍵點（"ST"，或省略出螺旋時本次
                    // 一併修正的 "CT"），座標就是這條鏈的最後一個節點。這裡
                    // 的 sentinel 錨在同一個座標、tsc[1] 同樣是 'T'，會被
                    // AlignmentDataTableDialog 既有的「相鄰兩列同為切線端點
                    // 且座標重合則合併」判斷自然去掉，不會留下多餘列。
                    const SolvedCompoundChain& chain = compoundData[groupStart].chain;
                    sentinel.easting  = chain.nodes.last().pt.x();
                    sentinel.northing = chain.nodes.last().pt.y();
                    sentinel.azimuth  = chain.nodes.last().az;
                }
                break;
            } else if (elems[i].type == EditableElementType::Tangent) {
                if (isBoundaryConstruction[i]) {
                    // 邊界建構線（isConstructionLine）本身不是真實量測到的
                    // 直線段，只是 seedFromRawPoints() 為了錨定邊界緩和
                    // 曲線虛擬延伸出來的佔位線，前面主迴圈已經不把它列進
                    // 資料表（見上方 isBoundaryConstruction 判斷）。sentinel
                    // 收尾列也必須比照跳過，繼續往前尋找真正的收尾元素
                    // （通常是它緊鄰的圓弧或緩和曲線），而不是誤把這條建構
                    // 線自己的（虛擬延伸）端點／方位角當成線形真正的終點。
                    // 例如線形實際上以圓弧收尾時，跳過建構線後會落到下面
                    // 的 CircularArc 分支，正確給出 tsc="CT"。
                    continue;
                }
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
                // Tangent azimuth at PT, derived from PC/PT/azPC/arcLen only
                // -- deliberately NOT using elems[i].arcCenter. arcCenter is
                // only populated for *Fixed* arcs (set once by
                // addFixedCurve()); a *Floating* arc's centre is computed
                // on the fly by Pass 2 (solveFloatingCurve()) and is never
                // written back into elems[i].arcCenter (elems is a const
                // reference here), so for a Floating arc that field is just
                // whatever default the EditableElement happened to have --
                // using it as "the centre" silently produced a wrong
                // azimuth whenever the alignment's last real element turned
                // out to be a Floating arc (e.g. G104).
                //
                // Instead, get the turn sign purely from the chord
                // PC→PT relative to the entry tangent azPC (right-perp
                // side test -- the same kind of test solveLC()/solveCA()
                // already use elsewhere), then apply the known unsigned
                // delta = arcLen / R. This only needs fields that Pass 1
                // (Fixed) and Pass 2 (Floating) both reliably populate.
                {
                    const double R = std::abs(elems[i].radius);
                    const double delta = (R > 1e-9) ? (arcData[i].arcLen / R) : 0.0;
                    const QPointF chord = arcData[i].pt - arcData[i].pc;
                    const double  rpx = std::cos(arcData[i].azPC);
                    const double  rpy = -std::sin(arcData[i].azPC);
                    const double  sideVal = chord.x() * rpx + chord.y() * rpy;
                    const double  sign    = (sideVal >= 0.0) ? 1.0 : -1.0;
                    sentinel.azimuth = arcData[i].azPC + sign * delta;
                }
                // 線形最後一段為圓弧（Circular Arc）時，終點是「弧→切線」
                // 過渡點，比照 2615 行同樣的判斷邏輯，tsc 應為 "CT"，
                // 而非預設的 "TT"（TT 僅適用於最後一段為切線的情況）。
                sentinel.tsc = QStringLiteral("CT");
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
