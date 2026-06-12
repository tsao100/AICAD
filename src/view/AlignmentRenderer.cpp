/**
 * @file AlignmentRenderer.cpp
 * @brief AIS display of HorizontalAlignment elements in CadView.
 */

#include "AlignmentRenderer.h"
#include "CadView.h"

#include "railway/AlignmentDocument.h"
#include "railway/RailwayAlignment.h"
#include "railway/RailwayAlignmentElement.h"

// OCCT – geometry builders
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Trsf.hxx>

// OCCT – AIS / presentation
#include <AIS_Shape.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Quantity_Color.hxx>

#include <QDebug>
#include <QTimer>
#include <cmath>

namespace aicad {
namespace view {

// ============================================================================
//  Helpers
// ============================================================================

/// Convert QPointF (TM2 Easting/Northing) to OCCT local model coordinates.
/// Subtracts the renderer's coordinate offset so that TM2 absolute coords
/// map to the correct OCCT world position near the model origin.
gp_Pnt AlignmentRenderer::toOCCT(const QPointF& p) const
{
    return gp_Pnt(p.x() - m_coordOffsetEasting,
                  p.y() - m_coordOffsetNorthing,
                  0.0);
}

// ============================================================================
//  Ctor / dtor
// ============================================================================

AlignmentRenderer::AlignmentRenderer(CadView* cadView, QObject* parent)
    : QObject(parent)
    , m_cadView(cadView)
{
}

AlignmentRenderer::~AlignmentRenderer()
{
    // Remove all overlays we registered
    for (const auto& obj : m_overlays)
        if (m_cadView) m_cadView->removeOverlayAIS(obj);

    clearPIGrips();
}

// ============================================================================
//  Public API
// ============================================================================

void AlignmentRenderer::setAlignment(railway::HorizontalAlignmentEdit* edit)
{
    m_edit     = edit;
    m_directHA = nullptr;
}

void AlignmentRenderer::setHorizontalAlignment(const railway::HorizontalAlignment* ha)
{
    m_directHA = ha;
    m_edit     = nullptr;
}

bool AlignmentRenderer::containsObject(const AIS_InteractiveObject* obj) const
{
    for (const auto& o : m_overlays)
        if (o.get() == obj) return true;
    for (const auto& o : m_piGrips)
        if (o.get() == obj) return true;
    return false;
}

void AlignmentRenderer::showPIGrips()
{
    m_piGripsVisible = true;
    buildPIGrips();
}

void AlignmentRenderer::hidePIGrips()
{
    m_piGripsVisible = false;
    clearPIGrips();
}

// ============================================================================
//  setVisible / clearOverlays
// ============================================================================

void AlignmentRenderer::setVisible(bool visible)
{
    if (m_visible == visible) return;
    m_visible = visible;

    if (!m_cadView) return;
    auto ctx = m_cadView->context();
    if (ctx.IsNull()) return;

    for (const auto& obj : m_overlays) {
        if (visible)
            ctx->Display(obj, Standard_False);
        else
            ctx->Erase(obj, Standard_False);
    }
    ctx->UpdateCurrentViewer();
}

void AlignmentRenderer::clearOverlays()
{
    for (const auto& obj : m_overlays)
        if (m_cadView) m_cadView->removeOverlayAIS(obj);
    m_overlays.clear();
    if (m_cadView) m_cadView->refreshView();
}

void AlignmentRenderer::setCoordinateOffset(double easting, double northing)
{
    m_coordOffsetEasting  = easting;
    m_coordOffsetNorthing = northing;
    refresh();   // 偏移改變後立即重新渲染
}



void AlignmentRenderer::refresh()
{
    if (!m_cadView) return;

    const bool hadOverlays = !m_overlays.isEmpty();

    // ── 1. Clear previously displayed geometry overlays ──────────────────────
    for (const auto& obj : m_overlays)
        m_cadView->removeOverlayAIS(obj);
    m_overlays.clear();

    if (!m_visible) return;
    if (!m_edit && !m_directHA) return;

    const railway::HorizontalAlignment* ha =
        m_edit ? m_edit->result() : m_directHA;
    if (!ha || ha->isEmpty()) return;

    // ── 2. Rebuild from solved element list ───────────────────────────────────
    const auto elems = ha->elements();   // vector<const AlignmentElement*>

    for (const railway::AlignmentElement* elem : elems) {
        Handle(AIS_Shape) shape;

        // TransitionElement covers Clothoid, HalfSine, Parabola, CubicJPN,
        // CubicECI, Egg — detect via dynamic_cast rather than ElementType.
        if (const auto* sp =
            dynamic_cast<const railway::TransitionElement*>(elem)) {
            shape = buildSpiralAIS(sp, 150);
        } else {
            shape = buildElementAIS(elem);
        }

        if (shape.IsNull()) continue;

<<<<<<< HEAD
<<<<<<< HEAD
        m_cadView->addOverlayAIS(shape, {1});   // mode 1 = 可被 OCCT 選取偵測
=======
=======
>>>>>>> 20d6d81 (H Alignment grips dbg1.)
<<<<<<< HEAD
        m_cadView->addOverlayAIS(shape, {0});   // mode 0 = whole-shape hit test
=======
        m_cadView->addOverlayAIS(shape, {1});   // mode 1 = 可被 OCCT 選取偵測
>>>>>>> 6bc721d (H Alignment grips dbg1.)
<<<<<<< HEAD
>>>>>>> 56324d0 (H Alignment grips dbg1.)
=======
=======
        m_cadView->addOverlayAIS(shape, {1});   // mode 1 = 可被 OCCT 選取偵測
>>>>>>> 2604ab0 (H Alignment grips dbg1.)
>>>>>>> 20d6d81 (H Alignment grips dbg1.)
        m_overlays.append(shape);
    }

    // ── 3. Refresh PI grips if they were visible ──────────────────────────────
    if (m_piGripsVisible) {
        clearPIGrips();
        buildPIGrips();
    }

    m_cadView->refreshView();

    // ── 4. 第一次加入幾何時（之前 overlays 為空），自動 fitAll 讓使用者看到結果 ──
    // 後續 refresh 不再自動縮放，避免干擾使用者的視角。
    if (!hadOverlays && !m_overlays.isEmpty()) {
        QTimer::singleShot(50, m_cadView, [this]() {
            if (m_cadView) m_cadView->fitAll();
        });
    }
}

// ============================================================================
//  buildElementAIS()
// ============================================================================

Handle(AIS_Shape) AlignmentRenderer::buildElementAIS(
    const railway::AlignmentElement* elem)
{
    if (!elem) return {};

    switch (elem->type()) {

        // ── Tangent: straight line edge ───────────────────────────────────────────
    case railway::ElementType::Tangent: {
        const QPointF p0 = elem->worldXY(elem->startChainage(), 0.0);
        const QPointF p1 = elem->worldXY(elem->endChainage(),   0.0);

        if ((p1 - p0).manhattanLength() < 1e-6) return {};

        BRepBuilderAPI_MakeEdge mkEdge(toOCCT(p0), toOCCT(p1));
        if (!mkEdge.IsDone()) {
            qWarning() << "[AlignmentRenderer] MakeEdge failed for TangentElement";
            return {};
        }

        Handle(AIS_Shape) shape = new AIS_Shape(mkEdge.Shape());
        applyStyle(shape,
                   Quantity_Color(0.2, 0.4, 0.8, Quantity_TOC_RGB), // blue
                   2.0);
        return shape;
    }

        // ── CircularArc: 3-point OCCT arc edge ───────────────────────────────────
    case railway::ElementType::CircularArc: {
        const double chStart = elem->startChainage();
        const double chEnd   = elem->endChainage();
        const double chMid   = 0.5 * (chStart + chEnd);

        const QPointF p0 = elem->worldXY(chStart, 0.0);
        const QPointF pm = elem->worldXY(chMid,   0.0);
        const QPointF p1 = elem->worldXY(chEnd,   0.0);

        // Degenerate guard
        if ((p1 - p0).manhattanLength() < 1e-6) return {};

        GC_MakeArcOfCircle mkArc(toOCCT(p0), toOCCT(pm), toOCCT(p1));
        if (!mkArc.IsDone()) {
            // Fallback: discretise with many points if 3-pt arc fails
            qWarning() << "[AlignmentRenderer] GC_MakeArcOfCircle failed; "
                          "falling back to polygon";

            BRepBuilderAPI_MakePolygon poly;
            constexpr int kFallback = 64;
            for (int i = 0; i <= kFallback; ++i) {
                double ch = chStart + (chEnd - chStart) * i / kFallback;
                poly.Add(toOCCT(elem->worldXY(ch, 0.0)));
            }
            if (!poly.IsDone()) return {};

            Handle(AIS_Shape) s = new AIS_Shape(poly.Shape());
            applyStyle(s,
                       Quantity_Color(0.9, 0.5, 0.1, Quantity_TOC_RGB), // orange
                       2.0);
            return s;
        }

        BRepBuilderAPI_MakeEdge mkEdge(mkArc.Value());
        if (!mkEdge.IsDone()) return {};

        Handle(AIS_Shape) shape = new AIS_Shape(mkEdge.Shape());
        applyStyle(shape,
                   Quantity_Color(0.9, 0.5, 0.1, Quantity_TOC_RGB), // orange
                   2.0);
        return shape;
    }

    default:
        // Transition elements are handled by the caller via buildSpiralAIS()
        return {};
    }
}

// ============================================================================
//  buildSpiralAIS()  — MUST use MakePolygon, not MakeEdge (task spec)
// ============================================================================

Handle(AIS_Shape) AlignmentRenderer::buildSpiralAIS(
    const railway::TransitionElement* elem,
    int nSamples)
{
    if (!elem || nSamples < 2) return {};

    const double chStart = elem->startChainage();
    const double chEnd   = elem->endChainage();
    const double span    = chEnd - chStart;

    if (span < 1e-6) return {};

    BRepBuilderAPI_MakePolygon poly;

    for (int i = 0; i <= nSamples; ++i) {
        // Uniform arc-length sampling
        const double ch = chStart + span * static_cast<double>(i) / nSamples;
        const QPointF wp = elem->worldXY(ch, 0.0);
        poly.Add(toOCCT(wp));
    }

    if (!poly.IsDone()) {
        qWarning() << "[AlignmentRenderer] MakePolygon failed for TransitionElement";
        return {};
    }

    Handle(AIS_Shape) shape = new AIS_Shape(poly.Shape());
    applyStyle(shape,
               Quantity_Color(0.2, 0.75, 0.3, Quantity_TOC_RGB), // green
               2.0);
    return shape;
}

// ============================================================================
//  PI marker helpers
// ============================================================================

void AlignmentRenderer::buildPIGrips()
{
    if (!m_cadView || !m_edit) return;

    const railway::HorizontalAlignment* ha = m_edit->result();
    if (!ha || ha->isEmpty()) return;

    // Collect unique PI world points from raw points
    const QVector<railway::AlignmentPoint>& rawPts = ha->rawPoints();

    // Marker radius: ~2 m in world space — small but visible
    constexpr double kR = 2.0;

    for (const railway::AlignmentPoint& pt : rawPts) {
        // rawPoints 的 easting/northing 是 TM2 絕對座標；
        // OCCT world 座標是本地模型座標，需減去 TM2 偏移
        const double localX = pt.easting  - m_coordOffsetEasting;
        const double localY = pt.northing - m_coordOffsetNorthing;
        gp_Pnt centre(localX, localY, 0.0);
        BRepPrimAPI_MakeSphere mkSphere(centre, kR);
        if (!mkSphere.IsDone()) continue;

        Handle(AIS_Shape) sphere = new AIS_Shape(mkSphere.Shape());
        // Yellow PI marker
        sphere->SetColor(Quantity_Color(1.0, 0.85, 0.0, Quantity_TOC_RGB));
        sphere->SetMaterial(Graphic3d_NameOfMaterial_Gold);

        m_cadView->addOverlayAIS(sphere);
        m_piGrips.append(sphere);
    }
}

void AlignmentRenderer::clearPIGrips()
{
    for (const auto& obj : m_piGrips)
        if (m_cadView) m_cadView->removeOverlayAIS(obj);
    m_piGrips.clear();
}

// ============================================================================
//  applyStyle()
// ============================================================================

void AlignmentRenderer::applyStyle(const Handle(AIS_Shape)& shape,
                                   const Quantity_Color& colour,
                                   Standard_Real lineWidth)
{
    if (shape.IsNull()) return;

    shape->SetColor(colour);
    shape->SetWidth(lineWidth);

    // Suppress shading — alignment geometry is 1-D curves/wires
    shape->SetDisplayMode(0); // 0 = AIS_WireFrame
}

} // namespace view
} // namespace aicad