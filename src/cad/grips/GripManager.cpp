// src/cad/grips/GripManager.cpp
#include "GripManager.h"
#include "SketchGripProvider.h"
#include <QDebug>
#include <cmath>

namespace aicad::cad {

GripManager::GripManager(QObject* parent)
    : QObject(parent)
{}

GripManager::~GripManager()
{
    detach();
}

void GripManager::setContext(const Handle(AIS_InteractiveContext)& ctx)
{
    m_context = ctx;
}

// ── 掛載 Provider，建立 grip 顯示 ─────────────────────────────────────

void GripManager::attachProvider(std::unique_ptr<IGripProvider> provider)
{
    detach();   // 自動 delete 舊 provider（unique_ptr 析構）
    m_provider = std::move(provider);
    if (m_provider) {
        m_grips = m_provider->computeGrips();
        displayHandles();
    }
}

void GripManager::detach()
{
    // 若有選取中的 grip，先還原幾何
    if (m_gripSelected) {
        for (const GripPoint& gp : m_grips) {
            if (gp.id == m_activeGripId && gp.onDrag) {
                gp.onDrag(m_dragStartPos, false);
                break;
            }
        }
        if (m_snapManager) m_snapManager->onGripDragEnded();
    }

    m_gripSelected = false;
    m_activeGripId.clear();
    m_hoveredGripId.clear();

    eraseHandles();
    m_handles.clear();
    m_grips.clear();
    m_provider.reset();
    m_activeGripId.clear();
    m_hoveredGripId.clear();
}

// ── Grip 顯示 ─────────────────────────────────────────────────────────
void GripManager::displayHandles()
{
    if (m_context.IsNull()) return;

    for (const GripPoint& gp : m_grips) {
        if (!gp.enabled) continue;

        Handle(AIS_GripHandle) handle = new AIS_GripHandle(gp, gripSizeForType(gp.type));

        handle->SetPlaneAxes(m_planeX, m_planeY);

        m_context->Display(handle, Standard_False);
        m_context->Deactivate(handle);   // 不參與一般 AIS 選取
        m_handles[gp.id] = handle;
    }

    m_context->UpdateCurrentViewer();
    qDebug() << "[GripManager] Displayed" << m_handles.size() << "grips";
}

void GripManager::eraseHandles()
{
    if (m_context.IsNull()) return;

    for (auto& h : m_handles) {
        if (!h.IsNull()) m_context->Erase(h, Standard_False);
    }

    if (!m_handles.isEmpty())
        m_context->UpdateCurrentViewer();
}

void GripManager::hideGrips()
{
    eraseHandles();
    m_handles.clear();
}

void GripManager::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (!enabled) {
        // 視覺隱藏，但保留 provider / m_grips，可供後續 refreshGrips()
        hideGrips();
        if (m_gripSelected) {
            // 取消未完成的選取
            if (m_provider) m_provider->onGripDragEnd(m_activeGripId, m_dragStartPos, m_dragStartPos);
            m_gripSelected = false;
            m_activeGripId.clear();
        }
        m_hoveredGripId.clear();
    } else {
        // 重新顯示（provider 還在）
        refreshGrips();
    }
    qDebug() << "[GripManager] Grips" << (enabled ? "enabled" : "disabled");
}

bool GripManager::cancelGrip()
{
    if (!m_gripSelected) return false;

    // ① 找到 active grip，呼叫 onDrag 還原到起始位置
    if (auto* sgp = dynamic_cast<SketchGripProvider*>(m_provider.get())) {
        sgp->restoreSnapshot();   // 精確還原所有受影響頂點
    } else {
        for (const GripPoint& gp : m_grips) {
            if (gp.id == m_activeGripId && gp.onDrag) {
                gp.onDrag(m_dragStartPos, false);
                break;
            }
        }
    }

    // ② 通知 provider 放棄此次編輯（startPos == endPos = 無位移）
    if (m_provider)
        m_provider->onGripDragEnd(m_activeGripId, m_dragStartPos, m_dragStartPos);

    if (m_snapManager) m_snapManager->onGripDragEnded();

    // ③ 還原 handle 視覺位置
    Handle(AIS_GripHandle) h = m_handles.value(m_activeGripId);
    if (!h.IsNull()) {
        h->SetPosition(m_dragStartPos);
        m_context->RecomputePrsOnly(h, Standard_False);
        m_context->UpdateCurrentViewer();
    }

    updateHandleColor(m_activeGripId, GripState::Normal);
    m_gripSelected = false;
    m_activeGripId.clear();

    refreshGrips();
    qDebug() << "[GripManager] Grip cancelled";
    return true;
}

