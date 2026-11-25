#include "view/MateMarkerPresenter.cpp"
#include <TopExp_Explorer.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <cstdio>
using namespace aicad::view;
static int bad = 0;
#define CHECK(c, m) do { if (!(c)) { ++bad; std::printf("FAIL %s\n", m); } } while (0)
static int edges(const TopoDS_Shape& s) { int n = 0; for (TopExp_Explorer e(s, TopAbs_EDGE); e.More(); e.Next()) ++n; return n; }
int main()
{
    double p[3] = {10, 20, 30}, dz[3] = {0, 0, 1}, dd[3] = {1, 1, 0};
    TopoDS_Shape pt = buildRef(RefKind::Point, p, dz, 2.0);
    CHECK(edges(pt) == 3, "point cross has 3 edges");
    TopoDS_Shape ax = buildRef(RefKind::Axis, p, dd, 2.0);
    CHECK(edges(ax) == 5, "axis = line + 4 arrow wings");
    Bnd_Box b; BRepBndLib::Add(ax, b); double x0, y0, z0, x1, y1, z1; b.Get(x0, y0, z0, x1, y1, z1);
    // 軸向 (1,1,0)/√2，半長 3*s = 6 → x 範圍約 10 ± 4.24
    CHECK(std::abs((x1 - x0) - 2 * 6 / std::sqrt(2.0)) < 0.8, "axis extent along direction");
    CHECK(std::abs(z1 - z0) < 1.2, "axis stays near z=30 plane (arrow wings add ±0.5)");
    TopoDS_Shape pl = buildRef(RefKind::Plane, p, dz, 2.0);
    CHECK(edges(pl) == 4 + 1 + 2 + 2, "plane = frame + normal arrow + small cross");
    Bnd_Box bp; BRepBndLib::Add(pl, bp); bp.Get(x0, y0, z0, x1, y1, z1);
    CHECK(std::abs((x1 - x0) - 2 * 1.6 * 2.0) < 0.3 && std::abs((y1 - y0) - 2 * 1.6 * 2.0) < 0.3, "plane frame 2*1.6*s square in XY");
    CHECK(z1 > 30 && z1 - 30 < 4.0 && std::abs(z0 - 30) < 0.1, "normal arrow rises along +Z");
    // 退化：零方向向量不崩潰
    double z0v[3] = {0, 0, 0};
    CHECK(edges(buildRef(RefKind::Axis, p, z0v, 2.0)) == 5, "zero direction falls back to +Z");
    // update() 以空 context 呼叫不崩潰
    MateMarkerPresenter pr; Handle(AIS_InteractiveContext) nullCtx;
    pr.update(nullCtx, {}, 1.0, true, QString()); pr.clear(nullCtx);
    std::printf(bad ? "MARKER TEST FAILED\n" : "marker shape tests passed\n");
    return bad;
}
