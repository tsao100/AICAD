// src/ui/GripEventFilter.cpp

#include "core/Application.h"
#include "core/EventBus.h"
#include "GripEventFilter.h"
#include <QMouseEvent>
#include <V3d_View.hxx>
#include <AIS_InteractiveContext.hxx>

namespace aicad::ui {

GripEventFilter::GripEventFilter(cad::GripManager* mgr,
                                 Handle(V3d_View) view,
                                 QObject* parent)
    : QObject(parent)
    , m_gripManager(mgr)
    , m_view(view)
{}

// src/ui/GripEventFilter.cpp
gp_Pnt GripEventFilter::screenToWorld(int x, int y) const
{
    if (!m_sketchPlane) {
        // Fallback: point on view plane
        double wx, wy, wz;
        m_view->Convert(x, y, wx, wy, wz);
        return gp_Pnt(wx, wy, wz);
    }

    // ✅ Project screen ray onto sketch plane

    // 1. Get ray origin (eye position) and ray direction
    double px, py, pz, dx, dy, dz;
    m_view->ProjReferenceAxe(x, y, px, py, pz, dx, dy, dz);

    gp_Pnt  rayOrigin(px, py, pz);
    gp_Dir  rayDir(dx, dy, dz);

    // 2. Plane equation: (P - planeOrigin) · normal = 0
    QVector3D qOrigin = m_sketchPlane->origin();
    QVector3D qNormal = m_sketchPlane->normal();

    gp_Pnt  planeOrigin(qOrigin.x(), qOrigin.y(), qOrigin.z());
    gp_Dir  planeNormal(qNormal.x(), qNormal.y(), qNormal.z());

    // 3. Ray-plane intersection:
    //    t = (planeOrigin - rayOrigin) · normal / (rayDir · normal)
    gp_Vec toPlane(rayOrigin, planeOrigin);
    double denom = rayDir.XYZ().Dot(planeNormal.XYZ());

    if (std::abs(denom) < 1e-10) {
        // Ray parallel to plane, fallback
        double wx, wy, wz;
        m_view->Convert(x, y, wx, wy, wz);
        return gp_Pnt(wx, wy, wz);
    }

    double t = toPlane.XYZ().Dot(planeNormal.XYZ()) / denom;

    return gp_Pnt(
        rayOrigin.X() + rayDir.X() * t,
        rayOrigin.Y() + rayDir.Y() * t,
        rayOrigin.Z() + rayDir.Z() * t
        );
}

bool GripEventFilter::eventFilter(QObject* /*obj*/, QEvent* event)
{
    if (!m_enabled) return false;
    if (m_view.IsNull() || !m_gripManager) return false;

    switch (event->type()) {
    case QEvent::MouseMove: {
        auto* e = static_cast<QMouseEvent*>(event);
        gp_Pnt wp = screenToWorld(e->x(), e->y());
        bool handled = m_gripManager->mouseMoveEvent(wp);
        // 在 mouseMoveEvent 處理中，當 grip hover 狀態改變時發布事件：
        bool wasHovered = m_lastHovered;

        if (handled != wasHovered) {
            auto* bus = core::Application::instance()->eventBus();
            if (bus) {
                if (handled)
                    bus->publish("grip.hovered", QVariant{});
                else
                    bus->publish("grip.released", QVariant{});
            }
            m_lastHovered = handled;
        }

        if (m_gripCaptured) return true;   // 吃掉事件，不傳給 orbit
        return handled;
    }

    case QEvent::MouseButtonPress: {
        auto* e = static_cast<QMouseEvent*>(event);
        if (e->button() != Qt::LeftButton) break;
        gp_Pnt wp = screenToWorld(e->x(), e->y());
        m_gripCaptured = m_gripManager->mousePressEvent(wp);
        return m_gripCaptured;
    }

    case QEvent::MouseButtonRelease: {
        auto* e = static_cast<QMouseEvent*>(event);
        if (e->button() != Qt::LeftButton) break;
        if (m_gripCaptured) {
            gp_Pnt wp = screenToWorld(e->x(), e->y());
            m_gripManager->mouseReleaseEvent(wp);
            m_gripCaptured = false;
            return true;
        }
        break;
    }

    default: break;
    }
    return false;
}

} // namespace aicad::ui
