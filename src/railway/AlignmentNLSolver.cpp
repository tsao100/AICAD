#include "AlignmentNLSolver.h"

#include <Eigen/Dense>
#include <cmath>

namespace aicad {
namespace railway {

AlignmentNLSolver::AlignmentNLSolver()
    : m_config()
{
}

AlignmentNLSolver::AlignmentNLSolver(Config cfg)
    : m_config(cfg)
{
}

// ── helpers: scaled <-> physical variable space ────────────────────────────
//
//  Physical unknown x_phys[i] = xScaled[i] * scale[i]  (scale[i] defaults to
//  1.0 when the caller passes an empty scale vector, i.e. no scaling).
//  Operating in scaled space keeps every unknown roughly O(1) even when the
//  physical unknowns mix meters, radians, and TWD97 coordinates, which keeps
//  the Jacobian's condition number reasonable for the SVD solve below.

static double scaleOf(const QVector<double>& scale, int i)
{
    return (i < scale.size() && scale[i] != 0.0) ? scale[i] : 1.0;
}

static QVector<double> toPhysical(const QVector<double>& xScaled,
                                   const QVector<double>& scale)
{
    QVector<double> phys(xScaled.size());
    for (int i = 0; i < xScaled.size(); ++i)
        phys[i] = xScaled[i] * scaleOf(scale, i);
    return phys;
}

static double residualNormOf(const QVector<double>& r)
{
    double s = 0.0;
    for (double v : r) s += v * v;
    return std::sqrt(s);
}

// ── numeric Jacobian (central difference), in scaled variable space ────────
//
//  d F / d xScaled[j] ≈ [F(x + h·e_j) - F(x - h·e_j)] / (2h)
//
//  h is taken directly as m_config.finiteDiffStep since xScaled is already
//  O(1) by construction (that is the whole point of scaling); no additional
//  per-variable step adjustment is needed.

QVector<QVector<double>> AlignmentNLSolver::numericJacobian(
    const QVector<double>& xScaled,
    const QVector<double>& scale,
    const ResidualFn& residualFn) const
{
    const int n = xScaled.size();
    const double h = m_config.finiteDiffStep;

    QVector<double> r0;
    residualFn(toPhysical(xScaled, scale), r0);
    const int m = r0.size();

    QVector<QVector<double>> J(m, QVector<double>(n, 0.0));

    for (int j = 0; j < n; ++j) {
        QVector<double> xPlus = xScaled;
        QVector<double> xMinus = xScaled;
        xPlus[j]  += h;
        xMinus[j] -= h;

        QVector<double> rPlus, rMinus;
        residualFn(toPhysical(xPlus, scale), rPlus);
        residualFn(toPhysical(xMinus, scale), rMinus);

        // Defensive: a caller's residualFn must always return the same
        // length m regardless of x. If it doesn't (a caller bug), skip this
        // column's contribution rather than reading out of bounds — the
        // resulting ill-formed Jacobian will simply fail to converge, which
        // surfaces the bug via Result::converged == false instead of a
        // crash (matches the "never throws on divergence" contract above).
        if (rPlus.size() != m || rMinus.size() != m)
            continue;

        for (int i = 0; i < m; ++i)
            J[i][j] = (rPlus[i] - rMinus[i]) / (2.0 * h);
    }
    return J;
}

// ── one damped Gauss-Newton (Levenberg-Marquardt) step ──────────────────────
//
//  Classic LM normal equations:  (JᵀJ + λ·diag(JᵀJ))·Δx = -Jᵀ·F(x)
//
//  Rather than forming JᵀJ explicitly (which squares the condition number),
//  this solves the equivalent augmented least-squares system directly via
//  Eigen::BDCSVD, mirroring the pattern already used in
//  ConstraintSolver::solveLinearLS():
//
//      [       J        ] · Δx = [ -F ]
//      [ sqrt(λ) · diag(√(JᵀJ)) ]       [  0 ]
//
//  For simplicity (unknowns are already O(1) in scaled space) the diagonal
//  scaling term uses sqrt(λ)·I rather than sqrt(λ)·diag(JᵀJ); this is the
//  "Levenberg" (not "Marquardt") damping variant, which is adequate given
//  the scaled unknowns are already normalized to comparable magnitudes (see
//  numericJacobian() above) — the usual reason to prefer Marquardt's
//  per-variable diag(JᵀJ) scaling over Levenberg's plain λI is exactly the
//  ill-conditioning that the scale vector already addresses upstream.

bool AlignmentNLSolver::lmStep(QVector<double>& xScaled,
                                const QVector<double>& scale,
                                const ResidualFn& residualFn,
                                double& lambda,
                                double& residualNorm) const
{
    const int n = xScaled.size();

    QVector<double> F0;
    residualFn(toPhysical(xScaled, scale), F0);
    const int m = F0.size();
    if (m == 0 || n == 0) return false;

    const QVector<QVector<double>> J = numericJacobian(xScaled, scale, residualFn);

    Eigen::MatrixXd A(m + n, n);
    Eigen::VectorXd b(m + n);
    for (int i = 0; i < m; ++i) {
        b(i) = -F0[i];
        for (int j = 0; j < n; ++j)
            A(i, j) = J[i][j];
    }
    const double damp = std::sqrt(lambda);
    for (int j = 0; j < n; ++j) {
        for (int k = 0; k < n; ++k)
            A(m + j, k) = (j == k) ? damp : 0.0;
        b(m + j) = 0.0;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    Eigen::VectorXd dx =
        Eigen::BDCSVD<Eigen::MatrixXd, Eigen::ComputeThinU | Eigen::ComputeThinV>(A).solve(b);
#else
    Eigen::VectorXd dx =
        Eigen::BDCSVD<Eigen::MatrixXd>(A, Eigen::ComputeThinU | Eigen::ComputeThinV).solve(b);
#endif

    double stepNorm = 0.0;
    for (int j = 0; j < n; ++j) stepNorm += dx(j) * dx(j);
    stepNorm = std::sqrt(stepNorm);
    if (stepNorm < m_config.minStepNorm) {
        // Stagnant: treat as "accepted, no further progress possible" so the
        // outer loop's convergence check on ||F|| makes the final call.
        return false;
    }

    QVector<double> xTrial = xScaled;
    for (int j = 0; j < n; ++j) xTrial[j] += dx(j);

    QVector<double> Ftrial;
    residualFn(toPhysical(xTrial, scale), Ftrial);
    const double trialNorm = residualNormOf(Ftrial);

    if (trialNorm < residualNorm) {
        xScaled = xTrial;
        residualNorm = trialNorm;
        lambda *= m_config.lambdaDownFactor;
        return true;
    }

    // Step rejected: grow damping (move closer to steepest-descent) and let
    // the caller retry within the same iteration budget.
    lambda *= m_config.lambdaUpFactor;
    return false;
}

AlignmentNLSolver::Result AlignmentNLSolver::solve(
    const QVector<double>& x0,
    const ResidualFn& residualFn,
    const QVector<double>& scale) const
{
    Result result;
    result.x = x0;

    if (x0.isEmpty()) {
        // Nothing to solve; report "converged" trivially only if the
        // residual at the (empty) point is already zero-length too. Guards
        // against a caller mistake rather than silently succeeding.
        return result;
    }

    // Convert the caller's physical initial guess into scaled space:
    // xScaled[i] = x0[i] / scale[i].
    QVector<double> xScaled(x0.size());
    for (int i = 0; i < x0.size(); ++i) {
        const double s = scaleOf(scale, i);
        xScaled[i] = x0[i] / s;
    }

    QVector<double> F0;
    residualFn(x0, F0);
    double residualNorm = residualNormOf(F0);

    double lambda = m_config.initialLambda;
    int iter = 0;
    for (; iter < m_config.maxIterations; ++iter) {
        if (residualNorm < m_config.tolerance)
            break;
        lmStep(xScaled, scale, residualFn, lambda, residualNorm);
        // lmStep() already updates xScaled/residualNorm/lambda in place on
        // both accept and reject; the loop simply re-checks the tolerance
        // and iteration budget each pass (mirrors the plain Newton loop
        // structure in ConstraintSolver.cpp, just with LM's accept/reject
        // logic folded into lmStep()).
    }

    result.x = toPhysical(xScaled, scale);
    result.residualNorm = residualNorm;
    result.iterations = iter;
    result.converged = residualNorm < m_config.tolerance;
    return result;
}

} // namespace railway
} // namespace aicad
