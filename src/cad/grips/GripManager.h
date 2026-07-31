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
#include <V3d_View.hxx>

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
    gp_Dir planeXAxis() const { return m_planeX; }
    gp_Dir planeYAxis() const { return m_planeY; }

    // ── 選取物件後，載入其 grip ────────────────────────────────
    IGripProvider* currentProvider() const { return m_provider.get(); }
    void attachProvider(std::unique_ptr<IGripProvider> provider);
    void detach();
    bool hasActiveGrips() const { return !m_handles.isEmpty(); }

    // ── Snap 設定 ─────────────────────────────────────────────
    void setGridSnap(bool on, double gridSize = 1.0);
    double gripSizeForType(GripType t);
    void setEndpointSnap(bool on) { m_snapEndpoint = on; }
    void setMidpointSnap(bool on) { m_snapMidpoint = on; }

    void setSnapManager(osnap::OSnapManager* mgr) { m_snapManager = mgr; }

    // ── Ortho Lock（F8）─────────────────────────────────────────
    /// 開啟後，拖曳中若沒有其他 snap 命中，座標會被鎖定在相對於拖曳起點
    /// 的水平/垂直方向上（依滑鼠偏移量較大的軸決定）。
    void setOrthoLock(bool on) { m_orthoLock = on; }
    bool isOrthoLock() const { return m_orthoLock; }

    /// 拖曳起點（僅在 isGripSelected()==true 時有意義），供 InputJig 計算
    /// 目前距離／角度顯示用。
    gp_Pnt dragStartPos() const { return m_dragStartPos; }

    /// 由 InputJig 提交數值時呼叫：等同於「第二次點擊」把 grip 放置在 pos，
    /// 完成本次拖曳（push undo、清除拖曳狀態、重新整理 grips）。
    /// 僅在 isGripSelected()==true 時有效。
    void commitDragAt(const gp_Pnt& pos);

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

    bool cancelGrip();          // ESC 取消選取
    bool isGripSelected() const { return m_gripSelected; }
    const QVector<GripPoint>& currentGrips() const { return m_grips; }
    void setView(const Handle(V3d_View)& view) { m_view = view; }

    gp_Pnt gripOriginPos(const QString& gripId) const {
        for (const GripPoint& gp : m_grips)
            if (gp.id == gripId) return gp.position;
        return gp_Pnt();
    }

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
    QString     hitTestGrip(const gp_Pnt& worldPos, double threshold = 6.0) const;
    void        displayHandles();
    void        eraseHandles();
    void        updateHandleColor(const QString& id, GripState state);
    void        finalizeDrag(const gp_Pnt& finalPos);

    Handle(AIS_InteractiveContext) m_context;
    std::unique_ptr<IGripProvider> m_provider;   // 替換原 raw pointer

    QVector<GripPoint>                         m_grips;
    QMap<QString, Handle(AIS_GripHandle)>      m_handles;

    // 拖拉狀態
    QString     m_activeGripId;
    gp_Pnt      m_dragStartPos;    
    bool        m_gripSelected = false;   // grip 已被選取，等待放置點
    gp_Pnt      m_lastSnapPos;            // mouseMoveEvent 中最後 snap 到的位置

    // Hover 狀態
    QString     m_hoveredGripId;

    // Snap 設定
    bool        m_gridSnap     = true;
    double      m_gridSize     = 5.0;
    bool        m_snapEndpoint = true;
    bool        m_snapMidpoint = true;
    bool        m_enabled = true;
    bool        m_orthoLock = false;
    osnap::OSnapManager* m_snapManager = nullptr;
    Handle(V3d_View) m_view;
};

} // namespace aicad::cad
