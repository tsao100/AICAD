/**
 * @file AssemblySolver.cpp
 */
#include "AssemblySolver.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace aicad {
namespace cad {
namespace asmsolve {

namespace {

constexpr double kPi = 3.14159265358979323846;

using VecX = Eigen::VectorXd;
using MatX = Eigen::MatrixXd;

inline void push3(std::vector<double>& out, const Vec3& v)
{
    out.push_back(v.x()); out.push_back(v.y()); out.push_back(v.z());
}

inline Vec3 perpPart(const Vec3& v, const Vec3& d)      // v 在垂直於單位向量 d 的分量
{
    return v - v.dot(d) * d;
}

/// 取向殘差：同向 u−v、反向 u+v、不限 u×v（皆為 3 個分量）
void orientRes(const Vec3& u, const Vec3& v, Orient o, std::vector<double>& out)
{
    switch (o) {
    case Orient::Aligned: push3(out, u - v); break;
    case Orient::Opposed: push3(out, u + v); break;
    default:              push3(out, u.cross(v)); break;
    }
}

/// 夾角殘差（兩個單位向量夾角 = th，th 在 [0, π]）。th 接近 0 或 π 時改用向量式，避免殘差在解附近平坦（二次）。
void angleRes(const Vec3& u, const Vec3& w, double th, std::vector<double>& out)
{
    const double eps = 1e-9;
    th = std::max(0.0, std::min(kPi, th));
    if (th < eps)            push3(out, u - w);
    else if (th > kPi - eps) push3(out, u + w);
    else {
        const double phi = std::atan2(u.cross(w).norm(), u.dot(w));
        out.push_back(phi - th);
    }
}

/// 把參照依 RefKind 排序（小者在前）；同種類保持原順序
void canon(const Mate& m, const Ref*& a, const Ref*& b)
{
    a = &m.a; b = &m.b;
    if (static_cast<int>(a->kind) > static_cast<int>(b->kind)) std::swap(a, b);
}

WorldGeom wg(const std::vector<Body>& bodies, const Ref& r)
{
    if (r.body < 0 || r.body >= static_cast<int>(bodies.size())) return { r.p, r.d };
    const Body& b = bodies[static_cast<size_t>(r.body)];
    return { b.R * (b.scale * r.p) + b.t, b.R * r.d };
}

/// 一個配合的殘差；長度項乘上 ls（= 1 / lengthScale，供無因次化；公開介面傳 1）
void resid(const std::vector<Body>& bodies, const Mate& m, double ls, std::vector<double>& out)
{
    const Ref *ra = nullptr, *rb = nullptr;
    canon(m, ra, rb);
    const WorldGeom a = wg(bodies, *ra);
    const WorldGeom b = wg(bodies, *rb);
    const RefKind ka = ra->kind, kb = rb->kind;
    const double v = m.value;

    auto isPA = [&] { return ka == RefKind::Point && kb == RefKind::Axis; };
    auto isPL = [&] { return ka == RefKind::Point && kb == RefKind::Plane; };
    auto isPP = [&] { return ka == RefKind::Point && kb == RefKind::Point; };
    auto isAA = [&] { return ka == RefKind::Axis  && kb == RefKind::Axis; };
    auto isAL = [&] { return ka == RefKind::Axis  && kb == RefKind::Plane; };
    auto isLL = [&] { return ka == RefKind::Plane && kb == RefKind::Plane; };

    switch (m.type) {
    case MateType::Coincident:
        if (isPP())      push3(out, (b.p - a.p) * ls);
        else if (isPA()) push3(out, perpPart(a.p - b.p, b.d) * ls);
        else if (isPL()) out.push_back((a.p - b.p).dot(b.d) * ls);
        break;

    case MateType::Collinear:
        if (isAA()) {
            orientRes(a.d, b.d, m.orient, out);
            push3(out, perpPart(b.p - a.p, a.d) * ls);
        }
        break;

    case MateType::Coplanar:
        if (isLL()) {
            orientRes(a.d, b.d, m.orient, out);
            out.push_back((b.p - a.p).dot(a.d) * ls);
        } else if (isAL()) {
            out.push_back(a.d.dot(b.d));
            out.push_back((a.p - b.p).dot(b.d) * ls);
        }
        break;

    case MateType::Parallel:
        if (isAA() || isLL()) orientRes(a.d, b.d, m.orient, out);
        else if (isAL())      out.push_back(a.d.dot(b.d));
        break;

    case MateType::Perpendicular:
        if (isAA() || isLL()) out.push_back(a.d.dot(b.d));
        else if (isAL())      push3(out, a.d.cross(b.d));
        break;

    case MateType::Distance: {
        // 長度為 0 時改用向量式（避免 |x| 在 0 的尖點）
        auto lenRes = [&](const Vec3& vec) {
            if (std::abs(v) < 1e-12) push3(out, vec * ls);
            else                     out.push_back((vec.norm() - v) * ls);
        };
        if (isPP())      lenRes(b.p - a.p);
        else if (isPA()) lenRes(perpPart(a.p - b.p, b.d));
        else if (isPL()) out.push_back(((a.p - b.p).dot(b.d) - v) * ls);          // 點到平面的有號距離（沿平面法向量）
        else if (isAA()) { orientRes(a.d, b.d, m.orient, out); lenRes(perpPart(b.p - a.p, a.d)); }
        else if (isAL()) { out.push_back(a.d.dot(b.d)); out.push_back(((a.p - b.p).dot(b.d) - v) * ls); }
        else if (isLL()) { orientRes(a.d, b.d, m.orient, out); out.push_back(((b.p - a.p).dot(a.d) - v) * ls); }
        break;
    }

    case MateType::Angle:
        if (isAA() || isLL()) {
            const Vec3 w = (m.orient == Orient::Opposed) ? Vec3(-b.d) : b.d;
            angleRes(a.d, w, v, out);
        } else if (isAL()) {
            const Vec3 n = (m.orient == Orient::Opposed) ? Vec3(-b.d) : b.d;
            const double th = std::max(-kPi / 2, std::min(kPi / 2, v));
            const double st = std::sin(th);
            if (std::abs(st) > 1.0 - 1e-9) push3(out, a.d - (st > 0 ? n : Vec3(-n)));
            else                           out.push_back(a.d.dot(n) - st);
        }
        break;
    }
}

// ── 變數配置：只有「未固定」零件有 6 個變數 [平移/lengthScale(3), 轉動向量(3)] ────────────────
struct Layout {
    std::vector<int> varOf;     ///< 零件 → 第一個變數的索引（固定者 -1）
    std::vector<int> freeIdx;   ///< 未固定零件的索引
    int nVar = 0;
};

Layout makeLayout(const Problem& pr)
{
    Layout L;
    L.varOf.assign(pr.bodies.size(), -1);
    for (size_t i = 0; i < pr.bodies.size(); ++i) {
        if (pr.bodies[i].fixed) continue;
        L.varOf[i] = L.nVar;
        L.nVar += 6;
        L.freeIdx.push_back(static_cast<int>(i));
    }
    return L;
}

void evalAll(const std::vector<Body>& bodies, const std::vector<Mate>& mates, double ls,
             VecX& r, std::vector<int>* offsets = nullptr)
{
    std::vector<double> tmp;
    if (offsets) offsets->assign(mates.size() + 1, 0);
    for (size_t k = 0; k < mates.size(); ++k) {
        if (offsets) (*offsets)[k] = static_cast<int>(tmp.size());
        if (mates[k].enabled) resid(bodies, mates[k], ls, tmp);
    }
    if (offsets) (*offsets)[mates.size()] = static_cast<int>(tmp.size());
    r = Eigen::Map<VecX>(tmp.data(), static_cast<Eigen::Index>(tmp.size()));
}

/// 在 base 的基礎上套用增量 dx（零件繞自己的幾何中心轉動 + 平移）
void applyStep(const std::vector<Body>& base, const Layout& L, double lengthScale,
               const VecX& dx, std::vector<Body>& out)
{
    out = base;
    for (int i : L.freeIdx) {
        const int o = L.varOf[static_cast<size_t>(i)];
        const Vec3 dt = dx.segment<3>(o) * lengthScale;
        const Vec3 w  = dx.segment<3>(o + 3);
        const Mat3 Rw = expSO3(w);
        const Body& b = base[static_cast<size_t>(i)];
        const Vec3 c  = b.R * (b.scale * b.center) + b.t;      // 世界座標下的轉動軸心
        Body& nb = out[static_cast<size_t>(i)];
        nb.R = Rw * b.R;
        nb.t = Rw * (b.t - c) + c + dt;
    }
}

/// 數值 Jacobian（前向差分）
MatX numJacobian(const std::vector<Body>& bodies, const std::vector<Mate>& mates, const Layout& L,
                 double lengthScale, double ls, const VecX& r0)
{
    const double h = 1e-6;
    MatX J(r0.size(), L.nVar);
    std::vector<Body> pert;
    VecX dx = VecX::Zero(L.nVar);
    VecX rj;
    for (int j = 0; j < L.nVar; ++j) {
        dx[j] = h;
        applyStep(bodies, L, lengthScale, dx, pert);
        evalAll(pert, mates, ls, rj);
        J.col(j) = (rj - r0) / h;
        dx[j] = 0.0;
    }
    return J;
}

/// Levenberg–Marquardt。回傳最終的無窮範數殘差
double runLM(Problem& pr, const Layout& L, const Options& opt, double ls, int& iters)
{
    VecX r;
    evalAll(pr.bodies, pr.mates, ls, r);
    double cost = 0.5 * r.squaredNorm();
    double lambda = 1e-3;
    auto rinf = [](const VecX& v) { return v.size() ? v.cwiseAbs().maxCoeff() : 0.0; };

    for (int it = 0; it < opt.maxIter; ++it) {
        iters += 1;
        if (rinf(r) < opt.tol || L.nVar == 0) break;

        const MatX J = numJacobian(pr.bodies, pr.mates, L, pr.lengthScale, ls, r);
        const VecX g = J.transpose() * r;
        const MatX H = J.transpose() * J;

        bool improved = false;
        double stepNorm = 0.0;
        for (int tries = 0; tries < 30; ++tries) {
            MatX A = H;
            A.diagonal().array() += lambda;
            const VecX dx = -A.ldlt().solve(g);
            if (!dx.allFinite()) { lambda *= 10.0; continue; }

            std::vector<Body> trial;
            applyStep(pr.bodies, L, pr.lengthScale, dx, trial);
            VecX rn;
            evalAll(trial, pr.mates, ls, rn);
            const double nc = 0.5 * rn.squaredNorm();
            if (nc < cost) {
                pr.bodies = trial;
                r = rn;
                cost = nc;
                lambda = std::max(lambda * 0.3, 1e-14);
                stepNorm = dx.norm();
                improved = true;
                break;
            }
            lambda *= 5.0;
        }
        if (!improved || stepNorm < 1e-16) break;
    }
    return rinf(r);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// 公開函式
// ─────────────────────────────────────────────────────────────────────────────

Mat3 expSO3(const Vec3& w)
{
    const double th = w.norm();
    if (th < 1e-12) {
        Mat3 K;
        K <<    0, -w.z(),  w.y(),
             w.z(),     0, -w.x(),
            -w.y(),  w.x(),     0;
        return Mat3::Identity() + K;              // 一階近似（th 極小）
    }
    const Vec3 k = w / th;
    Mat3 K;
    K <<    0, -k.z(),  k.y(),
         k.z(),     0, -k.x(),
        -k.y(),  k.x(),     0;
    return Mat3::Identity() + std::sin(th) * K + (1.0 - std::cos(th)) * K * K;
}

bool isValidPair(MateType type, RefKind a, RefKind b)
{
    int ka = static_cast<int>(a), kb = static_cast<int>(b);
    if (ka > kb) std::swap(ka, kb);
    auto is = [&](RefKind x, RefKind y) { return ka == static_cast<int>(x) && kb == static_cast<int>(y); };
    const RefKind P = RefKind::Point, A = RefKind::Axis, L = RefKind::Plane;
    switch (type) {
    case MateType::Coincident:     return is(P, P) || is(P, A) || is(P, L);
    case MateType::Collinear:      return is(A, A);
    case MateType::Coplanar:       return is(L, L) || is(A, L);
    case MateType::Parallel:
    case MateType::Perpendicular:
    case MateType::Angle:          return is(A, A) || is(L, L) || is(A, L);
    case MateType::Distance:       return true;      // 六種組合（點-點、點-軸、點-面、軸-軸、軸-面、面-面）
    }
    return false;
}

bool mateUsesValue(MateType type, RefKind a, RefKind b)
{
    return isValidPair(type, a, b) && (type == MateType::Distance || type == MateType::Angle);
}

bool mateUsesOrient(MateType type, RefKind a, RefKind b)
{
    if (!isValidPair(type, a, b)) return false;
    int ka = static_cast<int>(a), kb = static_cast<int>(b);
    if (ka > kb) std::swap(ka, kb);
    const bool AA = ka == static_cast<int>(RefKind::Axis)  && kb == ka;
    const bool LL = ka == static_cast<int>(RefKind::Plane) && kb == ka;
    const bool AL = ka == static_cast<int>(RefKind::Axis)  && kb == static_cast<int>(RefKind::Plane);
    switch (type) {
    case MateType::Collinear: return true;
    case MateType::Coplanar:  return LL;
    case MateType::Parallel:  return AA || LL;
    case MateType::Distance:  return AA || LL;
    case MateType::Angle:     return AA || LL || AL;      // Opposed = 以反向量計算夾角
    default:                  return false;
    }
}

WorldGeom worldGeom(const Problem& pr, const Ref& r) { return wg(pr.bodies, r); }

bool measureValue(const Problem& pr, MateType type, const Ref& ra0, const Ref& rb0, Orient orient, double& out)
{
    if (!isValidPair(type, ra0.kind, rb0.kind)) return false;
    const Ref *ra = &ra0, *rb = &rb0;
    if (static_cast<int>(ra->kind) > static_cast<int>(rb->kind)) std::swap(ra, rb);
    const WorldGeom a = wg(pr.bodies, *ra), b = wg(pr.bodies, *rb);
    const RefKind ka = ra->kind, kb = rb->kind;

    if (type == MateType::Distance) {
        if (ka == RefKind::Point && kb == RefKind::Point)  out = (b.p - a.p).norm();
        else if (ka == RefKind::Point && kb == RefKind::Axis)  out = perpPart(a.p - b.p, b.d).norm();
        else if (ka == RefKind::Point && kb == RefKind::Plane) out = (a.p - b.p).dot(b.d);
        else if (ka == RefKind::Axis  && kb == RefKind::Axis)  out = perpPart(b.p - a.p, a.d).norm();
        else if (ka == RefKind::Axis  && kb == RefKind::Plane) out = (a.p - b.p).dot(b.d);
        else                                                   out = (b.p - a.p).dot(a.d);
        return true;
    }
    if (type == MateType::Angle) {
        if (ka == RefKind::Axis && kb == RefKind::Plane) {
            const Vec3 n = (orient == Orient::Opposed) ? Vec3(-b.d) : b.d;
            out = std::asin(std::max(-1.0, std::min(1.0, a.d.dot(n))));
        } else {
            const Vec3 w = (orient == Orient::Opposed) ? Vec3(-b.d) : b.d;
            out = std::atan2(a.d.cross(w).norm(), a.d.dot(w));
        }
        return true;
    }
    return false;
}

Orient closestOrient(const Problem& pr, MateType type, const Ref& ra0, const Ref& rb0)
{
    if (!mateUsesOrient(type, ra0.kind, rb0.kind)) return Orient::Any;
    if (type == MateType::Angle) return Orient::Aligned;
    const WorldGeom a = wg(pr.bodies, ra0), b = wg(pr.bodies, rb0);
    return a.d.dot(b.d) >= 0.0 ? Orient::Aligned : Orient::Opposed;
}

void mateResiduals(const Problem& pr, const Mate& m, std::vector<double>& out)
{
    out.clear();
    resid(pr.bodies, m, 1.0, out);
}

Result solve(Problem& pr, const Options& opt)
{
    Result res;
    const size_t nb = pr.bodies.size(), nm = pr.mates.size();
    res.mateResidual.assign(nm, 0.0);
    res.mateOk.assign(nm, true);
    res.bodyDof.assign(nb, 0);

    if (pr.lengthScale <= 0.0) pr.lengthScale = 1.0;
    const double ls = 1.0 / pr.lengthScale;
    const Layout L = makeLayout(pr);

    // 單位化所有參照方向
    for (Mate& m : pr.mates) {
        for (Ref* r : { &m.a, &m.b }) {
            const double n = r->d.norm();
            r->d = n > 1e-300 ? Vec3(r->d / n) : Vec3(Vec3::UnitZ());
        }
    }

    auto mateStats = [&](const std::vector<Body>& bodies, double& maxRes, bool& allOk,
                         std::vector<double>* perMate) {
        maxRes = 0.0; allOk = true;
        std::vector<double> tmp;
        for (size_t k = 0; k < nm; ++k) {
            double mr = 0.0;
            if (pr.mates[k].enabled) {
                tmp.clear();
                resid(bodies, pr.mates[k], ls, tmp);
                for (double x : tmp) mr = std::max(mr, std::abs(x));
            }
            if (perMate) (*perMate)[k] = mr;
            maxRes = std::max(maxRes, mr);
            if (mr >= opt.okTol) allOk = false;
        }
    };

    const std::vector<Body> original = pr.bodies;
    int iters = 0;
    runLM(pr, L, opt, ls, iters);
    double maxRes = 0.0; bool allOk = false;
    mateStats(pr.bodies, maxRes, allOk, nullptr);

    // 主要求解失敗：嘗試不同的初始姿態（避開反向／鏡像的局部極小）
    if (!allOk && opt.multiStart && L.nVar > 0) {
        std::vector<Body> best = pr.bodies;
        double bestRes = maxRes;
        const Vec3 axes[3] = { Vec3::UnitX(), Vec3::UnitY(), Vec3::UnitZ() };
        const double angs[3] = { kPi, kPi / 2, -kPi / 2 };
        bool found = false;
        for (int ai = 0; ai < 3 && !found; ++ai) {
            for (int xi = 0; xi < 3 && !found; ++xi) {
                Problem p2 = pr;
                p2.bodies = original;
                VecX dx = VecX::Zero(L.nVar);
                for (int i : L.freeIdx) dx.segment<3>(L.varOf[static_cast<size_t>(i)] + 3) = axes[xi] * angs[ai];
                std::vector<Body> start;
                applyStep(original, L, pr.lengthScale, dx, start);
                p2.bodies = start;
                int it2 = 0;
                runLM(p2, L, opt, ls, it2);
                iters += it2;
                double mr = 0.0; bool ok = false;
                mateStats(p2.bodies, mr, ok, nullptr);
                if (ok) { pr.bodies = p2.bodies; maxRes = mr; allOk = true; found = true; res.usedRestart = true; }
                else if (mr < bestRes) { bestRes = mr; best = p2.bodies; }
            }
        }
        if (!found) { pr.bodies = best; maxRes = bestRes; }
    }

    res.iterations  = iters;
    res.maxResidual = maxRes;
    mateStats(pr.bodies, maxRes, allOk, &res.mateResidual);
    res.converged = allOk;
    for (size_t k = 0; k < nm; ++k) res.mateOk[k] = !pr.mates[k].enabled || res.mateResidual[k] < opt.okTol;

    // ── 剩餘自由度（Jacobian 的零空間維度）──────────────────────────────────────
    if (opt.computeDof) {
        if (L.nVar == 0) {
            res.dof = 0;
        } else {
            VecX r0;
            evalAll(pr.bodies, pr.mates, ls, r0);
            if (r0.size() == 0) {
                res.dof = L.nVar;
                for (int i : L.freeIdx) res.bodyDof[static_cast<size_t>(i)] = 6;
            } else {
                const MatX J = numJacobian(pr.bodies, pr.mates, L, pr.lengthScale, ls, r0);
                Eigen::JacobiSVD<MatX> svd(J, Eigen::ComputeFullV);
                const VecX sv = svd.singularValues();
                const double smax = sv.size() ? sv[0] : 0.0;
                const double thr = 1e-4 * std::max(1.0, smax);
                int rank = 0;
                for (Eigen::Index i = 0; i < sv.size(); ++i) if (sv[i] > thr) ++rank;
                res.dof = L.nVar - rank;
                const MatX V = svd.matrixV();
                for (int i : L.freeIdx) {
                    const int o = L.varOf[static_cast<size_t>(i)];
                    double s = 0.0;
                    for (int k = rank; k < L.nVar; ++k)
                        for (int j = 0; j < 6; ++j) s += V(o + j, k) * V(o + j, k);
                    res.bodyDof[static_cast<size_t>(i)] = std::max(0, std::min(6, static_cast<int>(std::lround(s))));
                }
            }
        }
    }

    if (res.converged) res.message = "ok";
    else               res.message = "unsatisfied";
    return res;
}

} // namespace asmsolve
} // namespace cad
} // namespace aicad
