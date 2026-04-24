// src/cad/grips/GripManager.cpp
#include "GripManager.h"
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
void GripManager::attachProvider(IGripProvider* provider)
{
    detach();
    m_provider = provider;
    if (m_provider) {
        m_grips = m_provider->computeGrips();
        displayHandles();
    }
}

void GripManager::detach()
{
    m_isDragging    = false;
    m_activeGripId.clear();
    m_hoveredGripId.clear();

    eraseHandles();
    m_handles.clear();
    m_grips.clear();
    m_provider    = nullptr;
    m_isDragging  = false;
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
        // 中止懸掛的 hover 狀態
        m_hoveredGripId.clear();
    } else {
        // 重新顯示（provider 還在）
        refreshGrips();
    }
    qDebug() << "[GripManager] Grips" << (enabled ? "enabled" : "disabled");
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
QString GripManager::hitTestGrip(const gp_Pnt& worldPos, double threshold) const
{
    QString best;
    double  bestDist = threshold;

    for (const GripPoint& gp : m_grips) {
        if (!gp.enabled) continue;
        double d = worldPos.Distance(gp.position);
        if (d < bestDist) {
            bestDist = d;
            best     = gp.id;
        }
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

    if (m_isDragging && !m_activeGripId.isEmpty()) {

        // ── OSnap 優先 ──────────────────────────────────────────
        gp_Pnt snapPos = worldPos;
        bool   snapped = false;
        QString snapDesc;

        if (m_snapManager) {
            m_snapManager->onMouseMove(sx, sy);      // 驅動偵測
            auto pt3d = m_snapManager->snapPoint3D();
            if (pt3d.has_value()) {
                snapPos   = *pt3d;
                snapped   = true;
                //snapDesc  = m_snapManager->currentSnapDescription(); // 可選
            }
        }

        // ── OSnap 無結果時退回 grip-to-grip / grid snap ─────────
        if (!snapped) {
            SnapResult fallback = computeSnap(worldPos);
            snapPos  = fallback.point;
            snapped  = fallback.snapped;
            snapDesc = fallback.description;
        }

        SnapResult result{ snapped, snapPos, snapDesc };
        Q_EMIT snapOccurred(result);

        // 更新 AIS handle 位置
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
        return true;
    }

    // ── Hover 偵測 ────────────────────────────────────────────────
    QString hoverId = hitTestGrip(worldPos);

    if (hoverId != m_hoveredGripId) {
        // 恢復舊 hover
        if (!m_hoveredGripId.isEmpty())
            updateHandleColor(m_hoveredGripId, GripState::Normal);

        m_hoveredGripId = hoverId;

        // 高亮新 hover
        if (!m_hoveredGripId.isEmpty())
            updateHandleColor(m_hoveredGripId, GripState::Hover);
    }

    return !hoverId.isEmpty();
}

bool GripManager::mousePressEvent(const gp_Pnt& worldPos, int sx, int sy)
{
    if (m_handles.isEmpty()) return false;
    QString hitId = hitTestGrip(worldPos);
    if (hitId.isEmpty()) return false;

    m_activeGripId = hitId;
    m_isDragging   = true;
    m_dragStartPos = worldPos;

    // 通知 OSnapManager 進入 drag 模式
    if (m_snapManager) m_snapManager->onGripDragStarted();

    updateHandleColor(hitId, GripState::Active);
    if (m_provider) m_provider->onGripDragBegin(hitId);
    Q_EMIT gripDragStarted(hitId);
    return true;
}

bool GripManager::mouseReleaseEvent(const gp_Pnt& worldPos)
{
    if (!m_isDragging) return false;

    // 用最後一次 snap 結果做最終位置
    gp_Pnt finalPos = worldPos;
    if (m_snapManager) {
        auto pt3d = m_snapManager->snapPoint3D();
        if (pt3d.has_value()) finalPos = *pt3d;
        m_snapManager->onGripDragEnded();             // 清除 drag 狀態
    }

    bool didSnap = finalPos.Distance(worldPos) > Precision::Confusion();
    SnapResult snap{ didSnap, finalPos, QString{} };

    if (m_provider)
        m_provider->onGripDragEnd(m_activeGripId, m_dragStartPos, finalPos);
    Q_EMIT gripDragFinished(m_activeGripId, m_dragStartPos, finalPos);

    updateHandleColor(m_activeGripId, GripState::Normal);
    m_isDragging   = false;
    m_activeGripId.clear();

    // 拖拉結束後重新計算所有 grip 位置
    refreshGrips();

    qDebug() << "[GripManager] Drag ended";
    return true;
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
