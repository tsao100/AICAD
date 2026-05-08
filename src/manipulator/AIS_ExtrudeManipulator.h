#pragma once

#include <AIS_InteractiveObject.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Ax1.hxx>
#include <Quantity_Color.hxx>
#include <SelectMgr_SelectionManager.hxx>

namespace aicad::manipulator {

class ExtrudeOwner : public SelectMgr_EntityOwner {
    DEFINE_STANDARD_RTTIEXT(ExtrudeOwner, SelectMgr_EntityOwner)
public:
    ExtrudeOwner(const Handle(SelectMgr_SelectableObject)& obj,
                 int partMode, Standard_Integer priority = 8)
        : SelectMgr_EntityOwner(obj, priority)
        , m_partMode(partMode) {}
    int partMode() const { return m_partMode; }
private:
    int m_partMode;
};
DEFINE_STANDARD_HANDLE(ExtrudeOwner, SelectMgr_EntityOwner)

/// 拉伸操控器 AIS 物件
/// Selection mode 1 = 主箭頭 (高度拖曳)
/// Selection mode 2 = 翻轉箭頭 (Reverse/Symmetric toggle)
class AIS_ExtrudeManipulator : public AIS_InteractiveObject
{
    DEFINE_STANDARD_RTTIEXT(AIS_ExtrudeManipulator, AIS_InteractiveObject)

public:
    AIS_ExtrudeManipulator(const gp_Pnt&  baseCenter,
                           const gp_Dir&  extrudeDir,
                           double         height,
                           const TopoDS_Shape& profileFace = TopoDS_Shape());

    void SetHeight(double height);
    void SetSymmetric(bool symmetric);
    void SetReversed(bool reversed);
    void SetBase(const gp_Pnt& base) { m_base = base; }
    void SetDir (const gp_Dir& dir)  { m_dir  = dir;  }

    /// 從 2D 螢幕 delta 計算新高度（由 ExtrudeManipulator 呼叫）
    double ComputeHeightFromDrag(const gp_Pnt& worldStart,
                                 const gp_Pnt& worldCurrent) const;

    /// 箭頭尖端的世界座標（用來定位 mini input box 的螢幕位置）
    gp_Pnt ArrowTipPosition() const;

    // AIS overrides
    void Compute(const Handle(PrsMgr_PresentationManager)& pm,
                 const Handle(Prs3d_Presentation)& prs,
                 Standard_Integer mode) override;

    void ComputeSelection(const Handle(SelectMgr_Selection)& sel,
                          Standard_Integer mode) override;

private:
    void buildArrow(const Handle(Prs3d_Presentation)& prs,
                    const Quantity_Color& shaftColor,
                    const Quantity_Color& coneColor);

    void buildFlipButton(const Handle(Prs3d_Presentation)& prs);

    void buildHeightLabel(const Handle(Prs3d_Presentation)& prs);

    gp_Pnt  m_base;
    gp_Dir  m_dir;
    double  m_height;
    bool    m_symmetric = false;
    bool    m_reversed  = false;

    // 幾何參數（固定比例）
    static constexpr double kShaftRadius  = 1.5;   // mm
    static constexpr double kConeRadius   = 4.0;
    static constexpr double kConeHeight   = 10.0;
    static constexpr double kFlipOffset   = -8.0;  // 距 base 向下偏移
    static constexpr double kFlipHalfLen  = 6.0;
    static constexpr double kShaftLength = 25.0;

    TopoDS_Shape m_profile;
};

DEFINE_STANDARD_HANDLE(AIS_ExtrudeManipulator, AIS_InteractiveObject)

} // namespace aicad::manipulator
