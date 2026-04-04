/**
 * @file OSnapManager.cpp
 * @brief Object Snap 管理器實作
 */

#include "OSnapManager.h"

#include "../core/Application.h"
#include "../core/EventBus.h"
#include "../cad/Plane.h"

#include <AIS_InteractiveContext.hxx>
#include <V3d_View.hxx>

#include <QDebug>
#include <QVariantMap>

namespace aicad {
namespace osnap {

// ──────────────────────────────────────────────────────────────────────────────
OSnapManager::OSnapManager(QObject* parent)
    : QObject(parent)
    , m_indicator(new OSnapIndicator())
{
    qDebug() << "[OSnapManager] Created";
}

OSnapManager::~OSnapManager() {
    shutdown();
    qDebug() << "[OSnapManager] Destroyed";
}

// ──────────────────────────────────────────────────────────────────────────────
void OSnapManager::initialize(const Handle(AIS_InteractiveContext)& context,
                              const Handle(V3d_View)& view)
{
    if (m_initialized) {
        qWarning() << "[OSnapManager] Already initialized";
        return;
    }

    m_indicator->setView(view);   // ✅ 新增

    m_context = context;
    m_view    = view;

    m_detector.setSettings(m_settings);

    // OSnapIndicator 加入 context 但先隱藏
    if (!m_context.IsNull()) {
        m_context->Display(m_indicator, Standard_False);
        m_context->Deactivate(m_indicator);  // 不參與選取
        m_context->Erase(m_indicator, Standard_False);
    }

    connectEventBus();

    m_initialized = true;
    qDebug() << "[OSnapManager] Initialized";
}

void OSnapManager::shutdown() {
    if (!m_initialized) return;

    // 從 context 移除指示器
    if (!m_context.IsNull() && !m_indicator.IsNull()) {
        m_context->Erase(m_indicator, Standard_False);
        m_context->Remove(m_indicator, Standard_False);
    }

    m_initialized = false;
    qDebug() << "[OSnapManager] Shut down";
}

// ──────────────────────────────────────────────────────────────────────────────
//  Snap 模式控制
// ──────────────────────────────────────────────────────────────────────────────
void OSnapManager::setSnapEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;

    if (!enabled) {
        m_currentSnap.reset();
        updateIndicator(std::nullopt);
        Q_EMIT snapCleared();
    }

