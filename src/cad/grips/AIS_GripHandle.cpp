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

    // ✅ 修正：原本用 planeX/planeY 在「模型空間」算方塊角點（Compute()
    // 產生的是實際幾何頂點），大小會隨視圖縮放而放大/縮小 —— 在真實世界
    // 座標（如鐵路測量的 TM2/TWD97，單位為公尺）下，同一個 m_size 在不同
    // 縮放層級下對應的螢幕像素差異極大，導致 Grip 視覺大小、以及使用者
    // 主觀感受的 hover/點擊範圍忽大忽小、普遍偏大。
    //
    // 改用 Graphic3d_AspectMarker3d（比照 SketchPointAIS::drawEndpointMarker）：
    // marker 的 scale 參數以「螢幕像素」為單位，不受視圖縮放影響，行為與
    // SketchPoint 的固定大小圓點一致。外框用環狀 marker 表示方塊/十字選取框，
    // 中心再疊一個較小的實心點以維持原本「外框 + 中心填充」的視覺層次。

    // 外框
    Handle(Graphic3d_Group) grpOuter = prs->NewGroup();
    Handle(Graphic3d_ArrayOfPoints) outerPt = new Graphic3d_ArrayOfPoints(1);
    outerPt->AddVertex(m_position);

    Handle(Graphic3d_AspectMarker3d) outerAspect =
        new Graphic3d_AspectMarker3d(Aspect_TOM_RING1, col,
                                      static_cast<Standard_ShortReal>(m_size));
    grpOuter->SetGroupPrimitivesAspect(outerAspect);
    grpOuter->AddPrimitiveArray(outerPt);

    // 中心填充點
    Handle(Graphic3d_Group) grpCenter = prs->NewGroup();
    Handle(Graphic3d_ArrayOfPoints) centerPt = new Graphic3d_ArrayOfPoints(1);
    centerPt->AddVertex(m_position);

    Handle(Graphic3d_AspectMarker3d) centerAspect =
        new Graphic3d_AspectMarker3d(Aspect_TOM_BALL, col,
                                      static_cast<Standard_ShortReal>(m_size * 0.4));
    grpCenter->SetGroupPrimitivesAspect(centerAspect);
    grpCenter->AddPrimitiveArray(centerPt);
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
