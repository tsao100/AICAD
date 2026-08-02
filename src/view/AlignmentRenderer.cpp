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
#include <cmath>

namespace aicad {
namespace view {

// ============================================================================
//  Helpers
// ============================================================================

namespace {

/// Convert QPointF (Local CAD coords) to a flat gp_Pnt (Z = 0).
/// NOTE: worldXY() already returns Local coords (solver runs in Local space).
/// No ProjectOrigin conversion needed here — coords are already local.
inline gp_Pnt toOCCT(const QPointF& localPt)
{
    return gp_Pnt(localPt.x(), localPt.y(), 0.0);
}

} // anonymous namespace

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
    if (m_edit == edit) return;
    m_edit     = edit;
    m_directHA = nullptr;
    m_overlayFingerprints.clear();   // force full rebuild on next refresh()
}

void AlignmentRenderer::setHorizontalAlignment(const railway::HorizontalAlignment* ha)
{
    if (m_directHA == ha) return;
    m_directHA = ha;
    m_edit     = nullptr;
    m_overlayFingerprints.clear();   // force full rebuild on next refresh()
}

bool AlignmentRenderer::containsObject(const AIS_InteractiveObject* obj) const
{
    for (const auto& o : m_overlays)
        if (o.get() == obj) return true;
    for (const auto& o : m_piGrips)
        if (o.get() == obj) return true;
    return false;
}

int AlignmentRenderer::editableIndexForObject(const AIS_InteractiveObject* obj) const
{
    if (!m_edit || !obj) return -1;

    int overlayIdx = -1;
    for (int i = 0; i < m_overlays.size(); ++i) {
        if (m_overlays[i].get() == obj) { overlayIdx = i; break; }
    }
    if (overlayIdx < 0 || overlayIdx >= m_overlayFingerprints.size()) return -1;

    const ElemFingerprint& fp = m_overlayFingerprints[overlayIdx];

    // Nearest-start-point match against the editable element list — see
    // header doc comment for why this can't just be m_elems[overlayIdx].
    const QVector<railway::EditableElement>& elems = m_edit->elements();
    constexpr double kMaxDist = 0.05;   // 5 cm — generous vs. real point spacing
    int    best      = -1;
    double bestDist2 = kMaxDist * kMaxDist;
    for (int i = 0; i < elems.size(); ++i) {
        const double dx = elems[i].startPI.x() - fp.easting;
        const double dy = elems[i].startPI.y() - fp.northing;
        const double d2 = dx * dx + dy * dy;
        if (d2 < bestDist2) { bestDist2 = d2; best = i; }
    }
    return best;
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

    // 用 CadView::setOverlayAISVisible() 更新登錄中的 visible 旗標，
    // 確保 displayAllFeatures() 重建時不會誤把 eye-close 的 overlay 重新顯示。
    for (const auto& obj : m_overlays)
        m_cadView->setOverlayAISVisible(obj, visible);
}

void AlignmentRenderer::clearOverlays()
{
    for (const auto& obj : m_overlays)
        if (m_cadView) m_cadView->removeOverlayAIS(obj);
    m_overlays.clear();
    m_overlayFingerprints.clear();
    if (m_cadView) m_cadView->refreshView();
}



void AlignmentRenderer::refresh()
{
    if (!m_cadView) return;

    // Note: m_visible is purely a display toggle, independently managed by
    // setVisible() (which keeps overlays registered but calls
    // CadView::setOverlayAISVisible() to hide/show them without destroying
    // them). refresh() always rebuilds the underlying geometry/fingerprint
    // cache regardless of visibility, so that a subsequent setVisible(true)
    // has up-to-date shapes to reveal instead of stale ones.

    if (!m_edit && !m_directHA) return;

    const railway::HorizontalAlignment* ha =
        m_edit ? m_edit->result() : m_directHA;

    if (!ha || ha->isEmpty()) {
        // Alignment cleared: drop everything.
        for (const auto& obj : m_overlays)
            m_cadView->removeOverlayAIS(obj);
        m_overlays.clear();
        m_overlayFingerprints.clear();
        if (m_piGripsVisible) {
            clearPIGrips();
        }
        m_cadView->refreshView();
        return;
    }

    // ── Incremental diff against the previous element list ──────────────────
    //
    // Element order/index is stable across a solve() call (the vector only
    // grows/shrinks via explicit insert/remove operations on the document,
    // never inside solve() itself), so we can compare overlay[i] against
    // elements[i] by index and only rebuild the AIS shapes whose underlying
    // geometry actually changed — instead of tearing down and re-adding
    // every overlay on every solve.
    const auto elems = ha->elements();   // vector<const AlignmentElement*>
    bool anyChanged = false;

    for (std::size_t i = 0; i < elems.size(); ++i) {
        const railway::AlignmentElement* elem = elems[i];
        const ElemFingerprint fp = fingerprintOf(elem);

        const int idx = static_cast<int>(i);
        const bool haveExisting = (idx < m_overlays.size());
        const bool unchanged    = haveExisting
                                   && (idx < m_overlayFingerprints.size())
                                   && (m_overlayFingerprints[idx] == fp);

        if (unchanged) {
            // Geometry identical — keep the existing AIS shape untouched.
            continue;
        }

        anyChanged = true;

        // Build the replacement shape first; only touch the view if it
        // actually produced something (degenerate elements render nothing,
        // matching the previous full-rebuild behaviour).
        Handle(AIS_Shape) shape;
        if (const auto* sp =
            dynamic_cast<const railway::TransitionElement*>(elem)) {
            shape = buildSpiralAIS(sp, 150);
        } else {
            shape = buildElementAIS(elem);
        }

        if (haveExisting) {
            m_cadView->removeOverlayAIS(m_overlays[idx]);
            m_overlays[idx] = shape;
        } else {
            m_overlays.append(shape);
        }

        if (idx < m_overlayFingerprints.size())
            m_overlayFingerprints[idx] = fp;
        else
            m_overlayFingerprints.append(fp);

        if (!shape.IsNull()) {
            // mode 0 = whole-shape hit test; Display(..., Standard_False)
            // defers the actual viewer redraw to the batched refreshView()
            // call below, instead of updating once per shape.
            m_cadView->addOverlayAIS(shape, {0});
            // 若目前 eye-close，立刻把剛加入/重建的 shape 設為不可見，
            // 維持與 setVisible() 一致的顯示狀態。
            if (!m_visible)
                m_cadView->setOverlayAISVisible(shape, false);
        }
    }

    // Trailing overlays for elements that no longer exist (list shrank).
    const int newCount = static_cast<int>(elems.size());
    if (newCount < m_overlays.size()) {
        anyChanged = true;
        for (int i = newCount; i < m_overlays.size(); ++i)
            m_cadView->removeOverlayAIS(m_overlays[i]);
        // QList (Qt5) has no resize(); erase the trailing range instead.
        m_overlays.erase(m_overlays.begin() + newCount, m_overlays.end());
        if (m_overlayFingerprints.size() > newCount)
            m_overlayFingerprints.erase(m_overlayFingerprints.begin() + newCount,
                                         m_overlayFingerprints.end());
    }

    // ── PI grips: cheap to rebuild in full, only do so when something moved ──
    if (m_piGripsVisible && anyChanged) {
        clearPIGrips();
        buildPIGrips();
    }

    if (anyChanged)
        m_cadView->refreshView();
}

// ============================================================================
//  ElemFingerprint
// ============================================================================

bool AlignmentRenderer::ElemFingerprint::operator==(const ElemFingerprint& o) const
{
    // 1e-6 m / rad — well below solver-meaningful change, but well above
    // double round-off noise at TM2 magnitudes (~1e6 m absolute coords).
    constexpr double kEps = 1e-6;
    return type == o.type
        && std::abs(chainage - o.chainage) < kEps
        && std::abs(easting  - o.easting)  < kEps
        && std::abs(northing - o.northing) < kEps
        && std::abs(azimuth  - o.azimuth)  < kEps
        && std::abs(length   - o.length)   < kEps
        && std::abs(radius   - o.radius)   < kEps;
}

AlignmentRenderer::ElemFingerprint
AlignmentRenderer::fingerprintOf(const railway::AlignmentElement* elem)
{
    ElemFingerprint fp;
    if (!elem) return fp;

    fp.type     = static_cast<int>(elem->type());
    fp.chainage = elem->placement().chainage;
    fp.easting  = elem->placement().easting;
    fp.northing = elem->placement().northing;
    fp.azimuth  = elem->placement().azimuth;
    fp.length   = elem->length();

    if (const auto* arc = dynamic_cast<const railway::CircularArcElement*>(elem))
        fp.radius = arc->radius();

    return fp;
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
        // pt.easting/northing are Local coords (solver runs in Local space)
        gp_Pnt centre = toOCCT(QPointF(pt.easting, pt.northing));
        BRepPrimAPI_MakeSphere mkSphere(centre, kR);
        if (!mkSphere.IsDone()) continue;

        Handle(AIS_Shape) sphere = new AIS_Shape(mkSphere.Shape());
        // Yellow PI marker
        sphere->SetColor(Quantity_Color(1.0, 0.85, 0.0, Quantity_TOC_RGB));
        sphere->SetMaterial(Graphic3d_NameOfMaterial_Gold);

        m_cadView->addOverlayAIS(sphere);
        // 若目前 eye-close，PI grip 也要隱藏
        if (!m_visible)
            m_cadView->setOverlayAISVisible(sphere, false);
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