// src/cad/grips/GripManager.h
#pragma once

#include "GripPoint.h"
#include "AIS_GripHandle.h"
#include "GripProvider.h"
#include "osnap/OSnapManager.h"

#include <QObject>
#include <QMap>
#include <QVector>
#include <AIS_InteractiveContext.hxx>

namespace aicad::cad {

struct SnapResult {
    bool    snapped = false;
    gp_Pnt  point;
    QString description;   // "Grid", "Endpoint", "Midpoint"...
};

/// 核心 Grip 管理器
class GripManager : public QObject
{
    Q_OBJECT

public:
    explicit GripManager(QObject* parent = nullptr);
    ~GripManager() override;

    void setContext(const Handle(AIS_InteractiveContext)& ctx);

    void setPlaneAxes(const gp_Dir& xAxis, const gp_Dir& yAxis) {
        m_planeX = xAxis;
        m_planeY = yAxis;
    }

    // ── 選取物件後，載入其 grip ────────────────────────────────
    void attachProvider(IGripProvider* provider);
    IGripProvider* currentProvider(){return m_provider;}
    void detach();
    bool hasActiveGrips() const { return !m_handles.isEmpty(); }

    // ── Snap 設定 ─────────────────────────────────────────────
    void setGridSnap(bool on, double gridSize = 1.0);
    double gripSizeForType(GripType t);
    void setEndpointSnap(bool on) { m_snapEndpoint = on; }
    void setMidpointSnap(bool on) { m_snapMidpoint = on; }

    void setSnapManager(osnap::OSnapManager* mgr) { m_snapManager = mgr; }

    // ── 滑鼠事件（由 GripEventFilter 呼叫）──────────────────────
    //bool mouseMoveEvent(const gp_Pnt& worldPos);   // returns true if grip active
   // bool mousePressEvent(const gp_Pnt& worldPos);
    bool mouseReleaseEvent(const gp_Pnt& worldPos);

    bool mouseMoveEvent(const gp_Pnt& worldPos, int sx, int sy);
    bool mousePressEvent(const gp_Pnt& worldPos, int sx, int sy);

    // ── 視覺更新 ──────────────────────────────────────────────
    void refreshGrips();
    void hideGrips();

    /// Command 執行期間暫停 Grip 輸入，但保留 provider 不清除
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

Q_SIGNALS:
    void gripDragStarted(const QString& gripId);
    void gripDragging(const QString& gripId, const gp_Pnt& pos);
    void gripDragFinished(const QString& gripId,
                          const gp_Pnt& startPos,
                          const gp_Pnt& endPos);
    void snapOccurred(const SnapResult& result);

private:
    gp_Dir  m_planeX = gp_Dir(1, 0, 0);
    gp_Dir  m_planeY = gp_Dir(0, 1, 0);

    SnapResult  computeSnap(const gp_Pnt& rawPos) const;
    QString     hitTestGrip(const gp_Pnt& worldPos, double threshold = 5.0) const;
    void        displayHandles();
    void        eraseHandles();
    void        updateHandleColor(const QString& id, GripState state);

    Handle(AIS_InteractiveContext) m_context;
    IGripProvider*                 m_provider  = nullptr;

    QVector<GripPoint>                         m_grips;
    QMap<QString, Handle(AIS_GripHandle)>      m_handles;

    // 拖拉狀態
    QString     m_activeGripId;
    gp_Pnt      m_dragStartPos;
    bool        m_isDragging = false;

    // Hover 狀態
    QString     m_hoveredGripId;

    // Snap 設定
    bool        m_gridSnap     = true;
    double      m_gridSize     = 5.0;
    bool        m_snapEndpoint = true;
    bool        m_snapMidpoint = true;
    bool        m_enabled = true;
    osnap::OSnapManager* m_snapManager = nullptr;
};

} // namespace aicad::cad
