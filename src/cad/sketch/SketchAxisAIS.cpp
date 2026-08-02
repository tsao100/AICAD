// src/cad/sketch/SketchAxisAIS.cpp
#include "SketchAxisAIS.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Prs3d_Drawer.hxx>
#include <Quantity_Color.hxx>
#include <AIS_InteractiveContext.hxx>

namespace aicad::cad {

// ══════════════════════════════════════════════════════════════════════════
// SketchAxisAIS — 包裝 TopoDS_Edge，沿用 AIS_Shape 標準選取機制
// ══════════════════════════════════════════════════════════════════════════

namespace {
TopoDS_Edge buildAxisEdge(const gp_Ax3& plane, double halfLen,
                          SketchAxisAIS::AxisType axis)
{
    gp_Pnt org = plane.Location();
    gp_Dir dir = (axis == SketchAxisAIS::AxisType::X) ? plane.XDirection()
                                                       : plane.YDirection();
    gp_Pnt p0(org.X() - halfLen * dir.X(),
              org.Y() - halfLen * dir.Y(),
              org.Z() - halfLen * dir.Z());
    gp_Pnt p1(org.X() + halfLen * dir.X(),
              org.Y() + halfLen * dir.Y(),
              org.Z() + halfLen * dir.Z());
    return BRepBuilderAPI_MakeEdge(p0, p1);
}
}

SketchAxisAIS::SketchAxisAIS(const gp_Ax3&  plane,
                              double         halfLen,
                              AxisType       axis,
                              const QString& uuid)
    : AIS_Shape(buildAxisEdge(plane, halfLen, axis))
    , m_axis(axis)
    , m_uuid(uuid)
{
    // X 軸：紅色，Y 軸：綠色
    Quantity_Color color = (m_axis == AxisType::X)
        ? Quantity_Color(0.8, 0.2, 0.2, Quantity_TOC_sRGB)
        : Quantity_Color(0.2, 0.75, 0.2, Quantity_TOC_sRGB);

    SetColor(color);
    SetWidth(1.5);

    // 點線樣式（透過 Drawer 的 LineAspect 設定虛線型態）
    Handle(Prs3d_Drawer) drawer = Attributes();
    if (!drawer.IsNull()) {
        Handle(Prs3d_LineAspect) lineAspect =
            new Prs3d_LineAspect(color, Aspect_TOL_DOTDASH, 1.5);
        drawer->SetWireAspect(lineAspect);
        drawer->SetFreeBoundaryAspect(lineAspect);
    }

    SetDisplayMode(AIS_WireFrame);
}

// ══════════════════════════════════════════════════════════════════════════
// SketchOriginAIS — 包裝 TopoDS_Vertex
// ══════════════════════════════════════════════════════════════════════════

namespace {
TopoDS_Vertex buildOriginVertex(const gp_Ax3& plane)
{
    return BRepBuilderAPI_MakeVertex(plane.Location());
}
}

SketchOriginAIS::SketchOriginAIS(const gp_Ax3&  plane,
                                  double         /*size*/,
                                  const QString& uuid)
    : AIS_Shape(buildOriginVertex(plane))
    , m_uuid(uuid)
{
    Quantity_Color color(0.9, 0.9, 0.9, Quantity_TOC_sRGB);
    SetColor(color);

    Handle(Prs3d_Drawer) drawer = Attributes();
    if (!drawer.IsNull()) {
        Handle(Prs3d_PointAspect) ptAspect =
            new Prs3d_PointAspect(Aspect_TOM_PLUS, color, 2.0);
        drawer->SetPointAspect(ptAspect);
    }

    SetDisplayMode(AIS_WireFrame);
}

} // namespace aicad::cad
