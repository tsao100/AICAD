/**
 * @file OSnapIndicator.h
 * @brief Object Snap 視覺指示器 (AIS_InteractiveObject 子類別)
 *
 * 每種 Snap 類型對應不同幾何符號：
 *  Endpoint      → □ 方框
 *  Midpoint      → △ 三角形
 *  Center        → ○ 空心圓
 *  Quadrant      → ◇ 菱形
 *  Intersection  → × 交叉
 *  Perpendicular → ⊥ 垂直符號
 *  Tangent       → ⌒ 切線符號（圓 + 線）
 *  Nearest       → X 記號
 *  Extension     → ─ ─ 虛線延伸
 *
 * @author AICAD Team
 * @date 2025-01-08
 */

#pragma once

#include "OSnapTypes.h"

#include <V3d_View.hxx>
#include <AIS_InteractiveObject.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Prs3d_Presentation.hxx>
#include <SelectMgr_Selection.hxx>

namespace aicad {
namespace osnap {

/**
 * @class OSnapIndicator
 * @brief 在 3D 視圖中繪製 snap 符號的 AIS 物件
 *
 * 使用 Graphic3d primitives 繪製，永遠在最上層（ZLayer_Top），
 * 不參與選取（不實作 ComputeSelection）。
 */
class OSnapIndicator : public AIS_InteractiveObject
{
    DEFINE_STANDARD_RTTI_INLINE(OSnapIndicator, AIS_InteractiveObject)

public:
    OSnapIndicator();
    virtual ~OSnapIndicator() = default;

    /// 設定當前顯示的 snap 候選
    void setCandidate(const SnapCandidate& candidate);

    /// 清除顯示（隱藏指示器）
    void clearCandidate();

    /// 設定指示器螢幕像素大小
    void setScreenSize(double pixels) { m_screenSize = pixels; }

    /// 是否有有效的 snap 候選
    bool hasValidCandidate() const { return m_hasCandidate; }

    const SnapCandidate& currentCandidate() const { return m_candidate; }

    void setView(const Handle(V3d_View)& view) { m_view = view; }

protected:
    /// AIS_InteractiveObject 必要覆寫
    virtual void Compute(const Handle(PrsMgr_PresentationManager)& mgr,
                         const Handle(Prs3d_Presentation)& prs,
                         const Standard_Integer mode) override;

    virtual void ComputeSelection(const Handle(SelectMgr_Selection)& sel,
                                  const Standard_Integer mode) override;

private:
    void drawEndpointSymbol   (const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s);
    void drawMidpointSymbol   (const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s);
    void drawCenterSymbol     (const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s);
    void drawQuadrantSymbol   (const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s);
    void drawIntersectSymbol  (const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s);
    void drawPerpendSymbol    (const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s);
    void drawTangentSymbol    (const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s);
    void drawNearestSymbol    (const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s);

    /// 建立多段線（閉合或開放）的 Graphic3d_ArrayOfPolylines
    Handle(Graphic3d_ArrayOfPolylines) makePolyline(
        const std::vector<gp_Pnt>& pts, bool closed = false);

    SnapCandidate m_candidate;
    bool          m_hasCandidate = false;
    double        m_screenSize   = 14.0;  ///< 符號大小（像素）
    Handle(V3d_View) m_view;
};

} // namespace osnap
} // namespace aicad
