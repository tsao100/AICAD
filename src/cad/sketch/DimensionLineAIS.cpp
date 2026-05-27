#include "DimensionLineAIS.h"
#include "../Sketch.h"

#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Prs3d_Text.hxx>
#include <Prs3d_TextAspect.hxx>
#include <Prs3d_Drawer.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <Select3D_SensitiveBox.hxx>
#include <Bnd_Box.hxx>
#include <Quantity_Color.hxx>
#include <TCollection_ExtendedString.hxx>
#include <QDebug>
#include <cmath>

IMPLEMENT_STANDARD_RTTIEXT(aicad::cad::AIS_DimensionLine, AIS_InteractiveObject)

namespace aicad::cad {

AIS_DimensionLine::AIS_DimensionLine(
        const SketchConstraint& c,
        const QList<SketchGeometry*>& geoms,
        const gp_Trsf& sketchToWorld,
        SolveStatus status)
    : m_constraint(c)
    , m_geoms(geoms)
    , m_sketchToWorld(sketchToWorld)
    , m_status(status)
{
    SetInfiniteState(Standard_False);
}

void AIS_DimensionLine::Update(
        const SketchConstraint& c,
        const QList<SketchGeometry*>& geoms,
        SolveStatus status)
{
    m_constraint = c;
    m_geoms      = geoms;
    m_status     = status;
}

QString AIS_DimensionLine::labelText() const {
    if (!m_constraint.paramExpr.isEmpty())
        return QString("%1 = %2")
               .arg(m_constraint.paramExpr)
               .arg(m_constraint.value, 0, 'f', 2);
    return QString::number(m_constraint.value, 'f', 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// 輔助：從約束的幾何參考取得兩個世界座標點
// ─────────────────────────────────────────────────────────────────────────────

bool AIS_DimensionLine::getRefPoints(gp_Pnt& p1, gp_Pnt& p2) const {
    // 從 m_geoms 取幾何的中心點作為端點
    if (m_geoms.isEmpty()) return false;

    auto geomCenter = [](const SketchGeometry* g) -> QVector2D {
        if (!g || g->points.isEmpty()) return {};
        QVector2D c(0, 0);
        for (const QVector2D& p : g->points) c += p;
        return c / static_cast<float>(g->points.size());
    };

    QVector2D c1 = geomCenter(m_geoms.value(0));
    QVector2D c2 = m_geoms.size() > 1 ? geomCenter(m_geoms.value(1)) : c1 + QVector2D(m_constraint.value, 0);

    gp_Pnt pt1(c1.x(), c1.y(), 0.0);
    gp_Pnt pt2(c2.x(), c2.y(), 0.0);
    pt1.Transform(m_sketchToWorld);
    pt2.Transform(m_sketchToWorld);
    p1 = pt1;
    p2 = pt2;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 顏色
// ─────────────────────────────────────────────────────────────────────────────

static Quantity_Color dimColor(bool driving, SolveStatus s) {
    if (!driving) return Quantity_Color(Quantity_NOC_GRAY60);
    switch (s) {
    case SolveStatus::FullyConstrained:  return Quantity_Color(Quantity_NOC_GREEN4);
    case SolveStatus::UnderConstrained:  return Quantity_Color(Quantity_NOC_CYAN2);
    case SolveStatus::OverConstrained:   return Quantity_Color(Quantity_NOC_RED);
    default:                             return Quantity_Color(Quantity_NOC_YELLOW);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute — 根據約束類型繪製尺寸線
// ─────────────────────────────────────────────────────────────────────────────

void AIS_DimensionLine::Compute(
        const Handle(PrsMgr_PresentationManager)&,
        const Handle(Prs3d_Presentation)& prs,
        const Standard_Integer /*mode*/)
{
    prs->Clear();

    switch (m_constraint.type) {
    case ConstraintType::FixedRadius:
        drawRadiusDimension(prs);  break;
    case ConstraintType::FixedX:
        drawHorizontalDim(prs);    break;
    case ConstraintType::FixedY:
        drawVerticalDim(prs);      break;
    case ConstraintType::FixedAngleDim:
    case ConstraintType::FixedAngle:
        drawAngleDim(prs);         break;
    case ConstraintType::FixedDistance:
    default:
        drawLinearDimension(prs);  break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 尺寸線繪製輔助 lambda
// ─────────────────────────────────────────────────────────────────────────────

static void addArrow(const Handle(Prs3d_Presentation)& prs,
                     const gp_Pnt& tip, const gp_Vec& dir,
                     const Quantity_Color& col)
{
    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Handle(Graphic3d_AspectLine3d) asp =
        new Graphic3d_AspectLine3d(col, Aspect_TOL_SOLID, 1.0f);
    grp->SetPrimitivesAspect(asp);

    gp_Vec side = dir.Crossed(gp_Vec(0, 0, 1));
    side.Normalize();
    double aLen = 2.0, aWid = 0.8;

    gp_Pnt base = tip.Translated(dir * aLen);
    gp_Pnt l    = base.Translated(side *  aWid);
    gp_Pnt r    = base.Translated(side * -aWid);

    Handle(Graphic3d_ArrayOfPolylines) seg =
        new Graphic3d_ArrayOfPolylines(4, 2);
    seg->AddBound(3);
    seg->AddVertex(l); seg->AddVertex(tip); seg->AddVertex(r);
    seg->AddBound(1);
    seg->AddVertex(base);
    grp->AddPrimitiveArray(seg);
}

static void addDimLine(const Handle(Prs3d_Presentation)& prs,
                       const gp_Pnt& p1, const gp_Pnt& p2,
                       double offset,
                       const Quantity_Color& col,
                       const QString& label)
{
    gp_Vec up(0, 0, 1);
    gp_Vec along(p2.X() - p1.X(), p2.Y() - p1.Y(), p2.Z() - p1.Z());
    if (along.Magnitude() < Precision::Confusion()) return;
    along.Normalize();

    gp_Vec perp = along.Crossed(up);
    if (perp.Magnitude() < Precision::Confusion())
        perp = gp_Vec(0, 1, 0);
    perp.Normalize();

    gp_Pnt d1 = p1.Translated(perp * offset);
    gp_Pnt d2 = p2.Translated(perp * offset);

    // 主尺寸線
    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Handle(Graphic3d_AspectLine3d) asp =
        new Graphic3d_AspectLine3d(col, Aspect_TOL_SOLID, 1.5f);
    grp->SetPrimitivesAspect(asp);

    Handle(Graphic3d_ArrayOfPolylines) line =
        new Graphic3d_ArrayOfPolylines(6, 3);
    // 延伸線 p1→d1
    line->AddBound(2); line->AddVertex(p1); line->AddVertex(d1);
    // 延伸線 p2→d2
    line->AddBound(2); line->AddVertex(p2); line->AddVertex(d2);
    // 尺寸線 d1→d2
    line->AddBound(2); line->AddVertex(d1); line->AddVertex(d2);
    grp->AddPrimitiveArray(line);

    // 箭頭
    addArrow(prs, d1, along,       col);
    addArrow(prs, d2, along * -1., col);

    // 標籤文字
    if (!label.isEmpty()) {
        gp_Pnt mid(
            (d1.X() + d2.X()) * 0.5,
            (d1.Y() + d2.Y()) * 0.5 + 1.5,
            (d1.Z() + d2.Z()) * 0.5);

        // Use Prs3d_TextAspect — matches the API used by AIS_ExtrudeManipulator
        Handle(Prs3d_TextAspect) ta = new Prs3d_TextAspect();
        ta->SetColor(col);
        ta->SetHeight(12.0);
        ta->Aspect()->SetFont("Courier");
        TCollection_ExtendedString txt(label.toUtf8().constData(), Standard_True);
        Prs3d_Text::Draw(prs->NewGroup(), ta, txt, mid);
    }
}

void AIS_DimensionLine::drawLinearDimension(const Handle(Prs3d_Presentation)& prs) {
    gp_Pnt p1, p2;
    if (!getRefPoints(p1, p2)) return;
    addDimLine(prs, p1, p2, m_offsetDist,
               dimColor(m_constraint.driving, m_status),
               labelText());
}

void AIS_DimensionLine::drawHorizontalDim(const Handle(Prs3d_Presentation)& prs) {
    gp_Pnt p1, p2;
    if (!getRefPoints(p1, p2)) return;
    p2 = gp_Pnt(p1.X() + m_constraint.value, p1.Y(), p1.Z());
    addDimLine(prs, p1, p2, m_offsetDist,
               dimColor(m_constraint.driving, m_status),
               labelText());
}

void AIS_DimensionLine::drawVerticalDim(const Handle(Prs3d_Presentation)& prs) {
    gp_Pnt p1, p2;
    if (!getRefPoints(p1, p2)) return;
    p2 = gp_Pnt(p1.X(), p1.Y() + m_constraint.value, p1.Z());
    addDimLine(prs, p1, p2, m_offsetDist,
               dimColor(m_constraint.driving, m_status),
               labelText());
}

void AIS_DimensionLine::drawRadiusDimension(const Handle(Prs3d_Presentation)& prs) {
    gp_Pnt p1, p2;
    if (!getRefPoints(p1, p2)) return;
    // 半徑：從圓心到邊上的一條線
    gp_Pnt edge(p1.X() + m_constraint.value, p1.Y(), p1.Z());
    addDimLine(prs, p1, edge, 0.0,
               dimColor(m_constraint.driving, m_status),
               "R " + labelText());
}

void AIS_DimensionLine::drawAngleDim(const Handle(Prs3d_Presentation)& prs) {
    // 角度：簡化為標籤顯示
    gp_Pnt p1, p2;
    if (!getRefPoints(p1, p2)) return;
    addDimLine(prs, p1, p2, m_offsetDist,
               dimColor(m_constraint.driving, m_status),
               labelText() + "°");
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeSelection — 點擊尺寸線可選取以觸發編輯
// ─────────────────────────────────────────────────────────────────────────────

void AIS_DimensionLine::ComputeSelection(
        const Handle(SelectMgr_Selection)& sel,
        const Standard_Integer /*mode*/)
{
    gp_Pnt p1, p2;
    if (!getRefPoints(p1, p2)) return;

    Bnd_Box box;
    box.Add(p1);
    box.Add(p2);
    box.Enlarge(m_offsetDist + 5.0);

    Handle(SelectMgr_EntityOwner) owner =
        new SelectMgr_EntityOwner(this, 5);
    Handle(Select3D_SensitiveBox) sens =
        new Select3D_SensitiveBox(owner, box);
    sel->Add(sens);
}

} // namespace aicad::cad
