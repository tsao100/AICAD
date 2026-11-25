// AssemblySolver 獨立測試（不需 Qt／OCCT）
#include "AssemblySolver.h"
#include <cstdio>
#include <cmath>
#include <random>
#include <chrono>
using namespace aicad::cad::asmsolve;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg) do { if (cond) { ++g_pass; } else { ++g_fail; std::printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } } while (0)

static Ref mk(int body, RefKind k, Vec3 p, Vec3 d) { Ref r; r.body = body; r.kind = k; r.p = p; r.d = d.normalized(); return r; }
static Mat3 rot(const Vec3& axis, double a) { return expSO3(axis.normalized() * a); }

static Body freeBody(Mat3 R, Vec3 t) { Body b; b.R = R; b.t = t; return b; }

static Problem twoBoxes()
{
    Problem pr; pr.lengthScale = 100.0;
    Body A; A.fixed = true;                                    // A 固定在原點
    Body B = freeBody(rot(Vec3(1, 2, 3), 0.9), Vec3(40, -30, 25));   // B 任意姿態
    pr.bodies = { A, B };
    return pr;
}

// 全部啟用配合的最大殘差（有因次）
static double maxRes(const Problem& pr)
{
    double m = 0; std::vector<double> r;
    for (const Mate& mt : pr.mates) { mateResiduals(pr, mt, r); for (double x : r) m = std::max(m, std::abs(x)); }
    return m;
}

