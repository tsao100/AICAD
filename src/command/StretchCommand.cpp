/**
 * @file StretchCommand.cpp
 * @brief 見 StretchCommand.h 檔頭說明。
 */
#include "StretchCommand.h"
#include "CommandRubberBandHelper.h"

#include "../core/Application.h"
#include "../core/CommandLineManager.h"
#include "../core/EventBus.h"
#include "../cad/Sketch.h"
#include "../cad/sketch/SketchGeomTransformUtil.h"
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

StretchCommand::StretchCommand()
    : Command("STRETCH", "Stretch sketch geometry within a crossing window (alias: S)")
{
}

QString StretchCommand::getUsage() const
{
    return "Usage: STRETCH — run STRETCH, specify two points defining a crossing "
           "window, then specify base point and second point. Only endpoints "
           "inside the window move; geometry fully inside the window moves as a "
           "whole. If objects are pre-selected, STRETCH behaves like MOVE.";
}

cad::Sketch* StretchCommand::activeSketch() const
{
    return core::Application::instance()->activeSketch();
}

// ─────────────────────────────────────────────────────────────────────────
// execute
// ─────────────────────────────────────────────────────────────────────────

CommandResult StretchCommand::execute(const CommandContext& ctx)
{
    m_state          = State::Idle;
    m_usePreselected = false;
    m_preSelected.clear();
    m_corner1 = m_corner2 = m_basePoint = QVector2D();

    Sketch* sk = activeSketch();
    if (!sk) {
        auto* cmdMgr = core::CommandLineManager::instance();
        if (cmdMgr) cmdMgr->printError("No active sketch. Enter sketch edit mode first.");
        return CommandResult::Failure("No active sketch.");
    }

    if (!ctx.args.isEmpty()) {
        QStringList preSelected;
        for (const QString& uuid : ctx.args) {
            if (uuid.isEmpty() || isFixedReferenceUuid(uuid)) continue;
            if (sk->findGeometry(uuid)) preSelected.append(uuid);
        }
        if (preSelected.isEmpty()) {
            auto* cmdMgr = core::CommandLineManager::instance();
            if (cmdMgr) cmdMgr->printWarning("⚠️  No valid geometry in selection.");
            return CommandResult::Failure("No valid geometry in selection.");
        }

        m_usePreselected = true;
        m_preSelected    = preSelected;

        setWaitingForInput();
        beginBasePointStage();
        return CommandResult::Success("Waiting for base point...");
    }

    setWaitingForInput();
    beginCorner1Stage();
    return CommandResult::Success("Waiting for crossing window...");
}

// ─────────────────────────────────────────────────────────────────────────
// 窗選矩形兩角點
// ─────────────────────────────────────────────────────────────────────────

void StretchCommand::beginCorner1Stage()
{
    m_state = State::WaitCorner1;

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetPoint);

    subscribePointAcquired();
    subscribeCancelled();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt(
            "[STRETCH] Specify first corner of crossing window "
            "(only endpoints inside the window will move):");
    }
}

void StretchCommand::subscribePointAcquired()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::POINT_ACQUIRED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onPointAcquired(v); },
                                      Qt::QueuedConnection);
        });
}

// ─────────────────────────────────────────────────────────────────────────
// 取基準點 / 取第二點
// ─────────────────────────────────────────────────────────────────────────

void StretchCommand::beginBasePointStage()
{
    m_state = State::WaitBasePoint;

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetPoint);

    // 模式 A（跳過角點階段）尚未訂閱過，這裡才需要訂閱；模式 B（從角點
    // 階段轉入）已經在 beginCorner1Stage() 訂閱過，不重複訂閱。
    if (m_usePreselected) {
        subscribePointAcquired();
        subscribeCancelled();
    }

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->showPrompt("[STRETCH] Specify base point:");
}

void StretchCommand::onPointAcquired(const QVariant& payload)
{
    const QVariantMap map = payload.toMap();
    const QPointF ptF = map.value("point").value<QPointF>();
    const QVector2D pt(float(ptF.x()), float(ptF.y()));

    auto* cmdMgr = core::CommandLineManager::instance();

    switch (m_state) {
    case State::WaitCorner1:
        m_corner1 = pt;
        m_state = State::WaitCorner2;

        // 窗選第一角點→游標的矩形橡皮筋預覽。
        if (Sketch* sk = activeSketch())
            rb::armRectPreview(sk, m_corner1);

        if (cmdMgr) cmdMgr->showPrompt("[STRETCH] Specify opposite corner:");
        return;

    case State::WaitCorner2:
        if ((pt - m_corner1).length() < 1e-4f) {
            if (cmdMgr) {
                cmdMgr->printWarning(
                    "⚠️  Window corners must not coincide. Specify opposite corner again:");
            }
            return;  // 停留在 WaitCorner2
        }
        m_corner2 = pt;
        beginBasePointStage();
        return;

    case State::WaitBasePoint:
        m_basePoint = pt;
        m_state = State::WaitSecondPoint;

        // 從矩形預覽切換成基準點→游標的線預覽（位移向量）。
        if (Sketch* sk = activeSketch())
            rb::armLinePreview(sk, m_basePoint);

        if (cmdMgr) cmdMgr->showPrompt("[STRETCH] Specify second point (displacement target):");
        return;

    case State::WaitSecondPoint: {
        Sketch* sk = activeSketch();
        const QVector2D delta = pt - m_basePoint;

        if (sk) {
            if (m_usePreselected) {
                cad::transform::applyToSelection(
                    sk, m_preSelected, cad::transform::Transform2D::translation(delta));
                if (cmdMgr) {
                    cmdMgr->printSuccess(
                        QString("✅ Moved %1 pre-selected object(s) (STRETCH on a "
                               "pre-selection behaves like MOVE).").arg(m_preSelected.size()));
                }
            } else {
                const QVector2D rectMin(qMin(m_corner1.x(), m_corner2.x()),
                                        qMin(m_corner1.y(), m_corner2.y()));
                const QVector2D rectMax(qMax(m_corner1.x(), m_corner2.x()),
                                        qMax(m_corner1.y(), m_corner2.y()));
                const QStringList affected =
                    cad::transform::stretchWithinRect(sk, rectMin, rectMax, delta);

                if (cmdMgr) {
                    if (!affected.isEmpty())
                        cmdMgr->printSuccess(
                            QString("✅ Stretched %1 object(s).").arg(affected.size()));
                    else
                        cmdMgr->printWarning("⚠️  Nothing inside the crossing window.");
                }
            }
        }

        cleanup();
        return;
    }

    case State::Idle:
        return;
    }
}

// ─────────────────────────────────────────────────────────────────────────
// 取消
// ─────────────────────────────────────────────────────────────────────────

void StretchCommand::subscribeCancelled()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onCancelled(v); },
                                      Qt::QueuedConnection);
        });
}

void StretchCommand::onCancelled(const QVariant&)
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->printMessage("STRETCH cancelled.");
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// unsubscribeAll / cleanup
// ─────────────────────────────────────────────────────────────────────────

void StretchCommand::unsubscribeAll()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->unsubscribe(core::Events::POINT_ACQUIRED,    this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED, this);
}

void StretchCommand::cleanup()
{
    unsubscribeAll();
    rb::disarm();

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->clearPrompt();

    m_state          = State::Idle;
    m_usePreselected = false;
    m_preSelected.clear();
    m_corner1 = m_corner2 = m_basePoint = QVector2D();

    if (state() == CommandState::Running)
        complete(CommandResult::Success());
}

} // namespace command
} // namespace aicad
