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
//  solveSCS  (public static — Appendix B formula)
// ============================================================================

SolvedSCS AlignmentSolver::solveSCS(
    double radius, double spiralLength,
    const QPointF& tanStartPrev, const QPointF& tanEndPrev,
    const QPointF& tanStartNext, const QPointF& tanEndNext)
{
    SolvedSCS result;

    const double R  = std::abs(radius);
    const double Ls = std::abs(spiralLength);

    if (R < 1e-9) {
        qWarning() << "[AlignmentSolver] solveSCS: radius ≈ 0";
        return result;
    }
    if (Ls < 1e-9) {
        qWarning() << "[AlignmentSolver] solveSCS: spiral length ≈ 0";
        return result;
    }

    // ── Step 1: tangent azimuths ─────────────────────────────────────────────
    const double az1 = azimuthOf(tanStartPrev, tanEndPrev);  // incoming α₁
    const double az2 = azimuthOf(tanStartNext, tanEndNext);  // outgoing α₂

    // ── Step 2: intersection PI ──────────────────────────────────────────────
    const QPointF pi = lineIntersect(tanStartPrev, az1, tanStartNext, az2);

    // ── Step 3: total turning angle Δ ────────────────────────────────────────
    const double delta    = normaliseAngle(az2 - az1);
    const double absDelta = std::abs(delta);

    if (absDelta < 1e-9) {
        qWarning() << "[AlignmentSolver] solveSCS: Δ ≈ 0 (parallel tangents)";
        return result;
    }

    // ── Step 4: Appendix B spiral parameters ─────────────────────────────────
    //   Θs = Ls / (2R)
    //   Xm = Ls × (1 − Θs²/10)
    //   Ym = Ls × Θs / 3
    //   Ts = (R + Ym) × tan(Δ/2) + Xm

    const double thetaS = Ls / (2.0 * R);
    const double Xm     = Ls * (1.0 - (thetaS * thetaS) / 10.0);
    const double Ym     = Ls * thetaS / 3.0;
    const double Ts     = (R + Ym) * std::tan(absDelta * 0.5) + Xm;

    // ── Step 5: TS and ST (SCS start/end on tangents) ────────────────────────
    //   TS = PI − Ts × unit(α₁)
    //   ST = PI − Ts × unit(α₂) [going backward along exit tangent]

    const QPointF unit1(std::sin(az1), std::cos(az1));
    const QPointF unit2(std::sin(az2), std::cos(az2));

    const QPointF tsPoint = pi - Ts * unit1;   // entry spiral start
    const QPointF stPoint = pi + Ts * unit2;   // exit  spiral end

    // ── Step 6: SC and CS (arc tangent cut-points) ───────────────────────────
    //   The entry spiral deflects by Θs; the arc chord (from TS to SC) is
    //   in local spiral coordinates. We reconstruct from the shifted circle
    //   centre. In practice, given the local frame:
    //     SC = TS + Xm·unit(α₁) + Ym·unit_perp(α₁)
    //   where unit_perp is 90° right for a right-turn, left for a left-turn.

    const int sign = (delta >= 0.0) ? 1 : -1;   // +1 = right turn

    // unit perpendicular (90° right of unit1)
    const QPointF perp1( std::cos(az1), -std::sin(az1));   // (cosA, -sinA) in E/N
    const QPointF perp2( std::cos(az2), -std::sin(az2));

    const QPointF scPoint = tsPoint + Xm * unit1 + sign * Ym * perp1;

    // CS: mirror of SC construction from ST backward along exit tangent
    // Exit spiral: TS_exit = ST (reversed), so:
    //   CS = ST − Xm·unit(α₂) − sign·Ym·perp2   (walking from ST → CS)
    const QPointF csPoint = stPoint - Xm * unit2 - sign * Ym * perp2;

    // ── Step 7: arc length (SC → CS) ─────────────────────────────────────────
    //   Arc deflection = Δ − 2·Θs
    const double arcDelta = absDelta - 2.0 * thetaS;
    if (arcDelta < -1e-9) {
        qWarning() << "[AlignmentSolver] solveSCS: arc delta < 0 "
                      "(Δ < 2·Θs — spirals overlap). Δ="
                   << qRadiansToDegrees(absDelta)
                   << "2Θs=" << qRadiansToDegrees(2.0 * thetaS);
        return result;
    }
    const double arcLen = R * std::max(0.0, arcDelta);

    // ── Step 8: azimuths at key points ───────────────────────────────────────
    //   At SC: tangent has rotated by Θs from α₁
    //   At CS: tangent has rotated by (Δ − Θs) from α₁  i.e. (Θs before α₂)
    const double azSC = az1 + sign * thetaS;
    const double azCS = az2 - sign * thetaS;

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
    result.Ls       = Ls;
    result.R        = R;
    result.delta    = delta;
    result.thetaS   = thetaS;

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
        const double Ls = elems[i].length;

        const SolvedSCS scs = solveSCS(
            R, Ls,
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

    // Track which indices are part of an SCS group (to avoid double-emitting)
    QVector<bool> handledBySCS(n, false);
    for (int i = 0; i < n; ++i) {
        if (scsData[i].valid) {
            handledBySCS[i]                    = true;  // SpiralIn
            handledBySCS[scsData[i].arcIdx]    = true;  // CircularArc
            handledBySCS[scsData[i].spiralOutIdx] = true; // SpiralOut
        }
    }

    for (int i = 0; i < n; ++i) {
        const auto& e = elems[i];
        AlignmentPoint pt;

        // ── SCS group: SpiralIn drives emission of all three sub-elements ──
        if (e.type == EditableElementType::SpiralIn && scsData[i].valid) {
            const SolvedSCS& scs = scsData[i].scs;
            const double R = scs.R;
            const double Ls = scs.Ls;

            // Emit TS point (entry spiral start): tsc = "TS"
            AlignmentPoint tspt;
            tspt.tsc       = QStringLiteral("TS");
            tspt.curveType = QStringLiteral("SPIRAL");
            tspt.easting   = scs.tsPoint.x();
            tspt.northing  = scs.tsPoint.y();
            tspt.azimuth   = scs.azTS;
            tspt.length    = Ls;
            tspt.radius    = R;
            tspt.chainage  = chainage;
            chainage      += Ls;
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
            cspt.curveType = QStringLiteral("SPIRAL");
            cspt.easting   = scs.csPoint.x();
            cspt.northing  = scs.csPoint.y();
            cspt.azimuth   = scs.azCS;
            cspt.length    = Ls;
            cspt.radius    = R;
            cspt.chainage  = chainage;
            chainage      += Ls;
            pts.append(cspt);

            // SpiralOut's endpoint (ST) will appear as the next TT sentinel
            // or as the next tangent's start — no extra point needed here.
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
                const SolvedSCS& scs = scsData[i - 2].scs;
                sentinel.easting  = scs.stPoint.x();
                sentinel.northing = scs.stPoint.y();
                sentinel.azimuth  = scs.azST;
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
                sentinel.azimuth  = arcData[i].azPC;
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