/**
 * @file OSnapManager.h
 * @brief Object Snap 管理器 - 統籌偵測、顯示、與事件系統整合
 *
 * 職責：
 *  1. 持有 OSnapDetector（純算法）和 OSnapIndicator（AIS 顯示）
 *  2. 連接滑鼠事件 → 偵測 → 更新指示器 → 發布 EventBus 事件
 *  3. 管理 snap 模式開關（與 OSnapToolbar 雙向綁定）
 *  4. 與 Grip 系統協同：若目前有 Grip 活躍，OSnap 讓步
 *  5. 提供 isSnapActive() 讓 Command 系統查詢目前是否有鎖定的 snap 點
 *
 * 與 Grip 系統的協定：
 *  - GripManager 在 grip hover 時 publish "grip.hovered"
 *  - OSnapManager 訂閱此事件，收到後暫停 snap 偵測
 *  - GripManager 在 grip released 時 publish "grip.released"
 *  - OSnapManager 收到後恢復 snap 偵測
 *
 * EventBus 事件：
 *  Published:
 *   "osnap.locked"    → QVariantMap { "x","y","z","type","typeName" }
 *   "osnap.cleared"   → QVariant{}
 *
 *  Subscribed:
 *   "grip.hovered"    → 暫停 OSnap
 *   "grip.released"   → 恢復 OSnap
 *
 * @author AICAD Team
 * @date 2025-01-08
 */

#pragma once

#include "OSnapTypes.h"
#include "OSnapDetector.h"
#include "OSnapIndicator.h"

#include <AIS_InteractiveContext.hxx>
#include <V3d_View.hxx>

#include <QObject>
#include <QVector2D>
#include <QVector3D>
#include <optional>

namespace aicad {
namespace cad  {
class Plane;
class Sketch;
}
namespace osnap {

class OSnapManager : public QObject
{
    Q_OBJECT

public:
    explicit OSnapManager(QObject* parent = nullptr);
    ~OSnapManager() override;

    // ── 初始化 ────────────────────────────────────────────────────────────────
    /// 必須在 AIS_InteractiveContext 建立後呼叫
    void initialize(const Handle(AIS_InteractiveContext)& context,
                    const Handle(V3d_View)& view);

    /// 清理（關閉文件時呼叫）
    void shutdown();

    // ── Snap 模式控制 ─────────────────────────────────────────────────────────
    void setSnapEnabled(bool enabled);
    bool isSnapEnabled() const { return m_enabled && !m_gripHovered; }

    void setSnapTypeEnabled(SnapType type, bool enabled);
    bool isSnapTypeEnabled(SnapType type) const {
        return m_settings.enabledTypes.testFlag(type);
    }

    void setSettings(const OSnapSettings& s);
    const OSnapSettings& settings() const { return m_settings; }

    // ── 草圖平面模式（2D） ─────────────────────────────────────────────────────
    void setActivePlane(cad::Plane* plane);
    cad::Plane* activePlane() const;
    void setActiveSketch(cad::Sketch* sketch);   // ⭐新增

    /// 設定「上一個確認的輸入點」（用於 Perpendicular/Tangent）
    void setLastInputPoint(const gp_Pnt& pt);
    void clearLastInputPoint();

    // ── 即時狀態查詢 ──────────────────────────────────────────────────────────
    /// 目前是否有鎖定的 snap 點（可在 mouse press 時使用）
    bool isSnapActive() const { return m_currentSnap.has_value(); }

    /// 取得目前鎖定的 snap 候選（若無則 nullopt）
    std::optional<SnapCandidate> currentSnap() const { return m_currentSnap; }

    /// 取得目前鎖定的世界座標（無 snap 時回傳 nullopt）
    std::optional<gp_Pnt> snapPoint() const;

    /// 取得目前鎖定的 2D 草圖平面座標（無平面或無 snap 時 nullopt）
    std::optional<QVector2D> snapPoint2D() const;

    // ── 滑鼠事件入口（由 CadView 呼叫） ────────────────────────────────────────
    /**
     * @brief 滑鼠移動時呼叫，執行 snap 偵測並更新視覺指示器
     * @param mouseX  螢幕 X（像素）
     * @param mouseY  螢幕 Y（像素）
     */
    void onMouseMove(int mouseX, int mouseY);

    /**
     * @brief 滑鼠按下時呼叫，若有鎖定的 snap 點則確認
     * @param mouseX  螢幕 X
     * @param mouseY  螢幕 Y
     * @return 若有 snap 鎖定點，回傳 true（呼叫端應使用 snapPoint() 取得座標）
     */
    bool onMousePress(int mouseX, int mouseY);

    // ── 指示器管理 ────────────────────────────────────────────────────────────
    void showIndicator();
    void hideIndicator();
    OSnapDetector& detector() { return m_detector; }
    void refreshIndicator() { updateIndicator(m_currentSnap); }

    void onGripDragStarted();
    void onGripDragEnded();

    std::optional<gp_Pnt> snapPoint3D() const;

Q_SIGNALS:
    /// 有新的 snap 點鎖定
    void snapLocked(const SnapCandidate& candidate);

    /// snap 解除
    void snapCleared();

    /// snap 模式設定改變
    void settingsChanged(const OSnapSettings& settings);

    /// snap 點已確認（滑鼠按下時）
    void snapConfirmed(const gp_Pnt& worldPoint, SnapType type);

private Q_SLOTS:
    void onGripHovered();
    void onGripReleased();

private:
    void updateIndicator(const std::optional<SnapCandidate>& snap);
    void publishSnapEvent(const SnapCandidate& c);
    void connectEventBus();

    Handle(AIS_InteractiveContext) m_context;
    Handle(V3d_View)               m_view;
    Handle(OSnapIndicator)         m_indicator;

    OSnapDetector                  m_detector;
    OSnapSettings                  m_settings;

    std::optional<SnapCandidate>   m_currentSnap;

    bool m_enabled     = true;
    bool m_initialized = false;
    bool m_gripHovered  = false;  ///< Grip hover 中（游標在 handle 上）→ 抑制 OSnap 指示器
    bool m_gripDragging = false;  ///< Grip drag 中 → OSnap 應作用
    bool m_eventBusConnected = false;

};

} // namespace osnap
} // namespace aicad
