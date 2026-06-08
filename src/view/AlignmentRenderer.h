#pragma once

#include <QObject>
#include <QList>

#include <AIS_Shape.hxx>
#include <AIS_InteractiveObject.hxx>

// Forward declarations — avoid pulling in heavy OCCT / Qt headers here
namespace aicad {
namespace railway {
class HorizontalAlignmentEdit;
class AlignmentElement;
class TangentElement;
class CircularArcElement;
class TransitionElement;
}
namespace view {
class CadView;
}
}

namespace aicad {
namespace view {

/**
 * @brief Renders a HorizontalAlignmentEdit into CadView as AIS overlay objects.
 *
 * Lifecycle
 * ─────────
 *   AlignmentRenderer* r = new AlignmentRenderer(cadView, this);
 *   r->setAlignment(horizontalAlignmentEdit);
 *   connect(edit, &HorizontalAlignmentEdit::changed,
 *           r,    &AlignmentRenderer::refresh);
 *
 * Each call to refresh() removes all previously added overlay objects and
 * rebuilds them from scratch using the current solved HorizontalAlignment.
 *
 * AIS geometry rules (per task spec)
 * ───────────────────────────────────
 *   TangentElement     → BRepBuilderAPI_MakeEdge (straight line edge)
 *   CircularArcElement → GC_MakeArcOfCircle (3-point arc edge)
 *   TransitionElement  → BRepBuilderAPI_MakePolygon, 150 samples  ← no MakeEdge
 *   PI markers         → BRepPrimAPI_MakeSphere (small sphere at each PI point)
 */
class AlignmentRenderer : public QObject
{
    Q_OBJECT

public:
    /**
     * @param cadView  The 3-D viewport; must outlive this object.
     * @param parent   Qt parent for memory management.
     */
    explicit AlignmentRenderer(CadView* cadView, QObject* parent = nullptr);
    ~AlignmentRenderer() override;

    // ── Public API ────────────────────────────────────────────────────────────

    /** Attach the editable alignment model that drives this renderer. */
    void setAlignment(railway::HorizontalAlignmentEdit* edit);

    /**
     * @brief Returns true if @p obj is one of the AIS overlay objects managed
     *        by this renderer (used to detect alignment element clicks).
     */
    bool containsObject(const AIS_InteractiveObject* obj) const;

    /** Show PI marker grips in the CadView. */
    void showPIGrips();

    /** Hide PI marker grips from the CadView. */
    void hidePIGrips();

public Q_SLOTS:
    /**
     * @brief Rebuild all AIS objects from the current solved alignment.
     *
     * Clears previously displayed overlay objects, then iterates the element
     * list and calls buildElementAIS() / buildSpiralAIS() for each element.
     */
    void refresh();

private:
    // ── Geometry builders ─────────────────────────────────────────────────────

    /**
     * @brief Build an AIS_Shape for a single alignment element.
     *
     * Dispatches to the appropriate builder based on element type:
     *   - TangentElement      → straight line edge
     *   - CircularArcElement  → arc edge
     *   - TransitionElement   → delegates to buildSpiralAIS()
     *
     * Returns a null handle if the element geometry is degenerate.
     */
    Handle(AIS_Shape) buildElementAIS(const railway::AlignmentElement* elem);

    /**
     * @brief Build a polyline AIS_Shape approximating a TransitionElement.
     *
     * @param elem     The spiral/transition element.
     * @param nSamples Number of sample points (default 150 per spec).
     *
     * Samples localFrame(L) at uniform arc-length intervals, converts each
     * sample to world (Easting, Northing) via worldXY(), and assembles the
     * result with BRepBuilderAPI_MakePolygon.
     *
     * NOTE: BRepBuilderAPI_MakeEdge MUST NOT be used here (task spec).
     */
    Handle(AIS_Shape) buildSpiralAIS(const railway::TransitionElement* elem,
                                     int nSamples = 150);

    // ── PI marker helpers ─────────────────────────────────────────────────────

    /** Build small sphere markers at every PI point. */
    void buildPIGrips();

    /** Remove all currently displayed PI grip objects from CadView. */
    void clearPIGrips();

    // ── Utility ───────────────────────────────────────────────────────────────

    /** Apply line colour and width to an AIS_Shape. */
    static void applyStyle(const Handle(AIS_Shape)& shape,
                           const Quantity_Color& colour,
                           Standard_Real lineWidth = 2.0);

    // ── Members ───────────────────────────────────────────────────────────────

    CadView*                              m_cadView  = nullptr;
    railway::HorizontalAlignmentEdit*     m_edit     = nullptr;

    /** All geometry overlay objects currently displayed (excl. PI grips). */
    QList<Handle(AIS_InteractiveObject)>  m_overlays;

    /** PI marker sphere objects. */
    QList<Handle(AIS_InteractiveObject)>  m_piGrips;

    bool m_piGripsVisible = false;
};

} // namespace view
} // namespace aicad