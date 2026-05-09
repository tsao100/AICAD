// src/ui/GripEventFilter.h
#pragma once
#include <QObject>
#include <QMouseEvent>
#include "../cad/grips/GripManager.h"
#include "../cad/Plane.h"

class V3d_View;

namespace aicad::ui {

/// 安裝在 CadView 上，將滑鼠事件轉換為世界座標後傳給 GripManager
class GripEventFilter : public QObject
{
    Q_OBJECT

public:
    explicit GripEventFilter(cad::GripManager* mgr,
                             Handle(V3d_View) view,
                             QObject* parent = nullptr);

    bool eventFilter(QObject* obj, QEvent* event) override;

    // ✅ 讓外部在 attach provider 時設定當前 sketch plane
    void setSketchPlane(cad::Plane* plane) { m_sketchPlane = plane; }
    void clearSketchPlane()                { m_sketchPlane = nullptr; }

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const        { return m_enabled; }
    void setView(const Handle(V3d_View)& view) { m_view = view; }
    bool isGripSelected() const {
        return m_gripManager && m_gripManager->isGripSelected();
    }

private:
    gp_Pnt screenToWorld(int x, int y) const;

    cad::GripManager*  m_gripManager;
    Handle(V3d_View)   m_view;
    cad::Plane*        m_sketchPlane  = nullptr;
    bool m_lastHovered = false;    
    bool m_enabled = false;
};

} // namespace aicad::ui
