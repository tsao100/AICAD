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
    /// @param size Grip 的螢幕空間固定像素大小（Graphic3d_AspectMarker3d scale），
    ///             不受視圖縮放影響 —— 比照 SketchPointAIS::drawEndpointMarker 的作法。
    explicit AIS_GripHandle(const GripPoint& grip, double size = 3.0);

    void SetPlaneAxes(const gp_Dir& xAxis, const gp_Dir& yAxis) {
        m_planeX = xAxis;
        m_planeY = yAxis;
    }

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
    // ✅ 修正後 Compute() 改用螢幕空間 marker（AspectMarker3d），不再用
    // m_planeX/m_planeY 建構模型空間方框，故以下兩個欄位目前僅保留供
    // SetPlaneAxes() 呼叫相容，不影響繪製。
    gp_Dir  m_planeX = gp_Dir(1, 0, 0);
    gp_Dir  m_planeY = gp_Dir(0, 1, 0);
    Quantity_Color colorForState(GripState s) const;

    QString     m_id;
    gp_Pnt      m_position;
    GripState   m_state;
    double      m_size;
};

DEFINE_STANDARD_HANDLE(AIS_GripHandle, AIS_InteractiveObject)

} // namespace aicad::cad
