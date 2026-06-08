#pragma once
#include "SketchConstraint.h"
#include <AIS_InteractiveObject.hxx>
#include <Prs3d_Presentation.hxx>
#include <SelectMgr_Selection.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <QString>
#include <QList>
#include <QVector2D>

namespace aicad::cad {

struct SketchGeometry;
enum class SolveStatus;

DEFINE_STANDARD_HANDLE(AIS_DimensionLine, AIS_InteractiveObject)

/**
 * @brief 尺寸線 AIS 物件（Phase 5）
 *
 * 對尺寸約束（FixedDistance、FixedRadius、FixedX、FixedY、FixedAngleDim）
 * 繪製標準 CAD 尺寸線，標籤顯示 instance 的實際解析值及原始 paramExpr。
 *
 * 標籤格式：
 *   - 有 paramExpr：  "width = 200.00"
 *   - 無 paramExpr：  "200.00"
 */
class AIS_DimensionLine : public AIS_InteractiveObject {
    DEFINE_STANDARD_RTTIEXT(AIS_DimensionLine, AIS_InteractiveObject)
public:
    AIS_DimensionLine(const SketchConstraint& c,
                      const QList<SketchGeometry*>& geoms,
                      const gp_Trsf& sketchToWorld,
                      SolveStatus status);

    void Update(const SketchConstraint& c,
                const QList<SketchGeometry*>& geoms,
                SolveStatus status);

    /// 顯示文字：優先顯示 paramExpr，次顯示純數值
    QString labelText() const;

    const QString& constraintUuid() const { return m_constraint.uuid; }

    /// 明確設定兩個參考點（供 FixedDistance 端點 handle 使用）
    void    setRefPositions(const QVector2D& p1, const QVector2D& p2);

    // Phase 3B：尺寸線拖曳支援
    void    setDimLineOffset(double offsetX, double offsetY);
    double  dimOffsetX() const { return m_dimOffsetX; }
    double  dimOffsetY() const { return m_dimOffsetY; }
    gp_Pnt  dimLineAnchorPoint3D() const;

    /// 計算數值標籤的世界座標中心（供 ComputeSelection 及拖曳錨點使用）
    gp_Pnt  labelPosition3D() const;

private:
    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& prs,
                 const Standard_Integer mode) override;

    void ComputeSelection(const Handle(SelectMgr_Selection)& sel,
                          const Standard_Integer mode) override;

    void drawLinearDimension (const Handle(Prs3d_Presentation)& prs);
    void drawRadiusDimension (const Handle(Prs3d_Presentation)& prs);
    void drawHorizontalDim   (const Handle(Prs3d_Presentation)& prs);
    void drawVerticalDim     (const Handle(Prs3d_Presentation)& prs);
    void drawAngleDim        (const Handle(Prs3d_Presentation)& prs);
    void drawLengthDimension    (const Handle(Prs3d_Presentation)& prs);
    void drawDiameterDimension  (const Handle(Prs3d_Presentation)& prs);
    void drawArcLengthDimension (const Handle(Prs3d_Presentation)& prs);
    void drawCoordinateDimension(const Handle(Prs3d_Presentation)& prs);

    // 輔助：從約束 refs 取得世界座標點
    bool getRefPoints(gp_Pnt& p1, gp_Pnt& p2) const;

    SketchConstraint         m_constraint;
    QList<SketchGeometry*>   m_geoms;
    gp_Trsf                  m_sketchToWorld;
    SolveStatus              m_status;
    double                   m_offsetDist = 8.0;  // mm，尺寸線偏移量

    // Phase 3B 新增
    DistanceMode             m_distMode   = DistanceMode::PointToPoint;
    double                   m_dimOffsetX = 0.0;   ///< 草圖平面偏移 X
    double                   m_dimOffsetY = 0.0;   ///< 草圖平面偏移 Y

    // 明確設定的參考點（供 FixedDistance 端點 handle 使用）
    bool                     m_hasRefPos  = false;
    QVector2D                m_refPos1;
    QVector2D                m_refPos2;
};

} // namespace aicad::cad