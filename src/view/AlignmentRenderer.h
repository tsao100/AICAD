#pragma once

#include <QObject>
#include <QList>
#include <QVector>

#include <AIS_Shape.hxx>
#include <AIS_InteractiveObject.hxx>

// Forward declarations — avoid pulling in heavy OCCT / Qt headers here
namespace aicad {
namespace railway {
class HorizontalAlignmentEdit;
class HorizontalAlignment;
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
     * @brief Directly set a read-only HorizontalAlignment for rendering
     *        (used for TrackCenterLine 3D visibility without an edit model).
     */
    void setHorizontalAlignment(const railway::HorizontalAlignment* ha);

    /**
     * @brief Returns true if @p obj is one of the AIS overlay objects managed
     *        by this renderer (used to detect alignment element clicks).
     */
    bool containsObject(const AIS_InteractiveObject* obj) const;

    /**
     * @brief Map a clicked overlay object back to its owning EditableElement
     *        index in the attached HorizontalAlignmentEdit (setAlignment()),
     *        for interactive erase (Delete key).
     *
     * m_overlays[i] corresponds to the *solved* HorizontalAlignment::elements()
     * list (rebuilt from sampled points every solve()), which can have a
     * different count/order than the editable m_elems — e.g. a near-zero-
     * length construction-line Tangent is dropped by HorizontalAlignment::
     * load(). So the lookup matches by nearest start point (placement /
     * startPI, the same physical point on both sides) instead of assuming
     * the overlay index equals the m_elems index.
     *
     * @return index into HorizontalAlignmentEdit::elements() (m_elems),
     *         or -1 if @p obj isn't one of ours, no edit model is attached,
     *         or no element starts near enough to it.
     */
    int editableIndexForObject(const AIS_InteractiveObject* obj) const;

    /** Show PI marker grips in the CadView. */
    void showPIGrips();

    /** Hide PI marker grips from the CadView. */
    void hidePIGrips();

    /** Show or hide all overlay objects without destroying them. */
    void setVisible(bool visible);

    /** Remove and clear all overlays (used when switching active TCL). */
    void clearOverlays();

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
    const railway::HorizontalAlignment*   m_directHA = nullptr;  ///< direct (no edit model)
    bool                                  m_visible  = true;  ///< overlay visibility

    /** All geometry overlay objects currently displayed (excl. PI grips). */
    QList<Handle(AIS_InteractiveObject)>  m_overlays;

    /**
     * @brief Lightweight per-element geometry fingerprint, parallel (1:1,
     *        index-aligned) to @ref m_overlays.
     *
     * Used by refresh() to detect which elements actually changed since the
     * last solve() so that only the affected AIS shapes are rebuilt instead
     * of tearing down and re-adding every overlay object on every solve.
     */
    struct ElemFingerprint {
        int    type     = -1;     ///< railway::ElementType, cast to int (-1 = invalid/unset)
        double chainage  = 0.0;   ///< Placement::chainage
        double easting   = 0.0;   ///< Placement::easting
        double northing  = 0.0;   ///< Placement::northing
        double azimuth   = 0.0;   ///< Placement::azimuth
        double length    = 0.0;
        double radius    = 0.0;   ///< CircularArcElement only; 0 otherwise

        bool operator==(const ElemFingerprint& o) const;
        bool operator!=(const ElemFingerprint& o) const { return !(*this == o); }
    };

    /** Fingerprints of the elements currently backing m_overlays. */
    QVector<ElemFingerprint> m_overlayFingerprints;

    /** Compute the fingerprint for @p elem (used to detect changes). */
    static ElemFingerprint fingerprintOf(const railway::AlignmentElement* elem);

    /** PI marker sphere objects. */
    QList<Handle(AIS_InteractiveObject)>  m_piGrips;

    bool m_piGripsVisible = false;
};

} // namespace view
} // namespace aicad