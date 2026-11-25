/**
 * @file MateMarkerPresenter.cpp
 */
#include "MateMarkerPresenter.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRep_Builder.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <cmath>

namespace aicad {
namespace view {

using cad::assemblyutil::MateMarker;
using cad::asmsolve::RefKind;

namespace {

void addEdge(BRep_Builder& bb, TopoDS_Compound& c, const gp_Pnt& a, const gp_Pnt& b)
{
    if (a.Distance(b) < 1e-9) return;
    try {
        BRepBuilderAPI_MakeEdge me(a, b);
        if (me.IsDone()) {
            const TopoDS_Shape edge = me.Edge();
            TopoDS_Shape& comp = c;                       // TopoDS_Builder::Add 需要 TopoDS_Shape&
            bb.Add(comp, edge);
        }
    } catch (const Standard_Failure&) {}
}

/// 垂直於 n 的一組正交基底
void basisOf(const gp_Dir& n, gp_Dir& u, gp_Dir& v)
{
    const gp_Dir helper = std::abs(n.X()) < 0.9 ? gp_Dir(1, 0, 0) : gp_Dir(0, 1, 0);
    u = n.Crossed(helper);
    v = n.Crossed(u);
}

TopoDS_Shape buildRef(RefKind kind, const double p[3], const double dd[3], double s)
{
    BRep_Builder bb;
    TopoDS_Compound c;
    bb.MakeCompound(c);
    const gp_Pnt o(p[0], p[1], p[2]);
    gp_Vec dv(dd[0], dd[1], dd[2]);
    if (dv.Magnitude() < 1e-12) dv = gp_Vec(0, 0, 1);
    const gp_Dir d(dv);

    switch (kind) {
    case RefKind::Point: {
        const double h = 0.6 * s;
        addEdge(bb, c, gp_Pnt(o.X() - h, o.Y(), o.Z()), gp_Pnt(o.X() + h, o.Y(), o.Z()));
        addEdge(bb, c, gp_Pnt(o.X(), o.Y() - h, o.Z()), gp_Pnt(o.X(), o.Y() + h, o.Z()));
        addEdge(bb, c, gp_Pnt(o.X(), o.Y(), o.Z() - h), gp_Pnt(o.X(), o.Y(), o.Z() + h));
        break;
    }
    case RefKind::Axis: {
        const double L = 3.0 * s;
        const gp_Pnt a = o.Translated(gp_Vec(d) * -L), b = o.Translated(gp_Vec(d) * L);
        addEdge(bb, c, a, b);
        gp_Dir u, v;
        basisOf(d, u, v);
        const double al = 0.6 * s, aw = 0.25 * s;                       // 箭頭：指向 +d
        const gp_Pnt back = b.Translated(gp_Vec(d) * -al);
        addEdge(bb, c, b, back.Translated(gp_Vec(u) * aw));
        addEdge(bb, c, b, back.Translated(gp_Vec(u) * -aw));
        addEdge(bb, c, b, back.Translated(gp_Vec(v) * aw));
        addEdge(bb, c, b, back.Translated(gp_Vec(v) * -aw));
        break;
    }
    case RefKind::Plane: {
        gp_Dir u, v;
        basisOf(d, u, v);
        const double h = 1.6 * s;
        const gp_Pnt c1 = o.Translated(gp_Vec(u) * h + gp_Vec(v) * h);
        const gp_Pnt c2 = o.Translated(gp_Vec(u) * -h + gp_Vec(v) * h);
        const gp_Pnt c3 = o.Translated(gp_Vec(u) * -h + gp_Vec(v) * -h);
        const gp_Pnt c4 = o.Translated(gp_Vec(u) * h + gp_Vec(v) * -h);
        addEdge(bb, c, c1, c2); addEdge(bb, c, c2, c3); addEdge(bb, c, c3, c4); addEdge(bb, c, c4, c1);
        // 法向量箭頭
        const gp_Pnt tip = o.Translated(gp_Vec(d) * (1.6 * s));
        addEdge(bb, c, o, tip);
        const double al = 0.5 * s, aw = 0.2 * s;
        const gp_Pnt back = tip.Translated(gp_Vec(d) * -al);
        addEdge(bb, c, tip, back.Translated(gp_Vec(u) * aw));
        addEdge(bb, c, tip, back.Translated(gp_Vec(u) * -aw));
        // 平面上的小十字：標出參照點
        addEdge(bb, c, o.Translated(gp_Vec(u) * -0.3 * s), o.Translated(gp_Vec(u) * 0.3 * s));
        addEdge(bb, c, o.Translated(gp_Vec(v) * -0.3 * s), o.Translated(gp_Vec(v) * 0.3 * s));
        break;
    }
    }
    return c;
}

} // namespace

void MateMarkerPresenter::clear(const Handle(AIS_InteractiveContext)& context)
{
    if (!context.IsNull())
        for (const Handle(AIS_Shape)& o : m_objects)
            if (!o.IsNull() && context->IsDisplayed(o)) context->Remove(o, Standard_False);
    m_objects.clear();
}

void MateMarkerPresenter::update(const Handle(AIS_InteractiveContext)& context,
                                 const QVector<MateMarker>& markers, double size,
                                 bool visible, const QString& highlightMateId)
{
    clear(context);
    if (context.IsNull() || !visible || size <= 0.0) return;

    const Quantity_Color colA(0.10, 0.55, 1.00, Quantity_TOC_RGB);     // 參照 1：藍
    const Quantity_Color colB(1.00, 0.55, 0.10, Quantity_TOC_RGB);     // 參照 2：橘
    const Quantity_Color colErr(0.90, 0.10, 0.10, Quantity_TOC_RGB);   // 不滿足：紅
    const Quantity_Color colOff(0.60, 0.60, 0.60, Quantity_TOC_RGB);   // 抑制：灰
    const Quantity_Color colSel(1.00, 0.90, 0.10, Quantity_TOC_RGB);   // 選取：黃

    for (const MateMarker& mk : markers) {
        const bool selected = (!highlightMateId.isEmpty() && mk.mateId == highlightMateId);
        for (int i = 0; i < 2; ++i) {
            try {
                const TopoDS_Shape sh = buildRef(mk.kind[i], mk.p[i], mk.d[i], size * (selected ? 1.25 : 1.0));
                Handle(AIS_Shape) ais = new AIS_Shape(sh);
                const Quantity_Color& col = selected ? colSel
                                          : mk.state == MateMarker::Error ? colErr
                                          : mk.state == MateMarker::Suppressed ? colOff
                                          : (i == 0 ? colA : colB);
                ais->SetColor(col);
                ais->SetWidth(selected ? 4.0 : (mk.state == MateMarker::Suppressed ? 1.0 : 2.0));
                context->Display(ais, Standard_False);
                context->Deactivate(ais);                                         // 不可選取，不擋住零件的點選
                context->SetZLayer(ais, Graphic3d_ZLayerId_Topmost);              // 畫在實體前面
                m_objects.push_back(ais);
            } catch (const Standard_Failure&) {}
        }
    }
}

} // namespace view
} // namespace aicad
