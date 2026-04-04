/**
 * @file OSnapIndicator.cpp
 * @brief Object Snap 視覺指示器實作
 */

#include "OSnapIndicator.h"

#include <Prs3d_LineAspect.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Prs3d_Presentation.hxx>
#include <PrsMgr_PresentationManager.hxx>

#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <GCE2d_MakeCircle.hxx>
#include <ElCLib.hxx>

#include <QtMath>
#include <QDebug>

namespace aicad {
namespace osnap {

// ──────────────────────────────────────────────────────────────────────────────
//IMPLEMENT_STANDARD_RTTIEXT(OSnapIndicator, AIS_InteractiveObject)

OSnapIndicator::OSnapIndicator() {
    // 設定永遠在最上層渲染，不被其他物件遮擋
    SetZLayer(Graphic3d_ZLayerId_TopOSD);

    // 指示器不參與選取
    SetHilightMode(0);

    // 預設屬性
    Handle(Prs3d_Drawer) drawer = Attributes();
    drawer->SetLineAspect(new Prs3d_LineAspect(
        Quantity_NOC_YELLOW, Aspect_TOL_SOLID, 2.0));
}

void OSnapIndicator::setCandidate(const SnapCandidate& candidate) {
    m_candidate    = candidate;
    m_hasCandidate = candidate.isValid;
    SetToUpdate();    // 標記需要重新繪製
}

void OSnapIndicator::clearCandidate() {
    m_hasCandidate = false;
    SetToUpdate();
}

// ──────────────────────────────────────────────────────────────────────────────
//  AIS_InteractiveObject::Compute
// ──────────────────────────────────────────────────────────────────────────────
void OSnapIndicator::Compute(const Handle(PrsMgr_PresentationManager)& /*mgr*/,
                             const Handle(Prs3d_Presentation)& prs,
                             const Standard_Integer /*mode*/)
{
    prs->Clear();

    if (!m_hasCandidate || !m_candidate.isValid)
        return;

    const gp_Pnt& p = m_candidate.worldPoint;

    // ✅ 動態計算：將 m_screenSize 像素換算為當前視圖的世界單位
    double s = m_screenSize;  // fallback
    if (!m_view.IsNull()) {
        // 用兩個相鄰螢幕點之間的世界距離推算
        double x1, y1, z1, x2, y2, z2;
        Standard_Integer px, py;
        m_view->Convert(p.X(), p.Y(), p.Z(), px, py);
        m_view->Convert(px, py, x1, y1, z1);
        m_view->Convert(px + static_cast<int>(m_screenSize), py, x2, y2, z2);
        gp_Pnt wp1(x1, y1, z1), wp2(x2, y2, z2);
        double worldDist = wp1.Distance(wp2);
        if (worldDist > 1e-10) s = worldDist;
    }

    // 建立 Graphic3d_Group 並設定顏色
    Handle(Graphic3d_Group) grp = prs->NewGroup();

    Quantity_Color color(snapColor(m_candidate.type));
    Handle(Graphic3d_AspectLine3d) lineAsp =
        new Graphic3d_AspectLine3d(color, Aspect_TOL_SOLID, 2.0f);
    grp->SetGroupPrimitivesAspect(lineAsp);

    // 根據 snap 類型繪製對應符號
    switch (m_candidate.type) {
    case SnapType::Endpoint:
        drawEndpointSymbol(grp, p, s);
        break;
    case SnapType::Midpoint:
        drawMidpointSymbol(grp, p, s);
        break;
    case SnapType::Center:
        drawCenterSymbol(grp, p, s);
        break;
    case SnapType::Quadrant:
        drawQuadrantSymbol(grp, p, s);
        break;
    case SnapType::Intersection:
        drawIntersectSymbol(grp, p, s);
        break;
    case SnapType::Perpendicular:
        drawPerpendSymbol(grp, p, s);
        break;
    case SnapType::Tangent:
        drawTangentSymbol(grp, p, s);
        break;
    case SnapType::Nearest:
        drawNearestSymbol(grp, p, s);
        break;
    case SnapType::Node:
        drawEndpointSymbol(grp, p, s);  // 重用端點符號
        break;
    default:
        drawNearestSymbol(grp, p, s);
        break;
    }
}

void OSnapIndicator::ComputeSelection(const Handle(SelectMgr_Selection)& /*sel*/,
                                      const Standard_Integer /*mode*/) {
    // 指示器不參與選取，刻意留空
}

// ──────────────────────────────────────────────────────────────────────────────
//  符號繪製函數
// ──────────────────────────────────────────────────────────────────────────────

/**  □  Endpoint：空心方框 */
void OSnapIndicator::drawEndpointSymbol(
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s)
{
    std::vector<gp_Pnt> pts = {
        gp_Pnt(p.X()-s, p.Y()-s, p.Z()),
        gp_Pnt(p.X()+s, p.Y()-s, p.Z()),
        gp_Pnt(p.X()+s, p.Y()+s, p.Z()),
        gp_Pnt(p.X()-s, p.Y()+s, p.Z())
    };
    grp->AddPrimitiveArray(makePolyline(pts, true));
}

/**  △  Midpoint：等邊三角形 */
void OSnapIndicator::drawMidpointSymbol(
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s)
{
    const double h = s * 1.732;  // sqrt(3)
    std::vector<gp_Pnt> pts = {
        gp_Pnt(p.X(),   p.Y()+h*0.667, p.Z()),   // 頂點
        gp_Pnt(p.X()-s, p.Y()-h*0.333, p.Z()),   // 左下
        gp_Pnt(p.X()+s, p.Y()-h*0.333, p.Z())    // 右下
    };
    grp->AddPrimitiveArray(makePolyline(pts, true));
}

/**  ○  Center：空心圓（16段近似） */
void OSnapIndicator::drawCenterSymbol(
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s)
{
    constexpr int N = 24;
    std::vector<gp_Pnt> pts;
    pts.reserve(N);
    for (int i = 0; i < N; ++i) {
        double a = 2.0 * M_PI * i / N;
        pts.emplace_back(p.X() + s * std::cos(a),
                         p.Y() + s * std::sin(a),
                         p.Z());
    }
    grp->AddPrimitiveArray(makePolyline(pts, true));

    // 加十字準心
    Handle(Graphic3d_ArrayOfPolylines) cross =
        new Graphic3d_ArrayOfPolylines(4, 2);
    cross->AddVertex(gp_Pnt(p.X()-s*0.4, p.Y(), p.Z()));
    cross->AddVertex(gp_Pnt(p.X()+s*0.4, p.Y(), p.Z()));
    cross->AddBound(2);
    cross->AddVertex(gp_Pnt(p.X(), p.Y()-s*0.4, p.Z()));
    cross->AddVertex(gp_Pnt(p.X(), p.Y()+s*0.4, p.Z()));
    grp->AddPrimitiveArray(cross);
}

/**  ◇  Quadrant：旋轉 45° 的菱形 */
void OSnapIndicator::drawQuadrantSymbol(
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s)
{
    std::vector<gp_Pnt> pts = {
        gp_Pnt(p.X(),   p.Y()+s, p.Z()),
        gp_Pnt(p.X()+s, p.Y(),   p.Z()),
        gp_Pnt(p.X(),   p.Y()-s, p.Z()),
        gp_Pnt(p.X()-s, p.Y(),   p.Z())
    };
    grp->AddPrimitiveArray(makePolyline(pts, true));
}

/**  ×  Intersection：兩條斜線交叉 */
void OSnapIndicator::drawIntersectSymbol(
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s)
{
    Handle(Graphic3d_ArrayOfPolylines) lines =
        new Graphic3d_ArrayOfPolylines(4, 2);
    lines->AddVertex(gp_Pnt(p.X()-s, p.Y()-s, p.Z()));
    lines->AddVertex(gp_Pnt(p.X()+s, p.Y()+s, p.Z()));
    lines->AddBound(2);
    lines->AddVertex(gp_Pnt(p.X()+s, p.Y()-s, p.Z()));
    lines->AddVertex(gp_Pnt(p.X()-s, p.Y()+s, p.Z()));
    grp->AddPrimitiveArray(lines);

    // 外框小圓
    drawCenterSymbol(grp, p, s * 0.8);
}

/**  ⊥  Perpendicular：L 形垂直符號 */
void OSnapIndicator::drawPerpendSymbol(
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s)
{
    Handle(Graphic3d_ArrayOfPolylines) lines =
        new Graphic3d_ArrayOfPolylines(5, 2);
    // 垂直線
    lines->AddVertex(gp_Pnt(p.X(), p.Y()-s, p.Z()));
    lines->AddVertex(gp_Pnt(p.X(), p.Y()+s, p.Z()));
    lines->AddBound(2);
    // 底部水平線
    lines->AddVertex(gp_Pnt(p.X()-s, p.Y()-s, p.Z()));
    lines->AddVertex(gp_Pnt(p.X()+s, p.Y()-s, p.Z()));
    lines->AddVertex(gp_Pnt(p.X()+s*0.5, p.Y()-s, p.Z()));
    grp->AddPrimitiveArray(lines);
}

/**  切線符號：小圓 + 切線 */
void OSnapIndicator::drawTangentSymbol(
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s)
{
    // 小圓
    constexpr int N = 16;
    const double r = s * 0.6;
    std::vector<gp_Pnt> circle;
    circle.reserve(N);
    for (int i = 0; i < N; ++i) {
        double a = 2.0 * M_PI * i / N;
        circle.emplace_back(p.X() + r * std::cos(a),
                            p.Y() + r * std::sin(a),
                            p.Z());
    }
    grp->AddPrimitiveArray(makePolyline(circle, true));

    // 切線（水平線穿過頂部）
    Handle(Graphic3d_ArrayOfPolylines) tang =
        new Graphic3d_ArrayOfPolylines(2, 1);
    tang->AddVertex(gp_Pnt(p.X()-s, p.Y()+r, p.Z()));
    tang->AddVertex(gp_Pnt(p.X()+s, p.Y()+r, p.Z()));
    tang->AddBound(2);
    grp->AddPrimitiveArray(tang);
}

/**  Nearest：X 記號（與 Intersection 不同：無外圓） */
void OSnapIndicator::drawNearestSymbol(
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s)
{
    const double h = s * 0.7;
    Handle(Graphic3d_ArrayOfPolylines) lines =
        new Graphic3d_ArrayOfPolylines(4, 2);
    lines->AddVertex(gp_Pnt(p.X()-h, p.Y()-h, p.Z()));
    lines->AddVertex(gp_Pnt(p.X()+h, p.Y()+h, p.Z()));
    lines->AddBound(2);
    lines->AddVertex(gp_Pnt(p.X()+h, p.Y()-h, p.Z()));
    lines->AddVertex(gp_Pnt(p.X()-h, p.Y()+h, p.Z()));
    grp->AddPrimitiveArray(lines);
}

// ──────────────────────────────────────────────────────────────────────────────
//  輔助：建立 Graphic3d_ArrayOfPolylines
// ──────────────────────────────────────────────────────────────────────────────
Handle(Graphic3d_ArrayOfPolylines)
    OSnapIndicator::makePolyline(const std::vector<gp_Pnt>& pts, bool closed)
{
    int n = static_cast<int>(pts.size());
    if (n < 2)
        return nullptr;

    int total = closed ? n + 1 : n;
    Handle(Graphic3d_ArrayOfPolylines) poly =
        new Graphic3d_ArrayOfPolylines(total, 1);

    for (const auto& pt : pts)
        poly->AddVertex(pt);

    if (closed)
        poly->AddVertex(pts[0]);   // 閉合

    poly->AddBound(total);
    return poly;
}

} // namespace osnap
} // namespace aicad
