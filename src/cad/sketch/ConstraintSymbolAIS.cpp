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
    // ✅ 修正：約束符號是位置固定的小圖標，不是真正的「無限」物件
    // （無限物件應該是貫穿整個場景的軸線之類）。原本誤設為
    // Standard_True，導致 OCCT 在 View 的 Z-clip 範圍尚未涵蓋這個
    // 物件時，第一次 Display() 常常直接不畫出來，要等到之後一次
    // Erase()+Display()（例如按下「顯示/隱藏約束符號」切換鈕觸發
    // setVisible()）強迫重新計算才會顯示 —— 這正是「新增約束/開啟
    // 已存檔 sketch 時符號不出現，需手動切換一次才顯示」的成因。
    // 比照 SketchPointAIS / AIS_GripHandle / DimensionLineAIS，改為
    // Standard_False。
    SetInfiniteState(Standard_False);
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
    QVector2D mid(0, 0);
    if (!m_geoms.isEmpty()) {
        const SketchGeometry* g = m_geoms.first();
        if (!g->points.isEmpty()) {
            for (const QVector2D& p : g->points)
                mid += p;
            mid /= static_cast<float>(g->points.size());
        }
    }

    // 偏移 symbol 避免重疊在幾何上（此偏移量是「草圖平面局部座標」，
    // 不可與世界座標混用 — 見 localToWorld() 註解）
    constexpr float offset = 5.0f;
    m_symbolPosLocal = gp_Pnt(mid.x() + offset, mid.y() + offset, 0.0);

    gp_Pnt pos = m_symbolPosLocal;
    pos.Transform(m_sketchToWorld);
    return pos;
}

