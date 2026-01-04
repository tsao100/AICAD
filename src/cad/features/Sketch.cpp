#include "Sketch.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <gp_Pnt.hxx>

namespace aicad::cad {

Sketch::Sketch()
{
}

QString Sketch::name() const
{
    return "Sketch";
}

void Sketch::addLine(const gp_Pnt2d& p1, const gp_Pnt2d& p2)
{
    m_lines.push_back({ p1, p2 });
}

TopoDS_Shape Sketch::build(const TopoDS_Shape&)
{
    BRepBuilderAPI_MakeWire wireBuilder;

    for (const auto& line : m_lines)
    {
        gp_Pnt p1(line.p1.X(), line.p1.Y(), 0.0);
        gp_Pnt p2(line.p2.X(), line.p2.Y(), 0.0);

        wireBuilder.Add(
            BRepBuilderAPI_MakeEdge(p1, p2)
        );
    }

    return wireBuilder.Shape();
}

} // namespace aicad::cad