void GripManager::refreshGrips()
{
    if (!m_provider) return;
    eraseHandles();
    m_handles.clear();
    m_grips = m_provider->computeGrips();
    displayHandles();
}

// ── Hit Test ─────────────────────────────────────────────────────────
QString GripManager::hitTestGrip(const gp_Pnt& worldPos, double /*unused*/) const
{
    // 計算目前 zoom 下 12 像素對應的世界單位距離
    double pixThreshold = 12.0;
    double worldThreshold = pixThreshold;   // fallback

    if (!m_view.IsNull()) {
        // V3d_View::Convert(pixSize) → world size
        double wx1, wy1, wz1, wx2, wy2, wz2;
        m_view->Convert(0, 0, wx1, wy1, wz1);
        m_view->Convert((int)pixThreshold, 0, wx2, wy2, wz2);
        worldThreshold = gp_Pnt(wx1,wy1,wz1).Distance(gp_Pnt(wx2,wy2,wz2));
    }

    QString best;
    double  bestDist = worldThreshold;
    for (const GripPoint& gp : m_grips) {
        if (!gp.enabled) continue;
        double d = worldPos.Distance(gp.position);
        if (d < bestDist) { bestDist = d; best = gp.id; }
    }
    return best;
}

// ── Snap ─────────────────────────────────────────────────────────────
SnapResult GripManager::computeSnap(const gp_Pnt& rawPos) const
{
    SnapResult result;
    result.point = rawPos;

    // 1. Endpoint snap（其他 grips 的頂點）
    if (m_snapEndpoint) {
        for (const GripPoint& gp : m_grips) {
            if (gp.id == m_activeGripId) continue;
            if (rawPos.Distance(gp.position) < m_gridSize * 0.8) {
                result.snapped     = true;
                result.point       = gp.position;
                result.description = "Endpoint";
                return result;
            }
        }
    }

    // 2. Grid snap
    if (m_gridSnap && m_gridSize > 0.0) {
        double x = std::round(rawPos.X() / m_gridSize) * m_gridSize;
        double y = std::round(rawPos.Y() / m_gridSize) * m_gridSize;
        double z = std::round(rawPos.Z() / m_gridSize) * m_gridSize;
        result.snapped     = true;
        result.point       = gp_Pnt(x, y, z);
        result.description = "Grid";
    }

    return result;
}

