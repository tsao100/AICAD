#pragma once

/**
 * @file AlignmentNLSolver.h
 * @brief Generic (domain-agnostic) Levenberg-Marquardt nonlinear least-squares
 *        engine, used to progressively replace the one-off bisection/secant
 *        solvers in AlignmentSolver.cpp (solveLC/solveCA/solveACA/
 *        solveSSJunction/solveReverseSpiral/solveCompoundChain) once a line
 *        type needs more than one simultaneous unknown or more than one
 *        coupled soft constraint.
 *
 * See AlignmentSolver_LM統一升級計畫.md for the full migration plan, trigger
 * conditions, and phased rollout. This header covers Phase 1 only: the engine
 * itself, with zero dependency on railway-domain types (EditableElement,
 * TransitionElement, etc.). Callers supply a plain residual function of a
 * flat QVector<double>.
 *
 * Design notes (see plan doc section 1.3/1.4 for rationale):
 *   - Numeric (central-difference) Jacobian only. Callers are never required
 *     to hand-derive analytic partial derivatives for a new line-type
 *     combination.
 *   - Classic Levenberg-Marquardt: Δx = -(JᵀJ + λ·diag(JᵀJ))⁻¹ · Jᵀ·F(x),
 *     solved via Eigen::BDCSVD pseudo-inverse of the augmented normal-equation
 *     system (same numerical building block already used in
 *     src/cad/sketch/ConstraintSolver.cpp).
 *   - Optional per-variable `scale` to counter poorly-conditioned Jacobians
 *     when unknowns span very different magnitudes (lengths in meters vs.
 *     azimuths in radians vs. TWD97 coordinates).
 *   - Never throws on divergence/non-convergence; returns Result::converged
 *     = false so callers can fall back to their existing bisection path
 *     (matches the "valid=false, no exception" convention used throughout
 *     AlignmentSolver.cpp).
 *
 * @author AICAD Team
 * @date   2026-07
 */

#include <QVector>
#include <functional>

namespace aicad {
namespace railway {

/**
 * @brief Standalone Levenberg-Marquardt nonlinear least-squares solver.
 *
 * Solves for x ∈ R^n minimizing ||F(x)||² where F: R^n → R^m (m >= n is the
 * common case here; m == n reduces to ordinary nonlinear root-finding, which
 * is the case for every railway solver migrated so far).
 *
 * This class has no knowledge of alignments, spirals, or arcs — it is a pure
 * numerical utility. Railway-specific residual assembly (e.g. "S1 must be
 * tangent to arc1", "L1 == L2") lives in the caller (AlignmentSolver.cpp),
 * not here. Keeping the engine domain-agnostic is what lets it be reused for
 * every future compound line type without modification.
 */
class AlignmentNLSolver {
public:
    /// Residual function: given the current unknown vector, fill `residual`.
    /// `residual` may be resized by the callee to any length m >= 1; m need
    /// not equal x.size() (least-squares case).
    using ResidualFn = std::function<void(const QVector<double>& x,
                                           QVector<double>& residual)>;

    struct Config {
        int    maxIterations    = 100;
        double tolerance        = 1e-9;    ///< convergence threshold on ||F(x)||.
                                            ///< This is an ABSOLUTE threshold on the raw
                                            ///< residual norm, not relative to problem
                                            ///< scale. Railway closure-gap residuals are
                                            ///< meters (O(1) or smaller), so the default
                                            ///< is appropriate there; a caller whose
                                            ///< residuals are naturally large (e.g. raw
                                            ///< squared-distance residuals in the 1e6+
                                            ///< range) must loosen this accordingly or
                                            ///< convergence will report false negatives
                                            ///< even after reaching the correct solution
                                            ///< (confirmed empirically in the Phase-1
                                            ///< sanity tests — see test_nlsolver.cpp).
        double initialLambda    = 1e-3;    ///< initial LM damping factor
        double lambdaUpFactor   = 10.0;    ///< growth factor when a step is rejected
        double lambdaDownFactor = 0.1;     ///< shrink factor when a step is accepted
        double finiteDiffStep   = 1e-6;    ///< central-difference step (in scaled units)
        double minStepNorm      = 1e-12;   ///< stop if ||Δx|| falls below this (stagnation)

        Config() = default;
    };

    struct Result {
        bool            converged    = false;
        QVector<double> x;              ///< solution (or best iterate if not converged)
        double          residualNorm = 0.0;
        int             iterations   = 0;
    };

    explicit AlignmentNLSolver();
    explicit AlignmentNLSolver(Config cfg);

    /**
     * @brief Run the Levenberg-Marquardt main loop.
     *
     * @param x0         Initial guess. Callers should seed this from a cheap
     *                   analytic estimate (e.g. a family-sampling scan; see
     *                   the plan doc's "initial guess" guidance) rather than
     *                   an arbitrary constant, to reduce divergence risk.
     * @param residualFn Residual function; see ResidualFn above.
     * @param scale      Optional per-variable scale factors (same length as
     *                   x0). Internally the solver iterates on x' = x/scale
     *                   and rescales before returning, to counter Jacobian
     *                   ill-conditioning when unknowns span very different
     *                   magnitudes (e.g. a length in meters mixed with an
     *                   azimuth in radians). Empty vector (default) disables
     *                   scaling (equivalent to all-ones).
     */
    Result solve(const QVector<double>& x0,
                 const ResidualFn& residualFn,
                 const QVector<double>& scale = QVector<double>()) const;

private:
    Config m_config;

    /// Central-difference numeric Jacobian, evaluated in *scaled* variable
    /// space (i.e. residualFn is invoked with x already rescaled back to
    /// physical units internally — see .cpp for the exact composition).
    QVector<QVector<double>> numericJacobian(
        const QVector<double>& xScaled,
        const QVector<double>& scale,
        const ResidualFn& residualFn) const;

    /// One damped Gauss-Newton (LM) step. Returns true if the step was
    /// accepted (residual decreased) and updates xScaled/lambda/residualNorm
    /// in place; returns false if the step was rejected (lambda already
    /// grown internally — caller should retry the same iteration budget).
    bool lmStep(QVector<double>& xScaled,
                const QVector<double>& scale,
                const ResidualFn& residualFn,
                double& lambda,
                double& residualNorm) const;
};

} // namespace railway
} // namespace aicad
