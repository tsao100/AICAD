#pragma once
#include "../Sketch.h"
#include "SketchConstraint.h"
#include "ConstraintSolver.h"
#include <AIS_InteractiveObject.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Prs3d_Presentation.hxx>
#include <SelectMgr_Selection.hxx>
#include <gp_Pnt.hxx>
#include <gp_Ax3.hxx>
#include <QString>

namespace aicad::cad {

DEFINE_STANDARD_HANDLE(SketchPointAIS, AIS_InteractiveObject)

/**
 * @brief 草圖點 AIS 物件（Phase 0B / Task D）
 *
 * 在草圖平面上顯示 SketchPoint，並支援 AIS 選取（用於約束選點）。
 *
 * 顯示樣式：
 *   Endpoint  → 實心紅色圓點（螢幕空間固定像素大小，不受縮放影響）
 *   Center    → 加號  +，6px
 *   Explicit  → 菱形  ◇，5px，黃色
 *
 * SelectionSensitivity = 6（比曲線高，優先被 snap 到）。
 */
class SketchPointAIS : public AIS_InteractiveObject {
    DEFINE_STANDARD_RTTIEXT(SketchPointAIS, AIS_InteractiveObject)
public:
    explicit SketchPointAIS(const SketchPoint* pt, const gp_Ax3& sketchPlane);

    /// 更新顯示位置（Solver 求解後呼叫）
    void updatePosition(const QVector2D& newPos);

    /// 更新顯示狀態（Under/Fully/Over constrained）
    void updateSolveStatus(SolveStatus status);

    /// 目前的求解狀態（供呼叫端判斷是否需要 Redisplay，避免不必要的 context 更新）
    SolveStatus solveStatus() const { return m_status; }

    const QString& pointUuid()           const { return m_uuid; }
    SketchPoint::Origin origin()         const { return m_origin; }
    gp_Pnt position3D()                  const { return m_pos3D; }

private:
    void Compute(const Handle(PrsMgr_PresentationManager)& pm,
                 const Handle(Prs3d_Presentation)& prs,
                 const Standard_Integer mode) override;

    void ComputeSelection(const Handle(SelectMgr_Selection)& sel,
                          const Standard_Integer mode) override;

    /// 草圖平面 2D 座標 → 世界座標
    gp_Pnt toWorld(const QVector2D& pos2D) const;

    /// 依 origin 繪製符號（加號/菱形，模型空間，會隨縮放改變大小）
    void drawSymbol(const Handle(Prs3d_Presentation)& prs,
                    const Quantity_Color& color,
                    double halfSize) const;

    /// 繪製 Endpoint 專用符號：實心紅色圓點，螢幕空間固定像素大小，不受縮放影響
    void drawEndpointMarker(const Handle(Prs3d_Presentation)& prs) const;

    /// Endpoint 圓點的螢幕空間大小（像素，Graphic3d_AspectMarker3d 的 scale 參數）
    static constexpr Standard_ShortReal kEndpointMarkerScale = 3.0f;

    QString             m_uuid;
    SketchPoint::Origin m_origin;
    gp_Ax3              m_plane;
    gp_Pnt              m_pos3D;
    SolveStatus         m_status = SolveStatus::UnderConstrained;
};

} // namespace aicad::cad
