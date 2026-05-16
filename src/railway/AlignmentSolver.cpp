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

#include "AlignmentSolver.h"

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

    // ════════════════════════════════════════════════════════════════════════
    //  Pass 1 – Fixed CircularArc: foot-of-perpendicular T1 / T2
    // ════════════════════════════════════════════════════════════════════════
    for (int i = 0; i < n; ++i) {
        const auto& e = elems[i];
        if (e.type != EditableElementType::CircularArc) continue;
        if (e.mode != ConstraintMode::Fixed)            continue;

        const QPointF center = e.startPI;  // Fixed arc: startPI = circle centre
        const double  R      = std::abs(e.radius);
        if (R < 1e-9) {
            qWarning() << "[AlignmentSolver] Pass1 Fixed arc radius ≈ 0 at idx" << i;
            continue;
        }

        // Find nearest preceding and following Tangent elements
        int prevT = -1;
        for (int j = i - 1; j >= 0; --j)
            if (elems[j].type == EditableElementType::Tangent) { prevT = j; break; }

        int nextT = -1;
        for (int j = i + 1; j < n; ++j)
            if (elems[j].type == EditableElementType::Tangent) { nextT = j; break; }

        ArcData arc;

        // T1: foot of perpendicular from centre onto incoming tangent
        if (prevT >= 0) {
            arc.pc             = footOfPerp(tanStart[prevT], tanEnd[prevT], center);
            tanEnd[prevT]      = arc.pc;
            arc.azPC           = azimuthOf(tanStart[prevT], arc.pc);
        } else {
            arc.pc   = center + QPointF(0.0, R);  // North fallback
            arc.azPC = 0.0;
        }

        // T2: foot of perpendicular from centre onto outgoing tangent
        if (nextT >= 0) {
            arc.pt             = footOfPerp(tanStart[nextT], tanEnd[nextT], center);
            tanStart[nextT]    = arc.pt;
        } else {
            arc.pt = center + QPointF(R, 0.0);    // East fallback
        }

        // Arc length via included angle between radii to T1 and T2
        const QPointF v1     = arc.pc - center;
        const QPointF v2     = arc.pt - center;
        const double  cross  = v1.x() * v2.y() - v1.y() * v2.x();
        const double  dot    = v1.x() * v2.x() + v1.y() * v2.y();
        const double  delta  = std::abs(std::atan2(cross, dot));
        arc.arcLen = R * delta;

        if (arc.arcLen < 1e-9) {
            qWarning() << "[AlignmentSolver] Pass1 Fixed arc length ≈ 0 at idx" << i;
            continue;
        }

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
    //  Pass 3 – Build AlignmentPoint sequence and load HorizontalAlignment
    // ════════════════════════════════════════════════════════════════════════
    QVector<AlignmentPoint> pts;
    pts.reserve(n + 1);
    double chainage = 0.0;

    for (int i = 0; i < n; ++i) {
        const auto& e = elems[i];
        AlignmentPoint pt;

        if (e.type == EditableElementType::Tangent) {
            const double len = QLineF(tanStart[i], tanEnd[i]).length();
            if (len < 1e-9) continue;   // degenerate segment — skip

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
        // SpiralIn / SpiralOut → Step 4
    }

    // ── Sentinel end-point (length = 0) ──────────────────────────────────────
    if (!pts.isEmpty()) {
        AlignmentPoint sentinel;
        sentinel.tsc      = QStringLiteral("TT");
        sentinel.length   = 0.0;
        sentinel.chainage = chainage;

        // Derive easting/northing/azimuth from the last actual element
        const int last = n - 1;

        // Walk backward to find the last element with valid geometry
        for (int i = last; i >= 0; --i) {
            if (elems[i].type == EditableElementType::Tangent) {
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
                // Approximation: use arc start azimuth as end azimuth
                // (load() will assign the signed radius, which governs
                // the exact outgoing tangent direction internally)
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
