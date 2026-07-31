#include "LeaderNoteAIS.h"

#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Text.hxx>
#include <Graphic3d_AspectText3d.hxx>
#include <Select3D_SensitivePoint.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <Quantity_Color.hxx>
#include <TCollection_ExtendedString.hxx>
#include <cmath>

IMPLEMENT_STANDARD_RTTIEXT(aicad::cad::AIS_LeaderNote, AIS_InteractiveObject)

namespace aicad::cad {

namespace {
// 沒有任何 leaderVertices 時的預設文字錨點偏移（sketch 平面單位），
// 避免文字直接疊在 target 點上
constexpr float kDefaultAnchorOffsetX = 20.0f;
constexpr float kDefaultAnchorOffsetY = 20.0f;
constexpr double kTerminatorRadius = 1.5;  ///< target 端小圓圈半徑
}

AIS_LeaderNote::AIS_LeaderNote(const SketchAnnotation& ann,
                                  const QVector2D& targetPos,
                                  const gp_Trsf& sketchToWorld)
    : m_annotation(ann)
    , m_targetPos(targetPos)
    , m_sketchToWorld(sketchToWorld)
{
    // ✅ 同 ConstraintSymbolAIS 的修正：LeaderNote 也是位置固定的小型
    // 標註物件，不該標記為無限物件，否則會有同樣「首次 Display() 常
    // 常不畫出來，要等下一次 Erase()+Display() 才顯示」的問題。
    SetInfiniteState(Standard_False);
}

void AIS_LeaderNote::Update(const SketchAnnotation& ann, const QVector2D& targetPos) {
    m_annotation = ann;
    m_targetPos  = targetPos;
}

QVector2D AIS_LeaderNote::anchorPlanePt() const {
    if (!m_annotation.leaderVertices.isEmpty())
        return m_annotation.leaderVertices.last();
    return m_targetPos + QVector2D(kDefaultAnchorOffsetX, kDefaultAnchorOffsetY);
}

void AIS_LeaderNote::Compute(
    const Handle(PrsMgr_PresentationManager)&,
    const Handle(Prs3d_Presentation)& prs,
    const Standard_Integer /*mode*/)
{
    prs->Clear();

    // ── 折線路徑：target → leaderVertices... → anchor ──────────────────────
    QList<QVector2D> path;
    path.append(m_targetPos);
    for (const auto& v : m_annotation.leaderVertices) path.append(v);
    const QVector2D anchor = anchorPlanePt();
    if (m_annotation.leaderVertices.isEmpty()) path.append(anchor);

    Handle(Graphic3d_Group) lineGrp = prs->NewGroup();
    lineGrp->SetPrimitivesAspect(
        new Graphic3d_AspectLine3d(Quantity_Color(Quantity_NOC_YELLOW),
                                    Aspect_TOL_SOLID, 1.5f));

    Handle(Graphic3d_ArrayOfPolylines) poly =
        new Graphic3d_ArrayOfPolylines(path.size(), 1);
    poly->AddBound(path.size());
    for (const auto& p : path) {
        gp_Pnt wp(p.x(), p.y(), 0.0);
        wp.Transform(m_sketchToWorld);
        poly->AddVertex(wp);
    }
    lineGrp->AddPrimitiveArray(poly);

    // ── target 端終端小圓圈（近似：8 段折線畫圓）─────────────────────────────
    m_targetWorld = gp_Pnt(m_targetPos.x(), m_targetPos.y(), 0.0);
    m_targetWorld.Transform(m_sketchToWorld);

    constexpr int kCircleSegs = 12;
    Handle(Graphic3d_ArrayOfPolylines) circle =
        new Graphic3d_ArrayOfPolylines(kCircleSegs + 1, 1);
    circle->AddBound(kCircleSegs + 1);
    for (int i = 0; i <= kCircleSegs; ++i) {
        double t = 2.0 * M_PI * i / kCircleSegs;
        QVector2D cp = m_targetPos + QVector2D(
            static_cast<float>(kTerminatorRadius * std::cos(t)),
            static_cast<float>(kTerminatorRadius * std::sin(t)));
        gp_Pnt wp(cp.x(), cp.y(), 0.0);
        wp.Transform(m_sketchToWorld);
        circle->AddVertex(wp);
    }
    lineGrp->AddPrimitiveArray(circle);

    // ── 文字（noteText，套用 prefix/suffix，其餘標註屬性沿用
    //    AnnotationTextFormatter 的一般規則，但 LeaderNote 沒有 tolerance/
    //    precision 這些數值公差概念，這裡只套用 prefix/suffix）───────────────
    QString label = m_annotation.prefix + m_annotation.noteText + m_annotation.suffix;
    m_anchorWorld = gp_Pnt(anchor.x(), anchor.y(), 0.0);
    m_anchorWorld.Transform(m_sketchToWorld);

    Handle(Graphic3d_Text) gtext = new Graphic3d_Text(36.0f);
    gtext->SetText(TCollection_ExtendedString(label.toUtf8().constData(), Standard_True));
    gtext->SetPosition(m_anchorWorld);
    gtext->SetHorizontalAlignment(Graphic3d_HTA_LEFT);
    gtext->SetVerticalAlignment(Graphic3d_VTA_CENTER);

    Handle(Graphic3d_AspectText3d) textAsp = new Graphic3d_AspectText3d();
    textAsp->SetColor(Quantity_Color(Quantity_NOC_YELLOW));
    Handle(Graphic3d_Group) txtGrp = prs->NewGroup();
    txtGrp->SetGroupPrimitivesAspect(textAsp);
    txtGrp->AddText(gtext);
}

void AIS_LeaderNote::ComputeSelection(
    const Handle(SelectMgr_Selection)& sel,
    const Standard_Integer /*mode*/)
{
    // 與 AIS_ConstraintSymbol 相同做法：單一 sensitive point + 像素容差，
    // 涵蓋文字錨點附近的 hover/選取範圍（不含折線本身，簡化版）
    constexpr int kSensitivity = 25;
    Handle(SelectMgr_EntityOwner) owner = new SelectMgr_EntityOwner(this, 5);
    Handle(Select3D_SensitivePoint) sens =
        new Select3D_SensitivePoint(owner, m_anchorWorld);
    sens->SetSensitivityFactor(kSensitivity);
    sel->Add(sens);
}

} // namespace aicad::cad
