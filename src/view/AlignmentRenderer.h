#pragma once

#include <QObject>
#include <QList>
#include <QPointF>

#include <AIS_Shape.hxx>
#include <AIS_InteractiveObject.hxx>
#include <gp_Pnt.hxx>

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

    /** Show PI marker grips in the CadView. */
    void showPIGrips();

    /** Hide PI marker grips from the CadView. */
    void hidePIGrips();

    /** Show or hide all overlay objects without destroying them. */
    void setVisible(bool visible);

    /**
     * @brief 設定 TM2 座標原點偏移（公尺），應與 CadView::setCoordinateOffset() 同步。
     *
     * AlignmentDocument 儲存的座標是 TM2 絕對座標；OCCT 世界座標是模型本地座標。
     * toOCCT() 會用此偏移將 TM2 座標還原成 OCCT 本地座標後再建立幾何。
     */
    void setCoordinateOffset(double easting, double northing);

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

    // TM2 座標偏移（與 CadView 同步），toOCCT() 時減去
    double m_coordOffsetEasting  = 0.0;
    double m_coordOffsetNorthing = 0.0;

    /// TM2 → OCCT 本地座標轉換（減去偏移）
    gp_Pnt toOCCT(const QPointF& p) const;

    /** All geometry overlay objects currently displayed (excl. PI grips). */
    QList<Handle(AIS_InteractiveObject)>  m_overlays;

    /** PI marker sphere objects. */
    QList<Handle(AIS_InteractiveObject)>  m_piGrips;

    bool m_piGripsVisible = false;
};

} // namespace view
} // namespace aicad