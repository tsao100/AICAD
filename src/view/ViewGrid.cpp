/**
 * @file ViewGrid.cpp
 * @brief 視圖網格類別實作 (OCCT built-in grid)
 * @author Felicia
 * @date 2024-12-04
 */

#include "ViewGrid.h"
#include "cad/Plane.h"

#include <QDebug>

#include <V3d_Viewer.hxx>
#include <V3d_View.hxx>
#include <Aspect_Grid.hxx>
#include <Aspect_GridType.hxx>
#include <Aspect_GridDrawMode.hxx>
#include <Aspect_Window.hxx>
#include <Quantity_Color.hxx>
#include <Graphic3d_Camera.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <IntAna_IntConicQuad.hxx>
#include <Precision.hxx>

#include <algorithm>
#include <cmath>

namespace aicad {
namespace view {

// ============================================================================
// Private
// ============================================================================

class ViewGrid::Private {
public:
    Handle(V3d_Viewer) viewer;
    Handle(V3d_View)   view;        // 用於依視窗大小/縮放比例自動調整格線
    cad::Plane*        plane       = nullptr;
    float              spacing     = 10.0f;
    GridStyle          style       = GridStyle::Lines;
    bool               visible     = false;
    bool               autoScale   = true;   ///< 預設開啟：格線間距/範圍隨縮放自動調整

