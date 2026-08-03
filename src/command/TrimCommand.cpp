/**
 * @file TrimCommand.cpp
 * @brief 見 TrimCommand.h 檔頭說明。
 */
#include "TrimCommand.h"
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

TrimCommand::TrimCommand()
    : Command("TRIM", "Trim sketch geometry against cutting edges (alias: TR)")
{
}

QString TrimCommand::getUsage() const
{
    return "Usage: TRIM — select cutting edges (Enter = use all objects), "
           "then click objects to trim (click the segment to remove); "
           "press Enter or Esc to finish.";
}

cad::Sketch* TrimCommand::activeSketch() const
{
    return core::Application::instance()->activeSketch();
}

// ─────────────────────────────────────────────────────────────────────────
// execute
// ─────────────────────────────────────────────────────────────────────────

CommandResult TrimCommand::execute(const CommandContext& ctx)
{
    m_state = State::Idle;
    m_cuttingEdges.clear();
    m_trimCount = 0;

    Sketch* sk = activeSketch();
    if (!sk) {
        auto* cmdMgr = core::CommandLineManager::instance();
        if (cmdMgr) cmdMgr->printError("No active sketch. Enter sketch edit mode first.");
        return CommandResult::Failure("No active sketch.");
    }

    // ── 模式 A：呼叫時已帶有選取的 UUID，直接作為剪切邊 ─────────────
    if (!ctx.args.isEmpty()) {
        QStringList cutting;
        for (const QString& uuid : ctx.args) {
            if (uuid.isEmpty() || isFixedReferenceUuid(uuid)) continue;
            if (sk->findGeometry(uuid)) cutting.append(uuid);
        }
        if (cutting.isEmpty()) {
            auto* cmdMgr = core::CommandLineManager::instance();
            if (cmdMgr) cmdMgr->printWarning("⚠️  No valid geometry in selection.");
            return CommandResult::Failure("No valid geometry in selection.");
        }
        m_cuttingEdges = cutting;
        setWaitingForInput();
        beginPickSegmentStage(sk);
        return CommandResult::Success("Waiting for object to trim...");
    }

    // ── 模式 B：互動選取剪切邊 ───────────────────────────────────────
    setWaitingForInput();
    beginSelectCuttingStage(sk);
    return CommandResult::Success("Waiting for cutting edges...");
}

// ─────────────────────────────────────────────────────────────────────────
// 選取剪切邊
// ─────────────────────────────────────────────────────────────────────────

void TrimCommand::beginSelectCuttingStage(cad::Sketch* sketch)
{
    m_state = State::SelectingCuttingEdges;

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetGeom);

    m_picker = new SketchSelectionPicker(this);
    connect(m_picker, &SketchSelectionPicker::confirmed,
            this, &TrimCommand::onSelectionConfirmed);
    connect(m_picker, &SketchSelectionPicker::cancelled,
            this, &TrimCommand::onSelectionCancelled);

    m_picker->begin(sketch, SketchSelectionPicker::Mode::PickMultiple,
                    "[TRIM] Select cutting edges, or press Enter to use all objects:");
}

void TrimCommand::onSelectionConfirmed(const QStringList& uuids)
{
    Sketch* sk = activeSketch();
    if (!sk) { cleanup(); return; }

    if (uuids.isEmpty()) {
        // 空選取 = 以全部幾何作為剪切邊（TrimExtendHelper::extractShape()
        // 會自動略過不支援的型別，這裡不需要事先過濾）。
        QStringList all;
        for (SketchGeometry* g : sk->geometriesRef())
            if (g) all.append(g->uuid);
        m_cuttingEdges = all;
    } else {
        m_cuttingEdges = uuids;
    }

    beginPickSegmentStage(sk);
}

void TrimCommand::onSelectionCancelled()
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->printMessage("TRIM cancelled.");
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// 逐次點選要裁切的物件
// ─────────────────────────────────────────────────────────────────────────

void TrimCommand::beginPickSegmentStage(cad::Sketch* sketch)
{
    m_state = State::PickingSegment;

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetGeom);

    subscribeGeomPicked();
    subscribeConfirm();
    subscribeCancelled();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt("[TRIM] Select object to trim, or press Enter to finish:");
        cmdMgr->waitForInput(core::InputType::String);
    }
}

void TrimCommand::subscribeGeomPicked()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::GEOM_PICKED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onGeomPicked(v); },
                                      Qt::QueuedConnection);
        });
}

void TrimCommand::subscribeConfirm()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::STRING_INPUT, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onConfirm(v); },
                                      Qt::QueuedConnection);
        });
}

void TrimCommand::subscribeCancelled()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onCancelled(v); },
                                      Qt::QueuedConnection);
        });
}

void TrimCommand::onGeomPicked(const QVariant& payload)
{
    if (m_state != State::PickingSegment) return;

    const QVariantMap map = payload.toMap();
    const QString uuid = map.value("geomUuid").toString();
    const QPointF ptF = map.value("point").value<QPointF>();
    const QVector2D clickPt(float(ptF.x()), float(ptF.y()));

    auto* cmdMgr = core::CommandLineManager::instance();

    if (uuid.isEmpty() || isFixedReferenceUuid(uuid)) {
        if (cmdMgr) cmdMgr->printWarning("⚠️  No selectable geometry at that point.");
        return;
    }

    Sketch* sk = activeSketch();
    if (!sk) return;

    const bool ok = trimext::trimAt(sk, uuid, m_cuttingEdges, clickPt);
    if (ok) {
        ++m_trimCount;
        if (cmdMgr) cmdMgr->printSuccess("✂️  Trimmed.");
    } else {
        if (cmdMgr) {
            cmdMgr->printWarning(
                "⚠️  Cannot trim here (no intersection found, or unsupported geometry type).");
        }
    }
    // 停留在 PickingSegment 狀態，繼續等待下一次點擊（迴圈式命令）。
}

void TrimCommand::onConfirm(const QVariant& /*data*/)
{
    if (m_state != State::PickingSegment) return;
    finishLoop();
}

void TrimCommand::onCancelled(const QVariant&)
{
    if (m_state != State::PickingSegment) return;
    finishLoop();
}

void TrimCommand::finishLoop()
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        if (m_trimCount > 0)
            cmdMgr->printSuccess(QString("✅ TRIM finished, %1 object(s) trimmed.").arg(m_trimCount));
        else
            cmdMgr->printMessage("TRIM finished, nothing was trimmed.");
    }
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// unsubscribeAll / cleanup
// ─────────────────────────────────────────────────────────────────────────

void TrimCommand::unsubscribeAll()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->unsubscribe(core::Events::GEOM_PICKED,       this);
    bus->unsubscribe(core::Events::STRING_INPUT,      this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED, this);
}

void TrimCommand::cleanup()
{
    unsubscribeAll();

    if (m_picker) {
        m_picker->abortSilently();
        m_picker->deleteLater();
        m_picker = nullptr;
    }

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->clearPrompt();

    m_state = State::Idle;
    m_cuttingEdges.clear();
    m_trimCount = 0;

    if (state() == CommandState::Running)
        complete(CommandResult::Success());
}

} // namespace command
} // namespace aicad
