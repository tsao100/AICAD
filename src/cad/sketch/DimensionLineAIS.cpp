#include "DimensionLineAIS.h"
#include "AnnotationTextFormatter.h"
#include "../Sketch.h"

#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_Text.hxx>
#include <Graphic3d_AspectText3d.hxx>
#include <Prs3d_Text.hxx>
#include <Prs3d_TextAspect.hxx>
#include <Prs3d_Drawer.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <Select3D_SensitivePoint.hxx>
#include <Quantity_Color.hxx>
#include <TCollection_ExtendedString.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec2d.hxx>
#include <QDebug>
#include <cmath>

IMPLEMENT_STANDARD_RTTIEXT(aicad::cad::AIS_DimensionLine, AIS_InteractiveObject)

namespace aicad::cad {

// ─────────────────────────────────────────────────────────────────────────────
// DimLabelOwner — 數值標籤專用的 EntityOwner
//
// 預設 OCCT 行為（SelectMgr_EntityOwner::HilightWithColor）在 hover 時會把整個
// AIS 物件（尺寸線、延伸線、箭頭、文字全部）依目前顯示模式重新著色，這正是
// 「尺寸線／延伸線也會被高亮」的成因；同時若 sensitive 區域跟實際文字位置/
// 大小對不上，滑鼠在數值附近移動也偵測不到，造成「完全不會高亮」。
//
// 這裡改為完全自訂 hover 高亮：只重繪這一個數值標籤的文字（hilightLabel），
// 尺寸線本體與延伸線、箭頭一律不參與。
// ─────────────────────────────────────────────────────────────────────────────
class DimLabelOwner;
DEFINE_STANDARD_HANDLE(DimLabelOwner, SelectMgr_EntityOwner)

class DimLabelOwner : public SelectMgr_EntityOwner {
    DEFINE_STANDARD_RTTIEXT(DimLabelOwner, SelectMgr_EntityOwner)
public:
    DimLabelOwner(const Handle(SelectMgr_SelectableObject)& theSel,
                  AIS_DimensionLine* theDim, int theIndex)
        : SelectMgr_EntityOwner(theSel, 5)
        , m_dim(theDim)
        , m_index(theIndex)
    {}

    int LabelIndex() const { return m_index; }

    void HilightWithColor(const Handle(PrsMgr_PresentationManager)& thePM,
                           const Handle(Prs3d_Drawer)& theStyle,
                           const Standard_Integer /*theMode*/) Standard_OVERRIDE
    {
        if (m_dim) m_dim->hilightLabel(thePM, theStyle, m_index);
    }

private:
    AIS_DimensionLine* m_dim;
    int                m_index;
};

}

IMPLEMENT_STANDARD_RTTIEXT(aicad::cad::DimLabelOwner, SelectMgr_EntityOwner)

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
    m_hasRefPos  = false;  // 清除舊的端點快取，待呼叫端重新設定
    m_hasAnnotation = false;  // GDIM v2 Phase 3：清除舊的標註掛載，待呼叫端重新 setAnnotation()
}

QString AIS_DimensionLine::labelText() const {
    const double v = m_constraint.value;

    // GDIM v2 Phase 3：若有掛載 SketchAnnotation，改用共用的
    // AnnotationTextFormatter 組字（套用 prefix/suffix/tolerance/precision/
    // isBasic/isInspection）。目前僅取 mainText 單行顯示；Limit/Deviation
    // 模式的雙行堆疊與 Basic 外框，留待 Compute() 真正拆分為
    // AnnotationAIS 家族時再繪製（此處先確保「文字內容」本身是對的）。
    if (m_hasAnnotation) {
        auto label = AnnotationTextFormatter::format(m_annotation, v);
        return label.mainText;
    }

    QString base;
    switch (m_constraint.type) {
    case ConstraintType::FixedDiameter:
        base = QString("Ø%1").arg(v, 0, 'f', 2);
        break;
    case ConstraintType::CoordinateDim:
        // drawCoordinateDimension 各自畫 X/Y 標籤；此處回傳 X 標籤
        base = QString("X=%1").arg(v, 0, 'f', 2);
        break;
    default:
        base = QString::number(v, 'f', 2);
        break;
    }
    // 需求：尺寸約束只顯示計算結果（值），不顯示公式本身。
    // 公式仍保留在 m_constraint.paramExpr 中，雙擊編輯時會帶出公式供使用者修改。
    return base;
}

// ─────────────────────────────────────────────────────────────────────────────
// 輔助：從約束的幾何參考取得兩個世界座標點
// ─────────────────────────────────────────────────────────────────────────────

void AIS_DimensionLine::setRefPositions(const QVector2D& p1, const QVector2D& p2)
{
    m_hasRefPos = true;
    m_refPos1   = p1;
    m_refPos2   = p2;
}

bool AIS_DimensionLine::getRefPoints(gp_Pnt& p1, gp_Pnt& p2) const {
    // 若已明確設定端點位置（例如 FixedDistance 端點 handle），優先使用
    if (m_hasRefPos) {
        gp_Pnt pt1(m_refPos1.x(), m_refPos1.y(), 0.0);
        gp_Pnt pt2(m_refPos2.x(), m_refPos2.y(), 0.0);
        pt1.Transform(m_sketchToWorld);
        pt2.Transform(m_sketchToWorld);
        p1 = pt1; p2 = pt2;
        return true;
    }
    // 從 m_geoms 取幾何的中心點作為端點
    if (m_geoms.isEmpty()) return false;

    auto geomCenter = [](const SketchGeometry* g) -> QVector2D {
        if (!g || g->points.isEmpty()) return {};
        QVector2D c(0, 0);
        for (const QVector2D& p : g->points) c += p;
        return c / static_cast<float>(g->points.size());
    };

    QVector2D c1 = geomCenter(m_geoms.value(0));
    QVector2D c2;

    if (m_geoms.size() > 1) {
        c2 = geomCenter(m_geoms.value(1));
    } else {
        // 單 ref：依類型合成第二點
        switch (m_constraint.type) {
        case ConstraintType::FixedX:
            // 水平引線：從點 → (value, pt.y)
            c2 = QVector2D(static_cast<float>(m_constraint.value), c1.y());
            break;
        case ConstraintType::FixedY:
            // 垂直引線：從點 → (pt.x, value)
            c2 = QVector2D(c1.x(), static_cast<float>(m_constraint.value));
            break;
        case ConstraintType::CoordinateDim:
            // 水平 X 引線
            c2 = QVector2D(static_cast<float>(m_constraint.value), c1.y());
            break;
        default:
            c2 = c1 + QVector2D(static_cast<float>(m_constraint.value), 0);
            break;
        }
    }

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
    m_labelRegions.clear();

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
    case ConstraintType::FixedLength:
        drawLengthDimension(prs);  break;
    case ConstraintType::FixedDiameter:
        drawDiameterDimension(prs); break;
    case ConstraintType::FixedHorizDist:
        drawHorizontalDim(prs);    break;  // 複用既有
    case ConstraintType::FixedVertDist:
        drawVerticalDim(prs);      break;  // 複用既有
    case ConstraintType::FixedArcLength:
        drawArcLengthDimension(prs); break;
    case ConstraintType::CoordinateDim:
        drawCoordinateDimension(prs); break;
    case ConstraintType::FixedDistance:
    default:
        drawLinearDimension(prs);  break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 尺寸線繪製輔助 lambda
// ─────────────────────────────────────────────────────────────────────────────

static Handle(Graphic3d_AspectText3d) makeTextAspect(const Quantity_Color& col)
{
    Handle(Graphic3d_AspectText3d) asp = new Graphic3d_AspectText3d();
    asp->SetColor(col);
    return asp;
}

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
                       AIS_DimensionLine* self,
                       const gp_Pnt& p1, const gp_Pnt& p2,
                       double offset,
                       const Quantity_Color& /*col*/,
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

    // 主尺寸線（綠色）
    Quantity_Color lineCol(0.0, 0.8, 0.0, Quantity_TOC_RGB);
    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Handle(Graphic3d_AspectLine3d) asp =
        new Graphic3d_AspectLine3d(lineCol, Aspect_TOL_SOLID, 1.5f);
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

    // 箭頭（綠色）
    addArrow(prs, d1, along,       lineCol);
    addArrow(prs, d2, along * -1., lineCol);

    // 標籤文字（紅色，字高 36，平行尺寸線，居中）
    if (!label.isEmpty()) {
        gp_Pnt mid(
            (d1.X() + d2.X()) * 0.5,
            (d1.Y() + d2.Y()) * 0.5,
            (d1.Z() + d2.Z()) * 0.5);

        // 使用 Graphic3d_Text 支援旋轉（平行尺寸線方向）
        Handle(Graphic3d_Text) gtext = new Graphic3d_Text(36.0f);
        gtext->SetText(TCollection_ExtendedString(label.toUtf8().constData(), Standard_True));
        gtext->SetPosition(mid);
        // 設定文字朝向：X 軸對齊尺寸線方向（along），Z 軸為螢幕外
        gp_Vec d1d2(d1, d2);
        if (d1d2.Magnitude() > Precision::Confusion()) {
            d1d2.Normalize();
            gp_Vec zAxis(0, 0, 1);
            gp_Vec yAxis = zAxis.Crossed(d1d2);
            if (yAxis.Magnitude() > Precision::Confusion()) {
                yAxis.Normalize();
                gtext->SetOrientation(gp_Ax2(mid, gp_Dir(zAxis), gp_Dir(d1d2)));
            }
        }
        gtext->SetHorizontalAlignment(Graphic3d_HTA_CENTER);
        gtext->SetVerticalAlignment(Graphic3d_VTA_CENTER);
        Handle(Graphic3d_Group) txtGrp = prs->NewGroup();
        txtGrp->SetGroupPrimitivesAspect(makeTextAspect(Quantity_Color(Quantity_NOC_RED)));
        txtGrp->AddText(gtext);

        if (self) self->addLabelRegion(mid, d1d2, label);
    }
}

