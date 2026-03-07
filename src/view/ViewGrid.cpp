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
#include <Aspect_Grid.hxx>
#include <Aspect_GridType.hxx>
#include <Aspect_GridDrawMode.hxx>
#include <Quantity_Color.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>

namespace aicad {
namespace view {

// ============================================================================
// Private
// ============================================================================

class ViewGrid::Private {
public:
    Handle(V3d_Viewer) viewer;
    cad::Plane*        plane       = nullptr;
    float              spacing     = 10.0f;
    GridStyle          style       = GridStyle::Lines;
    bool               visible     = false;

    // Grid colour (minor lines)
    Quantity_Color gridColor  { 0.4, 0.4, 0.5, Quantity_TOC_RGB };
    // Tenth-line colour (every 10 cells, shown automatically by OCCT)
    Quantity_Color tenthColor { 0.6, 0.6, 0.7, Quantity_TOC_RGB };
};

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

    // ── 3. Rectangular grid parameters ───────────────────────────────────────
    //   SetRectangularGridValues(originX, originY, stepX, stepY, rotationAngle)
    d->viewer->SetRectangularGridValues(
        0.0, 0.0,
        d->spacing, d->spacing,
        0.0
        );

    // ── 4. Colour & echo ──────────────────────────────────────────────────────
    d->viewer->SetRectangularGridGraphicValues(
        200.0,        // total extent in X (display size, not geometry)
        200.0,        // total extent in Y
        0.0           // offset along normal
        );

    // Apply minor / tenth colours
    d->viewer->Grid()->SetColors(d->gridColor, d->tenthColor);

    // Draw grid echo (cursor snap highlight)
    d->viewer->SetGridEcho(Standard_True);

    d->viewer->Update();

    qDebug() << "[ViewGrid] Updated — spacing:" << d->spacing
             << "style:" << (d->style == GridStyle::Lines ? "Lines" : "Dots");
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
