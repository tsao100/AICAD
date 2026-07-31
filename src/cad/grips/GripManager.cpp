// src/cad/grips/GripManager.cpp
#include "GripManager.h"
#include "SketchGripProvider.h"
#include <QDebug>
#include <cmath>
#include <IntAna_IntConicQuad.hxx>
#include <gp_Pln.hxx>
#include <gp_Lin.hxx>
#include <gp_Vec.hxx>
#include <Precision.hxx>

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
    if (m_snapManager) m_snapManager->clearSnapExcludePoint();

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
QString GripManager::hitTestGrip(const gp_Pnt& worldPos, double threshold) const
{
    // Compute worldThreshold = world distance for pixThreshold pixels at Z=0,
    // using the same ray-plane method as GripEventFilter::screenToWorld.
    //
    // ✅ 修正：原本忽略呼叫端傳入的 threshold，內部寫死 12px，比 SketchPointAIS
    // 等其他可選取物件慣用的判定範圍明顯大上許多，導致 Grip 的 hover/點擊
    // 感應範圍過大（容易誤觸相鄰 Grip）。現在改為實際使用傳入的 threshold
    // （預設 6px，見 GripManager.h），與 sketch point 的判定尺度一致。
    const double pixThreshold = threshold;
    // ✅ fallback 值原本是 50.0——對一般 sketch 局部座標（通常只有幾十
    // 單位）來說完全不合理，一旦真的落到 fallback（例如 m_view 意外失效）
    // 會讓 hover/點擊範圍大到幾乎涵蓋整張圖。改成明顯偏小、安全失敗的值，
    // 並記錄警告以便未來若再發生能立刻在 log 中發現，而不是靜默吃下一個
    // 不合理的大範圍。正常情況下（m_view 有效）下面會被即時計算出的
    // worldThreshold 覆蓋，不會用到這個 fallback。
    double worldThreshold = 2.0;   // fallback（僅在 m_view 無效時使用）

    if (m_view.IsNull()) {
        qWarning() << "[GripManager] hitTestGrip: m_view is null, "
                       "falling back to a small default threshold ("
                    << worldThreshold << "). This should not happen "
                       "once the view is ready — check GripManager::setView() wiring.";
    }

    if (!m_view.IsNull()) {
        auto projectToZ0 = [&](int sx, int sy) -> gp_Pnt {
            Standard_Real Xeye,Yeye,Zeye, Xproj,Yproj,Zproj, Xv,Yv,Zv;
            m_view->Eye (Xeye,  Yeye,  Zeye);
            m_view->Proj(Xproj, Yproj, Zproj);
            m_view->Convert(sx, sy, Xv, Yv, Zv);

            gp_Pnt  rayStart;
            gp_Dir  rayDir;
            if (m_view->Camera()->IsOrthographic()) {
                rayStart = gp_Pnt(Xv,Yv,Zv);
                rayDir   = gp_Dir(Xproj,Yproj,Zproj);
            } else {
                rayStart = gp_Pnt(Xeye,Yeye,Zeye);
                gp_Vec v(rayStart, gp_Pnt(Xv,Yv,Zv));
                rayDir = v.Magnitude() > Precision::Confusion()
                         ? gp_Dir(v) : gp_Dir(Xproj,Yproj,Zproj);
            }

            gp_Pln plane(gp_Pnt(0,0,0), gp_Dir(0,0,1));
            IntAna_IntConicQuad inter(gp_Lin(rayStart,rayDir), plane, Precision::Angular());
            if (inter.IsDone() && inter.NbPoints() > 0)
                return inter.Point(1);
            return gp_Pnt(Xv,Yv,0.0);
        };

        // ✅ 修正：原本固定在螢幕角落 (0,0) 附近取樣像素→世界座標的比例，
        // 但只要目前視角不是正上方俯視（例如稍微傾斜、或透視投影），
        // 螢幕角落的射線打到 Z=0 平面的角度可能與實際游標/Grip 所在
        // 位置差異很大（甚至接近掠射角），算出來的「每像素對應世界
        // 距離」會被嚴重放大 —— 這正是回報的「約 100 單位」異常大範圍
        // 的成因。改為先把 worldPos 換算成它自己的螢幕座標，再以該點
        // 為基準做局部取樣，讓比例尺與實際判定位置一致。
        Standard_Integer sx0 = 0, sy0 = 0;
        m_view->Convert(worldPos.X(), worldPos.Y(), worldPos.Z(), sx0, sy0);

        gp_Pnt w0 = projectToZ0(sx0, sy0);
        gp_Pnt w1 = projectToZ0(sx0 + (int)pixThreshold, sy0);
        double d  = w0.Distance(w1);
        if (d > 1e-6) worldThreshold = d;
    }

    qDebug() << "[GripManager] hitTestGrip worldPos=("
             << worldPos.X() << worldPos.Y() << worldPos.Z()
             << ") threshold=" << worldThreshold;

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
            // ★ 用上一幀的位置（m_lastSnapPos）作為排除點
            //   避免 snap to itself，且不需要從 AIS_GripHandle 取位置
            m_snapManager->setSnapExcludePoint(m_lastSnapPos, 1.0);

            m_snapManager->onMouseMove(sx, sy);
            auto pt3d = m_snapManager->snapPoint3D();
            if (pt3d.has_value()) {
                snapPos = *pt3d;
                snapped = true;
            }
        }

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

        // ── Ortho Lock（F8）：沒有其他 snap 命中時，鎖定沿平面 X/Y 軸方向 ──────
        //    用平面自身的 X/Y 軸（m_planeX/m_planeY，草圖可能是任意方向的平面，
        //    對齊 alignment 則預設為世界 X/Y）投影，而非直接比較世界座標差，
        //    以確保非水平草圖平面上的 ortho 行為同樣正確。
        if (!snapped && m_orthoLock) {
            gp_Vec delta(m_dragStartPos, worldPos);
            double du = delta.Dot(gp_Vec(m_planeX));
            double dv = delta.Dot(gp_Vec(m_planeY));
            gp_Vec offset = (std::abs(du) >= std::abs(dv))
                                ? gp_Vec(m_planeX) * du
                                : gp_Vec(m_planeY) * dv;
            snapPos  = m_dragStartPos.Translated(offset);
            snapped  = true;
            snapDesc = "Ortho";
        }

        m_lastSnapPos = snapPos;   // ← 儲存供 mousePressEvent 第二次點擊使用

        SnapResult result{ snapped, snapPos, snapDesc };
        Q_EMIT snapOccurred(result);

        // Handle(AIS_GripHandle) h = m_handles.value(m_activeGripId);
        // if (!h.IsNull()) {
        //     h->SetPosition(snapPos);
        //     m_context->RecomputePrsOnly(h, Standard_False);
        //     m_context->UpdateCurrentViewer();
        // }

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