// 明確指定尺寸線端點（d1、d2）的繪製版本，延伸線從 p1→d1、p2→d2
static void addDimLineExplicit(const Handle(Prs3d_Presentation)& prs,
                                AIS_DimensionLine* self,
                                const gp_Pnt& p1, const gp_Pnt& p2,
                                const gp_Pnt& d1, const gp_Pnt& d2,
                                const Quantity_Color& /*col*/,
                                const QString& label)
{
    gp_Vec along(d1, d2);
    if (along.Magnitude() < Precision::Confusion()) return;
    along.Normalize();

    Quantity_Color lineColEx(0.0, 0.8, 0.0, Quantity_TOC_RGB);
    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Handle(Graphic3d_AspectLine3d) asp =
        new Graphic3d_AspectLine3d(lineColEx, Aspect_TOL_SOLID, 1.5f);
    grp->SetPrimitivesAspect(asp);

    Handle(Graphic3d_ArrayOfPolylines) line =
        new Graphic3d_ArrayOfPolylines(6, 3);
    line->AddBound(2); line->AddVertex(p1); line->AddVertex(d1);
    line->AddBound(2); line->AddVertex(p2); line->AddVertex(d2);
    line->AddBound(2); line->AddVertex(d1); line->AddVertex(d2);
    grp->AddPrimitiveArray(line);

    addArrow(prs, d1, along,       lineColEx);
    addArrow(prs, d2, along * -1., lineColEx);

    if (!label.isEmpty()) {
        gp_Pnt mid(
            (d1.X() + d2.X()) * 0.5,
            (d1.Y() + d2.Y()) * 0.5,
            (d1.Z() + d2.Z()) * 0.5);
        Handle(Graphic3d_Text) gtext = new Graphic3d_Text(36.0f);
        gtext->SetText(TCollection_ExtendedString(label.toUtf8().constData(), Standard_True));
        gtext->SetPosition(mid);
        gp_Vec d1d2(d1, d2);
        if (d1d2.Magnitude() > Precision::Confusion()) {
            d1d2.Normalize();
            gp_Vec zAxis(0, 0, 1);
            gtext->SetOrientation(gp_Ax2(mid, gp_Dir(zAxis), gp_Dir(d1d2)));
        }
        gtext->SetHorizontalAlignment(Graphic3d_HTA_CENTER);
        gtext->SetVerticalAlignment(Graphic3d_VTA_CENTER);
        Handle(Graphic3d_Group) txtGrp = prs->NewGroup();
        txtGrp->SetGroupPrimitivesAspect(makeTextAspect(Quantity_Color(Quantity_NOC_RED)));
        txtGrp->AddText(gtext);

        if (self) self->addLabelRegion(mid, d1d2, label);
    }
}

// 使用者拖曳偏移向量版本（世界座標偏移 gp_Vec）
static void addDimLineWithOffset(const Handle(Prs3d_Presentation)& prs,
                                  AIS_DimensionLine* self,
                                  const gp_Pnt& p1, const gp_Pnt& p2,
                                  const gp_Vec& offsetVec,
                                  const Quantity_Color& col,
                                  const QString& label)
{
    // 延伸線必須垂直於 p1p2 連線：將 offsetVec 投影到 p1p2 的法向量上
    gp_Vec along(p1, p2);
    if (along.Magnitude() < Precision::Confusion()) {
        gp_Pnt d1 = p1.Translated(offsetVec);
        gp_Pnt d2 = p2.Translated(offsetVec);
        addDimLineExplicit(prs, self, p1, p2, d1, d2, col, label);
        return;
    }
    along.Normalize();
    gp_Vec up(0, 0, 1);
    gp_Vec perp = along.Crossed(up);
    perp.Normalize();
    double projDist = offsetVec.Dot(perp);
    if (std::abs(projDist) < Precision::Confusion()) projDist = 8.0;
    gp_Pnt d1 = p1.Translated(perp * projDist);
    gp_Pnt d2 = p2.Translated(perp * projDist);
    addDimLineExplicit(prs, self, p1, p2, d1, d2, col, label);
}

// 垂足小方框符號
void AIS_DimensionLine::drawPerpendicularSymbol(
    const Handle(Prs3d_Presentation)& prs,
    const gp_Pnt& foot,
    const gp_Pnt& fromPt,
    const gp_Pnt& /*onLinePt*/,
    double symSize)
{
    // 方向：foot→fromPt（垂距方向），法向（沿線方向）
    gp_Vec toFrom(foot, fromPt);
    if (toFrom.Magnitude() < Precision::Confusion()) return;
    toFrom.Normalize();

    // 沿線方向 = toFrom × Z
    gp_Vec along = toFrom.Crossed(gp_Vec(0, 0, 1));
    if (along.Magnitude() < Precision::Confusion()) along = gp_Vec(1, 0, 0);
    along.Normalize();

    // 四個角點
    gp_Pnt c1 = foot.Translated(toFrom * symSize);
    gp_Pnt c2 = c1.Translated(along * symSize);
    gp_Pnt c3 = foot.Translated(along * symSize);

    Handle(Graphic3d_ArrayOfPolylines) sq = new Graphic3d_ArrayOfPolylines(4, 1);
    sq->AddBound(4);
    sq->AddVertex(foot);
    sq->AddVertex(c1);
    sq->AddVertex(c2);
    sq->AddVertex(c3);

    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Quantity_Color col(0.0, 0.8, 0.0, Quantity_TOC_RGB);
    Handle(Graphic3d_AspectLine3d) asp =
        new Graphic3d_AspectLine3d(col, Aspect_TOL_SOLID, 1.5f);
    grp->SetPrimitivesAspect(asp);
    grp->AddPrimitiveArray(sq);
}

