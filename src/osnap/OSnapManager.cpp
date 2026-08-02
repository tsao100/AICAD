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
    // ✅ 確保跨執行緒 signal 安全
    qRegisterMetaType<aicad::osnap::SnapCandidate>("SnapCandidate");
    qRegisterMetaType<aicad::osnap::OSnapSettings>("OSnapSettings");
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

    // ✅ 先從 EventBus 取消所有訂閱
    auto* bus = core::Application::instance()->eventBus();
    if (bus) bus->unsubscribeAll(this);

    // 從 context 移除指示器
    if (!m_context.IsNull() && !m_indicator.IsNull()) {
        m_context->Erase(m_indicator, Standard_False);
        m_context->Remove(m_indicator, Standard_False);
    }

    m_initialized = false;
    m_eventBusConnected = false;  // 配合問題三十五
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

    // ✅ Grid 類型需要同步更新 gridSnapEnabled 旗標
    if (type == SnapType::Grid) {
        m_settings.gridSnapEnabled = enabled;
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

    // ✅ 同步更新 indicator 的平面軸向
    if (!m_indicator.IsNull() && plane) {
        QVector3D qx = plane->xAxis();
        QVector3D qy = plane->yAxis();
        m_indicator->setPlaneAxes(
            gp_Dir(qx.x(), qx.y(), qx.z()),
            gp_Dir(qy.x(), qy.y(), qy.z()));
    }

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

void OSnapManager::setAlignmentDocument(railway::AlignmentDocument* doc) {
    m_detector.setAlignmentDocument(doc);
    qDebug() << "[OSnapManager] Alignment document:" << (doc ? "set" : "none");
}

void OSnapManager::setAlignmentDocuments(const QVector<railway::AlignmentDocument*>& docs) {
    m_detector.setAlignmentDocuments(docs);
    qDebug() << "[OSnapManager] Alignment documents:" << docs.size();
}

void OSnapManager::setHorizontalAlignments(const QVector<railway::HorizontalAlignment*>& haligns) {
    m_detector.setHorizontalAlignments(haligns);
    qDebug() << "[OSnapManager] Horizontal alignments:" << haligns.size();
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
    const gp_Pnt& wp = m_currentSnap->worldPoint;

    cad::Plane* plane = m_detector.activePlane();
    if (!plane) {
        // ✅ 修正：Alignment edit（FC/FT/AS/SCS 等）沒有 cad::Plane 這個
        // 「草圖平面」概念，activePlane() 永遠是 nullptr——先前遇到這種情況
        // 直接 return nullopt，導致 CadView 的 snapConfirmed 處理器（見
        // CadView.cpp 的 `pt2d.has_value() ? pt2d.value() : screenToPlaneD(...)`
        // 那段）誤判成「沒有鎖點」，改用當下滑鼠游標的原始位置重新投影，
        // 結果就是：OSnap 明明已經正確鎖到 alignment 上的點（log 裡的
        // "Snap confirmed"），最後卻送出一個跟游標當下位置幾乎一樣、但跟
        // 鎖點對不上的座標。Alignment 系統全程都在 Z=0 的 XY 地平面上運作
        // （見 detectAlignmentSnapForHAlign()／AlignmentRenderer 一律用
        // gp_Pnt(easting, northing, 0.0)），因此沒有 cad::Plane 時，直接
        // 回傳世界座標的 X/Y 即為正確結果，不需要（也不能）放棄。
        return QVector2D(static_cast<float>(wp.X()), static_cast<float>(wp.Y()));
    }

    QVector3D worldPt(wp.X(), wp.Y(), wp.Z());
    return plane->toPlane(worldPt);
}

std::optional<QPointF> OSnapManager::snapPoint2DF() const {
    if (!m_currentSnap.has_value()) return std::nullopt;
    const gp_Pnt& wp = m_currentSnap->worldPoint;

    cad::Plane* plane = m_detector.activePlane();
    if (!plane) {
        // ✅ 修正：理由同上（snapPoint2D() 的說明）——Alignment edit 沒有
        // 作用中的 cad::Plane，直接回傳 XY 地平面座標，保留 double 精度
        // （TM2 大座標需要）。
        return QPointF(wp.X(), wp.Y());
    }
    // 直接使用 gp_Pnt 的 double 座標，經 toPlaneD() 保持精度，不經 QVector3D float 轉換
    return plane->toPlaneD(QVector3D(wp.X(), wp.Y(), wp.Z()));
}

// ──────────────────────────────────────────────────────────────────────────────
//  滑鼠事件處理
// ──────────────────────────────────────────────────────────────────────────────
void OSnapManager::onMouseMove(int mouseX, int mouseY) {
    // hover 中：OSnap 讓步給 grip handle 游標
    if (m_gripHovered) {
        if (m_currentSnap.has_value()) {
            m_currentSnap.reset();
            updateIndicator(std::nullopt);
            Q_EMIT snapCleared();
        }
        return;
    }
    // drag 中：m_enabled 可能為 false（無 command），但仍需偵測
    if (!m_initialized) return;

    // ✅ 修正：grip drag 中即使 m_enabled=false 也要偵測
    if (!m_enabled && !m_gripDragging) {
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

bool OSnapManager::onMousePress(int /*mouseX*/, int /*mouseY*/) {
    if (!m_initialized || !isSnapActive()) return false;

    const SnapCandidate& snap = *m_currentSnap;

    qDebug() << "[OSnapManager] Snap confirmed:"
             << snapTypeName(snap.type)
             << "at" << snap.worldPoint.X()
             << snap.worldPoint.Y()
             << snap.worldPoint.Z();

    // Q_EMIT snapConfirmed → CadView 的 lambda 會將世界座標投影到草圖平面，
    // 並以正確格式發布 POINT_ACQUIRED，Command 系統從那裡取點。
    Q_EMIT snapConfirmed(snap.worldPoint, snap.type);

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
    m_gripHovered = true;
    m_currentSnap.reset();
    updateIndicator(std::nullopt);
}

void OSnapManager::onGripReleased() {
    m_gripHovered  = false;
    m_gripDragging = false;   // 防禦性清除
}

void OSnapManager::onGripDragStarted() {
    m_gripHovered  = false;   // hover 解除（handle 已被捕捉）
    m_gripDragging = true;
}

void OSnapManager::onGripDragEnded() {
    m_gripDragging = false;
    m_currentSnap.reset();
    updateIndicator(std::nullopt);
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
    if (m_eventBusConnected) return;   // ✅ 防重複訂閱
    m_eventBusConnected = true;

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
        // ✅ 若事件帶有 Sketch* 則同時設定平面（防禦性）
        auto* sketch = v.value<cad::Sketch*>();
        if (sketch && sketch->plane()) {
            setActivePlane(sketch->plane());
            setActiveSketch(sketch);
        }
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


std::optional<gp_Pnt> OSnapManager::snapPoint3D() const {
    if (!m_currentSnap.has_value()) return std::nullopt;
    // m_currentSnap 是 2D sketch 平面座標，需投影回 3D
    // 若 m_activePlane 存在則做投影，否則直接用原始 3D hit
    return m_currentSnap->worldPoint;  // 假設 OSnapResult 已存 3D 點
}

} // namespace osnap
} // namespace aicad
