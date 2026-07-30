#include "LeaderNoteCommand.h"
#include "../core/Application.h"
#include "../core/CommandLineManager.h"
#include "../core/EventBus.h"
#include "../cad/Sketch.h"
#include "../cad/sketch/SketchAnnotation.h"
#include "../ui/UIManager.h"
#include "../view/CadView.h"
#include <QMetaObject>
#include <QPointF>

namespace aicad::command {

using namespace cad;

LeaderNoteCommand::LeaderNoteCommand()
    : Command("LEADER", "建立 Leader / Hole Note 標註") {}

cad::Sketch* LeaderNoteCommand::activeSketch() const {
    return core::Application::instance()->activeSketch();
}

// ─────────────────────────────────────────────────────────────────────────────
// 訂閱
// ─────────────────────────────────────────────────────────────────────────────

void LeaderNoteCommand::subscribeGeomPicked() {
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::GEOM_PICKED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onGeomPicked(v); },
                                      Qt::QueuedConnection);
        });
}

void LeaderNoteCommand::subscribePointAcquired() {
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::POINT_ACQUIRED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onPointAcquired(v); },
                                      Qt::QueuedConnection);
        });
}

void LeaderNoteCommand::subscribeStringInput() {
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::STRING_INPUT, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onStringInput(v); },
                                      Qt::QueuedConnection);
        });
}

void LeaderNoteCommand::subscribeCancelled() {
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onCancelled(v); },
                                      Qt::QueuedConnection);
        });
}

void LeaderNoteCommand::unsubscribeAll() {
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->unsubscribe(core::Events::GEOM_PICKED,      this);
    bus->unsubscribe(core::Events::POINT_ACQUIRED,   this);
    bus->unsubscribe(core::Events::STRING_INPUT,     this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED, this);
}

// ─────────────────────────────────────────────────────────────────────────────
// execute
// ─────────────────────────────────────────────────────────────────────────────

CommandResult LeaderNoteCommand::execute(const CommandContext&) {
    m_state = State::Idle;
    m_target = GeomRef();
    m_anchor = QVector2D();

    auto* app     = core::Application::instance();
    auto* ui      = app ? app->uiManager() : nullptr;
    auto* cadView = ui  ? ui->cadView()    : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetGeom);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->showPrompt("LEADER 選取目標幾何（點/圓心/端點...）");

    subscribeGeomPicked();
    subscribeCancelled();

    return CommandResult::Success("Waiting for target geometry");
}

// ─────────────────────────────────────────────────────────────────────────────
// onGeomPicked —— 只需要「一個」目標幾何，選到即進下一步（不做候選分類）
// ─────────────────────────────────────────────────────────────────────────────

void LeaderNoteCommand::onGeomPicked(const QVariant& payload) {
    if (m_state != State::Idle) return;

    QVariantMap map = payload.toMap();
    QString uuid   = map.value("geomUuid").toString();
    int     handle = map.value("handle", static_cast<int>(GeomHandle::WholeGeom)).toInt();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (uuid.isEmpty()) {
        if (cmdMgr) cmdMgr->printError("請選取草圖上的點或幾何元素");
        return;
    }

    m_target = GeomRef(uuid, static_cast<GeomHandle>(handle));

    Sketch* sk = activeSketch();
    if (sk && cmdMgr) {
        QVector2D pos = m_target.resolvePosition(sk);
        cmdMgr->printMessage(
            QString("  目標: (%1, %2)")
            .arg(static_cast<double>(pos.x()), 0, 'f', 2)
            .arg(static_cast<double>(pos.y()), 0, 'f', 2));
    }

    transitionToWaitAnchor();
}

// ─────────────────────────────────────────────────────────────────────────────
// onPointAcquired —— 放置文字錨點
// ─────────────────────────────────────────────────────────────────────────────

void LeaderNoteCommand::onPointAcquired(const QVariant& payload) {
    if (m_state != State::WaitAnchor) return;

    QVariantMap map = payload.toMap();
    QPointF ptF = map.value("point").value<QPointF>();
    m_anchor = QVector2D(static_cast<float>(ptF.x()), static_cast<float>(ptF.y()));

    transitionToWaitText();
}

// ─────────────────────────────────────────────────────────────────────────────
// onStringInput —— 輸入文字（如 "M20"），空白輸入視為取消
// ─────────────────────────────────────────────────────────────────────────────

void LeaderNoteCommand::onStringInput(const QVariant& payload) {
    if (m_state != State::WaitText) return;

    QString text = payload.toString().trimmed();
    if (text.isEmpty()) {
        auto* cmdMgr = core::CommandLineManager::instance();
        if (cmdMgr) cmdMgr->printMessage("已取消（未輸入文字）", core::MessageType::Info);
        cleanup();
        return;
    }
    commitLeaderNote(text);
}

void LeaderNoteCommand::onCancelled(const QVariant&) {
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────────
// 狀態轉換
// ─────────────────────────────────────────────────────────────────────────────

void LeaderNoteCommand::transitionToWaitAnchor() {
    m_state = State::WaitAnchor;

    auto* app     = core::Application::instance();
    auto* ui      = app ? app->uiManager() : nullptr;
    auto* cadView = ui  ? ui->cadView()    : nullptr;
    // GetPoint：單純點選平面上任一點（不需要選中既有幾何），
    // 與 memory 中 V3D 指令採用的修法一致（見 Sketch.h ALIGNMENT3DADDVPROFILE 的 fix）
    if (cadView) cadView->setMode(view::InteractionMode::GetPoint);

    auto* bus = core::Application::instance()->eventBus();
    if (bus) bus->unsubscribe(core::Events::GEOM_PICKED, this);
    subscribePointAcquired();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->showPrompt("LEADER 點一下放置文字位置");
}

void LeaderNoteCommand::transitionToWaitText() {
    m_state = State::WaitText;

    auto* app     = core::Application::instance();
    auto* ui      = app ? app->uiManager() : nullptr;
    auto* cadView = ui  ? ui->cadView()    : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    auto* bus = core::Application::instance()->eventBus();
    if (bus) bus->unsubscribe(core::Events::POINT_ACQUIRED, this);
    subscribeStringInput();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt("LEADER 輸入文字（如 M20，直接 Enter 取消）");
        cmdMgr->waitForInput(core::InputType::String);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// commitLeaderNote
// ─────────────────────────────────────────────────────────────────────────────

void LeaderNoteCommand::commitLeaderNote(const QString& text) {
    Sketch* sk = activeSketch();
    auto* cmdMgr = core::CommandLineManager::instance();
    if (!sk) { cleanup(); return; }

    SketchAnnotation ann = SketchAnnotation::makeLeaderNote(m_target, text);
    ann.leaderVertices = { m_anchor };
    sk->addAnnotation(ann);

    if (cmdMgr)
        cmdMgr->printSuccess(QString("✅ Leader Note 已建立：%1").arg(text));

    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────────
// cleanup
// ─────────────────────────────────────────────────────────────────────────────

void LeaderNoteCommand::cleanup() {
    unsubscribeAll();

    auto* app     = core::Application::instance();
    auto* ui      = app ? app->uiManager() : nullptr;
    auto* cadView = ui  ? ui->cadView()    : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    m_state  = State::Idle;
    m_target = GeomRef();
    m_anchor = QVector2D();

    Q_EMIT finished(CommandResult::Success("LEADER completed"));
}

} // namespace aicad::command
