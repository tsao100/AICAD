#pragma once
#include "SketchConstraint.h"
#include "SketchAnnotation.h"
#include <AIS_InteractiveObject.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_Drawer.hxx>
#include <SelectMgr_Selection.hxx>
#include <SelectMgr_SequenceOfOwner.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Quantity_Color.hxx>
#include <Graphic3d_HorizontalTextAlignment.hxx>
#include <Graphic3d_VerticalTextAlignment.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
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

    // ── GDIM v2 Phase 3 ──────────────────────────────────────────────────
    /// 掛載對應的 SketchAnnotation（若有）。掛載後 labelText() 會改用
    /// AnnotationTextFormatter 依 prefix/suffix/tolerance/precision/
    /// isBasic/isInspection 組字；未掛載時行為與掛載前完全相同
    /// （僅顯示純數值，向下相容既有純 SketchConstraint 驅動的尺寸線）。
    void setAnnotation(const SketchAnnotation& a) { m_annotation = a; m_hasAnnotation = true; }
    void clearAnnotation() { m_hasAnnotation = false; }
    bool hasAnnotation() const { return m_hasAnnotation; }

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

    // ─────────────────────────────────────────────────────────────────────
    // 數值標籤 hover 高亮（僅文字本身反白，尺寸線／延伸線／箭頭永不參與）
    // ─────────────────────────────────────────────────────────────────────

    /// 單一數值標籤的世界座標區域快取（Compute() 時依實際繪製內容填入，
    /// 供 ComputeSelection 建立精確的 hover/選取方框，以及 hover 高亮重繪文字用）
    struct LabelRegion {
        gp_Pnt  pos;               ///< 數值文字的世界座標中心
        gp_Dir  alongDir;          ///< 文字水平方向（與繪製時 SetOrientation 一致）
        QString text;
        bool    oriented = true;
        Graphic3d_HorizontalTextAlignment hAlign = Graphic3d_HTA_CENTER;
        Graphic3d_VerticalTextAlignment   vAlign = Graphic3d_VTA_CENTER;
    };

    /// 記錄一個數值標籤的世界座標位置／方向／內容，供 ComputeSelection 與 hover 高亮使用。
    /// hAlign/vAlign/oriented 對應實際繪製時使用的 Graphic3d_Text 設定，確保 hover 高亮
    /// 重繪出的文字與原本顯示的文字完全疊合（位置、朝向皆一致）。
    void addLabelRegion(const gp_Pnt& pos, const gp_Vec& along, const QString& text,
                         bool oriented = true,
                         Graphic3d_HorizontalTextAlignment hAlign = Graphic3d_HTA_CENTER,
                         Graphic3d_VerticalTextAlignment   vAlign = Graphic3d_VTA_CENTER);

    /// GDIM v2 Phase 7：目前 Compute() 收集到的所有數值標籤世界座標區域
    /// （唯讀），供 CadView::checkAnnotationCollisions() 投影到螢幕座標
    /// 做碰撞偵測使用。
    const QList<LabelRegion>& labelRegions() const { return m_labelRegions; }

    /// 由自訂 EntityOwner 在 hover 時呼叫：僅重繪指定索引的數值文字（反白色），
    /// 不重繪尺寸線、延伸線、箭頭，確保只有數值本身會高亮
    void hilightLabel(const Handle(PrsMgr_PresentationManager)& thePM,
                       const Handle(Prs3d_Drawer)& theStyle,
                       int labelIndex);

    /// 點選後的持續高亮（與 hover 邏輯共用 hilightLabel，同樣僅高亮文字）
    void HilightSelected(const Handle(PrsMgr_PresentationManager)& thePM,
                          const SelectMgr_SequenceOfOwner& theOwners) override;

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
    // 單點座標標註（FixedX/FixedY）專用：與 FixedHorizDist/FixedVertDist
    // 共用 drawHorizontalDim/drawVerticalDim 會經過 getRefPoints() 的雙點
    // 合成邏輯，對單點來說 c2 恆等於 c1（退化成零長度尺寸線）。改用這兩個
    // 專屬函式，直接用點本身 + m_dimOffsetX/Y（提交當下的滑鼠位置）畫出
    // 水平/垂直引線，與 DimPreviewOverlay 的預覽演算法一致。
    void drawXDimension      (const Handle(Prs3d_Presentation)& prs);
    void drawYDimension      (const Handle(Prs3d_Presentation)& prs);
    void drawAngleDim        (const Handle(Prs3d_Presentation)& prs);
    void drawLengthDimension    (const Handle(Prs3d_Presentation)& prs);
    void drawDiameterDimension  (const Handle(Prs3d_Presentation)& prs);
    void drawArcLengthDimension (const Handle(Prs3d_Presentation)& prs);
    void drawCoordinateDimension(const Handle(Prs3d_Presentation)& prs);
    void drawPerpendicularSymbol(const Handle(Prs3d_Presentation)& prs,
                                 const gp_Pnt& foot,
                                 const gp_Pnt& fromPt,
                                 const gp_Pnt& onLinePt,
                                 double symSize = 3.0);

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

    // hover/選取用：本次 Compute() 收集到的所有數值標籤世界座標區域
    QList<LabelRegion>       m_labelRegions;

    // GDIM v2 Phase 3：可選掛載的標註資料（見 setAnnotation()）
    SketchAnnotation          m_annotation;
    bool                      m_hasAnnotation = false;
};

} // namespace aicad::cad