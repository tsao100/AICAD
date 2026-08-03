/**
 * @file FilletCommand.cpp
 * @brief 見 FilletCommand.h 檔頭說明。
 */
#include "FilletCommand.h"
#include "TrimExtendHelper.h"

#include "../core/Application.h"
#include "../core/CommandLineManager.h"
#include "../core/EventBus.h"
#include "../cad/Sketch.h"
#include "../ui/UIManager.h"
#include "../view/CadView.h"

#include <QMetaObject>
#include <QPointF>

namespace aicad {
namespace command {

using namespace cad;

namespace {
bool isFixedReferenceUuid(const QString& uuid)
{
    return uuid.startsWith("sketch_xaxis:") ||
           uuid.startsWith("sketch_yaxis:") ||
           uuid.startsWith("sketch_origin:");
}
} // namespace

FilletCommand::FilletCommand()
    : Command("FILLET", "Fillet two straight lines (alias: F)")
{
}

QString FilletCommand::getUsage() const
{
    return "Usage: FILLET — specify fillet radius (0 = trim to intersection), "
           "then select two straight lines. Arc/circle participants are not "
           "supported yet.";
}

cad::Sketch* FilletCommand::activeSketch() const
{
    return core::Application::instance()->activeSketch();
}

// ─────────────────────────────────────────────────────────────────────────
// execute
// ─────────────────────────────────────────────────────────────────────────

CommandResult FilletCommand::execute(const CommandContext& /*ctx*/)
{
    m_state  = State::Idle;
    m_radius = 0.0;
    m_line1Uuid.clear();
    m_clickPt1 = QVector2D();

    Sketch* sk = activeSketch();
    if (!sk) {
        auto* cmdMgr = core::CommandLineManager::instance();
        if (cmdMgr) cmdMgr->printError("No active sketch. Enter sketch edit mode first.");
        return CommandResult::Failure("No active sketch.");
    }

    // FILLET 一律走互動流程（半徑＋兩次點選），不支援模式 A 預先選取——
    // 預先選取兩條線的「先後順序」與「各自點擊位置」無從得知，這兩者
    // 對圓角計算是必要資訊，因此忽略 ctx.args。
    setWaitingForInput();
    beginRadiusStage();
    return CommandResult::Success("Waiting for fillet radius...");
}

// ─────────────────────────────────────────────────────────────────────────
// 取半徑
// ─────────────────────────────────────────────────────────────────────────

void FilletCommand::beginRadiusStage()
{
    m_state = State::WaitRadius;

    subscribeNumberInput();
    subscribeCancelled();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt("[FILLET] Specify fillet radius (0 = trim to intersection):");
        cmdMgr->waitForInput(core::InputType::Number);
    }
}

void FilletCommand::subscribeNumberInput()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::NUMBER_INPUT, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onNumberInput(v); },
                                      Qt::QueuedConnection);
        });
}

void FilletCommand::onNumberInput(const QVariant& payload)
{
    if (m_state != State::WaitRadius) return;

    bool ok = false;
    const double r = payload.toString().trimmed().toDouble(&ok);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (!ok || r < 0.0) {
        if (cmdMgr) {
            cmdMgr->printError("Invalid radius. Enter a non-negative number:");
            cmdMgr->waitForInput(core::InputType::Number);
        }
        return;  // 停留在 WaitRadius
    }

    m_radius = r;

    auto* bus = core::Application::instance()->eventBus();
    if (bus) bus->unsubscribe(core::Events::NUMBER_INPUT, this);

    beginFirstObjectStage();
}

// ─────────────────────────────────────────────────────────────────────────
// 選取兩條直線
// ─────────────────────────────────────────────────────────────────────────

void FilletCommand::beginFirstObjectStage()
{
    m_state = State::WaitFirstObject;
    m_line1Uuid.clear();
    m_clickPt1 = QVector2D();

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetGeom);

    subscribeGeomPicked();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->showPrompt("[FILLET] Select first line:");
}

void FilletCommand::subscribeGeomPicked()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::GEOM_PICKED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onGeomPicked(v); },
                                      Qt::QueuedConnection);
        });
}

void FilletCommand::onGeomPicked(const QVariant& payload)
{
    if (m_state != State::WaitFirstObject && m_state != State::WaitSecondObject) return;

    const QVariantMap map = payload.toMap();
    const QString uuid = map.value("geomUuid").toString();
    const QPointF ptF = map.value("point").value<QPointF>();
    const QVector2D clickPt(float(ptF.x()), float(ptF.y()));

    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk = activeSketch();

    if (uuid.isEmpty() || isFixedReferenceUuid(uuid) || !sk) {
        if (cmdMgr) cmdMgr->printWarning("⚠️  No selectable geometry at that point.");
        return;
    }

    // MVP 範圍限制：只支援直線，選到別的型別當場提示、停留在同一階段。
    if (!dynamic_cast<SketchLine*>(sk->findGeometry(uuid))) {
        if (cmdMgr) cmdMgr->printWarning("⚠️  FILLET currently only supports straight lines.");
        return;
    }

    if (m_state == State::WaitFirstObject) {
        m_line1Uuid = uuid;
        m_clickPt1  = clickPt;
        m_state = State::WaitSecondObject;
        if (cmdMgr) cmdMgr->showPrompt("[FILLET] Select second line:");
        return;
    }

    // State::WaitSecondObject
    if (uuid == m_line1Uuid) {
        if (cmdMgr) cmdMgr->printWarning("⚠️  Select a different line for the second object.");
        return;
    }

    const bool ok = trimext::filletAt(sk, m_line1Uuid, uuid, m_radius, m_clickPt1, clickPt);
    if (cmdMgr) {
        if (ok)
            cmdMgr->printSuccess(QString("✅ Filleted (R=%1).").arg(m_radius));
        else
            cmdMgr->printWarning(
                "⚠️  Cannot fillet: lines are parallel/collinear, or radius is unreasonable.");
    }

    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// 取消
// ─────────────────────────────────────────────────────────────────────────

void FilletCommand::subscribeCancelled()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onCancelled(v); },
                                      Qt::QueuedConnection);
        });
}

void FilletCommand::onCancelled(const QVariant&)
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->printMessage("FILLET cancelled.");
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// unsubscribeAll / cleanup
// ─────────────────────────────────────────────────────────────────────────

void FilletCommand::unsubscribeAll()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->unsubscribe(core::Events::NUMBER_INPUT,      this);
    bus->unsubscribe(core::Events::GEOM_PICKED,       this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED, this);
}

void FilletCommand::cleanup()
{
    unsubscribeAll();

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->clearPrompt();

    m_state  = State::Idle;
    m_radius = 0.0;
    m_line1Uuid.clear();
    m_clickPt1 = QVector2D();

    if (state() == CommandState::Running)
        complete(CommandResult::Success());
}

} // namespace command
} // namespace aicad