bool GripManager::mousePressEvent(const gp_Pnt& worldPos, int /*sx*/, int /*sy*/)
{
    if (m_handles.isEmpty()) {
        qDebug() << "[GripManager] mousePressEvent: m_handles is EMPTY — no grips displayed";
        return false;
    }

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
    finalizeDrag(m_lastSnapPos);   // 使用 mouseMoveEvent 中最後的 snap 結果
    return true;
}

void GripManager::finalizeDrag(const gp_Pnt& finalPos)
{
    if (m_provider)
        m_provider->onGripDragEnd(m_activeGripId, m_dragStartPos, finalPos);
    Q_EMIT gripDragFinished(m_activeGripId, m_dragStartPos, finalPos);

    if (m_snapManager) m_snapManager->onGripDragEnded();
    if (m_snapManager) m_snapManager->clearSnapExcludePoint();

    updateHandleColor(m_activeGripId, GripState::Normal);
    m_gripSelected = false;
    m_activeGripId.clear();

    refreshGrips();
    qDebug() << "[GripManager] Grip placed at" << finalPos.X() << finalPos.Y() << finalPos.Z();
}

void GripManager::commitDragAt(const gp_Pnt& pos)
{
    if (!m_gripSelected || m_activeGripId.isEmpty()) return;

    // 先套用最終位置到幾何（等同 mouseMoveEvent 的即時預覽），
    // 確保 onGripDragEnd 前幾何已經反映 InputJig 指定的座標。
    for (GripPoint& gp : m_grips) {
        if (gp.id == m_activeGripId && gp.onDrag) {
            gp.onDrag(pos, false);
            break;
        }
    }
    finalizeDrag(pos);
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
