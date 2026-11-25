// 端到端：真實 OCCT 方塊 → AssemblyPart 的位姿轉換 → 參照凍結（本地座標）→ 求解 → 重新放置 → 檢查實際 BRep 幾何
#include "AssemblyPart.h"
#include "AssemblyGeom.h"
#include "AssemblySolver.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <cstdio>
#include <random>
using namespace aicad::cad;
using namespace aicad::cad::asmsolve;
static int bad = 0;
#define CHECK(c, m) do { if (!(c)) { ++bad; std::printf("FAIL %s\n", m); } } while (0)

static Mat3 toMat(const double R[9]) { Mat3 m; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) m(i, j) = R[i * 3 + j]; return m; }

int main()
{
    std::mt19937 rng(5);
    auto U = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); };

    // 1) 位姿轉換
    for (int t = 0; t < 200; ++t) {
        double x = U(-100, 100), y = U(-100, 100), z = U(-100, 100), rx = U(-80, 80), ry = U(-80, 80), rz = U(-170, 170);
        gp_Trsf p = AssemblyPart::makePose(x, y, z, rx, ry, rz);
        double x2, y2, z2, rx2, ry2, rz2;
        AssemblyPart::decomposePose(p, x2, y2, z2, rx2, ry2, rz2);
        gp_Trsf p2 = AssemblyPart::makePose(x2, y2, z2, rx2, ry2, rz2);
        gp_Pnt q(1.5, -2.5, 3.5), a = q.Transformed(p), b = q.Transformed(p2);
        CHECK(a.Distance(b) < 1e-9, "euler roundtrip reproduces same pose");
        CHECK(std::abs(x - x2) < 1e-9 && std::abs(y - y2) < 1e-9 && std::abs(z - z2) < 1e-9, "translation roundtrip");
        double R[9], T[3]; AssemblyPart::poseToRT(p, R, T);
        gp_Trsf p3 = AssemblyPart::poseFromRT(R, T);
        CHECK(q.Transformed(p3).Distance(a) < 1e-9, "RT roundtrip");
        // 與求解器的映射 R*(s*p)+t 一致
        Mat3 M = toMat(R); Vec3 tv(T[0], T[1], T[2]); Vec3 w = M * (2.5 * Vec3(q.X(), q.Y(), q.Z())) + tv;
        gp_Trsf sc; sc.SetScale(gp_Pnt(0, 0, 0), 2.5); gp_Pnt full = q.Transformed(p * sc);
        CHECK(gp_Pnt(w.x(), w.y(), w.z()).Distance(full) < 1e-9, "solver mapping == fullTrsf");
    }
    // makePose 的約定：外旋 XYZ（先 X 後 Y 最後 Z，皆繞世界軸）
    {
        gp_Trsf p = AssemblyPart::makePose(0, 0, 0, 90, 0, 0);
        gp_Pnt r = gp_Pnt(0, 1, 0).Transformed(p);
        CHECK(r.Distance(gp_Pnt(0, 0, 1)) < 1e-9, "Rx(90) maps +Y to +Z");
        gp_Trsf p2 = AssemblyPart::makePose(0, 0, 0, 90, 0, 90);          // 先 X 再 Z：Y→Z（Z 軸轉動不動 Z）
        CHECK(gp_Pnt(0, 1, 0).Transformed(p2).Distance(gp_Pnt(0, 0, 1)) < 1e-9, "extrinsic order X then Z");
    }

    // 2) 兩個方塊 A(固定)、B(浮動，單位縮放 0.5，亂擺)：B 底面貼 A 頂面、B 的邊與 A 的邊共線…用真實 BRep 驗證
    TopoDS_Shape boxA = BRepPrimAPI_MakeBox(100, 60, 40).Shape();           // 本地原生座標（A 比例 1）
    TopoDS_Shape boxB = BRepPrimAPI_MakeBox(60, 60, 20).Shape();            // B 原生尺寸 60×60×20，縮放 0.5 → 30×30×10
    const double sA = 1.0, sB = 0.5;
    gp_Trsf poseA = AssemblyPart::makePose(10, 20, 30, 0, 0, 30);
    gp_Trsf poseB = AssemblyPart::makePose(-80, 40, 90, 33, -50, 120);
    auto full = [](const gp_Trsf& pose, double s) { gp_Trsf sc; sc.SetScale(gp_Pnt(0, 0, 0), s); return pose * sc; };
    TopoDS_Shape placedA = BRepBuilderAPI_Transform(boxA, full(poseA, sA), Standard_True).Shape();
    TopoDS_Shape placedB = BRepBuilderAPI_Transform(boxB, full(poseB, sB), Standard_True).Shape();

    // 在「放置後」形狀上挑面（模擬 3D 點選）：A 頂面（外向 +Z_local → 世界旋轉後）與 B 底面，再轉回各自本地座標
    auto pickWorld = [&](const TopoDS_Shape& placed, const gp_Dir& localNormalWanted, const gp_Trsf& pose, double s, const TopoDS_Shape& local, MateRefData& out) {
        auto refs = assemblygeom::enumerate(placed);
        gp_Vec want(localNormalWanted); want.Transform(pose);          // 本地法向 → 世界
        int best = -1; double bd = 1e9;
        for (int i = 0; i < refs.size(); ++i) {
            if (refs[i].kind != RefKind::Plane) continue;
            double e = (gp_Vec(refs[i].d[0], refs[i].d[1], refs[i].d[2]) - want).Magnitude();
            if (e < bd) { bd = e; best = i; }
        }
        if (best < 0 || bd > 1e-6) return false;
        MateRefData r = refs[best];
        const gp_Trsf inv = full(pose, s).Inverted();
        gp_Pnt p(r.p[0], r.p[1], r.p[2]); p.Transform(inv);
        gp_Vec d(r.d[0], r.d[1], r.d[2]); d.Transform(inv); d.Normalize();
        r.p[0] = p.X(); r.p[1] = p.Y(); r.p[2] = p.Z(); r.d[0] = d.X(); r.d[1] = d.Y(); r.d[2] = d.Z();
        // 驗證：與直接從「本地形狀」列舉出的同序號面一致
        MateRefData chk = r; chk.topoIndex = r.topoIndex;
        CHECK(assemblygeom::refresh(local, chk), "refresh against local shape");
        for (int k = 0; k < 3; ++k) CHECK(std::abs(chk.d[k] - r.d[k]) < 1e-9, "frozen normal equals local normal by index");
        out = r; return true;
    };
    MateRefData fa, fb;
    CHECK(pickWorld(placedA, gp_Dir(0, 0, 1), poseA, sA, boxA, fa), "pick A top");
    CHECK(pickWorld(placedB, gp_Dir(0, 0, -1), poseB, sB, boxB, fb), "pick B bottom");
    // A 的前邊與 B 的前邊共線？改用 A 的一個側面（法向 +X 本地）與 B 的側面（本地 -X）距離 12，加上 B 的另一側面與 A 的另一側面共面
    MateRefData fa2, fb2, fa3, fb3;
    CHECK(pickWorld(placedA, gp_Dir(1, 0, 0), poseA, sA, boxA, fa2), "pick A +X face");
    CHECK(pickWorld(placedB, gp_Dir(-1, 0, 0), poseB, sB, boxB, fb2), "pick B -X face");
    CHECK(pickWorld(placedA, gp_Dir(0, -1, 0), poseA, sA, boxA, fa3), "pick A -Y face");
    CHECK(pickWorld(placedB, gp_Dir(0, -1, 0), poseB, sB, boxB, fb3), "pick B -Y face");

    Problem pr; pr.lengthScale = 150;
    auto body = [](const gp_Trsf& pose, double s, bool fixed) { Body b; double R[9], T[3]; AssemblyPart::poseToRT(pose, R, T); b.R = toMat(R); b.t = Vec3(T[0], T[1], T[2]); b.scale = s; b.fixed = fixed; return b; };
    pr.bodies = { body(poseA, sA, true), body(poseB, sB, false) };
    auto ref = [](int bodyIdx, const MateRefData& r) { Ref x; x.body = bodyIdx; x.kind = r.kind; x.p = Vec3(r.p[0], r.p[1], r.p[2]); x.d = Vec3(r.d[0], r.d[1], r.d[2]); return x; };
    Mate m1; m1.type = MateType::Coplanar; m1.orient = Orient::Opposed; m1.a = ref(0, fa); m1.b = ref(1, fb);            // 面貼面
    Mate m2; m2.type = MateType::Distance; m2.orient = Orient::Opposed; m2.value = 12.0; m2.a = ref(0, fa2); m2.b = ref(1, fb2); // 側面對側面距離 12（沿 A 的 +X 外向法向）
    Mate m3; m3.type = MateType::Coplanar; m3.orient = Orient::Aligned; m3.a = ref(0, fa3); m3.b = ref(1, fb3);            // 前面齊平
    pr.mates = { m1, m2, m3 };
    Result res = solve(pr);
    std::printf("solve: converged=%d dof=%d restart=%d iters=%d\n", (int)res.converged, res.dof, (int)res.usedRestart, res.iterations);
    CHECK(res.converged, "solver converged on real geometry");
    CHECK(res.dof == 0, "fully constrained (face-face + side distance + flush)");

    // 把解出的位姿寫回，重新放置 B，並用實際 BRep 量
    double R[9], T[3];
    for (int i = 0; i < 3; ++i) { for (int j = 0; j < 3; ++j) R[i * 3 + j] = pr.bodies[1].R(i, j); T[i] = pr.bodies[1].t[i]; }
    gp_Trsf newB = AssemblyPart::poseFromRT(R, T);
    TopoDS_Shape placedB2 = BRepBuilderAPI_Transform(boxB, full(newB, sB), Standard_True).Shape();
    auto A = assemblygeom::enumerate(placedA), B = assemblygeom::enumerate(placedB2);
    // 找 A 頂面（外向 = poseA * +Z）與 B 底面（外向 = newB * -Z）
    gp_Vec nA(0, 0, 1); nA.Transform(poseA); gp_Vec nBb(0, 0, -1); nBb.Transform(newB);
    const MateRefData *aTop = nullptr, *bBot = nullptr;
    for (auto& r : A) if (r.kind == RefKind::Plane && (gp_Vec(r.d[0], r.d[1], r.d[2]) - nA).Magnitude() < 1e-6) aTop = &r;
    for (auto& r : B) if (r.kind == RefKind::Plane && (gp_Vec(r.d[0], r.d[1], r.d[2]) - nBb).Magnitude() < 1e-6) bBot = &r;
    CHECK(aTop && bBot, "found faces on re-placed shapes");
    if (aTop && bBot) {
        gp_Vec dv(gp_Pnt(aTop->p[0], aTop->p[1], aTop->p[2]), gp_Pnt(bBot->p[0], bBot->p[1], bBot->p[2]));
        CHECK(std::abs(dv.Dot(gp_Vec(aTop->d[0], aTop->d[1], aTop->d[2]))) < 1e-6, "B bottom lies in A top plane (real BRep)");
        CHECK((gp_Vec(aTop->d[0], aTop->d[1], aTop->d[2]) + gp_Vec(bBot->d[0], bBot->d[1], bBot->d[2])).Magnitude() < 1e-6, "outward normals are opposed (face-to-face)");
    }
    // 側面距離 12：沿 A 的 +X 面外向法向，A 的 +X 面到 B 的 -X 面
    gp_Vec nAx(1, 0, 0); nAx.Transform(poseA); gp_Vec nBx(-1, 0, 0); nBx.Transform(newB);
    const MateRefData *aX = nullptr, *bX = nullptr;
    for (auto& r : A) if (r.kind == RefKind::Plane && (gp_Vec(r.d[0], r.d[1], r.d[2]) - nAx).Magnitude() < 1e-6) aX = &r;
    for (auto& r : B) if (r.kind == RefKind::Plane && (gp_Vec(r.d[0], r.d[1], r.d[2]) - nBx).Magnitude() < 1e-6) bX = &r;
    if (aX && bX) {
        gp_Vec dv(gp_Pnt(aX->p[0], aX->p[1], aX->p[2]), gp_Pnt(bX->p[0], bX->p[1], bX->p[2]));
        std::printf("side gap along A's +X outward normal = %.9f\n", dv.Dot(gp_Vec(aX->d[0], aX->d[1], aX->d[2])));
        CHECK(std::abs(dv.Dot(gp_Vec(aX->d[0], aX->d[1], aX->d[2])) - 12.0) < 1e-6, "side gap == 12 (real BRep)");
    } else CHECK(false, "found side faces");
    std::printf(bad ? "E2E FAILED (%d)\n" : "E2E all passed\n", bad);
    return bad;
}
