#include "ConstraintSymbolAIS.h"
#include "../Sketch.h"

#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Prs3d_TextAspect.hxx>
#include <Quantity_Color.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <Select3D_SensitivePoint.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <SelectMgr_Selection.hxx>
#include <QDebug>
#include <cmath>

IMPLEMENT_STANDARD_RTTIEXT(aicad::cad::AIS_ConstraintSymbol, AIS_InteractiveObject)

namespace aicad::cad {

AIS_ConstraintSymbol::AIS_ConstraintSymbol(
    const SketchConstraint& c,
    const QList<SketchGeometry*>& geoms,
    const gp_Trsf& sketchToWorld,
    SolveStatus status)
    : m_constraint(c)
    , m_geoms(geoms)
    , m_sketchToWorld(sketchToWorld)
    , m_status(status)
{
    m_symbolPos = computeSymbolPos();
    SetInfiniteState(Standard_True);
}

void AIS_ConstraintSymbol::Update(
    const SketchConstraint& c,
    const QList<SketchGeometry*>& geoms,
    SolveStatus status)
{
    m_constraint = c;
    m_geoms      = geoms;
    m_status     = status;
    m_symbolPos  = computeSymbolPos();
}

// ─────────────────────────────────────────────────────────────────────────────
// 符號位置計算
// ─────────────────────────────────────────────────────────────────────────────

gp_Pnt AIS_ConstraintSymbol::computeSymbolPos() const {
    // 取第一個參考幾何的中心作為符號位置（若找不到則原點）
    if (m_geoms.isEmpty()) return gp_Pnt(0, 0, 0);

    const SketchGeometry* g = m_geoms.first();
    QVector2D mid(0, 0);
    if (!g->points.isEmpty()) {
        for (const QVector2D& p : g->points)
            mid += p;
        mid /= static_cast<float>(g->points.size());
    }

    // 偏移 symbol 避免重疊在幾何上
    constexpr float offset = 5.0f;
    gp_Pnt pos(mid.x() + offset, mid.y() + offset, 0.0);
    pos.Transform(m_sketchToWorld);
    return pos;
}

// ─────────────────────────────────────────────────────────────────────────────
// 顏色規則
// ─────────────────────────────────────────────────────────────────────────────

Quantity_Color AIS_ConstraintSymbol::symbolColor() const {
    if (!m_constraint.driving)
        return Quantity_Color(Quantity_NOC_GRAY60);
    switch (m_status) {
    case SolveStatus::FullyConstrained:  return Quantity_Color(Quantity_NOC_GREEN3);
    case SolveStatus::UnderConstrained:  return Quantity_Color(Quantity_NOC_CYAN1);
    case SolveStatus::OverConstrained:   return Quantity_Color(Quantity_NOC_RED);
    default:                             return Quantity_Color(Quantity_NOC_YELLOW);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute — 根據約束類型分派繪製
// ─────────────────────────────────────────────────────────────────────────────

void AIS_ConstraintSymbol::Compute(
    const Handle(PrsMgr_PresentationManager)&,
    const Handle(Prs3d_Presentation)& prs,
    const Standard_Integer /*mode*/)
{
    prs->Clear();

    switch (m_constraint.type) {
    case ConstraintType::Coincident:
    case ConstraintType::Concentric:
        drawCoincident(prs);   break;
    case ConstraintType::Tangent:
        drawTangent(prs);      break;
    case ConstraintType::Horizontal:
        drawHorizontal(prs);   break;
    case ConstraintType::Vertical:
        drawVertical(prs);     break;
    case ConstraintType::Parallel:
    case ConstraintType::Collinear:
        drawParallel(prs);     break;
    case ConstraintType::Perpendicular:
        drawPerpendicular(prs); break;
    case ConstraintType::EqualLength:
        drawEqualLength(prs);  break;
    case ConstraintType::EqualRadius:
        drawEqualRadius(prs);  break;
    case ConstraintType::Fixed:
        drawFixed(prs);        break;
    case ConstraintType::Midpoint:
        drawMidpoint(prs);     break;
    case ConstraintType::Symmetric:
        drawSymmetric(prs);    break;
    case ConstraintType::PointOnCurve:
    case ConstraintType::PointOnMidpoint:
        drawPointOnCurve(prs); break;
    default:
        drawGeneric(prs, "•"); break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeSelection — 建立單一 sensitive point，涵蓋整個符號範圍
//
// 符號本身很小（kSymbolSize ≈ 3mm），改用 Select3D_SensitivePoint + 像素容差
// （與 AIS_DimensionLine 的數值標籤相同做法），確保各縮放層級下 hover/選取
// 範圍都貼近符號實際大小，而不會隨視圖縮放而失準。
// ─────────────────────────────────────────────────────────────────────────────

void AIS_ConstraintSymbol::ComputeSelection(
        const Handle(SelectMgr_Selection)& sel,
        const Standard_Integer /*mode*/)
{
    // kSymbolSensitivity 乘以 SelectMgr 的 PixelTolerance（預設 2px）即為偵測半徑。
    // 符號比尺寸線數值文字小，容差略收斂一些。
    constexpr int kSymbolSensitivity = 20;

    Handle(SelectMgr_EntityOwner) owner = new SelectMgr_EntityOwner(this, 5);
    Handle(Select3D_SensitivePoint) sens =
        new Select3D_SensitivePoint(owner, m_symbolPos);
    sens->SetSensitivityFactor(kSymbolSensitivity);
    sel->Add(sens);
}

// ─────────────────────────────────────────────────────────────────────────────
// 各符號繪製（簡化版：用線段組合幾何形狀）
// ─────────────────────────────────────────────────────────────────────────────

static void drawLine2Pts(const Handle(Prs3d_Presentation)& prs,
                         const gp_Pnt& p1, const gp_Pnt& p2,
                         const Quantity_Color& col)
{
    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Handle(Graphic3d_AspectLine3d) asp =
        new Graphic3d_AspectLine3d(col, Aspect_TOL_SOLID, 1.5f);
    grp->SetPrimitivesAspect(asp);

    Handle(Graphic3d_ArrayOfPolylines) seg =
        new Graphic3d_ArrayOfPolylines(2, 1);
    seg->AddBound(2);
    seg->AddVertex(p1);
    seg->AddVertex(p2);
    grp->AddPrimitiveArray(seg);
}

static void drawCircleSymbol(const Handle(Prs3d_Presentation)& prs,
                             const gp_Pnt& center, double r,
                             const Quantity_Color& col)
{
    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Handle(Graphic3d_AspectLine3d) asp =
        new Graphic3d_AspectLine3d(col, Aspect_TOL_SOLID, 1.5f);
    grp->SetPrimitivesAspect(asp);

    constexpr int N = 16;
    Handle(Graphic3d_ArrayOfPolylines) circ =
        new Graphic3d_ArrayOfPolylines(N + 1, 1);
    circ->AddBound(N + 1);
    for (int i = 0; i <= N; ++i) {
        double a = 2.0 * M_PI * i / N;
        circ->AddVertex(gp_Pnt(
            center.X() + r * std::cos(a),
            center.Y() + r * std::sin(a),
            center.Z()));
    }
    grp->AddPrimitiveArray(circ);
}

void AIS_ConstraintSymbol::drawCoincident(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    drawCircleSymbol(prs, m_symbolPos, kSymbolSize * 0.5, col);
}

void AIS_ConstraintSymbol::drawTangent(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    // 兩個相切的小弧（用直線近似）
    double s = kSymbolSize;
    gp_Pnt p1(m_symbolPos.X() - s, m_symbolPos.Y(), m_symbolPos.Z());
    gp_Pnt p2(m_symbolPos.X(),     m_symbolPos.Y(), m_symbolPos.Z());
    gp_Pnt p3(m_symbolPos.X() + s, m_symbolPos.Y(), m_symbolPos.Z());
    drawLine2Pts(prs, p1, p2, col);
    drawLine2Pts(prs, p2, p3, col);
}

void AIS_ConstraintSymbol::drawHorizontal(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    double s = kSymbolSize;
    // 水平線 ─
    gp_Pnt p1(m_symbolPos.X() - s, m_symbolPos.Y(), m_symbolPos.Z());
    gp_Pnt p2(m_symbolPos.X() + s, m_symbolPos.Y(), m_symbolPos.Z());
    drawLine2Pts(prs, p1, p2, col);
}

void AIS_ConstraintSymbol::drawVertical(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    double s = kSymbolSize;
    // 垂直線 |
    gp_Pnt p1(m_symbolPos.X(), m_symbolPos.Y() - s, m_symbolPos.Z());
    gp_Pnt p2(m_symbolPos.X(), m_symbolPos.Y() + s, m_symbolPos.Z());
    drawLine2Pts(prs, p1, p2, col);
}

void AIS_ConstraintSymbol::drawParallel(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    double s = kSymbolSize * 0.5;
    // 兩條短橫線 ═
    for (int row : {-1, 1}) {
        gp_Pnt p1(m_symbolPos.X() - s, m_symbolPos.Y() + row * s * 0.6, m_symbolPos.Z());
        gp_Pnt p2(m_symbolPos.X() + s, m_symbolPos.Y() + row * s * 0.6, m_symbolPos.Z());
        drawLine2Pts(prs, p1, p2, col);
    }
}

void AIS_ConstraintSymbol::drawPerpendicular(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    double s = kSymbolSize;
    // L 形
    gp_Pnt a(m_symbolPos.X(),     m_symbolPos.Y() + s, m_symbolPos.Z());
    gp_Pnt b(m_symbolPos.X(),     m_symbolPos.Y(),     m_symbolPos.Z());
    gp_Pnt c(m_symbolPos.X() + s, m_symbolPos.Y(),     m_symbolPos.Z());
    drawLine2Pts(prs, a, b, col);
    drawLine2Pts(prs, b, c, col);
}

void AIS_ConstraintSymbol::drawEqualLength(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    double s = kSymbolSize * 0.5;
    // = 符號
    for (int row : {-1, 1}) {
        gp_Pnt p1(m_symbolPos.X() - s, m_symbolPos.Y() + row * s * 0.5, m_symbolPos.Z());
        gp_Pnt p2(m_symbolPos.X() + s, m_symbolPos.Y() + row * s * 0.5, m_symbolPos.Z());
        drawLine2Pts(prs, p1, p2, col);
    }
}

void AIS_ConstraintSymbol::drawEqualRadius(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    drawCircleSymbol(prs, m_symbolPos, kSymbolSize * 0.4, col);
    drawCircleSymbol(prs, gp_Pnt(m_symbolPos.X() + kSymbolSize,
                                 m_symbolPos.Y(), m_symbolPos.Z()),
                     kSymbolSize * 0.4, col);
}

void AIS_ConstraintSymbol::drawFixed(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    double s = kSymbolSize;
    // 鎖形：矩形底座 + 弧頂
    gp_Pnt bl(m_symbolPos.X() - s*0.5, m_symbolPos.Y() - s*0.5, m_symbolPos.Z());
    gp_Pnt br(m_symbolPos.X() + s*0.5, m_symbolPos.Y() - s*0.5, m_symbolPos.Z());
    gp_Pnt tr(m_symbolPos.X() + s*0.5, m_symbolPos.Y() + s*0.3, m_symbolPos.Z());
    gp_Pnt tl(m_symbolPos.X() - s*0.5, m_symbolPos.Y() + s*0.3, m_symbolPos.Z());
    drawLine2Pts(prs, bl, br, col);
    drawLine2Pts(prs, br, tr, col);
    drawLine2Pts(prs, tr, tl, col);
    drawLine2Pts(prs, tl, bl, col);
}

void AIS_ConstraintSymbol::drawGeneric(const Handle(Prs3d_Presentation)& prs,
                                       const char* /*label*/)
{
    drawCircleSymbol(prs, m_symbolPos, kSymbolSize * 0.3, symbolColor());
}

// ── Midpoint：倒三角形（▽），表示「位於中點」────────────────────────────
void AIS_ConstraintSymbol::drawMidpoint(const Handle(Prs3d_Presentation)& prs)
{
    Quantity_Color col = symbolColor();
    double s = kSymbolSize * 0.6;
    gp_Pnt p = m_symbolPos;
    // 三角頂點：上方左/右 + 下方頂點
    gp_Pnt tl(p.X()-s, p.Y()+s*0.6, p.Z());
    gp_Pnt tr(p.X()+s, p.Y()+s*0.6, p.Z());
    gp_Pnt bt(p.X(),   p.Y()-s*0.6, p.Z());
    drawLine2Pts(prs, tl, tr, col);
    drawLine2Pts(prs, tr, bt, col);
    drawLine2Pts(prs, bt, tl, col);
}

// ── Symmetric：兩個小三角形面對面 (◁▷)，表示對稱 ─────────────────────
void AIS_ConstraintSymbol::drawSymmetric(const Handle(Prs3d_Presentation)& prs)
{
    Quantity_Color col = symbolColor();
    double s = kSymbolSize * 0.5;
    gp_Pnt p = m_symbolPos;
    // 左三角 ◁
    gp_Pnt ll(p.X()-s*0.2, p.Y(),    p.Z());
    gp_Pnt lt(p.X()-s,     p.Y()+s*0.7, p.Z());
    gp_Pnt lb(p.X()-s,     p.Y()-s*0.7, p.Z());
    drawLine2Pts(prs, ll, lt, col);
    drawLine2Pts(prs, lt, lb, col);
    drawLine2Pts(prs, lb, ll, col);
    // 右三角 ▷
    gp_Pnt rl(p.X()+s*0.2, p.Y(),    p.Z());
    gp_Pnt rt(p.X()+s,     p.Y()+s*0.7, p.Z());
    gp_Pnt rb(p.X()+s,     p.Y()-s*0.7, p.Z());
    drawLine2Pts(prs, rl, rt, col);
    drawLine2Pts(prs, rt, rb, col);
    drawLine2Pts(prs, rb, rl, col);
}

// ── PointOnCurve：小菱形（◇），表示點落在曲線上 ─────────────────────
void AIS_ConstraintSymbol::drawPointOnCurve(const Handle(Prs3d_Presentation)& prs)
{
    Quantity_Color col = symbolColor();
    double s = kSymbolSize * 0.5;
    gp_Pnt p = m_symbolPos;
    gp_Pnt top(p.X(),    p.Y()+s, p.Z());
    gp_Pnt rgt(p.X()+s,  p.Y(),   p.Z());
    gp_Pnt bot(p.X(),    p.Y()-s, p.Z());
    gp_Pnt lft(p.X()-s,  p.Y(),   p.Z());
    drawLine2Pts(prs, top, rgt, col);
    drawLine2Pts(prs, rgt, bot, col);
    drawLine2Pts(prs, bot, lft, col);
    drawLine2Pts(prs, lft, top, col);
}

} // namespace aicad::cad
