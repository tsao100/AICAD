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

gp_Pnt GripEventFilter::screenToWorld(int x, int y) const
{
    if (m_view.IsNull()) return gp_Pnt(0, 0, 0);

    // ── 1. Build pick ray ────────────────────────────────────────────────────

    Standard_Real Xeye, Yeye, Zeye;
    Standard_Real Xproj, Yproj, Zproj;
    m_view->Eye (Xeye,  Yeye,  Zeye);
    m_view->Proj(Xproj, Yproj, Zproj);

    Standard_Real Xv, Yv, Zv;
    m_view->Convert(x, y, Xv, Yv, Zv);   // point on view plane (world coords)
    gp_Pnt screenPt(Xv, Yv, Zv);

    gp_Pnt  rayStart;
    gp_Dir  rayDir;

    if (m_view->Camera()->IsOrthographic()) {
        // Orthographic: all rays are parallel to the projection direction.
        rayStart = screenPt;
        rayDir   = gp_Dir(Xproj, Yproj, Zproj);
    } else {
        // Perspective: ray from eye through the screen point.
        rayStart = gp_Pnt(Xeye, Yeye, Zeye);
        gp_Vec v(rayStart, screenPt);
        if (v.Magnitude() < Precision::Confusion())
            rayDir = gp_Dir(Xproj, Yproj, Zproj);
        else
            rayDir = gp_Dir(v);
    }

    // ── 2. Choose target plane ───────────────────────────────────────────────

    gp_Pln plane;
    if (m_sketchPlane) {
        QVector3D qO = m_sketchPlane->origin();
        QVector3D qN = m_sketchPlane->normal();
        plane = gp_Pln(gp_Pnt(qO.x(), qO.y(), qO.z()),
                       gp_Dir(qN.x(), qN.y(), qN.z()));
    } else {
        // Alignment geometry lives at Z = 0.
        plane = gp_Pln(gp_Pnt(0,0,0), gp_Dir(0,0,1));
    }

    // ── 3. Ray-plane intersection ────────────────────────────────────────────

    gp_Lin pickLine(rayStart, rayDir);
    IntAna_IntConicQuad intersect(pickLine, plane, Precision::Angular());

    if (intersect.IsDone() && intersect.NbPoints() > 0) {
        return intersect.Point(1);
    }

    // ── 4. Fallback: project screen point onto plane along Z ─────────────────
    // (ray nearly parallel to plane — very unusual for plan-view alignment)
    if (!m_sketchPlane) {
        return gp_Pnt(Xv, Yv, 0.0);   // clamp Z=0
    }
    return screenPt;
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
        // 窗選/穿越窗選進行中：放行給 CadView 處理選取框，不做 grip 命中檢測。
        if (m_boxSelectActiveQuery && m_boxSelectActiveQuery()) return false;

        auto* e = static_cast<QMouseEvent*>(event);
        int px, py;
        toPhys(e->pos(), px, py);
        gp_Pnt wp = screenToWorld(px, py);

        bool handled = m_gripManager->mouseMoveEvent(wp, px, py);

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
        // 窗選/穿越窗選進行中：這次點擊是完成選取的第二次點擊，
        // 放行給 CadView 處理，不要被 grip 命中檢測攔截。
        if (m_boxSelectActiveQuery && m_boxSelectActiveQuery()) break;
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
