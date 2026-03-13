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
bool GripManager::mouseMoveEvent(const gp_Pnt& worldPos)
{
    if (m_handles.isEmpty()) return false;

    if (m_isDragging && !m_activeGripId.isEmpty()) {
        // ── 正在拖拉 ──────────────────────────────────────────────
        SnapResult snap = computeSnap(worldPos);
        Q_EMIT snapOccurred(snap);

        // 更新 AIS handle 位置
        Handle(AIS_GripHandle) h = m_handles.value(m_activeGripId);
        if (!h.IsNull()) {
            h->SetPosition(snap.point);
            m_context->RecomputePrsOnly(h, Standard_False);
            m_context->UpdateCurrentViewer();
        }

        // 呼叫 provider 即時更新幾何
        for (GripPoint& gp : m_grips) {
            if (gp.id == m_activeGripId && gp.onDrag) {
                gp.onDrag(snap.point, snap.snapped);
                break;
            }
        }

        Q_EMIT gripDragging(m_activeGripId, snap.point);
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

bool GripManager::mousePressEvent(const gp_Pnt& worldPos)
{
    if (m_handles.isEmpty()) return false;

    QString hitId = hitTestGrip(worldPos);
    if (hitId.isEmpty()) return false;

    m_activeGripId = hitId;
    m_isDragging   = true;
    m_dragStartPos = worldPos;

    updateHandleColor(hitId, GripState::Active);

    if (m_provider) m_provider->onGripDragBegin(hitId);
    Q_EMIT gripDragStarted(hitId);

    qDebug() << "[GripManager] Drag started:" << hitId;
    return true;
}

bool GripManager::mouseReleaseEvent(const gp_Pnt& worldPos)
{
    if (!m_isDragging) return false;

    SnapResult snap = computeSnap(worldPos);

    if (m_provider) {
        m_provider->onGripDragEnd(m_activeGripId, m_dragStartPos, snap.point);
    }

    Q_EMIT gripDragFinished(m_activeGripId, m_dragStartPos, snap.point);

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
