#include "AssemblyGeom.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Trsf.hxx>
#include <cstdio>
using namespace aicad::cad;
using namespace aicad::cad::asmsolve;
static int bad = 0;
#define CHECK(c, m) do { if (!(c)) { ++bad; std::printf("FAIL %s\n", m); } } while (0)
int main()
{
    TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    auto refs = assemblygeom::enumerate(box);
    int planes = 0, axes = 0, pts = 0; bool topFound = false, botFound = false;
    for (auto& r : refs) {
        if (r.kind == RefKind::Plane) { ++planes; if (r.d[2] > .99 && std::abs(r.p[2] - 30) < 1e-9) topFound = true; if (r.d[2] < -.99 && std::abs(r.p[2]) < 1e-9) botFound = true; }
        else if (r.kind == RefKind::Axis) ++axes; else ++pts;
    }
    std::printf("box: %d planes, %d line edges, %d vertices (of %d)\n", planes, axes, pts, (int)refs.size());
    CHECK(planes == 6 && axes == 12 && pts == 8, "box counts");
    CHECK(topFound && botFound, "outward normals of top/bottom faces");

    TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(5, 40).Shape();
    int cylAxes = 0; for (auto& r : assemblygeom::enumerate(cyl)) if (r.kind == RefKind::Axis && r.label.contains(QString::fromUtf8("圓柱軸"))) ++cylAxes;
    CHECK(cylAxes == 1, "one cylinder axis");

    // 放置後（轉動＋平移）的形狀：取子形 → 凍結 → 以位姿反算回本地座標，應與未放置前的本地幾何一致
    gp_Trsf t; t.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 1, 1)), 0.8); gp_Trsf tr; tr.SetTranslation(gp_Vec(100, -50, 25)); t = tr * t;
    TopoDS_Shape placed = BRepBuilderAPI_Transform(box, t, Standard_True).Shape();
    auto localRefs = assemblygeom::enumerate(box);
    auto placedRefs = assemblygeom::enumerate(placed);
    CHECK(localRefs.size() == placedRefs.size(), "same enumeration size");
    int matched = 0;
    for (int i = 0; i < localRefs.size(); ++i) {
        gp_Pnt p(localRefs[i].p[0], localRefs[i].p[1], localRefs[i].p[2]); p.Transform(t);
        gp_Vec d(localRefs[i].d[0], localRefs[i].d[1], localRefs[i].d[2]); d.Transform(t);
        const auto& q = placedRefs[i];
        bool pOk = localRefs[i].kind == RefKind::Point ? gp_Pnt(q.p[0], q.p[1], q.p[2]).Distance(p) < 1e-7 : true;
        bool dOk = localRefs[i].kind == RefKind::Point ? true : (gp_Vec(q.d[0], q.d[1], q.d[2]) - d).Magnitude() < 1e-7 || localRefs[i].kind == RefKind::Axis;
        // 邊的軸線方向可能在變換後取向不同（參數方向）——只要求平行
        if (localRefs[i].kind == RefKind::Axis) dOk = std::abs(std::abs(gp_Vec(q.d[0], q.d[1], q.d[2]).Dot(d)) - 1.0) < 1e-7;
        if (localRefs[i].kind == RefKind::Plane) { gp_Pnt qp(q.p[0], q.p[1], q.p[2]); pOk = std::abs(gp_Vec(p, qp).Dot(gp_Vec(q.d[0], q.d[1], q.d[2]))) < 1e-7; }
        if (pOk && dOk && localRefs[i].kind == q.kind) ++matched;
    }
    std::printf("placed refs matching transformed local refs: %d / %d\n", matched, (int)localRefs.size());
    CHECK(matched == localRefs.size(), "placed geometry == transformed local geometry");
    // refresh by index
    MateRefData r = localRefs[3]; r.p[0] += 1; CHECK(assemblygeom::refresh(box, r), "refresh ok");
    // base64 roundtrip
    TopoDS_Shape rt = assemblygeom::shapeFromBase64(assemblygeom::shapeToBase64(box));
    CHECK(!rt.IsNull() && assemblygeom::enumerate(rt).size() == localRefs.size(), "brep roundtrip");
    double c[3], diag; CHECK(assemblygeom::boundsOf(box, c, &diag) && std::abs(diag - std::sqrt(100 + 400 + 900)) < 1e-6, "bounds");
    std::printf(bad ? "FAILED\n" : "all geom checks passed\n");
    return bad;
}
