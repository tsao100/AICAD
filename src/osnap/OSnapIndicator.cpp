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
#include <BRep_Tool.hxx>
#include <Geom_Curve.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <TopoDS.hxx>

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

    // ✅ 用視圖相機軸向取代 hardcoded 世界 XY，確保符號在任何草圖平面都正確顯示
    gp_Vec axX(m_planeX), axY(m_planeY);
    if (!m_view.IsNull()) {
        double vx, vy, vz;
        m_view->Up(vx, vy, vz);
        gp_Vec up(vx, vy, vz);

        double ex, ey, ez, atx, aty, atz;
        m_view->Eye(ex, ey, ez);
        m_view->At(atx, aty, atz);
        gp_Vec viewDir(atx - ex, aty - ey, atz - ez);
        if (viewDir.Magnitude() > 1e-10) {
            viewDir.Normalize();
            axX = viewDir.Crossed(up).Normalized();
            axY = up.Normalized();
        }
    }

    // 根據 snap 類型繪製對應符號
    switch (m_candidate.type) {
    case SnapType::Endpoint:
        drawEndpointSymbol(grp, p, s, axX, axY);
        break;
    case SnapType::Midpoint:
        drawMidpointSymbol(grp, p, s, axX, axY);
        break;
    case SnapType::Center:
        drawCenterSymbol(grp, p, s, axX, axY);
        break;
    case SnapType::Quadrant:
        drawQuadrantSymbol(grp, p, s, axX, axY);
        break;
    case SnapType::Intersection:
        drawIntersectSymbol(grp, p, s, axX, axY);
        break;
    case SnapType::Perpendicular:
        drawPerpendSymbol(grp, p, s, axX, axY);
        break;
    case SnapType::Tangent:
        drawTangentSymbol(grp, p, s, axX, axY);
        break;
    case SnapType::Nearest:
        drawNearestSymbol(grp, p, s, axX, axY);
        break;
    case SnapType::Extension:
        drawExtensionSymbol(grp, p, s, axX, axY);  // 虛線標示
        break;
    case SnapType::Node:
        drawEndpointSymbol(grp, p, s, axX, axY);  // 重用端點符號
        break;
    default:
        drawNearestSymbol(grp, p, s, axX, axY);
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
// OSnapIndicator.cpp - drawEndpointSymbol 實作改為：
void OSnapIndicator::drawEndpointSymbol(
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s,
    const gp_Vec& axX, const gp_Vec& axY)
{
    gp_Vec dx = axX * s, dy = axY * s;
    std::vector<gp_Pnt> pts = {
        p.Translated(-dx - dy),
        p.Translated( dx - dy),
        p.Translated( dx + dy),
        p.Translated(-dx + dy),
    };
    grp->AddPrimitiveArray(makePolyline(pts, true));
}

/**  △  Midpoint：等邊三角形 */
void OSnapIndicator::drawMidpointSymbol(
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s,
    const gp_Vec& axX, const gp_Vec& axY)
{
    gp_Vec dx = axX * s, dy = axY * s;
    std::vector<gp_Pnt> pts = {
        p.Translated(-dx - dy),
        p.Translated( dx - dy),
        p.Translated(        dy),  // top center
    };
    grp->AddPrimitiveArray(makePolyline(pts, true));
}

/**  ○  Center：空心圓（16段近似） */
void OSnapIndicator::drawCenterSymbol(
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s,
    const gp_Vec& axX, const gp_Vec& axY)
{
    const int N = 16;
    std::vector<gp_Pnt> pts;
    for (int i = 0; i < N; ++i) {
        double a = 2.0 * M_PI * i / N;
        pts.push_back(p.Translated(axX * (s * std::cos(a)) + axY * (s * std::sin(a))));
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
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s,
    const gp_Vec& axX, const gp_Vec& axY)
{
    gp_Vec dx = axX * s, dy = axY * s;
    std::vector<gp_Pnt> pts = {
        p.Translated(-dx),
        p.Translated(-dy),
        p.Translated( dx),
        p.Translated( dy),
    };
    grp->AddPrimitiveArray(makePolyline(pts, true));
}

/**  ×  Intersection：兩條斜線交叉 */
void OSnapIndicator::drawIntersectSymbol(
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s,
    const gp_Vec& axX, const gp_Vec& axY)
{
    gp_Vec dx = axX * s, dy = axY * s;
    // line 1:
    auto l1 = makePolyline({ p.Translated(-dx - dy), p.Translated(dx + dy) });
    // line 2: /
    auto l2 = makePolyline({ p.Translated( dx - dy), p.Translated(-dx + dy) });
    grp->AddPrimitiveArray(l1);
    grp->AddPrimitiveArray(l2);
}

/**  ⊥  Perpendicular：L 形垂直符號 */
void OSnapIndicator::drawPerpendSymbol(
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s,
    const gp_Vec& /*axX*/, const gp_Vec& /*axY*/)
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
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s,
    const gp_Vec& /*axX*/, const gp_Vec& /*axY*/)
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
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s,
    const gp_Vec& axX, const gp_Vec& axY)
{
    drawIntersectSymbol(grp, p, s * 0.7, axX, axY);
}

