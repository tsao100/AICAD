#include "ExtrudeFeature.h"

#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepTools.hxx>

#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Face.hxx>

#include <gp_Vec.hxx>
#include <Standard_Failure.hxx>

namespace aicad::cad {

ExtrudeFeature::ExtrudeFeature(double height)
    : m_height(height)
{
}

QString ExtrudeFeature::name() const
{
    return "Extrude";
}

TopoDS_Shape ExtrudeFeature::build(const TopoDS_Shape& input)
{
    if (input.IsNull())
        return TopoDS_Shape();

    TopoDS_Shape profile = input;

    try
    {
        // Case 1: Wire → Face
        if (profile.ShapeType() == TopAbs_WIRE)
        {
            TopoDS_Wire wire = TopoDS::Wire(profile);

            BRepBuilderAPI_MakeFace faceMaker(wire);
            if (!faceMaker.IsDone())
                return TopoDS_Shape();

            profile = faceMaker.Face();
        }

        // Case 2: Face → Extrude
        if (profile.ShapeType() == TopAbs_FACE)
        {
            gp_Vec dir(0, 0, m_height);
            BRepPrimAPI_MakePrism prism(profile, dir);
            return prism.Shape();
        }
    }
    catch (Standard_Failure const&)
    {
        return TopoDS_Shape();
    }

    return TopoDS_Shape();
}

} // namespace aicad::cad