    // Grid colour (minor lines)
    Quantity_Color gridColor  { 0.4, 0.4, 0.5, Quantity_TOC_RGB };
    // Tenth-line colour (every 10 cells, shown automatically by OCCT)
    Quantity_Color tenthColor { 0.2, 0.2, 0.9, Quantity_TOC_RGB };
};

// ----------------------------------------------------------------------------
// "Nice number" 間距：將原始間距向上取整為 1 / 2 / 5 × 10^n，
// 使格線在任何縮放比例下都呈現整齊好讀的間距。
// ----------------------------------------------------------------------------
static double niceGridStep(double raw)
{
    if (!(raw > 0.0) || !std::isfinite(raw)) {
        return 1.0;
    }
    const double exponent = std::floor(std::log10(raw));
    const double base     = std::pow(10.0, exponent);
    const double fraction = raw / base;

    double niceFraction;
    if (fraction <= 1.0)      niceFraction = 1.0;
    else if (fraction <= 2.0) niceFraction = 2.0;
    else if (fraction <= 5.0) niceFraction = 5.0;
    else                      niceFraction = 10.0;

    return niceFraction * base;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

ViewGrid::ViewGrid(const Handle(V3d_Viewer)& viewer, QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    d->viewer = viewer;
    qDebug() << "[ViewGrid] Created (OCCT built-in grid)";
}

ViewGrid::~ViewGrid()
{
    hide();
    delete d;
    qDebug() << "[ViewGrid] Destroyed";
}

// ============================================================================
// View (used for adaptive spacing/extent)
// ============================================================================

void ViewGrid::setView(const Handle(V3d_View)& view)
{
    d->view = view;
    if (d->visible) update();
}

// ============================================================================
// Plane
// ============================================================================

void ViewGrid::setPlane(cad::Plane* plane)
{
    d->plane = plane;
    if (d->visible) update();
}

cad::Plane* ViewGrid::plane() const { return d->plane; }

// ============================================================================
// Spacing
// ============================================================================

void ViewGrid::setSpacing(float spacing)
{
    d->spacing = qMax(0.1f, spacing);
    if (d->visible) update();
}

float ViewGrid::spacing() const { return d->spacing; }

// ============================================================================
// Auto-scale
// ============================================================================

void ViewGrid::setAutoScale(bool enabled)
{
    d->autoScale = enabled;
    if (d->visible) update();
}

bool ViewGrid::autoScale() const { return d->autoScale; }

// ============================================================================
// Style
// ============================================================================

void ViewGrid::setStyle(GridStyle style)
{
    d->style = style;
    if (d->visible) update();
}

GridStyle ViewGrid::style() const { return d->style; }

// ============================================================================
// Colours
// ============================================================================

void ViewGrid::setGridColor(float r, float g, float b)
{
    d->gridColor = Quantity_Color(r, g, b, Quantity_TOC_RGB);
    if (d->visible) update();
}

void ViewGrid::setTenthColor(float r, float g, float b)
{
    d->tenthColor = Quantity_Color(r, g, b, Quantity_TOC_RGB);
    if (d->visible) update();
}

// ============================================================================
// Visibility
// ============================================================================

void ViewGrid::show()
{
    if (d->visible) return;
    d->visible = true;
    update();
    Q_EMIT visibilityChanged(true);
}

void ViewGrid::hide()
{
    if (!d->visible) return;
    d->visible = false;

    if (!d->viewer.IsNull()) {
        d->viewer->DeactivateGrid();
        d->viewer->SetGridEcho(Standard_False);
        d->viewer->Update();
    }

    Q_EMIT visibilityChanged(false);
    qDebug() << "[ViewGrid] Hidden";
}

bool ViewGrid::isVisible() const { return d->visible; }

// ============================================================================
// Update — apply all settings to the OCCT viewer grid
// ============================================================================

void ViewGrid::update()
{
    if (!d->visible || d->viewer.IsNull()) return;

    // ── 1. Privileged plane (orientation) ────────────────────────────────────
    applyPrivilegedPlane();

    // ── 2. Activate grid type ─────────────────────────────────────────────────
    Aspect_GridDrawMode drawMode =
        (d->style == GridStyle::Lines) ? Aspect_GDM_Lines : Aspect_GDM_Points;

    d->viewer->ActivateGrid(Aspect_GT_Rectangular, drawMode);

    // ── 3. 計算間距與顯示範圍 ──────────────────────────────────────────────────
    //    原則：畫面內永遠要鋪滿格線；間距依目前縮放比例調整，
    //    讓螢幕上每格大小維持在好讀的像素區間（不會過密或過疏）。
    double originU = 0.0, originV = 0.0;
    double effSpacing = static_cast<double>(d->spacing);
    double extentU = 200.0, extentV = 200.0;

    bool adaptive = false;
    if (d->autoScale && !d->view.IsNull()) {
        double minU, minV, maxU, maxV;
        if (computeViewportBoundsOnPlane(minU, minV, maxU, maxV)) {
            const double worldW = maxU - minU;
            const double worldH = maxV - minV;

            Standard_Integer winW = 0, winH = 0;
            const Handle(Aspect_Window)& win = d->view->Window();
            if (!win.IsNull()) {
                win->Size(winW, winH);
            }

            if (worldW > Precision::Confusion() && winW > 0) {
                const double desiredPx   = 40.0;   // 目標：每格約 40px
                const double pxPerUnit   = static_cast<double>(winW) / worldW;
                const double idealStep   = desiredPx / pxPerUnit;

                effSpacing = niceGridStep(idealStep);

                const double centerU = 0.5 * (minU + maxU);
                const double centerV = 0.5 * (minV + maxV);
                const double majorStep = effSpacing * 10.0;   // 每10格一條強調線

                originU = std::floor(centerU / majorStep) * majorStep;
                originV = std::floor(centerV / majorStep) * majorStep;

                // 範圍：覆蓋整個視窗，外加數格邊界，避免平移/縮放邊緣露白
                const double margin = effSpacing * 4.0;
                extentU = worldW + margin * 2.0;
                extentV = worldH + margin * 2.0;
                adaptive = true;
            }
        }
    }

    if (!adaptive) {
        // Fallback：無法取得視窗資訊時，沿用固定間距/範圍
        effSpacing = qMax(0.1, static_cast<double>(d->spacing));
        originU = originV = 0.0;
        extentU = extentV = 200.0;
    }

    // ── 4. Rectangular grid parameters ───────────────────────────────────────
    //   SetRectangularGridValues(originX, originY, stepX, stepY, rotationAngle)
    d->viewer->SetRectangularGridValues(
        originU, originV,
        effSpacing, effSpacing,
        0.0
        );

    // ── 5. 顯示範圍 / 顏色 / echo ────────────────────────────────────────────
    d->viewer->SetRectangularGridGraphicValues(
        extentU,      // 總顯示範圍 X
        extentV,      // 總顯示範圍 Y
        0.0           // offset along normal
        );

    // Apply minor / tenth colours
    d->viewer->Grid()->SetColors(d->gridColor, d->tenthColor);

    // Draw grid echo (cursor snap highlight)
    d->viewer->SetGridEcho(Standard_True);

    d->viewer->Update();

    qDebug() << "[ViewGrid] Updated — spacing:" << effSpacing
             << "extent:" << extentU << "x" << extentV
             << "style:" << (d->style == GridStyle::Lines ? "Lines" : "Dots")
             << "adaptive:" << adaptive;
}

// ============================================================================
// Private — 將視窗四個角落投影到目前格線平面，取得世界座標範圍
// ============================================================================

bool ViewGrid::computeViewportBoundsOnPlane(double& minU, double& minV,
                                             double& maxU, double& maxV) const
{
    if (d->view.IsNull()) return false;

    // 平面參考座標系：有自訂平面則用自訂平面，否則預設為世界 XY 平面
    gp_Pnt planeOrigin(0.0, 0.0, 0.0);
    gp_Dir planeNormal(0.0, 0.0, 1.0);
    gp_Dir planeXDir  (1.0, 0.0, 0.0);

    if (d->plane) {
        const QVector3D& o = d->plane->origin();
        const QVector3D& n = d->plane->normal();
        const QVector3D& x = d->plane->xAxis();
        planeOrigin = gp_Pnt(o.x(), o.y(), o.z());
        planeNormal = gp_Dir(n.x(), n.y(), n.z());
        planeXDir   = gp_Dir(x.x(), x.y(), x.z());
    }

    const gp_Ax3 ax3(planeOrigin, planeNormal, planeXDir);
    const gp_Pln gpPlane(ax3);
    const gp_Dir yDir = ax3.YDirection();

    Standard_Integer winW = 0, winH = 0;
    const Handle(Aspect_Window)& win = d->view->Window();
    if (win.IsNull()) return false;
    win->Size(winW, winH);
    if (winW <= 0 || winH <= 0) return false;

    Standard_Real Xeye, Yeye, Zeye, Xproj, Yproj, Zproj;
    d->view->Eye(Xeye, Yeye, Zeye);
    d->view->Proj(Xproj, Yproj, Zproj);
    const gp_Pnt eyePoint(Xeye, Yeye, Zeye);
    const gp_Dir projDir(Xproj, Yproj, Zproj);
    const bool ortho = !d->view->Camera().IsNull() && d->view->Camera()->IsOrthographic();

    const Standard_Integer corners[4][2] = {
        {0, 0}, {winW, 0}, {0, winH}, {winW, winH}
    };

    bool any = false;
    for (const auto& corner : corners) {
        Standard_Real Xv, Yv, Zv;
        d->view->Convert(corner[0], corner[1], Xv, Yv, Zv);
        const gp_Pnt screenPnt(Xv, Yv, Zv);

        gp_Pnt rayStart;
        gp_Dir rayDir;
        if (ortho) {
            rayStart = screenPnt;
            rayDir   = projDir;
        } else {
            rayStart = eyePoint;
            const gp_Vec dirVec(eyePoint, screenPnt);
            rayDir = (dirVec.Magnitude() < Precision::Confusion())
                         ? projDir
                         : gp_Dir(dirVec);
        }

        const gp_Lin pickLine(rayStart, rayDir);
        const IntAna_IntConicQuad inter(pickLine, gpPlane, Precision::Angular());
        if (!inter.IsDone() || inter.NbPoints() == 0) continue;

        const gp_Pnt ip = inter.Point(1);
        const gp_Vec local(planeOrigin, ip);
        const double u = local.Dot(gp_Vec(planeXDir));
        const double v = local.Dot(gp_Vec(yDir));

        if (!any) {
            minU = maxU = u;
            minV = maxV = v;
            any = true;
        } else {
            minU = std::min(minU, u); maxU = std::max(maxU, u);
            minV = std::min(minV, v); maxV = std::max(maxV, v);
        }
    }

    return any;
}

// ============================================================================
// Private — align OCCT privileged plane to the sketch plane
// ============================================================================

void ViewGrid::applyPrivilegedPlane()
{
    if (d->viewer.IsNull()) return;

    gp_Ax3 ax3;   // default: XY plane at origin

    if (d->plane) {
        const QVector3D& o = d->plane->origin();
        const QVector3D& n = d->plane->normal();
        const QVector3D& x = d->plane->xAxis();

        gp_Pnt origin(o.x(), o.y(), o.z());
        gp_Dir normal(n.x(), n.y(), n.z());
        gp_Dir xDir  (x.x(), x.y(), x.z());

        ax3 = gp_Ax3(origin, normal, xDir);
    }

    d->viewer->SetPrivilegedPlane(ax3);
}

} // namespace view
} // namespace aicad