void AIS_DimensionLine::drawLinearDimension(const Handle(Prs3d_Presentation)& prs) {
    gp_Pnt p1, p2;
    if (!getRefPoints(p1, p2)) return;

    // ── PointToLine：p2 改為點到線的投影點，並畫垂足符號 ───────────────────
    if (m_constraint.distMode == DistanceMode::PointToLine
        && m_geoms.size() >= 2)
    {
        auto* ln = dynamic_cast<const SketchLine*>(m_geoms[1]);
        if (ln) {
            // 在草圖平面計算投影點，再轉世界座標
            gp_Vec2d AB(ln->end.x() - ln->start.x(),
                        ln->end.y() - ln->start.y());
            double len2 = AB.X()*AB.X() + AB.Y()*AB.Y();
            // p1 的草圖座標（反變換）
            gp_Pnt p1sk = p1;
            gp_Trsf inv = m_sketchToWorld.Inverted();
            p1sk.Transform(inv);
            gp_Vec2d AP(p1sk.X() - ln->start.x(), p1sk.Y() - ln->start.y());
            double t = (len2 > 1e-10) ? (AP.X()*AB.X() + AP.Y()*AB.Y()) / len2 : 0.0;
            gp_Pnt p2sk(ln->start.x() + t * AB.X(),
                         ln->start.y() + t * AB.Y(), 0.0);
            p2sk.Transform(m_sketchToWorld);
            p2 = p2sk;

            // 垂足小方框（邊長 = 3mm）
            drawPerpendicularSymbol(prs, p2, p1, p2);
        }
    }

    // ── LineToLine：p1=線A起點, p2=線A起點投影到線B ─────────────────────────
    if (m_constraint.distMode == DistanceMode::LineToLine
        && m_geoms.size() >= 2)
    {
        auto* lnA = dynamic_cast<const SketchLine*>(m_geoms[0]);
        auto* lnB = dynamic_cast<const SketchLine*>(m_geoms[1]);
        if (lnA && lnB) {
            gp_Vec2d AB(lnB->end.x() - lnB->start.x(),
                        lnB->end.y() - lnB->start.y());
            double len2 = AB.X()*AB.X() + AB.Y()*AB.Y();
            gp_Vec2d AP(lnA->start.x() - lnB->start.x(),
                        lnA->start.y() - lnB->start.y());
            double t = (len2 > 1e-10) ? (AP.X()*AB.X() + AP.Y()*AB.Y()) / len2 : 0.0;
            gp_Pnt p1sk(lnA->start.x(), lnA->start.y(), 0.0);
            gp_Pnt p2sk(lnB->start.x() + t * AB.X(),
                         lnB->start.y() + t * AB.Y(), 0.0);
            p1sk.Transform(m_sketchToWorld);
            p2sk.Transform(m_sketchToWorld);
            p1 = p1sk;
            p2 = p2sk;
            drawPerpendicularSymbol(prs, p2, p1, p2);
        }
    }

    // 若使用者已拖曳設定偏移，用 XY 偏移向量；否則用預設垂直偏移
    double offDist = m_offsetDist;
    if (m_dimOffsetX != 0.0 || m_dimOffsetY != 0.0) {
        gp_Pnt op(m_dimOffsetX, m_dimOffsetY, 0.0);
        op.Transform(m_sketchToWorld);
        gp_Pnt orig(0.0, 0.0, 0.0);
        orig.Transform(m_sketchToWorld);
        gp_Vec offsetVec(orig, op);
        addDimLineWithOffset(prs, this, p1, p2, offsetVec,
                             dimColor(m_constraint.driving, m_status),
                             labelText());
        return;
    }
    addDimLine(prs, this, p1, p2, offDist,
               dimColor(m_constraint.driving, m_status),
               labelText());
}

void AIS_DimensionLine::drawHorizontalDim(const Handle(Prs3d_Presentation)& prs) {
    gp_Pnt p1, p2;
    if (!getRefPoints(p1, p2)) return;

    // 水平距離：尺寸線平行草圖 X 軸（草圖 Y 固定）
    // m_dimOffsetY：尺寸線草圖 Y 相對 AB 中點 Y 的偏移（與 overlay 同基準）
    double rawOffset = (m_dimOffsetY != 0.0) ? m_dimOffsetY : m_offsetDist;

    // 草圖座標系基向量（世界座標）
    gp_Pnt skO(0.0, 0.0, 0.0); skO.Transform(m_sketchToWorld);
    gp_Pnt skX(1.0, 0.0, 0.0); skX.Transform(m_sketchToWorld);
    gp_Pnt skY(0.0, 1.0, 0.0); skY.Transform(m_sketchToWorld);
    gp_Vec xAxis(skO, skX);
    gp_Vec yAxis(skO, skY);

    // 反求 p1/p2 的草圖座標
    gp_Vec v1(skO, p1), v2(skO, p2);
    double p1x = v1.Dot(xAxis), p1y = v1.Dot(yAxis);
    double p2x = v2.Dot(xAxis), p2y = v2.Dot(yAxis);

    // 尺寸線 Y = AB 中點 Y + rawOffset（與 CadView/overlay 的 offset 基準一致）
    double abMidY = (p1y + p2y) * 0.5;
    double dimY   = abMidY + rawOffset;

    gp_Pnt d1(skO.XYZ() + xAxis.XYZ() * p1x + yAxis.XYZ() * dimY);
    gp_Pnt d2(skO.XYZ() + xAxis.XYZ() * p2x + yAxis.XYZ() * dimY);
    // 延伸線：p1→d1 長 |p1y - dimY|，p2→d2 長 |p2y - dimY|（兩條不等長）

    addDimLineExplicit(prs, this, p1, p2, d1, d2,
                       dimColor(m_constraint.driving, m_status),
                       labelText());
}

void AIS_DimensionLine::drawVerticalDim(const Handle(Prs3d_Presentation)& prs) {
    gp_Pnt p1, p2;
    if (!getRefPoints(p1, p2)) return;

    // 垂直距離：尺寸線平行草圖 Y 軸（草圖 X 固定）
    double rawOffset = (m_dimOffsetX != 0.0) ? m_dimOffsetX : m_offsetDist;

    gp_Pnt skO(0.0, 0.0, 0.0); skO.Transform(m_sketchToWorld);
    gp_Pnt skX(1.0, 0.0, 0.0); skX.Transform(m_sketchToWorld);
    gp_Pnt skY(0.0, 1.0, 0.0); skY.Transform(m_sketchToWorld);
    gp_Vec xAxis(skO, skX);
    gp_Vec yAxis(skO, skY);

    gp_Vec v1(skO, p1), v2(skO, p2);
    double p1x = v1.Dot(xAxis), p1y = v1.Dot(yAxis);
    double p2x = v2.Dot(xAxis), p2y = v2.Dot(yAxis);

    // 尺寸線 X = AB 中點 X + rawOffset（與 CadView/overlay 的 offset 基準一致）
    double abMidX = (p1x + p2x) * 0.5;
    double dimX   = abMidX + rawOffset;

    gp_Pnt d1(skO.XYZ() + xAxis.XYZ() * dimX + yAxis.XYZ() * p1y);
    gp_Pnt d2(skO.XYZ() + xAxis.XYZ() * dimX + yAxis.XYZ() * p2y);
    // 延伸線：p1→d1 長 |p1x - dimX|，p2→d2 長 |p2x - dimX|（兩條不等長）

    addDimLineExplicit(prs, this, p1, p2, d1, d2,
                       dimColor(m_constraint.driving, m_status),
                       labelText());
}

void AIS_DimensionLine::drawRadiusDimension(const Handle(Prs3d_Presentation)& prs) {
    // 半徑：從圓心到圓周（方向由 m_dimOffsetX/Y 決定）
    // 支援 SketchCircle 和 SketchArc
    if (m_geoms.isEmpty()) return;

    QVector2D center2D;
    double r = m_constraint.value;

    if (auto* circ = dynamic_cast<const SketchCircle*>(m_geoms[0])) {
        center2D = circ->center;
    } else if (auto* arc = dynamic_cast<const SketchArc*>(m_geoms[0])) {
        // SketchArc: points[2] = center（若有），否則從 GeomRef 取
        if (arc->points.size() >= 3)
            center2D = arc->points[2];
        else if (!m_constraint.refs.isEmpty())
            center2D = GeomRef(m_constraint.refs[0].geomUuid,
                               GeomHandle::Center)
                       .resolvePosition(nullptr);  // fallback = (0,0)
    } else {
        return;
    }

    // 方向（草圖平面內），優先用 offset，否則草圖 X 軸
    gp_Vec sk_dir(1.0, 0.0, 0.0);
    if (m_dimOffsetX != 0.0 || m_dimOffsetY != 0.0) {
        gp_Vec off(m_dimOffsetX, m_dimOffsetY, 0.0);
        if (off.Magnitude() > Precision::Confusion())
            sk_dir = off.Normalized();
    }

    gp_Pnt sk_ctr(center2D.x(), center2D.y(), 0.0);
    gp_Pnt sk_edge(sk_ctr.X() + sk_dir.X() * r,
                   sk_ctr.Y() + sk_dir.Y() * r, 0.0);
    sk_ctr.Transform(m_sketchToWorld);
    sk_edge.Transform(m_sketchToWorld);

    addDimLine(prs, this, sk_ctr, sk_edge, 0.0,
               dimColor(m_constraint.driving, m_status),
               "R " + labelText());
}

