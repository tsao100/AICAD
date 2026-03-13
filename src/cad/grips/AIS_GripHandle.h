// src/cad/grips/AIS_GripHandle.h
#pragma once

#include <AIS_InteractiveObject.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <gp_Pnt.hxx>
#include "GripPoint.h"

namespace aicad::cad {

/// 自訂 AIS 物件：渲染一個 Grip 方塊
class AIS_GripHandle : public AIS_InteractiveObject
{
    DEFINE_STANDARD_RTTIEXT(AIS_GripHandle, AIS_InteractiveObject)

public:
    explicit AIS_GripHandle(const GripPoint& grip, double size = 3.0);

    void            SetGripState(GripState state);
    GripState       GetGripState() const { return m_state; }
    const QString&  GetGripId()    const { return m_id; }
    void            SetPosition(const gp_Pnt& pos);
    const gp_Pnt&   GetPosition()  const { return m_position; }

    // AIS overrides
    void Compute(const Handle(PrsMgr_PresentationManager)& pm,
                 const Handle(Prs3d_Presentation)& prs,
                 Standard_Integer mode) override;

    void ComputeSelection(const Handle(SelectMgr_Selection)& sel,
                          Standard_Integer mode) override;

private:
    Quantity_Color colorForState(GripState s) const;

    QString     m_id;
    gp_Pnt      m_position;
    GripState   m_state;
    double      m_size;
};

DEFINE_STANDARD_HANDLE(AIS_GripHandle, AIS_InteractiveObject)

} // namespace aicad::cad