/**
 *  Extension：延伸線符號
 *
 *  設計語意：snap 點位於邊的延伸線上（超出端點之外），
 *  符號需要同時傳達兩件事：
 *    1. 「這裡有一個點」— 中心小十字（×）
 *    2. 「這個點在某條線的延伸方向上」— 沿延伸方向的虛線段（實線段交替表示虛線）
 *
 *  繪製策略：
 *    · 從 m_candidate.sourceEdge 取出邊的曲線，在端點外求切線方向（延伸方向）
 *    · 若無法取得邊資訊（sourceEdge 為空），退而使用水平方向作為預設
 *    · 沿延伸方向繪製三段實線（模擬虛線效果：─ · ─ · ─）
 *    · 在 snap 點中心繪製小十字標示精確落點
 *    · 在靠近端點一側加一個小圓圈，提示「這是從哪裡延伸出來的」
 *
 *  座標系：
 *    p   = snap 點（世界座標）
 *    s   = 符號半徑（模型單位）
 *    dir = 延伸方向單位向量（從邊切線推導，指向 snap 點方向）
 */
void OSnapIndicator::drawExtensionSymbol(
    const Handle(Graphic3d_Group)& grp, const gp_Pnt& p, double s,
    const gp_Vec& /*axX*/, const gp_Vec& /*axY*/)
{
    // ── Step 1：推導延伸方向 ──────────────────────────────────────────────────
    //
    // 優先從 sourceEdge 取實際切線方向，使虛線平行於原邊。
    // 失敗時退回水平向量，至少讓符號可見。
    gp_Dir extDir(1.0, 0.0, 0.0);  // 預設：水平向右

    const TopoDS_Edge& edge = m_candidate.sourceEdge;
    if (!edge.IsNull()) {
        double first = 0.0, last = 0.0;
        Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);

        if (!curve.IsNull()) {
            // paramOnEdge 儲存的是在曲線上的實際參數值（不是 0~1）
            // 延伸點在端點之外，取最近端點的切線方向
            const double param = m_candidate.paramOnEdge;
            const double clampedParam = (param < first) ? first : last;

            gp_Pnt  onEdge;
            gp_Vec  tangent;
            curve->D1(clampedParam, onEdge, tangent);  // 一階導數 = 切線向量

            if (tangent.Magnitude() > 1e-10) {
                gp_Dir tanDir(tangent);

                // 判斷延伸方向：snap 點應在邊「外側」
                // 若 snap 點在曲線終點之後，方向與切線同向；否則反向
                gp_Vec toSnap(onEdge, p);
                if (toSnap.Dot(tangent) < 0.0)
                    tanDir.Reverse();

                extDir = tanDir;
            }
        }
    }

    // ── Step 2：虛線段（三段實線模擬虛線） ────────────────────────────────────
    //
    // 沿延伸方向，在 snap 點兩側（偏向端點那側更長）繪製虛線：
    //   ─ · ─ · ─
    //
    // 虛線節奏：段長 = 0.45s，間距 = 0.2s，共三段（總長 ≈ 2.35s）
    // 起始偏移：從 -2.0s 到 +0.5s（讓 snap 點在虛線的「前段」）
    const double segLen = s * 0.45;   // 每段實線長度
    const double gap    = s * 0.20;   // 間距
    const double startOffset = -s * 2.0;  // 虛線起點（向端點方向偏移）
    const int    numSegs = 3;

    // 總共 numSegs 條獨立線段，每條 2 個頂點 → numSegs*2 頂點，numSegs 個 bound
    Handle(Graphic3d_ArrayOfPolylines) dashes =
        new Graphic3d_ArrayOfPolylines(numSegs * 2, numSegs);

    for (int i = 0; i < numSegs; ++i) {
        // 每段起點沿 extDir 偏移
        double t0 = startOffset + i * (segLen + gap);
        double t1 = t0 + segLen;

        gp_Pnt a(p.X() + extDir.X() * t0,
                 p.Y() + extDir.Y() * t0,
                 p.Z() + extDir.Z() * t0);
        gp_Pnt b(p.X() + extDir.X() * t1,
                 p.Y() + extDir.Y() * t1,
                 p.Z() + extDir.Z() * t1);

        dashes->AddVertex(a);
        dashes->AddVertex(b);
        dashes->AddBound(2);
    }
    grp->AddPrimitiveArray(dashes);

    // ── Step 3：snap 點中心小十字（標示精確落點）────────────────────────────
    //
    // 十字方向：沿延伸方向 ± 垂直方向，各伸出 0.35s
    // 垂直方向：extDir 在 XY 平面的法向（Z 方向固定）
    const double cSize = s * 0.35;

    // 垂直於 extDir 的 2D 法向（繞 Z 軸旋轉 90°）
    gp_Dir perpDir(-extDir.Y(), extDir.X(), extDir.Z());

    Handle(Graphic3d_ArrayOfPolylines) cross =
        new Graphic3d_ArrayOfPolylines(4, 2);

    // 沿延伸方向的橫線
    cross->AddVertex(gp_Pnt(p.X() - extDir.X() * cSize,
                            p.Y() - extDir.Y() * cSize,
                            p.Z() - extDir.Z() * cSize));
    cross->AddVertex(gp_Pnt(p.X() + extDir.X() * cSize,
                            p.Y() + extDir.Y() * cSize,
                            p.Z() + extDir.Z() * cSize));
    cross->AddBound(2);

    // 垂直方向的橫線
    cross->AddVertex(gp_Pnt(p.X() - perpDir.X() * cSize,
                            p.Y() - perpDir.Y() * cSize,
                            p.Z() - perpDir.Z() * cSize));
    cross->AddVertex(gp_Pnt(p.X() + perpDir.X() * cSize,
                            p.Y() + perpDir.Y() * cSize,
                            p.Z() + perpDir.Z() * cSize));
    cross->AddBound(2);

    grp->AddPrimitiveArray(cross);

    // ── Step 4：端點側小圓圈（提示「延伸自哪裡」）──────────────────────────
    //
    // 在虛線的起始端（靠近原邊端點方向）繪製小空心圓，
    // 半徑 = 0.25s，圓心在 p + extDir * (startOffset - 0.3s)
    const double circRadius = s * 0.25;
    const double circOffset = startOffset - circRadius * 1.5;  // 緊貼虛線起點外側
    gp_Pnt circCenter(p.X() + extDir.X() * circOffset,
                      p.Y() + extDir.Y() * circOffset,
                      p.Z() + extDir.Z() * circOffset);

    constexpr int N = 12;
    std::vector<gp_Pnt> circlePts;
    circlePts.reserve(N);
    for (int i = 0; i < N; ++i) {
        // 在垂直於 extDir 的平面內畫圓（以 extDir 為法向）
        double angle = 2.0 * M_PI * i / N;
        // 建立局部座標系：perpDir 為 X 軸，extDir×perpDir 為 Y 軸
        gp_Dir yDir = extDir.Crossed(perpDir);  // extDir × perpDir
        circlePts.emplace_back(
            circCenter.X() + circRadius * (perpDir.X() * std::cos(angle)
                                           + yDir.X()    * std::sin(angle)),
            circCenter.Y() + circRadius * (perpDir.Y() * std::cos(angle)
                                           + yDir.Y()    * std::sin(angle)),
            circCenter.Z() + circRadius * (perpDir.Z() * std::cos(angle)
                                           + yDir.Z()    * std::sin(angle)));
    }
    grp->AddPrimitiveArray(makePolyline(circlePts, true));
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