void AIS_DimensionLine::drawAngleDim(const Handle(Prs3d_Presentation)& prs) {
    // 角度尺寸：兩條線夾角，繪製角弧＋引線＋標籤
    // refs[0] = 線A, refs[1] = 線B
    if (m_geoms.size() < 2) return;
    auto* geomA = dynamic_cast<const SketchLine*>(m_geoms[0]);
    auto* geomB = dynamic_cast<const SketchLine*>(m_geoms[1]);
    if (!geomA || !geomB) {
        // fallback
        gp_Pnt p1, p2;
        if (!getRefPoints(p1, p2)) return;
        addDimLine(prs, this, p1, p2, m_offsetDist,
                   dimColor(m_constraint.driving, m_status),
                   labelText() + "°");
        return;
    }

    // 求兩線交點（草圖平面）
    // A: Pa + t*Da, B: Pb + s*Db
    gp_Pnt2d pA(geomA->start.x(), geomA->start.y());
    gp_Vec2d dA(geomA->end.x() - geomA->start.x(),
                geomA->end.y() - geomA->start.y());
    gp_Pnt2d pB(geomB->start.x(), geomB->start.y());
    gp_Vec2d dB(geomB->end.x() - geomB->start.x(),
                geomB->end.y() - geomB->start.y());

    double cross = dA.X() * dB.Y() - dA.Y() * dB.X();
    gp_Pnt2d apex;
    if (std::abs(cross) < 1e-8) {
        // 平行：使用 A 中點
        apex = gp_Pnt2d((geomA->start.x() + geomA->end.x()) * 0.5,
                        (geomA->start.y() + geomA->end.y()) * 0.5);
    } else {
        gp_Vec2d ab(pA, pB);
        double t = (ab.X() * dB.Y() - ab.Y() * dB.X()) / cross;
        apex = gp_Pnt2d(pA.X() + t * dA.X(), pA.Y() + t * dA.Y());
    }

    // 角弧半徑：由 offset 決定，若無 offset 則用 m_offsetDist
    double arcR = m_offsetDist;
    if (m_dimOffsetX != 0.0 || m_dimOffsetY != 0.0) {
        double ox = m_dimOffsetX, oy = m_dimOffsetY;
        arcR = std::max(5.0, std::sqrt(ox*ox + oy*oy));
    }

    // 各線方向角（0..2π）
    double angA = std::atan2(dA.Y(), dA.X());
    double angB = std::atan2(dB.Y(), dB.X());

    // 夾角中間方向（用於放標籤）
    double midAng = (angA + angB) * 0.5;
    // 確保標籤方向和角弧在同側
    if (std::abs(angB - angA) > M_PI) midAng += M_PI;

    // 角弧（離散折線逼近）
    double a0 = angA, a1 = angB;
    // 讓 a1 > a0
    while (a1 < a0) a1 += 2 * M_PI;
    if (a1 - a0 > M_PI) { double tmp = a0; a0 = a1 - 2*M_PI; a1 = tmp + 2*M_PI; std::swap(a0,a1); a0 -= 2*M_PI; a1 -= 2*M_PI; while(a1<a0) a1+=2*M_PI; }

    Quantity_Color lineCol(0.0, 0.8, 0.0, Quantity_TOC_RGB);

    // 角弧（20段折線）
    {
        int nSeg = 20;
        Handle(Graphic3d_ArrayOfPolylines) arc =
            new Graphic3d_ArrayOfPolylines(nSeg + 1, 1);
        arc->AddBound(nSeg + 1);
        for (int i = 0; i <= nSeg; ++i) {
            double a = a0 + (a1 - a0) * i / nSeg;
            gp_Pnt sk(apex.X() + arcR * std::cos(a),
                      apex.Y() + arcR * std::sin(a), 0.0);
            sk.Transform(m_sketchToWorld);
            arc->AddVertex(sk);
        }
        Handle(Graphic3d_Group) grp = prs->NewGroup();
        Handle(Graphic3d_AspectLine3d) asp =
            new Graphic3d_AspectLine3d(lineCol, Aspect_TOL_SOLID, 1.5f);
        grp->SetPrimitivesAspect(asp);
        grp->AddPrimitiveArray(arc);

        // 兩條短引線：apex→弧起/終
        double extLen = arcR * 0.3;
        auto addRefLine = [&](double ang) {
            gp_Pnt inner(apex.X() + (arcR - extLen) * std::cos(ang),
                         apex.Y() + (arcR - extLen) * std::sin(ang), 0.0);
            gp_Pnt outer(apex.X() + (arcR + extLen) * std::cos(ang),
                         apex.Y() + (arcR + extLen) * std::sin(ang), 0.0);
            inner.Transform(m_sketchToWorld);
            outer.Transform(m_sketchToWorld);
            Handle(Graphic3d_ArrayOfPolylines) seg = new Graphic3d_ArrayOfPolylines(2, 1);
            seg->AddBound(2);
            seg->AddVertex(inner); seg->AddVertex(outer);
            grp->AddPrimitiveArray(seg);
        };
        addRefLine(a0);
        addRefLine(a1);
    }

    // 標籤（放在角弧中點方向）
    double labelAng = (a0 + a1) * 0.5;
    gp_Pnt sk_label(apex.X() + arcR * std::cos(labelAng),
                    apex.Y() + arcR * std::sin(labelAng), 0.0);
    sk_label.Transform(m_sketchToWorld);

    // 角度轉換：弧度 → 度
    double deg = m_constraint.value * 180.0 / M_PI;
    // 需求：只顯示計算結果（角度值），不顯示公式本身。
    QString lbl = QString::number(deg, 'f', 2) + "°";

    Handle(Graphic3d_Text) gtext = new Graphic3d_Text(36.0f);
    gtext->SetText(TCollection_ExtendedString(lbl.toUtf8().constData(), Standard_True));
    gtext->SetPosition(sk_label);
    // 文字朝向：沿角弧切線方向（與半徑方向垂直），讓 hover 區域與實際渲染對齊
    gp_Vec tangentSk(-std::sin(labelAng), std::cos(labelAng), 0.0);
    gp_Vec tangentW = tangentSk;
    tangentW.Transform(m_sketchToWorld);
    if (tangentW.Magnitude() > Precision::Confusion()) {
        gp_Vec dirW = tangentW.Normalized();
        gtext->SetOrientation(gp_Ax2(sk_label, gp_Dir(0, 0, 1), gp_Dir(dirW)));
    }
    gtext->SetHorizontalAlignment(Graphic3d_HTA_CENTER);
    gtext->SetVerticalAlignment(Graphic3d_VTA_CENTER);
    Handle(Graphic3d_Group) txtGrp = prs->NewGroup();
    txtGrp->SetGroupPrimitivesAspect(makeTextAspect(Quantity_Color(Quantity_NOC_RED)));
    txtGrp->AddText(gtext);

    addLabelRegion(sk_label, tangentW, lbl);
}

// ── General Dimension 新增繪製函式 ────────────────────────────────────────