    qDebug() << "[OSnapManager] Snap" << (enabled ? "enabled" : "disabled");
    Q_EMIT settingsChanged(m_settings);
}

void OSnapManager::setSnapTypeEnabled(SnapType type, bool enabled) {
    if (enabled) {
        m_settings.enabledTypes |= type;
    } else {
        m_settings.enabledTypes &= ~SnapTypes(type);
    }
    m_detector.setSettings(m_settings);

    qDebug() << "[OSnapManager] SnapType" << snapTypeName(type)
             << (enabled ? "enabled" : "disabled");
    Q_EMIT settingsChanged(m_settings);
}

void OSnapManager::setSettings(const OSnapSettings& s) {
    m_settings = s;
    m_detector.setSettings(s);
    Q_EMIT settingsChanged(s);
}

// ──────────────────────────────────────────────────────────────────────────────
//  草圖平面
// ──────────────────────────────────────────────────────────────────────────────
void OSnapManager::setActivePlane(cad::Plane* plane) {
    m_detector.setActivePlane(plane);
    qDebug() << "[OSnapManager] Active plane:"
             << (plane ? plane->displayName() : "none");
}

cad::Plane* OSnapManager::activePlane() const {
    return m_detector.activePlane();
}

void OSnapManager::setActiveSketch(cad::Sketch* sketch) {
    m_detector.setActiveSketch(sketch);
    qDebug() << "[OSnapManager] Active sketch:"
             << (sketch ? sketch->id() : "none");
}

void OSnapManager::setLastInputPoint(const gp_Pnt& pt) {
    m_detector.setLastInputPoint(pt);
}

void OSnapManager::clearLastInputPoint() {
    m_detector.clearLastInputPoint();
}

// ──────────────────────────────────────────────────────────────────────────────
//  座標查詢
// ──────────────────────────────────────────────────────────────────────────────
std::optional<gp_Pnt> OSnapManager::snapPoint() const {
    if (!m_currentSnap.has_value()) return std::nullopt;
    return m_currentSnap->worldPoint;
}

std::optional<QVector2D> OSnapManager::snapPoint2D() const {
    if (!m_currentSnap.has_value()) return std::nullopt;
    cad::Plane* plane = m_detector.activePlane();
    if (!plane) return std::nullopt;

    const gp_Pnt& wp = m_currentSnap->worldPoint;
    QVector3D worldPt(wp.X(), wp.Y(), wp.Z());
    return plane->toPlane(worldPt);
}

// ──────────────────────────────────────────────────────────────────────────────
//  滑鼠事件處理
// ──────────────────────────────────────────────────────────────────────────────
void OSnapManager::onMouseMove(int mouseX, int mouseY) {
    if (!m_initialized || !m_enabled || m_gripActive) {
        if (m_currentSnap.has_value()) {
            m_currentSnap.reset();
            updateIndicator(std::nullopt);
            Q_EMIT snapCleared();
        }
        return;
    }

    if (m_context.IsNull() || m_view.IsNull()) return;

    // 執行偵測
    auto result = m_detector.detect(m_context, m_view, mouseX, mouseY);

    bool changed = (result.has_value() != m_currentSnap.has_value());
    if (!changed && result.has_value() && m_currentSnap.has_value()) {
        // 檢查是否真的改變了
        changed = (result->type != m_currentSnap->type) ||
                  (result->worldPoint.Distance(m_currentSnap->worldPoint) > 1e-6);
    }

    if (!changed) return;

    m_currentSnap = result;
    updateIndicator(result);

    if (result.has_value()) {
        Q_EMIT snapLocked(*result);
        publishSnapEvent(*result);
    } else {
        Q_EMIT snapCleared();

        // EventBus
        auto* bus = core::Application::instance()->eventBus();
        if (bus) bus->publish("osnap.cleared", QVariant{});
    }
}

bool OSnapManager::onMousePress(int mouseX, int mouseY) {
    if (!m_initialized || !isSnapActive()) return false;

    const SnapCandidate& snap = *m_currentSnap;

    qDebug() << "[OSnapManager] Snap confirmed:"
             << snapTypeName(snap.type)
             << "at" << snap.worldPoint.X()
             << snap.worldPoint.Y()
             << snap.worldPoint.Z();

    Q_EMIT snapConfirmed(snap.worldPoint, snap.type);

    // EventBus
    auto* bus = core::Application::instance()->eventBus();
    if (bus) {
        QVariantMap data;
        data["x"]        = snap.worldPoint.X();
        data["y"]        = snap.worldPoint.Y();
        data["z"]        = snap.worldPoint.Z();
        data["type"]     = static_cast<int>(snap.type);
        data["typeName"] = snapTypeName(snap.type);
        bus->publish("osnap.confirmed", data);
    }

    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
//  指示器顯示控制
// ──────────────────────────────────────────────────────────────────────────────
void OSnapManager::showIndicator() {
    if (!m_context.IsNull() && !m_indicator.IsNull()) {
        m_context->Display(m_indicator, Standard_False);
        m_context->UpdateCurrentViewer();
    }
}

void OSnapManager::hideIndicator() {
    if (!m_context.IsNull() && !m_indicator.IsNull()) {
        m_context->Erase(m_indicator, Standard_False);
        m_context->UpdateCurrentViewer();
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  Grip 系統協同
// ──────────────────────────────────────────────────────────────────────────────
void OSnapManager::onGripHovered() {
    if (!m_gripActive) {
        m_gripActive = true;
        // Grip 活躍時隱藏 snap 指示器
        m_currentSnap.reset();
        updateIndicator(std::nullopt);
        qDebug() << "[OSnapManager] Grip active, OSnap suspended";
    }
}

void OSnapManager::onGripReleased() {
    if (m_gripActive) {
        m_gripActive = false;
        qDebug() << "[OSnapManager] Grip released, OSnap resumed";
    }
}

// ──────────────────────────────────────────────────────────────────────────────
//  內部輔助
// ──────────────────────────────────────────────────────────────────────────────
void OSnapManager::updateIndicator(const std::optional<SnapCandidate>& snap) {
    if (m_context.IsNull() || m_indicator.IsNull()) return;

    if (snap.has_value()) {
        m_indicator->setCandidate(*snap);
        m_context->Display(m_indicator, Standard_False);
        m_indicator->SetToUpdate();
        m_context->UpdateCurrentViewer();
    } else {
        m_indicator->clearCandidate();
        m_context->Erase(m_indicator, Standard_False);
        m_context->UpdateCurrentViewer();
    }
}

void OSnapManager::publishSnapEvent(const SnapCandidate& c) {
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;

    QVariantMap data;
    data["x"]        = c.worldPoint.X();
    data["y"]        = c.worldPoint.Y();
    data["z"]        = c.worldPoint.Z();
    data["type"]     = static_cast<int>(c.type);
    data["typeName"] = snapTypeName(c.type);

    // 若有 2D 平面座標也一起發送
    if (m_detector.activePlane()) {
        auto pt2d = snapPoint2D();
        if (pt2d.has_value()) {
            data["u"] = pt2d->x();
            data["v"] = pt2d->y();
        }
    }

    bus->publish("osnap.locked", data);
}

void OSnapManager::connectEventBus() {
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;

    // ── 訂閱 Grip 事件，與 Grip 系統協同 ──────────────────────────────────────
    bus->subscribe("grip.hovered", this, [this](const QVariant&) {
        onGripHovered();
    });

    bus->subscribe("grip.released", this, [this](const QVariant&) {
        onGripReleased();
    });

    // ── 訂閱 Sketch 進入/退出編輯事件 ─────────────────────────────────────────
    bus->subscribe("sketch.editStarted", this, [this](const QVariant& v) {
        // Sketch 開始編輯時啟用 snap
        setSnapEnabled(true);
        qDebug() << "[OSnapManager] Snap enabled (sketch edit started)";
    });

    bus->subscribe("sketch.editEnded", this, [this](const QVariant&) {
        setSnapEnabled(false);
        clearLastInputPoint();
        qDebug() << "[OSnapManager] Snap disabled (sketch edit ended)";
    });

    // ✅ 新增：自動追蹤最後確認的輸入點，供 Perp/Tangent snap 使用
    bus->subscribe(core::Events::POINT_ACQUIRED, this, [this](const QVariant& v) {
        QVariantMap map = v.toMap();
        QVector2D pt2d = map["point"].value<QVector2D>();
        // 若有 activePlane，轉回世界座標設定
        cad::Plane* plane = m_detector.activePlane();
        if (plane) {
            QVector3D w = plane->toWorld(pt2d);
            m_detector.setLastInputPoint(gp_Pnt(w.x(), w.y(), w.z()));
        }
    });

    qDebug() << "[OSnapManager] EventBus connected";
}

} // namespace osnap
} // namespace aicad
