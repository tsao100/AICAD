// src/ui/GripEventFilter.cpp

#include "core/Application.h"
#include "core/EventBus.h"
#include "GripEventFilter.h"
#include <QWidget>
#include <QMouseEvent>
#include <V3d_View.hxx>
#include <AIS_InteractiveContext.hxx>
#include <IntAna_IntConicQuad.hxx>
#include <gp_Pln.hxx>
#include <gp_Lin.hxx>
#include <Precision.hxx>
#include <cmath>

namespace aicad::ui {

GripEventFilter::GripEventFilter(cad::GripManager* mgr,
                                 Handle(V3d_View) view,
                                 QObject* parent)
    : QObject(parent)
    , m_gripManager(mgr)
    , m_view(view)
{}

<<<<<<< HEAD
<<<<<<< HEAD
=======
=======
>>>>>>> 20d6d81 (H Alignment grips dbg1.)
<<<<<<< HEAD
// ---------------------------------------------------------------------------
// screenToWorld
//
// Converts a physical-pixel screen position to a world-space 3-D point by
// intersecting the pick ray with a plane.
//
// Sketch mode  → intersect with the sketch plane (arbitrary orientation).
// Align mode   → intersect with Z=0 (the horizontal alignment plane).
//
// The ray construction mirrors CadView::screenToWorld exactly:
//   • Orthographic: ray origin = Convert(pixel), ray dir = Proj direction
//   • Perspective:  ray origin = Eye,            ray dir = Eye→Convert(pixel)
// ---------------------------------------------------------------------------

<<<<<<< HEAD
>>>>>>> 56324d0 (H Alignment grips dbg1.)
=======
=======
>>>>>>> 2604ab0 (H Alignment grips dbg1.)
>>>>>>> 20d6d81 (H Alignment grips dbg1.)
gp_Pnt GripEventFilter::screenToWorld(int x, int y) const
{
    // Project screen ray onto a plane.
    // For alignment editing (no sketch plane) the geometry lives at Z = 0.
    // For sketch editing, project onto the sketch plane.

    double px, py, pz, dx, dy, dz;
    m_view->ProjReferenceAxe(x, y, px, py, pz, dx, dy, dz);

    gp_Pnt rayOrigin(px, py, pz);
    gp_Dir rayDir(dx, dy, dz);

    gp_Pnt planeOrigin;
    gp_Dir planeNormal;

    if (m_sketchPlane) {
        QVector3D qOrigin = m_sketchPlane->origin();
        QVector3D qNormal = m_sketchPlane->normal();
        planeOrigin = gp_Pnt(qOrigin.x(), qOrigin.y(), qOrigin.z());
        planeNormal = gp_Dir(qNormal.x(), qNormal.y(), qNormal.z());
    } else {
        // Alignment grips live at Z = 0 (XY world plane)
        planeOrigin = gp_Pnt(0.0, 0.0, 0.0);
        planeNormal = gp_Dir(0.0, 0.0, 1.0);
    }

    // Ray-plane intersection: t = (planeOrigin - rayOrigin) · normal / (rayDir · normal)
    gp_Vec toPlane(rayOrigin, planeOrigin);
    double denom = rayDir.XYZ().Dot(planeNormal.XYZ());

    if (std::abs(denom) < 1e-10) {
        // Ray parallel to plane — fallback to view-plane point
        double wx, wy, wz;
        m_view->Convert(x, y, wx, wy, wz);
        return gp_Pnt(wx, wy, wz);
    }

<<<<<<< HEAD
<<<<<<< HEAD
=======
=======
>>>>>>> 20d6d81 (H Alignment grips dbg1.)
    // ── 4. Fallback: project screen point onto plane along Z ─────────────────
    // (ray nearly parallel to plane — very unusual for plan-view alignment)
    if (!m_sketchPlane) {
        return gp_Pnt(Xv, Yv, 0.0);   // clamp Z=0
    }
    return screenPt;
=======
gp_Pnt GripEventFilter::screenToWorld(int x, int y) const
{
    // Project screen ray onto a plane.
    // For alignment editing (no sketch plane) the geometry lives at Z = 0.
    // For sketch editing, project onto the sketch plane.

    double px, py, pz, dx, dy, dz;
    m_view->ProjReferenceAxe(x, y, px, py, pz, dx, dy, dz);

    gp_Pnt rayOrigin(px, py, pz);
    gp_Dir rayDir(dx, dy, dz);

    gp_Pnt planeOrigin;
    gp_Dir planeNormal;

    if (m_sketchPlane) {
        QVector3D qOrigin = m_sketchPlane->origin();
        QVector3D qNormal = m_sketchPlane->normal();
        planeOrigin = gp_Pnt(qOrigin.x(), qOrigin.y(), qOrigin.z());
        planeNormal = gp_Dir(qNormal.x(), qNormal.y(), qNormal.z());
    } else {
        // Alignment grips live at Z = 0 (XY world plane)
        planeOrigin = gp_Pnt(0.0, 0.0, 0.0);
        planeNormal = gp_Dir(0.0, 0.0, 1.0);
    }

    // Ray-plane intersection: t = (planeOrigin - rayOrigin) · normal / (rayDir · normal)
    gp_Vec toPlane(rayOrigin, planeOrigin);
    double denom = rayDir.XYZ().Dot(planeNormal.XYZ());

    if (std::abs(denom) < 1e-10) {
        // Ray parallel to plane — fallback to view-plane point
        double wx, wy, wz;
        m_view->Convert(x, y, wx, wy, wz);
        return gp_Pnt(wx, wy, wz);
    }

<<<<<<< HEAD
>>>>>>> 56324d0 (H Alignment grips dbg1.)
=======
=======
>>>>>>> 2604ab0 (H Alignment grips dbg1.)
>>>>>>> 20d6d81 (H Alignment grips dbg1.)
    double t = toPlane.XYZ().Dot(planeNormal.XYZ()) / denom;

    return gp_Pnt(
        rayOrigin.X() + rayDir.X() * t,
        rayOrigin.Y() + rayDir.Y() * t,
        rayOrigin.Z() + rayDir.Z() * t
    );
<<<<<<< HEAD
<<<<<<< HEAD
=======
>>>>>>> 6bc721d (H Alignment grips dbg1.)
>>>>>>> 56324d0 (H Alignment grips dbg1.)
=======
>>>>>>> 6bc721d (H Alignment grips dbg1.)
=======
>>>>>>> 2604ab0 (H Alignment grips dbg1.)
>>>>>>> 20d6d81 (H Alignment grips dbg1.)
}

bool GripEventFilter::eventFilter(QObject* obj, QEvent* event)
{
    if (!m_enabled) return false;
    if (m_view.IsNull() || !m_gripManager) return false;

    const qreal dpr = qobject_cast<QWidget*>(obj)
                          ? qobject_cast<QWidget*>(obj)->devicePixelRatio()
                          : 1.0;
    auto toPhys = [dpr](const QPointF& lp, int& px, int& py) {
        px = static_cast<int>(lp.x() * dpr);
        py = static_cast<int>(lp.y() * dpr);
    };

    switch (event->type()) {

    case QEvent::MouseMove: {
        auto* e = static_cast<QMouseEvent*>(event);
        int px, py;
        toPhys(e->pos(), px, py);
        gp_Pnt wp = screenToWorld(px, py);

        bool handled = m_gripManager->mouseMoveEvent(wp, px, py);

<<<<<<< HEAD
<<<<<<< HEAD
        // Hover 事件發布
=======
=======
>>>>>>> 20d6d81 (H Alignment grips dbg1.)
<<<<<<< HEAD
=======
        // Hover 事件發布
>>>>>>> 6bc721d (H Alignment grips dbg1.)
<<<<<<< HEAD
>>>>>>> 56324d0 (H Alignment grips dbg1.)
=======
=======
        // Hover 事件發布
>>>>>>> 2604ab0 (H Alignment grips dbg1.)
>>>>>>> 20d6d81 (H Alignment grips dbg1.)
        bool wasHovered = m_lastHovered;
        if (handled != wasHovered) {
            auto* bus = core::Application::instance()->eventBus();
            if (bus) {
                if (handled) bus->publish("grip.hovered",  QVariant{});
                else         bus->publish("grip.released", QVariant{});
            }
            m_lastHovered = handled;
        }

        if (m_gripManager->isGripSelected()) return true;
        return handled;
    }

    case QEvent::MouseButtonPress: {
        auto* e = static_cast<QMouseEvent*>(event);
        if (e->button() != Qt::LeftButton) break;
        int px, py;
        toPhys(e->pos(), px, py);
        gp_Pnt wp = screenToWorld(px, py);
        return m_gripManager->mousePressEvent(wp, px, py);
    }

    case QEvent::MouseButtonRelease:
        if (m_gripManager->isGripSelected()) return true;
        break;

    default: break;
    }
    return false;
}

} // namespace aicad::ui