void AIS_DimensionLine::drawLengthDimension(const Handle(Prs3d_Presentation)& prs) {
    // 線段長度：端點必定取 refs[0] 的 Start/End（不依中心計算）
    if (m_geoms.isEmpty()) return;
    auto* g = m_geoms[0];
    if (!g || g->points.size() < 2) {
        drawLinearDimension(prs);
        return;
    }
    gp_Pnt p1(g->points[0].x(), g->points[0].y(), 0.0);
    gp_Pnt p2(g->points[1].x(), g->points[1].y(), 0.0);
    p1.Transform(m_sketchToWorld);
    p2.Transform(m_sketchToWorld);

    // ★ 與 drawLinearDimension 相同：優先使用使用者點擊確認的偏移（m_dimOffsetX/Y）
    if (m_dimOffsetX != 0.0 || m_dimOffsetY != 0.0) {
        gp_Pnt op(m_dimOffsetX, m_dimOffsetY, 0.0);
        op.Transform(m_sketchToWorld);
        gp_Pnt orig(0.0, 0.0, 0.0);
        orig.Transform(m_sketchToWorld);
        gp_Vec offsetVec(orig, op);
        addDimLineWithOffset(prs, this, p1, p2, offsetVec,
                             dimColor(m_constraint.driving, m_status),
                             labelText());
        return;
    }
    addDimLine(prs, this, p1, p2, m_offsetDist,
               dimColor(m_constraint.driving, m_status),
               labelText());
}

void AIS_DimensionLine::drawDiameterDimension(const Handle(Prs3d_Presentation)& prs) {
    // m_dimOffsetX/Y = commit 時 (mouse - circCenter)（草圖座標）
    // 幾何（與 rubberband DimPreviewOverlay 完全一致）：
    //   dir    = normalize(offset)         ← 延伸線方向（圓心→滑鼠）
    //   perp   = (-dir.y, dir.x)           ← 尺寸線方向（垂直延伸線）
    //   A/B    = center ± perp*r           ← 延伸線起點（圓周上，沿 perp）
    //   dimMid = center + offset           ← 尺寸線中點（= commit 時滑鼠）
    //   dA/dB  = dimMid ± perp*r          ← 尺寸線端點
    //   Style  = addDimLineExplicit（與 FixedDistance 相同）
    if (m_geoms.isEmpty()) return;
    auto* circ = dynamic_cast<const SketchCircle*>(m_geoms[0]);
    if (!circ) return;

    double r = m_constraint.value / 2.0;

    // ── 草圖座標計算 ─────────────────────────────────────────────────────────
    gp_Vec2d sk_off(m_dimOffsetX, m_dimOffsetY);
    gp_Vec2d sk_dir(1.0, 0.0);
    if (sk_off.Magnitude() > Precision::Confusion())
        sk_dir = sk_off.Normalized();
    gp_Vec2d sk_perp(-sk_dir.Y(), sk_dir.X());

    gp_Pnt2d sk_ctr2(circ->center.x(), circ->center.y());
    gp_Pnt2d sk_A  (sk_ctr2.X() - sk_perp.X() * r, sk_ctr2.Y() - sk_perp.Y() * r);
    gp_Pnt2d sk_B  (sk_ctr2.X() + sk_perp.X() * r, sk_ctr2.Y() + sk_perp.Y() * r);
    gp_Pnt2d sk_mid(sk_ctr2.X() + sk_off.X(),       sk_ctr2.Y() + sk_off.Y());
    gp_Pnt2d sk_dA (sk_mid.X() - sk_perp.X() * r,   sk_mid.Y() - sk_perp.Y() * r);
    gp_Pnt2d sk_dB (sk_mid.X() + sk_perp.X() * r,   sk_mid.Y() + sk_perp.Y() * r);

    // ── 轉世界座標 ───────────────────────────────────────────────────────────
    auto toW = [&](const gp_Pnt2d& p) {
        gp_Pnt w(p.X(), p.Y(), 0.0);
        w.Transform(m_sketchToWorld);
        return w;
    };
    gp_Pnt wA  = toW(sk_A);  gp_Pnt wB  = toW(sk_B);
    gp_Pnt wdA = toW(sk_dA); gp_Pnt wdB = toW(sk_dB);

    // ── 用 addDimLineExplicit 繪製（與 FixedDistance 完全相同的 style）────────
    Quantity_Color col = dimColor(m_constraint.driving, m_status);
    addDimLineExplicit(prs, this, wA, wB, wdA, wdB, col, labelText());
}

void AIS_DimensionLine::drawArcLengthDimension(const Handle(Prs3d_Presentation)& prs) {
    // 弧長：以同心弧（半徑略大）為尺寸線，標籤加 ~ 前綴
    if (m_geoms.isEmpty()) return;
    auto* arc = dynamic_cast<const SketchArc*>(m_geoms[0]);
    if (!arc || arc->points.size() < 3) {
        // fallback：線性
        gp_Pnt p1, p2;
        if (!getRefPoints(p1, p2)) return;
        addDimLine(prs, this, p1, p2, m_offsetDist,
                   dimColor(m_constraint.driving, m_status),
                   "~" + labelText());
        return;
    }

    QVector2D startPt = arc->points[0];
    QVector2D endPt   = arc->points[1];
    QVector2D ctrPt   = arc->points[2];
    double r = (startPt - ctrPt).length();

    // 同心弧半徑（略大，由 offset 決定偏移距離）
    double extraR = m_offsetDist;
    if (m_dimOffsetX != 0.0 || m_dimOffsetY != 0.0) {
        double ox = m_dimOffsetX, oy = m_dimOffsetY;
        // offset 向外分量
        gp_Vec2d ov(ox, oy);
        if (ov.Magnitude() > 1e-6) extraR = ov.Magnitude();
    }
    double dimR = r + std::abs(extraR);

    double angStart = std::atan2(startPt.y() - ctrPt.y(), startPt.x() - ctrPt.x());
    double angEnd   = std::atan2(endPt.y()   - ctrPt.y(), endPt.x()   - ctrPt.x());
    // 保持 CCW 掃角
    while (angEnd <= angStart) angEnd += 2 * M_PI;
    if (angEnd - angStart > 2 * M_PI) angEnd = angStart + 2 * M_PI;

    Quantity_Color lineCol(0.0, 0.8, 0.0, Quantity_TOC_RGB);

    // 同心弧（30段折線）
    {
        int nSeg = 30;
        Handle(Graphic3d_ArrayOfPolylines) arcLine =
            new Graphic3d_ArrayOfPolylines(nSeg + 1, 1);
        arcLine->AddBound(nSeg + 1);
        for (int i = 0; i <= nSeg; ++i) {
            double a = angStart + (angEnd - angStart) * i / nSeg;
            gp_Pnt sk(ctrPt.x() + dimR * std::cos(a),
                      ctrPt.y() + dimR * std::sin(a), 0.0);
            sk.Transform(m_sketchToWorld);
            arcLine->AddVertex(sk);
        }
        Handle(Graphic3d_Group) grp = prs->NewGroup();
        Handle(Graphic3d_AspectLine3d) asp =
            new Graphic3d_AspectLine3d(lineCol, Aspect_TOL_SOLID, 1.5f);
        grp->SetPrimitivesAspect(asp);
        grp->AddPrimitiveArray(arcLine);

        // 延伸線：原弧端點 → 同心弧端點
        auto addExtLine = [&](double ang) {
            gp_Pnt inner(ctrPt.x() + r    * std::cos(ang),
                         ctrPt.y() + r    * std::sin(ang), 0.0);
            gp_Pnt outer(ctrPt.x() + dimR * std::cos(ang),
                         ctrPt.y() + dimR * std::sin(ang), 0.0);
            inner.Transform(m_sketchToWorld);
            outer.Transform(m_sketchToWorld);
            Handle(Graphic3d_ArrayOfPolylines) seg = new Graphic3d_ArrayOfPolylines(2, 1);
            seg->AddBound(2);
            seg->AddVertex(inner); seg->AddVertex(outer);
            grp->AddPrimitiveArray(seg);
        };
        addExtLine(angStart);
        addExtLine(angEnd);
    }

    // 標籤放在同心弧中點
    double midAng = (angStart + angEnd) * 0.5;
    gp_Pnt sk_label(ctrPt.x() + dimR * std::cos(midAng),
                    ctrPt.y() + dimR * std::sin(midAng), 0.0);
    sk_label.Transform(m_sketchToWorld);

    QString arcLbl = "~" + labelText();
    Handle(Graphic3d_Text) gtext = new Graphic3d_Text(36.0f);
    gtext->SetText(TCollection_ExtendedString(
        arcLbl.toUtf8().constData(), Standard_True));
    gtext->SetPosition(sk_label);
    gp_Vec tangentSk(-std::sin(midAng), std::cos(midAng), 0.0);
    gp_Vec tangentW = tangentSk;
    tangentW.Transform(m_sketchToWorld);
    if (tangentW.Magnitude() > Precision::Confusion()) {
        gp_Vec dirW = tangentW.Normalized();
        gtext->SetOrientation(gp_Ax2(sk_label, gp_Dir(0, 0, 1), gp_Dir(dirW)));
    }
    gtext->SetHorizontalAlignment(Graphic3d_HTA_CENTER);
    gtext->SetVerticalAlignment(Graphic3d_VTA_CENTER);
    Handle(Graphic3d_Group) txtGrp = prs->NewGroup();
    txtGrp->SetGroupPrimitivesAspect(makeTextAspect(Quantity_Color(Quantity_NOC_RED)));
    txtGrp->AddText(gtext);

    addLabelRegion(sk_label, tangentW, arcLbl);
}

