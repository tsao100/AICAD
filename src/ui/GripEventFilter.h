// src/ui/GripEventFilter.h
#pragma once
#include <QObject>
#include <QMouseEvent>
#include <functional>
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

    /// 供 CadView 註冊：查詢窗選/穿越窗選是否正在進行中（拖曳或等待第二次點擊）。
    /// 若為 true，本 filter 讓滑鼠事件直接放行給 CadView，不做 grip 命中檢測，
    /// 避免第二次點擊剛好落在 grip 上時卡住選取流程。
    void setBoxSelectActiveQuery(std::function<bool()> query) {
        m_boxSelectActiveQuery = std::move(query);
    }

private:
    gp_Pnt screenToWorld(int x, int y) const;

    cad::GripManager*  m_gripManager;
    Handle(V3d_View)   m_view;
    cad::Plane*        m_sketchPlane  = nullptr;
    bool m_lastHovered = false;    
    bool m_enabled = false;
    std::function<bool()> m_boxSelectActiveQuery;
};

} // namespace aicad::ui