int main()
{
    // ── 1. 面對面共面（Opposed）─────────────────────────────────────────────────
    {
        std::printf("[1] coplanar (opposed)\n");
        Problem pr = twoBoxes();
        Mate m; m.type = MateType::Coplanar; m.orient = Orient::Opposed;
        m.a = mk(0, RefKind::Plane, Vec3(0, 0, 10), Vec3(0, 0, 1));      // A 頂面（外向 +Z）
        m.b = mk(1, RefKind::Plane, Vec3(0, 0, 0),  Vec3(0, 0, -1));     // B 底面（外向 -Z）
        pr.mates = { m };
        Result r = solve(pr);
        CHECK(r.converged, "converged");
        CHECK(maxRes(pr) < 1e-6, "residual small");
        const WorldGeom gb = worldGeom(pr, pr.mates[0].b);
        CHECK(std::abs(gb.p.z() - 10) < 1e-6, "B bottom plane at z=10");
        CHECK(gb.d.z() < -0.999999, "B normal points down");
        CHECK(r.dof == 3, "3 dof remain (2 translation + 1 rotation)");
        std::printf("   dof=%d iters=%d restart=%d\n", r.dof, r.iterations, (int)r.usedRestart);
    }
    // ── 2. + 同軸（圓柱軸線）────────────────────────────────────────────────────
    {
        std::printf("[2] + collinear axes\n");
        Problem pr = twoBoxes();
        Mate m1; m1.type = MateType::Coplanar; m1.orient = Orient::Opposed;
        m1.a = mk(0, RefKind::Plane, Vec3(0, 0, 10), Vec3(0, 0, 1));
        m1.b = mk(1, RefKind::Plane, Vec3(0, 0, 0),  Vec3(0, 0, -1));
        Mate m2; m2.type = MateType::Collinear; m2.orient = Orient::Any;
        m2.a = mk(0, RefKind::Axis, Vec3(5, 5, 0), Vec3(0, 0, 1));
        m2.b = mk(1, RefKind::Axis, Vec3(2, 3, 0), Vec3(0, 0, 1));
        pr.mates = { m1, m2 };
        Result r = solve(pr);
        CHECK(r.converged, "converged");
        // B 的軸線上點 (2,3,0) 應落在世界 (5,5,*)
        const WorldGeom gb = worldGeom(pr, pr.mates[1].b);
        CHECK(std::abs(gb.p.x() - 5) < 1e-5 && std::abs(gb.p.y() - 5) < 1e-5, "axis passes through (5,5)");
        CHECK(r.dof == 1, "1 dof remains (spin about the axis)");
        std::printf("   dof=%d\n", r.dof);
    }
    // ── 3. 面距離 ───────────────────────────────────────────────────────────────
    {
        std::printf("[3] plane-plane distance 5 (+parallel opposed)\n");
        Problem pr = twoBoxes();
        Mate m; m.type = MateType::Distance; m.orient = Orient::Opposed; m.value = 5.0;
        m.a = mk(0, RefKind::Plane, Vec3(0, 0, 10), Vec3(0, 0, 1));
        m.b = mk(1, RefKind::Plane, Vec3(0, 0, 0),  Vec3(0, 0, -1));
        pr.mates = { m };
        Result r = solve(pr);
        CHECK(r.converged, "converged");
        const WorldGeom gb = worldGeom(pr, pr.mates[0].b);
        CHECK(std::abs(gb.p.z() - 15) < 1e-6, "gap of 5 above A top face");
    }
    // ── 4. 夾角 ─────────────────────────────────────────────────────────────────
    {
        std::printf("[4] plane-plane angle 30deg\n");
        Problem pr = twoBoxes();
        Mate m0; m0.type = MateType::Coincident;                         // 先讓 B 原點壓在 A 原點（點-點）
        m0.a = mk(0, RefKind::Point, Vec3(0, 0, 0), Vec3(0, 0, 1));
        m0.b = mk(1, RefKind::Point, Vec3(0, 0, 0), Vec3(0, 0, 1));
        Mate m; m.type = MateType::Angle; m.orient = Orient::Aligned; m.value = 30.0 * M_PI / 180.0;
        m.a = mk(0, RefKind::Plane, Vec3(0, 0, 0), Vec3(0, 0, 1));
        m.b = mk(1, RefKind::Plane, Vec3(0, 0, 0), Vec3(0, 0, 1));
        pr.mates = { m0, m };
        Result r = solve(pr);
        CHECK(r.converged, "converged");
        const WorldGeom gb = worldGeom(pr, pr.mates[1].b);
        const double ang = std::acos(std::max(-1.0, std::min(1.0, gb.d.z()))) * 180.0 / M_PI;
        CHECK(std::abs(ang - 30.0) < 1e-5, "angle is 30");
        CHECK(gb.p.norm() < 1e-6, "origin coincident");
        std::printf("   angle=%.8f dof=%d\n", ang, r.dof);
    }
    // ── 5. 與座標軸／座標平面的關係 ─────────────────────────────────────────────────
    {
        std::printf("[5] world datum relations\n");
        Problem pr; pr.lengthScale = 100.0;
        pr.bodies = { freeBody(rot(Vec3(1, 1, 0), 0.7), Vec3(10, 20, 30)) };
        // B 的軸線（本地 Z）與世界 X 軸共線
        Mate m1; m1.type = MateType::Collinear; m1.orient = Orient::Any;
        m1.a = mk(-1, RefKind::Axis, Vec3(0, 0, 0), Vec3(1, 0, 0));
        m1.b = mk(0, RefKind::Axis, Vec3(0, 0, 0), Vec3(0, 0, 1));
        // B 的某平面（本地 y=0，法向 +Y）與世界 XZ 平面（法向 -Y）面對面共面
        Mate m2; m2.type = MateType::Coplanar; m2.orient = Orient::Opposed;
        m2.a = mk(-1, RefKind::Plane, Vec3(0, 0, 0), Vec3(0, -1, 0));
        m2.b = mk(0, RefKind::Plane, Vec3(0, 0, 0), Vec3(0, 1, 0));
        // B 的另一平面（本地 x=0，法向 +X）與世界 YZ 平面距離 12
        Mate m3; m3.type = MateType::Distance; m3.orient = Orient::Any; m3.value = 12.0;
        m3.a = mk(-1, RefKind::Plane, Vec3(0, 0, 0), Vec3(1, 0, 0));
        m3.b = mk(0, RefKind::Plane, Vec3(0, 0, 0), Vec3(0, 0, 1));     // B 的 z=0 平面（法向 = 本地 Z，已被拉到世界 X）
        pr.mates = { m1, m2, m3 };
        Result r = solve(pr);
        CHECK(r.converged, "converged");
        const WorldGeom ax = worldGeom(pr, pr.mates[0].b);
        CHECK(std::abs(ax.d.x()) > 0.999999 && std::abs(ax.p.y()) < 1e-6 && std::abs(ax.p.z()) < 1e-6, "axis on world X");
        const WorldGeom pl = worldGeom(pr, pr.mates[2].b);
        CHECK(std::abs(std::abs(pl.p.x()) - 12) < 1e-6, "12 from YZ plane");
        std::printf("   dof=%d iters=%d restart=%d\n", r.dof, r.iterations, (int)r.usedRestart);
    }
    // ── 6. 衝突（過度約束）──────────────────────────────────────────────────────
    {
        std::printf("[6] conflicting mates are reported\n");
        Problem pr = twoBoxes();
        Mate m1; m1.type = MateType::Coplanar; m1.orient = Orient::Opposed;
        m1.a = mk(0, RefKind::Plane, Vec3(0, 0, 10), Vec3(0, 0, 1));
        m1.b = mk(1, RefKind::Plane, Vec3(0, 0, 0),  Vec3(0, 0, -1));
        Mate m2 = m1; m2.type = MateType::Distance; m2.value = 5.0;
        pr.mates = { m1, m2 };
        Result r = solve(pr);
        CHECK(!r.converged, "not converged");
        CHECK(!(r.mateOk[0] && r.mateOk[1]), "at least one mate flagged");
    }
    // ── 7. 抑制的配合不參與 ──────────────────────────────────────────────────────
    {
        std::printf("[7] suppressed mate ignored\n");
        Problem pr = twoBoxes();
        Mate m1; m1.type = MateType::Coplanar; m1.orient = Orient::Opposed;
        m1.a = mk(0, RefKind::Plane, Vec3(0, 0, 10), Vec3(0, 0, 1));
        m1.b = mk(1, RefKind::Plane, Vec3(0, 0, 0),  Vec3(0, 0, -1));
        Mate m2 = m1; m2.type = MateType::Distance; m2.value = 5.0; m2.enabled = false;
        pr.mates = { m1, m2 };
        Result r = solve(pr);
        CHECK(r.converged && r.mateOk[1], "ok with suppressed conflicting mate");
    }
    // ── 8. 量測與取向建議 ────────────────────────────────────────────────────────
    {
        std::printf("[8] measure / closestOrient\n");
        Problem pr = twoBoxes();
        pr.bodies[1] = freeBody(Mat3::Identity(), Vec3(0, 0, 7));
        Ref a = mk(0, RefKind::Plane, Vec3(0, 0, 0), Vec3(0, 0, 1));
        Ref b = mk(1, RefKind::Plane, Vec3(0, 0, 0), Vec3(0, 0, -1));
        double v = 0;
        CHECK(measureValue(pr, MateType::Distance, a, b, Orient::Opposed, v) && std::abs(v - 7) < 1e-12, "distance 7");
        CHECK(closestOrient(pr, MateType::Coplanar, a, b) == Orient::Opposed, "opposed suggested");
        Ref c = mk(1, RefKind::Axis, Vec3(0, 0, 0), Vec3(1, 0, 0));
        Ref d = mk(0, RefKind::Axis, Vec3(0, 0, 0), Vec3(0, 1, 0));
        CHECK(measureValue(pr, MateType::Angle, d, c, Orient::Aligned, v) && std::abs(v - M_PI / 2) < 1e-12, "angle 90");
        CHECK(isValidPair(MateType::Collinear, RefKind::Axis, RefKind::Axis) && !isValidPair(MateType::Collinear, RefKind::Plane, RefKind::Axis), "pair validity");
    }
    // ── 9. 隨機一致性測試：先決定真值姿態，再依配合類型「構造」在真值下成立的幾何，擾動起始姿態後必須能解回 ──────
    {
        std::printf("[9] random consistent assemblies\n");
        std::mt19937 rng(12345);
        auto U = [&](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); };
        auto randDir = [&]() { Vec3 v(U(-1, 1), U(-1, 1), U(-1, 1)); return v.normalized(); };
        auto perpTo = [&](const Vec3& n) { Vec3 v = randDir(); v -= v.dot(n) * n; return v.normalized(); };
        auto sgn = [&]() { return U(0, 1) < 0.5 ? 1.0 : -1.0; };
        const MateType types[] = { MateType::Coincident, MateType::Collinear, MateType::Coplanar, MateType::Parallel,
                                   MateType::Perpendicular, MateType::Distance, MateType::Angle };
        const RefKind kinds[] = { RefKind::Point, RefKind::Axis, RefKind::Plane };
        int trials = 0, ok = 0, mates = 0;
        for (int t = 0; t < 300; ++t) {
            Problem truth; truth.lengthScale = 100.0;
            const int nb = 3;
            for (int i = 0; i < nb; ++i) truth.bodies.push_back(freeBody(rot(randDir(), U(-3, 3)), Vec3(U(-80, 80), U(-80, 80), U(-80, 80))));
            truth.bodies[0].fixed = true;
            const int nm = 2 + static_cast<int>(U(0, 4));
            for (int k = 0; k < nm; ++k) {
                for (int guard = 0; guard < 100; ++guard) {
                    const MateType ty = types[static_cast<int>(U(0, 7)) % 7];
                    RefKind c1 = kinds[static_cast<int>(U(0, 3)) % 3], c2 = kinds[static_cast<int>(U(0, 3)) % 3];
                    if (static_cast<int>(c1) > static_cast<int>(c2)) std::swap(c1, c2);
                    if (!isValidPair(ty, c1, c2)) continue;
                    // 世界座標下的 g1（c1）與相容的 g2（c2）
                    Vec3 p1(U(-60, 60), U(-60, 60), U(-60, 60)), d1 = randDir(), p2, d2 = randDir();
                    p2 = Vec3(U(-60, 60), U(-60, 60), U(-60, 60));
                    const bool P1 = c1 == RefKind::Point, A1 = c1 == RefKind::Axis;
                    const bool P2 = c2 == RefKind::Point, A2 = c2 == RefKind::Axis, L2 = c2 == RefKind::Plane;
                    (void)P2;
                    switch (ty) {
                    case MateType::Coincident:
                        p2 = p1;                                              // (P,P)(P,A)(P,L)：g2 通過 g1 的點
                        break;
                    case MateType::Collinear:                                 // (A,A)
                        p2 = p1 + d1 * U(-40, 40); d2 = d1 * sgn();
                        break;
                    case MateType::Coplanar:
                        if (c1 == RefKind::Plane) { p2 = p1 + perpTo(d1) * U(-40, 40); d2 = d1 * sgn(); }     // (L,L)
                        else { p2 = p1; d2 = perpTo(d1); }                                                  // (A,L)：軸線在平面內
                        break;
                    case MateType::Parallel:
                        if (c1 == c2) d2 = d1 * sgn(); else d2 = perpTo(d1);   // (A,A)(L,L) / (A,L)
                        break;
                    case MateType::Perpendicular:
                        if (c1 == c2) d2 = perpTo(d1); else d2 = d1 * sgn();   // (A,A)(L,L) / (A,L)
                        break;
                    case MateType::Distance:
                        if (A1 && A2) d2 = d1 * sgn();                         // 軸-軸：平行
                        else if (A1 && L2) d2 = perpTo(d1);                    // 軸-面：平行
                        else if (c1 == RefKind::Plane && L2) d2 = d1 * sgn();  // 面-面：平行
                        break;
                    case MateType::Angle: break;
                    }
                    (void)P1;
                    int ba = static_cast<int>(U(-1, nb)), bb = static_cast<int>(U(0, nb));
                    if (ba == bb || bb >= nb || ba >= nb) continue;
                    if (truth.bodies[bb].fixed && (ba < 0 || truth.bodies[ba].fixed)) continue;
                    auto toLocal = [&](int body, const Vec3& pw, const Vec3& dw, RefKind kd) {
                        if (body < 0) return mk(body, kd, pw, dw);
                        const Body& B = truth.bodies[body];
                        return mk(body, kd, B.R.transpose() * (pw - B.t) / B.scale, B.R.transpose() * dw);
                    };
                    Mate m; m.type = ty;
                    if (U(0, 1) < 0.5) { m.a = toLocal(ba, p1, d1, c1); m.b = toLocal(bb, p2, d2, c2); }
                    else               { m.a = toLocal(ba, p2, d2, c2); m.b = toLocal(bb, p1, d1, c1); }
                    m.orient = closestOrient(truth, ty, m.a, m.b);
                    if (mateUsesValue(ty, m.a.kind, m.b.kind)) {
                        double v; if (!measureValue(truth, ty, m.a, m.b, m.orient, v)) continue; m.value = v;
                    }
                    // 自我檢查：構造出來的配合在真值下殘差必須為 0
                    std::vector<double> rr; mateResiduals(truth, m, rr);
                    double mx = 0; for (double x : rr) mx = std::max(mx, std::abs(x));
                    if (mx > 1e-9) { std::printf("   generator bug: type=%d res=%g\n", (int)ty, mx); ++g_fail; continue; }
                    truth.mates.push_back(m);
                    break;
                }
            }
            if (truth.mates.empty()) continue;
            ++trials; mates += static_cast<int>(truth.mates.size());
            Problem pr = truth;
            for (size_t i = 1; i < pr.bodies.size(); ++i) {
                pr.bodies[i].R = expSO3(randDir() * U(-0.35, 0.35)) * pr.bodies[i].R;
                pr.bodies[i].t += Vec3(U(-15, 15), U(-15, 15), U(-15, 15));
            }
            Result r = solve(pr);
            if (r.converged) ++ok;
        }
        std::printf("   solved %d / %d random assemblies (%d mates total)\n", ok, trials, mates);
        CHECK(ok >= trials * 95 / 100, "random success rate >= 95%");
    }


    // ── 10. 大幅擾動（起始姿態可能整個翻轉）與效能 ──────────────────────────────────
    {
        std::printf("[10] large perturbation / performance\n");
        std::mt19937 rng(777);
        auto U = [&](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); };
        auto randDir = [&]() { Vec3 v(U(-1, 1), U(-1, 1), U(-1, 1)); return v.normalized(); };
        // 疊箱子：N 個箱子，每個的底面 與 下一個的頂面 面對面共面 + 同軸 → 一整柱
        const int N = 25;
        Problem truth; truth.lengthScale = 500.0;
        Body base; base.fixed = true; truth.bodies.push_back(base);
        for (int i = 1; i < N; ++i) truth.bodies.push_back(freeBody(Mat3::Identity(), Vec3(0, 0, 20.0 * i)));
        for (int i = 1; i < N; ++i) {
            Mate m1; m1.type = MateType::Coplanar; m1.orient = Orient::Opposed;
            m1.a = mk(i - 1, RefKind::Plane, Vec3(0, 0, 20), Vec3(0, 0, 1));
            m1.b = mk(i,     RefKind::Plane, Vec3(0, 0, 0),  Vec3(0, 0, -1));
            Mate m2; m2.type = MateType::Collinear; m2.orient = Orient::Aligned;
            m2.a = mk(i - 1, RefKind::Axis, Vec3(0, 0, 0), Vec3(0, 0, 1));
            m2.b = mk(i,     RefKind::Axis, Vec3(0, 0, 0), Vec3(0, 0, 1));
            truth.mates.push_back(m1); truth.mates.push_back(m2);
        }
        int ok = 0, tr = 20; double tms = 0;
        for (int t = 0; t < tr; ++t) {
            Problem pr = truth;
            for (int i = 1; i < N; ++i) {
                pr.bodies[i].R = expSO3(randDir() * U(0, 3.1)) * pr.bodies[i].R;     // 最多 ~180° 的亂轉
                pr.bodies[i].t += Vec3(U(-100, 100), U(-100, 100), U(-100, 100));
            }
            auto t0 = std::chrono::steady_clock::now();
            Result r = solve(pr);
            tms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (r.converged) ++ok;
        }
        std::printf("   %d / %d stacks of %d bodies solved from wild starts, avg %.1f ms\n", ok, tr, N, tms / tr);
        CHECK(ok >= tr * 80 / 100, "wild-start stack success >= 80%");
    }

    std::printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