void AIS_DimensionLine::drawCoordinateDimension(const Handle(Prs3d_Presentation)& prs) {
    // 座標尺寸：從點畫兩條引線到 X/Y 軸，各自加標籤
    // 需要從 refs[0] 解析點的草圖位置

    // 取點座標（優先 m_hasRefPos，否則用 geoms 的第一個點）
    gp_Pnt2d sk_pt;
    if (m_hasRefPos) {
        sk_pt = gp_Pnt2d(m_refPos1.x(), m_refPos1.y());
    } else if (!m_geoms.isEmpty() && !m_geoms[0]->points.isEmpty()) {
        sk_pt = gp_Pnt2d(m_geoms[0]->points[0].x(), m_geoms[0]->points[0].y());
    } else {
        return;
    }

    // 轉世界座標
    auto toW = [&](double x, double y) {
        gp_Pnt p(x, y, 0.0);
        p.Transform(m_sketchToWorld);
        return p;
    };

    gp_Pnt wPt = toW(sk_pt.X(), sk_pt.Y());

    // X 引線：從點水平到 (value, pt.y)
    gp_Pnt wPx = toW(m_constraint.value, sk_pt.Y());
    // Y 引線：從點垂直到 (pt.x, value2)
    gp_Pnt wPy = toW(sk_pt.X(), m_constraint.value2);

    // X 尺寸線
    {
        Handle(Graphic3d_Group) grp = prs->NewGroup();
        Quantity_Color lineCol(0.0, 0.8, 0.0, Quantity_TOC_RGB);
        Handle(Graphic3d_AspectLine3d) asp =
            new Graphic3d_AspectLine3d(lineCol, Aspect_TOL_SOLID, 1.5f);
        grp->SetPrimitivesAspect(asp);
        Handle(Graphic3d_ArrayOfPolylines) seg = new Graphic3d_ArrayOfPolylines(2, 1);
        seg->AddBound(2);
        seg->AddVertex(wPt); seg->AddVertex(wPx);
        grp->AddPrimitiveArray(seg);

        gp_Pnt midX((wPt.X()+wPx.X())*0.5, (wPt.Y()+wPx.Y())*0.5, wPt.Z());
        Handle(Graphic3d_Text) gt = new Graphic3d_Text(36.0f);
        QString xl = QString("X=%1").arg(m_constraint.value, 0, 'f', 2);
        gt->SetText(TCollection_ExtendedString(xl.toUtf8().constData(), Standard_True));
        gt->SetPosition(midX);
        gt->SetHorizontalAlignment(Graphic3d_HTA_CENTER);
        gt->SetVerticalAlignment(Graphic3d_VTA_BOTTOM);
        Handle(Graphic3d_Group) tg = prs->NewGroup();
        tg->SetGroupPrimitivesAspect(makeTextAspect(Quantity_Color(Quantity_NOC_RED)));
        tg->AddText(gt);

        // billboard 文字（未呼叫 SetOrientation）：hover 區域不需 3D 朝向，
        // 對齊方式須與上方實際繪製一致（CENTER/BOTTOM），確保高亮文字與原文字重疊
        addLabelRegion(midX, gp_Vec(wPt, wPx), xl,
                       /*oriented=*/false, Graphic3d_HTA_CENTER, Graphic3d_VTA_BOTTOM);
    }

    // Y 尺寸線
    {
        Handle(Graphic3d_Group) grp = prs->NewGroup();
        Quantity_Color lineCol(0.0, 0.8, 0.0, Quantity_TOC_RGB);
        Handle(Graphic3d_AspectLine3d) asp =
            new Graphic3d_AspectLine3d(lineCol, Aspect_TOL_SOLID, 1.5f);
        grp->SetPrimitivesAspect(asp);
        Handle(Graphic3d_ArrayOfPolylines) seg = new Graphic3d_ArrayOfPolylines(2, 1);
        seg->AddBound(2);
        seg->AddVertex(wPt); seg->AddVertex(wPy);
        grp->AddPrimitiveArray(seg);

        gp_Pnt midY((wPt.X()+wPy.X())*0.5, (wPt.Y()+wPy.Y())*0.5, wPt.Z());
        Handle(Graphic3d_Text) gt = new Graphic3d_Text(36.0f);
        QString yl = QString("Y=%1").arg(m_constraint.value2, 0, 'f', 2);
        gt->SetText(TCollection_ExtendedString(yl.toUtf8().constData(), Standard_True));
        gt->SetPosition(midY);
        gt->SetHorizontalAlignment(Graphic3d_HTA_LEFT);
        gt->SetVerticalAlignment(Graphic3d_VTA_CENTER);
        Handle(Graphic3d_Group) tg = prs->NewGroup();
        tg->SetGroupPrimitivesAspect(makeTextAspect(Quantity_Color(Quantity_NOC_RED)));
        tg->AddText(gt);

        addLabelRegion(midY, gp_Vec(wPt, wPy), yl,
                       /*oriented=*/false, Graphic3d_HTA_LEFT, Graphic3d_VTA_CENTER);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// labelPosition3D — 計算數值標籤的世界座標中心
// 與各 draw*Dimension 中 mid 的計算邏輯保持一致
// ─────────────────────────────────────────────────────────────────────────────

gp_Pnt AIS_DimensionLine::labelPosition3D() const
{
    const ConstraintType ct = m_constraint.type;

    // ── FixedAngleDim：標籤在角弧中點方向 ────────────────────────────────────
    if (ct == ConstraintType::FixedAngleDim || ct == ConstraintType::FixedAngle) {
        if (m_geoms.size() >= 2) {
            auto* geomA = dynamic_cast<const SketchLine*>(m_geoms[0]);
            auto* geomB = dynamic_cast<const SketchLine*>(m_geoms[1]);
            if (geomA && geomB) {
                gp_Vec2d dA(geomA->end.x()-geomA->start.x(), geomA->end.y()-geomA->start.y());
                gp_Vec2d dB(geomB->end.x()-geomB->start.x(), geomB->end.y()-geomB->start.y());
                double cross = dA.X()*dB.Y() - dA.Y()*dB.X();
                gp_Pnt2d apex;
                if (std::abs(cross) < 1e-8) {
                    apex = gp_Pnt2d((geomA->start.x()+geomA->end.x())*0.5,
                                    (geomA->start.y()+geomA->end.y())*0.5);
                } else {
                    gp_Pnt2d pA(geomA->start.x(), geomA->start.y());
                    gp_Vec2d ab(pA.X()-geomB->start.x(), pA.Y()-geomB->start.y());
                    // reuse intersection formula
                    double t = (ab.X()*dB.Y() - ab.Y()*dB.X()) / cross;
                    apex = gp_Pnt2d(pA.X() + t*dA.X(), pA.Y() + t*dA.Y());
                }
                double arcR = m_offsetDist;
                if (m_dimOffsetX != 0.0 || m_dimOffsetY != 0.0)
                    arcR = std::max(5.0, std::sqrt(m_dimOffsetX*m_dimOffsetX + m_dimOffsetY*m_dimOffsetY));
                double angA = std::atan2(dA.Y(), dA.X());
                double angB = std::atan2(dB.Y(), dB.X());
                while (angB < angA) angB += 2*M_PI;
                if (angB - angA > M_PI) angB -= 2*M_PI;
                double midAng = (angA + angB) * 0.5;
                gp_Pnt sk(apex.X() + arcR*std::cos(midAng),
                          apex.Y() + arcR*std::sin(midAng), 0.0);
                sk.Transform(m_sketchToWorld);
                return sk;
            }
        }
    }

    // ── FixedArcLength：標籤在同心弧中點 ─────────────────────────────────────
    if (ct == ConstraintType::FixedArcLength) {
        if (!m_geoms.isEmpty()) {
            auto* arc = dynamic_cast<const SketchArc*>(m_geoms[0]);
            if (arc && arc->points.size() >= 3) {
                QVector2D ctrPt   = arc->points[2];
                QVector2D startPt = arc->points[0];
                QVector2D endPt   = arc->points[1];
                double r    = (startPt - ctrPt).length();
                double dimR = r + std::abs(m_offsetDist);
                if (m_dimOffsetX != 0.0 || m_dimOffsetY != 0.0) {
                    double mag = std::sqrt(m_dimOffsetX*m_dimOffsetX + m_dimOffsetY*m_dimOffsetY);
                    if (mag > 1e-6) dimR = r + mag;
                }
                double angStart = std::atan2(startPt.y()-ctrPt.y(), startPt.x()-ctrPt.x());
                double angEnd   = std::atan2(endPt.y()-ctrPt.y(),   endPt.x()-ctrPt.x());
                while (angEnd <= angStart) angEnd += 2*M_PI;
                double midAng = (angStart + angEnd) * 0.5;
                gp_Pnt sk(ctrPt.x() + dimR*std::cos(midAng),
                          ctrPt.y() + dimR*std::sin(midAng), 0.0);
                sk.Transform(m_sketchToWorld);
                return sk;
            }
        }
    }

    // ── CoordinateDim：標籤在 X 引線中點（主要交互點）───────────────────────
    if (ct == ConstraintType::CoordinateDim) {
        gp_Pnt2d sk_pt;
        if (m_hasRefPos) {
            sk_pt = gp_Pnt2d(m_refPos1.x(), m_refPos1.y());
        } else if (!m_geoms.isEmpty() && !m_geoms[0]->points.isEmpty()) {
            sk_pt = gp_Pnt2d(m_geoms[0]->points[0].x(), m_geoms[0]->points[0].y());
        } else {
            return gp_Pnt(0,0,0);
        }
        // X 引線中點（水平方向）
        gp_Pnt sk((sk_pt.X() + m_constraint.value) * 0.5, sk_pt.Y(), 0.0);
        sk.Transform(m_sketchToWorld);
        return sk;
    }

    // ── FixedRadius（含弧）────────────────────────────────────────────────────
    if (ct == ConstraintType::FixedRadius) {
        if (!m_geoms.isEmpty()) {
            QVector2D center2D;
            double r = m_constraint.value;
            if (auto* circ = dynamic_cast<const SketchCircle*>(m_geoms[0])) {
                center2D = circ->center;
            } else if (auto* arc = dynamic_cast<const SketchArc*>(m_geoms[0])) {
                center2D = arc->points.size() >= 3 ? arc->points[2] : QVector2D(0,0);
            }
            gp_Vec2d sk_dir(1.0, 0.0);
            if (m_dimOffsetX != 0.0 || m_dimOffsetY != 0.0) {
                gp_Vec2d off(m_dimOffsetX, m_dimOffsetY);
                if (off.Magnitude() > Precision::Confusion()) sk_dir = off.Normalized();
            }
            gp_Pnt sk_mid(center2D.x() + sk_dir.X() * r * 0.5,
                          center2D.y() + sk_dir.Y() * r * 0.5, 0.0);
            sk_mid.Transform(m_sketchToWorld);
            return sk_mid;
        }
    }

    // ── FixedDiameter ─────────────────────────────────────────────────────────
    if (ct == ConstraintType::FixedDiameter) {
        if (!m_geoms.isEmpty()) {
            auto* circ = dynamic_cast<const SketchCircle*>(m_geoms[0]);
            if (circ) {
                gp_Pnt sk_mid(circ->center.x() + m_dimOffsetX,
                              circ->center.y() + m_dimOffsetY, 0.0);
                sk_mid.Transform(m_sketchToWorld);
                return sk_mid;
            }
        }
    }

    // ── 取兩端點（通用）──────────────────────────────────────────────────────
    gp_Pnt p1, p2;
    if (!getRefPoints(p1, p2)) return gp_Pnt(0, 0, 0);

    // FixedX / FixedHorizDist：水平尺寸線中點
    if (ct == ConstraintType::FixedX || ct == ConstraintType::FixedHorizDist) {
        double rawOffset = (m_dimOffsetY != 0.0) ? m_dimOffsetY : m_offsetDist;
        gp_Pnt skO(0, 0, 0); skO.Transform(m_sketchToWorld);
        gp_Pnt skX(1, 0, 0); skX.Transform(m_sketchToWorld);
        gp_Pnt skY(0, 1, 0); skY.Transform(m_sketchToWorld);
        gp_Vec xAxis(skO, skX), yAxis(skO, skY);
        gp_Vec v1(skO, p1), v2(skO, p2);
        double p1x = v1.Dot(xAxis), p2x = v2.Dot(xAxis);
        double p1y = v1.Dot(yAxis), p2y = v2.Dot(yAxis);
        double dimY = (p1y + p2y) * 0.5 + rawOffset;
        gp_Pnt d1(skO.XYZ() + xAxis.XYZ() * p1x + yAxis.XYZ() * dimY);
        gp_Pnt d2(skO.XYZ() + xAxis.XYZ() * p2x + yAxis.XYZ() * dimY);
        return gp_Pnt((d1.X()+d2.X())*0.5, (d1.Y()+d2.Y())*0.5, (d1.Z()+d2.Z())*0.5);
    }

    // FixedY / FixedVertDist：垂直尺寸線中點
    if (ct == ConstraintType::FixedY || ct == ConstraintType::FixedVertDist) {
        double rawOffset = (m_dimOffsetX != 0.0) ? m_dimOffsetX : m_offsetDist;
        gp_Pnt skO(0, 0, 0); skO.Transform(m_sketchToWorld);
        gp_Pnt skX(1, 0, 0); skX.Transform(m_sketchToWorld);
        gp_Pnt skY(0, 1, 0); skY.Transform(m_sketchToWorld);
        gp_Vec xAxis(skO, skX), yAxis(skO, skY);
        gp_Vec v1(skO, p1), v2(skO, p2);
        double p1x = v1.Dot(xAxis), p2x = v2.Dot(xAxis);
        double p1y = v1.Dot(yAxis), p2y = v2.Dot(yAxis);
        double dimX = (p1x + p2x) * 0.5 + rawOffset;
        gp_Pnt d1(skO.XYZ() + xAxis.XYZ() * dimX + yAxis.XYZ() * p1y);
        gp_Pnt d2(skO.XYZ() + xAxis.XYZ() * dimX + yAxis.XYZ() * p2y);
        return gp_Pnt((d1.X()+d2.X())*0.5, (d1.Y()+d2.Y())*0.5, (d1.Z()+d2.Z())*0.5);
    }

    // 線性通用（FixedLength、FixedDistance、FixedArcLength fallback）
    if (m_dimOffsetX != 0.0 || m_dimOffsetY != 0.0) {
        gp_Pnt op(m_dimOffsetX, m_dimOffsetY, 0.0);
        op.Transform(m_sketchToWorld);
        gp_Pnt orig(0.0, 0.0, 0.0);
        orig.Transform(m_sketchToWorld);
        gp_Vec offsetVec(orig, op);
        gp_Vec along(p1, p2);
        if (along.Magnitude() > Precision::Confusion()) {
            along.Normalize();
            gp_Vec perp = along.Crossed(gp_Vec(0, 0, 1));
            if (perp.Magnitude() > Precision::Confusion()) perp.Normalize();
            double projDist = offsetVec.Dot(perp);
            if (std::abs(projDist) < Precision::Confusion()) projDist = 8.0;
            gp_Pnt d1 = p1.Translated(perp * projDist);
            gp_Pnt d2 = p2.Translated(perp * projDist);
            return gp_Pnt((d1.X()+d2.X())*0.5, (d1.Y()+d2.Y())*0.5, (d1.Z()+d2.Z())*0.5);
        }
    }

    // fallback：預設 perp 偏移
    gp_Vec along(p1, p2);
    if (along.Magnitude() < Precision::Confusion())
        return gp_Pnt((p1.X()+p2.X())*0.5, (p1.Y()+p2.Y())*0.5, (p1.Z()+p2.Z())*0.5);
    along.Normalize();
    gp_Vec perp = along.Crossed(gp_Vec(0, 0, 1));
    if (perp.Magnitude() < Precision::Confusion()) perp = gp_Vec(0, 1, 0);
    perp.Normalize();
    gp_Pnt d1 = p1.Translated(perp * m_offsetDist);
    gp_Pnt d2 = p2.Translated(perp * m_offsetDist);
    return gp_Pnt((d1.X()+d2.X())*0.5, (d1.Y()+d2.Y())*0.5, (d1.Z()+d2.Z())*0.5);
}
// ─────────────────────────────────────────────────────────────────────────────
// addLabelRegion — 記錄一個數值標籤的世界座標位置/方向/內容
// 供 ComputeSelection 建立精確的 hover 方框，以及 hilightLabel 重繪 hover 文字
// ─────────────────────────────────────────────────────────────────────────────

void AIS_DimensionLine::addLabelRegion(const gp_Pnt& pos, const gp_Vec& along,
                                        const QString& text,
                                        bool oriented,
                                        Graphic3d_HorizontalTextAlignment hAlign,
                                        Graphic3d_VerticalTextAlignment   vAlign)
{
    if (text.isEmpty()) return;

    LabelRegion lr;
    lr.pos = pos;
    gp_Vec a = along;
    if (a.Magnitude() < Precision::Confusion()) a = gp_Vec(1, 0, 0);
    a.Normalize();
    lr.alongDir = gp_Dir(a);
    lr.text     = text;
    lr.oriented = oriented;
    lr.hAlign   = hAlign;
    lr.vAlign   = vAlign;

    m_labelRegions.append(lr);
}

// ─────────────────────────────────────────────────────────────────────────────
// hilightLabel — hover/選取高亮時，只重繪「這一個」數值標籤的文字本身
// 尺寸線本體、延伸線、箭頭一律不重繪，因此永遠不會被高亮
// ─────────────────────────────────────────────────────────────────────────────

void AIS_DimensionLine::hilightLabel(const Handle(PrsMgr_PresentationManager)& thePM,
                                      const Handle(Prs3d_Drawer)& theStyle,
                                      int labelIndex)
{
    if (thePM.IsNull()) return;
    if (labelIndex < 0 || labelIndex >= m_labelRegions.size()) return;

    Handle(Prs3d_Presentation) hiPrs = GetHilightPresentation(thePM);
    if (hiPrs.IsNull()) return;
    hiPrs->Clear();

    const LabelRegion& lr = m_labelRegions[labelIndex];
    Quantity_Color col = (!theStyle.IsNull()) ? theStyle->Color()
                                               : Quantity_Color(Quantity_NOC_YELLOW);

    Handle(Graphic3d_Text) gtext = new Graphic3d_Text(36.0f);
    gtext->SetText(TCollection_ExtendedString(lr.text.toUtf8().constData(), Standard_True));
    gtext->SetPosition(lr.pos);
    if (lr.oriented) {
        gtext->SetOrientation(gp_Ax2(lr.pos, gp_Dir(0, 0, 1), lr.alongDir));
    }
    gtext->SetHorizontalAlignment(lr.hAlign);
    gtext->SetVerticalAlignment(lr.vAlign);

    Handle(Graphic3d_Group) grp = hiPrs->NewGroup();
    Handle(Graphic3d_AspectText3d) asp = new Graphic3d_AspectText3d();
    asp->SetColor(col);
    grp->SetGroupPrimitivesAspect(asp);
    grp->AddText(gtext);

    thePM->AddToImmediateList(hiPrs);
}

// ─────────────────────────────────────────────────────────────────────────────
// HilightSelected — 點選後的持續高亮，同樣只重繪數值文字本身
// ─────────────────────────────────────────────────────────────────────────────

void AIS_DimensionLine::HilightSelected(const Handle(PrsMgr_PresentationManager)& thePM,
                                         const SelectMgr_SequenceOfOwner& theOwners)
{
    if (theOwners.IsEmpty()) return;
    // 選取狀態固定使用一個明顯的反白色（不依賴 Drawer 的 SelectionStyle accessor，
    // 避免不同 OCCT 版本 API 差異），與 hover 的 hilightLabel 共用同一段重繪邏輯
    Handle(Prs3d_Drawer) selStyle = new Prs3d_Drawer();
    selStyle->SetColor(Quantity_Color(Quantity_NOC_ORANGE));
    for (SelectMgr_SequenceOfOwner::Iterator it(theOwners); it.More(); it.Next()) {
        Handle(DimLabelOwner) lo = Handle(DimLabelOwner)::DownCast(it.Value());
        if (lo.IsNull()) continue;
        hilightLabel(thePM, selStyle, lo->LabelIndex());
        break; // 同一物件每次只會有一個標籤被選取/拖曳
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeSelection — 只對「實際渲染出的數值標籤文字」建立 sensitive region；
// 尺寸線本體、延伸線、箭頭完全不參與選取，自然也就永遠不會被 hover 高亮。
//
// 每個標籤的方框直接取自 Compute() 時依實際文字內容/位置記錄下來的 m_labelRegions，
// 而不是另外用近似公式重新猜測位置，確保 hover 偵測範圍與畫面上看到的數值完全對齊。
// ─────────────────────────────────────────────────────────────────────────────

void AIS_DimensionLine::ComputeSelection(
        const Handle(SelectMgr_Selection)& sel,
        const Standard_Integer /*mode*/)
{
    // ─────────────────────────────────────────────────────────────────────
    // 重要：Graphic3d_Text(36.0f) 的 36 是「畫面像素高度」，與 model-space mm 無關；
    // 若繼續用 model-space Select3D_SensitiveBox 估算文字大小，不同縮放下 box 大小
    // 和實際渲染文字大小的比例永遠對不上，導致 hover 範圍偏大或偏小。
    //
    // 正確做法：改用 Select3D_SensitivePoint（偵測點 = 文字中心）並搭配
    // SetSensitivityFactor，以畫素容差而非 model-space 大小來決定命中範圍，
    // 確保各縮放層級下 hover 範圍都只涵蓋數值文字本身。
    // ─────────────────────────────────────────────────────────────────────

    // kLabelSensitivity 乘以 SelectMgr 的 PixelTolerance（預設 2px）後即為實際偵測半徑。
    // 數值文字高度約 36px，故設為 12 → 有效容差 ≈ 24px（字高的 2/3），
    // 可命中文字大部分範圍而不會蓋到旁邊的幾何元素。
    // 若實際效果偏緊或偏鬆，可在此微調此數值。
    constexpr int kLabelSensitivity = 36;

    auto addPointFor = [&](const LabelRegion& lr, int index) {
        Handle(DimLabelOwner) owner = new DimLabelOwner(this, this, index);
        Handle(Select3D_SensitivePoint) sens = new Select3D_SensitivePoint(owner, lr.pos);
        sens->SetSensitivityFactor(kLabelSensitivity);
        sel->Add(sens);
    };

    if (!m_labelRegions.isEmpty()) {
        for (int i = 0; i < m_labelRegions.size(); ++i) {
            addPointFor(m_labelRegions[i], i);
        }
        return;
    }

    // 後備路徑
    gp_Pnt labelPt = labelPosition3D();
    LabelRegion fallback;
    fallback.pos  = labelPt;
    fallback.text = labelText();
    addPointFor(fallback, 0);
}


// ─────────────────────────────────────────────────────────────────────────────
// Phase 3B：尺寸線拖曳支援
// ─────────────────────────────────────────────────────────────────────────────

void AIS_DimensionLine::setDimLineOffset(double offsetX, double offsetY)
{
    m_dimOffsetX = offsetX;
    m_dimOffsetY = offsetY;
}

gp_Pnt AIS_DimensionLine::dimLineAnchorPoint3D() const
{
    return labelPosition3D();
}

} // namespace aicad::cad