// ── 滑鼠事件 ─────────────────────────────────────────────────────────
bool GripManager::mouseMoveEvent(const gp_Pnt& worldPos, int sx, int sy)
{
    if (m_handles.isEmpty()) return false;

    // ── 已選取 grip：預覽移動（不需按住滑鼠）────────────────────
    if (m_gripSelected && !m_activeGripId.isEmpty()) {

        gp_Pnt snapPos = worldPos;
        bool   snapped = false;
        QString snapDesc;

        if (m_snapManager) {
            m_snapManager->onMouseMove(sx, sy);
            auto pt3d = m_snapManager->snapPoint3D();
            if (pt3d.has_value()) {
                snapPos = *pt3d;
                snapped = true;
            }
        }

        if (!snapped) {
            SnapResult fallback = computeSnap(worldPos);
            snapPos  = fallback.point;
            snapped  = fallback.snapped;
            snapDesc = fallback.description;
        }

        m_lastSnapPos = snapPos;   // ← 儲存供 mousePressEvent 第二次點擊使用

        SnapResult result{ snapped, snapPos, snapDesc };
        Q_EMIT snapOccurred(result);

        Handle(AIS_GripHandle) h = m_handles.value(m_activeGripId);
        if (!h.IsNull()) {
            h->SetPosition(snapPos);
            m_context->RecomputePrsOnly(h, Standard_False);
            m_context->UpdateCurrentViewer();
        }

        for (GripPoint& gp : m_grips) {
            if (gp.id == m_activeGripId && gp.onDrag) {
                gp.onDrag(snapPos, snapped);
                break;
            }
        }

        Q_EMIT gripDragging(m_activeGripId, snapPos);
        return true;   // 消費 move，阻止 orbit 旋轉
    }

    // ── Hover 偵測（同原邏輯）────────────────────────────────────
    QString hoverId = hitTestGrip(worldPos);
    if (hoverId != m_hoveredGripId) {
        if (!m_hoveredGripId.isEmpty())
            updateHandleColor(m_hoveredGripId, GripState::Normal);
        m_hoveredGripId = hoverId;
        if (!m_hoveredGripId.isEmpty())
            updateHandleColor(m_hoveredGripId, GripState::Hover);
    }
    return !hoverId.isEmpty();
}

bool GripManager::mousePressEvent(const gp_Pnt& worldPos, int sx, int sy)
{
    if (m_handles.isEmpty()) return false;

    // ── 狀態一：尚未選取 grip，嘗試 hit test ───────────────────
    if (!m_gripSelected) {
        QString hitId = hitTestGrip(worldPos);
        if (hitId.isEmpty()) return false;          // 沒點到，不消費事件

        m_activeGripId = hitId;
        m_gripSelected = true;
        m_dragStartPos = worldPos;
        m_lastSnapPos  = worldPos;

        if (m_snapManager) m_snapManager->onGripDragStarted();

        updateHandleColor(hitId, GripState::Active);
        if (m_provider) m_provider->onGripDragBegin(hitId);
        Q_EMIT gripDragStarted(hitId);

        qDebug() << "[GripManager] Grip selected:" << hitId;
        return true;   // 消費此點擊
    }

    // ── 狀態二：已選取 grip，此次點擊 = 確認放置位置 ────────────
    gp_Pnt finalPos = m_lastSnapPos;   // 使用 mouseMoveEvent 中最後的 snap 結果

    if (m_provider)
        m_provider->onGripDragEnd(m_activeGripId, m_dragStartPos, finalPos);
    Q_EMIT gripDragFinished(m_activeGripId, m_dragStartPos, finalPos);

    if (m_snapManager) m_snapManager->onGripDragEnded();

    updateHandleColor(m_activeGripId, GripState::Normal);
    m_gripSelected = false;
    m_activeGripId.clear();

    refreshGrips();
    qDebug() << "[GripManager] Grip placed at" << finalPos.X() << finalPos.Y() << finalPos.Z();
    return true;
}

bool GripManager::mouseReleaseEvent(const gp_Pnt& /*worldPos*/)
{
    return false;   // 放開滑鼠不再 commit，commit 在第二次 mousePressEvent
}

void GripManager::updateHandleColor(const QString& id, GripState state)
{
    Handle(AIS_GripHandle) h = m_handles.value(id);
    if (h.IsNull() || m_context.IsNull()) return;

    h->SetGripState(state);
    m_context->RecomputePrsOnly(h, Standard_False);
    m_context->UpdateCurrentViewer();
}

void GripManager::setGridSnap(bool on, double gridSize)
{
    m_gridSnap = on;
    m_gridSize = gridSize;
}

// ── Grip 大小（依類型調整）────────────────────────────────────────────
double GripManager::gripSizeForType(GripType t)
{
    switch (t) {
    case GripType::Center:   return 5.0;
    case GripType::Vertex:   return 4.0;
    case GripType::Midpoint: return 3.5;
    case GripType::Quadrant: return 3.5;
    case GripType::Rotation: return 4.5;
    default:                 return 4.0;
    }
}

} // namespace aicad::cad
