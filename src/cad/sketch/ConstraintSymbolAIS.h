#pragma once
#include "SketchConstraint.h"
#include <AIS_InteractiveObject.hxx>
#include <Prs3d_Presentation.hxx>
#include <SelectMgr_Selection.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Quantity_Color.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <QList>

namespace aicad::cad {

struct SketchGeometry;
enum class SolveStatus;

DEFINE_STANDARD_HANDLE(AIS_ConstraintSymbol, AIS_InteractiveObject)

/**
 * @brief 幾何約束符號 AIS 物件
 *
 * 在 3D 視埠中以小圖標顯示幾何約束（Coincident、Horizontal、Vertical 等），
 * 顏色依求解狀態變化：
 *   FullyConstrained → 綠色
 *   UnderConstrained → 青色
 *   OverConstrained  → 紅色
 *   Driving=false    → 灰色（量測模式）
 */
class AIS_ConstraintSymbol : public AIS_InteractiveObject {
    DEFINE_STANDARD_RTTIEXT(AIS_ConstraintSymbol, AIS_InteractiveObject)
public:
    AIS_ConstraintSymbol(const SketchConstraint& c,
                         const QList<SketchGeometry*>& involvedGeoms,
                         const gp_Trsf& sketchToWorld,
                         SolveStatus status);

    /// 更新約束資料（幾何求解後呼叫）
    void Update(const SketchConstraint& c,
                const QList<SketchGeometry*>& geoms,
                SolveStatus status);

    const QString& constraintUuid() const { return m_constraint.uuid; }

private:
    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& prs,
                 const Standard_Integer mode) override;

    void ComputeSelection(const Handle(SelectMgr_Selection)&,
                          const Standard_Integer) override {}  // 不可選取

    // 各種符號繪製
    void drawCoincident    (const Handle(Prs3d_Presentation)& prs);
    void drawTangent       (const Handle(Prs3d_Presentation)& prs);
    void drawHorizontal    (const Handle(Prs3d_Presentation)& prs);
    void drawVertical      (const Handle(Prs3d_Presentation)& prs);
    void drawParallel      (const Handle(Prs3d_Presentation)& prs);
    void drawPerpendicular (const Handle(Prs3d_Presentation)& prs);
    void drawEqualLength   (const Handle(Prs3d_Presentation)& prs);
    void drawEqualRadius   (const Handle(Prs3d_Presentation)& prs);
    void drawFixed         (const Handle(Prs3d_Presentation)& prs);
    void drawMidpoint      (const Handle(Prs3d_Presentation)& prs);
    void drawSymmetric     (const Handle(Prs3d_Presentation)& prs);
    void drawPointOnCurve  (const Handle(Prs3d_Presentation)& prs);
    void drawGeneric       (const Handle(Prs3d_Presentation)& prs, const char* label);

    gp_Pnt computeSymbolPos() const;
    Quantity_Color symbolColor() const;

    SketchConstraint         m_constraint;
    QList<SketchGeometry*>   m_geoms;
    gp_Trsf                  m_sketchToWorld;
    SolveStatus              m_status;
    gp_Pnt                   m_symbolPos;

    static constexpr double kSymbolSize = 3.0;  // mm
};

} // namespace aicad::cad