// ─────────────────────────────────────────────────────────────────────────────
// localToWorld — 草圖平面局部位移 → 世界座標
//
// 之前所有 draw* 函式都直接在 m_symbolPos（世界座標）上加減世界座標系的
// X/Y 分量，這只有在草圖平面剛好是世界 XY 平面時才恰好正確；草圖畫在
// YZ / XZ 或任意工作平面上時，符號的形狀（水平線、L 形、三角形…）就會
// 攤平到錯誤的方向，而不是貼著草圖所在平面繪製（issue #8）。
//
// 改為：形狀端點一律先以「符號中心為原點」的草圖平面局部 (du, dv) 表示，
// 再透過 m_sketchToWorld 轉成世界座標，這樣符號自然會跟著草圖平面旋轉。
// ─────────────────────────────────────────────────────────────────────────────
gp_Pnt AIS_ConstraintSymbol::localToWorld(double du, double dv) const {
    gp_Pnt p(m_symbolPosLocal.X() + du, m_symbolPosLocal.Y() + dv, 0.0);
    p.Transform(m_sketchToWorld);
    return p;
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

void AIS_ConstraintSymbol::drawCoincident(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    // 圓形符號改用 drawCircleSymbol 的局部版本：中心在局部原點，
    // 半徑沿局部 X 展開後個別轉世界座標，確保圓貼在草圖平面上。
    constexpr int N = 16;
    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Handle(Graphic3d_AspectLine3d) asp =
        new Graphic3d_AspectLine3d(col, Aspect_TOL_SOLID, 1.5f);
    grp->SetPrimitivesAspect(asp);
    double r = kSymbolSize * 0.5;
    Handle(Graphic3d_ArrayOfPolylines) circ = new Graphic3d_ArrayOfPolylines(N + 1, 1);
    circ->AddBound(N + 1);
    for (int i = 0; i <= N; ++i) {
        double a = 2.0 * M_PI * i / N;
        circ->AddVertex(localToWorld(r * std::cos(a), r * std::sin(a)));
    }
    grp->AddPrimitiveArray(circ);
}

void AIS_ConstraintSymbol::drawTangent(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    // 兩個相切的小弧（用直線近似）
    double s = kSymbolSize;
    gp_Pnt p1 = localToWorld(-s, 0);
    gp_Pnt p2 = localToWorld(0,  0);
    gp_Pnt p3 = localToWorld(s,  0);
    drawLine2Pts(prs, p1, p2, col);
    drawLine2Pts(prs, p2, p3, col);
}

void AIS_ConstraintSymbol::drawHorizontal(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    double s = kSymbolSize;
    // 水平線 ─（沿草圖平面的局部 U 軸）
    gp_Pnt p1 = localToWorld(-s, 0);
    gp_Pnt p2 = localToWorld( s, 0);
    drawLine2Pts(prs, p1, p2, col);
}

void AIS_ConstraintSymbol::drawVertical(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    double s = kSymbolSize;
    // 垂直線 |（沿草圖平面的局部 V 軸）
    gp_Pnt p1 = localToWorld(0, -s);
    gp_Pnt p2 = localToWorld(0,  s);
    drawLine2Pts(prs, p1, p2, col);
}

void AIS_ConstraintSymbol::drawParallel(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    double s = kSymbolSize * 0.5;
    // 兩條短橫線 ═
    for (int row : {-1, 1}) {
        gp_Pnt p1 = localToWorld(-s, row * s * 0.6);
        gp_Pnt p2 = localToWorld( s, row * s * 0.6);
        drawLine2Pts(prs, p1, p2, col);
    }
}

void AIS_ConstraintSymbol::drawPerpendicular(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    double s = kSymbolSize;
    // L 形
    gp_Pnt a = localToWorld(0, s);
    gp_Pnt b = localToWorld(0, 0);
    gp_Pnt c = localToWorld(s, 0);
    drawLine2Pts(prs, a, b, col);
    drawLine2Pts(prs, b, c, col);
}

void AIS_ConstraintSymbol::drawEqualLength(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    double s = kSymbolSize * 0.5;
    // = 符號
    for (int row : {-1, 1}) {
        gp_Pnt p1 = localToWorld(-s, row * s * 0.5);
        gp_Pnt p2 = localToWorld( s, row * s * 0.5);
        drawLine2Pts(prs, p1, p2, col);
    }
}

void AIS_ConstraintSymbol::drawEqualRadius(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    auto circleAt = [&](double cu, double cv, double r) {
        constexpr int N = 16;
        Handle(Graphic3d_Group) grp = prs->NewGroup();
        Handle(Graphic3d_AspectLine3d) asp =
            new Graphic3d_AspectLine3d(col, Aspect_TOL_SOLID, 1.5f);
        grp->SetPrimitivesAspect(asp);
        Handle(Graphic3d_ArrayOfPolylines) circ = new Graphic3d_ArrayOfPolylines(N + 1, 1);
        circ->AddBound(N + 1);
        for (int i = 0; i <= N; ++i) {
            double a = 2.0 * M_PI * i / N;
            circ->AddVertex(localToWorld(cu + r * std::cos(a), cv + r * std::sin(a)));
        }
        grp->AddPrimitiveArray(circ);
    };
    circleAt(0, 0, kSymbolSize * 0.4);
    circleAt(kSymbolSize, 0, kSymbolSize * 0.4);
}

void AIS_ConstraintSymbol::drawFixed(const Handle(Prs3d_Presentation)& prs) {
    Quantity_Color col = symbolColor();
    double s = kSymbolSize;
    // 鎖形：矩形底座 + 弧頂
    gp_Pnt bl = localToWorld(-s*0.5, -s*0.5);
    gp_Pnt br = localToWorld( s*0.5, -s*0.5);
    gp_Pnt tr = localToWorld( s*0.5,  s*0.3);
    gp_Pnt tl = localToWorld(-s*0.5,  s*0.3);
    drawLine2Pts(prs, bl, br, col);
    drawLine2Pts(prs, br, tr, col);
    drawLine2Pts(prs, tr, tl, col);
    drawLine2Pts(prs, tl, bl, col);
}

void AIS_ConstraintSymbol::drawGeneric(const Handle(Prs3d_Presentation)& prs,
                                       const char* /*label*/)
{
    Quantity_Color col = symbolColor();
    constexpr int N = 16;
    Handle(Graphic3d_Group) grp = prs->NewGroup();
    Handle(Graphic3d_AspectLine3d) asp =
        new Graphic3d_AspectLine3d(col, Aspect_TOL_SOLID, 1.5f);
    grp->SetPrimitivesAspect(asp);
    double r = kSymbolSize * 0.3;
    Handle(Graphic3d_ArrayOfPolylines) circ = new Graphic3d_ArrayOfPolylines(N + 1, 1);
    circ->AddBound(N + 1);
    for (int i = 0; i <= N; ++i) {
        double a = 2.0 * M_PI * i / N;
        circ->AddVertex(localToWorld(r * std::cos(a), r * std::sin(a)));
    }
    grp->AddPrimitiveArray(circ);
}

// ── Midpoint：倒三角形（▽），表示「位於中點」────────────────────────────
void AIS_ConstraintSymbol::drawMidpoint(const Handle(Prs3d_Presentation)& prs)
{
    Quantity_Color col = symbolColor();
    double s = kSymbolSize * 0.6;
    // 三角頂點：上方左/右 + 下方頂點
    gp_Pnt tl = localToWorld(-s,  s*0.6);
    gp_Pnt tr = localToWorld( s,  s*0.6);
    gp_Pnt bt = localToWorld( 0, -s*0.6);
    drawLine2Pts(prs, tl, tr, col);
    drawLine2Pts(prs, tr, bt, col);
    drawLine2Pts(prs, bt, tl, col);
}

// ── Symmetric：兩個小三角形面對面 (◁▷)，表示對稱 ─────────────────────
void AIS_ConstraintSymbol::drawSymmetric(const Handle(Prs3d_Presentation)& prs)
{
    Quantity_Color col = symbolColor();
    double s = kSymbolSize * 0.5;
    // 左三角 ◁
    gp_Pnt ll = localToWorld(-s*0.2, 0);
    gp_Pnt lt = localToWorld(-s,     s*0.7);
    gp_Pnt lb = localToWorld(-s,    -s*0.7);
    drawLine2Pts(prs, ll, lt, col);
    drawLine2Pts(prs, lt, lb, col);
    drawLine2Pts(prs, lb, ll, col);
    // 右三角 ▷
    gp_Pnt rl = localToWorld( s*0.2, 0);
    gp_Pnt rt = localToWorld( s,     s*0.7);
    gp_Pnt rb = localToWorld( s,    -s*0.7);
    drawLine2Pts(prs, rl, rt, col);
    drawLine2Pts(prs, rt, rb, col);
    drawLine2Pts(prs, rb, rl, col);
}

// ── PointOnCurve：小菱形（◇），表示點落在曲線上 ─────────────────────
void AIS_ConstraintSymbol::drawPointOnCurve(const Handle(Prs3d_Presentation)& prs)
{
    Quantity_Color col = symbolColor();
    double s = kSymbolSize * 0.5;
    gp_Pnt top = localToWorld( 0,  s);
    gp_Pnt rgt = localToWorld( s,  0);
    gp_Pnt bot = localToWorld( 0, -s);
    gp_Pnt lft = localToWorld(-s,  0);
    drawLine2Pts(prs, top, rgt, col);
    drawLine2Pts(prs, rgt, bot, col);
    drawLine2Pts(prs, bot, lft, col);
    drawLine2Pts(prs, lft, top, col);
}

} // namespace aicad::cad
