// src/cad/grips/AIS_GripHandle.cpp
#include "AIS_GripHandle.h"

#include <Prs3d_PointAspect.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_Drawer.hxx>
#include <SelectMgr_Selection.hxx>
#include <Select3D_SensitivePoint.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Quantity_Color.hxx>

IMPLEMENT_STANDARD_RTTIEXT(aicad::cad::AIS_GripHandle, AIS_InteractiveObject)

namespace aicad::cad {

AIS_GripHandle::AIS_GripHandle(const GripPoint& grip, double size)
    : AIS_InteractiveObject()
    , m_id(grip.id)
    , m_position(grip.position)
    , m_state(grip.state)
    , m_size(size)
{
    // 永遠顯示在最上層，不被幾何遮蔽
    SetZLayer(Graphic3d_ZLayerId_Top);
    SetInfiniteState(Standard_False);
    myDrawer->SetPointAspect(
        new Prs3d_PointAspect(Aspect_TOM_BALL, Quantity_NOC_BLUE1, size));
}

void AIS_GripHandle::SetGripState(GripState state)
{
    m_state = state;
    // 強制重繪
    SetToUpdate();
    UpdatePresentations();
}

void AIS_GripHandle::SetPosition(const gp_Pnt& pos)
{
    m_position = pos;
    SetToUpdate();
    UpdatePresentations();
}

Quantity_Color AIS_GripHandle::colorForState(GripState s) const
{
    switch (s) {
    case GripState::Normal:   return Quantity_Color(0.20, 0.45, 0.85, Quantity_TOC_sRGB);
    case GripState::Hover:    return Quantity_Color(0.10, 0.85, 0.35, Quantity_TOC_sRGB);
    case GripState::Active:   return Quantity_Color(0.95, 0.20, 0.20, Quantity_TOC_sRGB);
    case GripState::Disabled: return Quantity_Color(0.50, 0.50, 0.50, Quantity_TOC_sRGB);
    default:                  return Quantity_Color(Quantity_NOC_BLUE1);
    }
}

void AIS_GripHandle::Compute(
    const Handle(PrsMgr_PresentationManager)&,
    const Handle(Prs3d_Presentation)& prs,
    Standard_Integer)
{
    prs->Clear();

    Quantity_Color col = colorForState(m_state);
    double half = m_size * 0.5;

    // ✅ 用 sketch plane 的 X/Y 軸計算方塊角點，而非固定 XY offset
    gp_Vec vX = gp_Vec(m_planeX) * half;
    gp_Vec vY = gp_Vec(m_planeY) * half;

    gp_Pnt p0 = m_position.Translated(-vX - vY);  // bottom-left
    gp_Pnt p1 = m_position.Translated( vX - vY);  // bottom-right
    gp_Pnt p2 = m_position.Translated( vX + vY);  // top-right
    gp_Pnt p3 = m_position.Translated(-vX + vY);  // top-left

    // 方塊邊框
    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Handle(Graphic3d_ArrayOfPolylines) border =
        new Graphic3d_ArrayOfPolylines(5);
    border->AddVertex(p0);
    border->AddVertex(p1);
    border->AddVertex(p2);
    border->AddVertex(p3);
    border->AddVertex(p0);  // close

    Handle(Graphic3d_AspectLine3d) lineAspect =
        new Graphic3d_AspectLine3d(col, Aspect_TOL_SOLID, 1.5f);
    grp->SetGroupPrimitivesAspect(lineAspect);
    grp->AddPrimitiveArray(border);

    // 中心填充點
    Handle(Graphic3d_Group) grp2 = prs->NewGroup();
    Handle(Graphic3d_ArrayOfPoints) centerPt =
        new Graphic3d_ArrayOfPoints(1);
    centerPt->AddVertex(m_position);

    Handle(Graphic3d_AspectMarker3d) markerAspect =
        new Graphic3d_AspectMarker3d(Aspect_TOM_BALL, col, m_size * 0.4);
    grp2->SetGroupPrimitivesAspect(markerAspect);
    grp2->AddPrimitiveArray(centerPt);
}

void AIS_GripHandle::ComputeSelection(
    const Handle(SelectMgr_Selection)& sel,
    Standard_Integer /*mode*/)
{
    // 給每個 Grip 一個 Sensitive Point，選取半徑 = size * 2
    Handle(SelectMgr_EntityOwner) owner =
        new SelectMgr_EntityOwner(this, 10 /*priority*/);

    Handle(Select3D_SensitivePoint) sensPoint =
        new Select3D_SensitivePoint(owner, m_position);

    sel->Add(sensPoint);
}

} // namespace aicad::cad
