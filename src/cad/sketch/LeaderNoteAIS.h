#pragma once
#include "SketchAnnotation.h"
#include <AIS_InteractiveObject.hxx>
#include <Prs3d_Presentation.hxx>
#include <SelectMgr_Selection.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <QVector2D>

namespace aicad::cad {

DEFINE_STANDARD_HANDLE(AIS_LeaderNote, AIS_InteractiveObject)

/**
 * @brief Leader / Hole Note 的 OCCT AIS 呈現 —— GDIM 昇級規劃 v2 Phase 8
 *
 * 對應 GDIM.md 第四節「建立 Leader」與 GDIM_昇級規劃_v2.md 第 1.3 節提到的
 * LeaderNoteAIS（Phase 8）。資料來源是 SketchAnnotation
 * （kind == AnnotationKind::LeaderNote），其 leaderVertices 欄位已在
 * Phase 1 預留好，這裡不需要再擴充資料結構。
 *
 * 這是全新的 OCCT 型別（不像 Phase 3 討論過的 AIS_DimensionLine 拆分），
 * 沒有任何既有呼叫端建立/操作這個型別，因此不會有 Phase 3 說明過的
 * 「牽動既有 8 個檔案」風險，可以放心用獨立的 OCCT RTTI 類別實作。
 *
 * 繪製規則：
 *   - target（annotation.refs[0] 解析出的位置）畫一個小圓圈終端符號
 *     （對照 GDIM.md 的 "○──── M20"）
 *   - 依序連接 leaderVertices 的折點（可以是 0 個，代表直線 target→anchor）
 *   - 最後一段連到文字錨點（leaderVertices 最後一個點；若 leaderVertices
 *     為空，退化為 target + 一個固定的預設偏移，避免文字疊在 target 上）
 *   - 文字錨點處顯示 annotation.noteText（例如 "M20"），左對齊
 */
class AIS_LeaderNote : public AIS_InteractiveObject {
    DEFINE_STANDARD_RTTIEXT(AIS_LeaderNote, AIS_InteractiveObject)
public:
    AIS_LeaderNote(const SketchAnnotation& ann,
                    const QVector2D& targetPos,
                    const gp_Trsf& sketchToWorld);

    /// 更新標註資料（annotation 內容或 target 幾何變動後呼叫）
    void Update(const SketchAnnotation& ann, const QVector2D& targetPos);

    const QString& annotationUuid() const { return m_annotation.uuid; }

    /// 文字錨點（sketch 平面座標），供 GripManager 之後串接「拖曳移動
    /// Leader 折點/文字錨點」時取用（見 .h 頂端關於 grip-editing 的說明）
    QVector2D anchorPlanePt() const;

private:
    void Compute(const Handle(PrsMgr_PresentationManager)&,
                 const Handle(Prs3d_Presentation)& prs,
                 const Standard_Integer mode) override;

    void ComputeSelection(const Handle(SelectMgr_Selection)&,
                          const Standard_Integer) override;

    SketchAnnotation m_annotation;
    QVector2D        m_targetPos;     ///< annotation.refs[0] 解析出的 sketch 平面座標
    gp_Trsf          m_sketchToWorld;

    gp_Pnt           m_targetWorld;   ///< Compute() 快取，供 ComputeSelection 使用
    gp_Pnt           m_anchorWorld;
};

} // namespace aicad::cad
