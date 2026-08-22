/**
 * @file ExtendCommand.cpp
 * @brief 見 ExtendCommand.h 檔頭說明。
 */
#include "ExtendCommand.h"
#include "TrimExtendHelper.h"
#include "CommandRubberBandHelper.h"

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

ExtendCommand::ExtendCommand()
    : Command("EXTEND", "Extend sketch geometry to boundary edges (alias: EX)")
{
}

QString ExtendCommand::getUsage() const
{
    return "Usage: EXTEND — select boundary edges (window/crossing/fence supported; "
           "Enter or right-click = use all objects), then click objects to extend "
           "(click near the end to extend); press Enter, right-click, or Esc to finish.";
}

cad::Sketch* ExtendCommand::activeSketch() const
{
    return core::Application::instance()->activeSketch();
}

// ─────────────────────────────────────────────────────────────────────────
// execute
// ─────────────────────────────────────────────────────────────────────────

CommandResult ExtendCommand::execute(const CommandContext& ctx)
{
    m_state = State::Idle;
    m_boundaryEdges.clear();
    m_extendCount = 0;

    Sketch* sk = activeSketch();
    if (!sk) {
        auto* cmdMgr = core::CommandLineManager::instance();
        if (cmdMgr) cmdMgr->printError("No active sketch. Enter sketch edit mode first.");
        return CommandResult::Failure("No active sketch.");
    }

    if (!ctx.args.isEmpty()) {
        QStringList boundary;
        for (const QString& uuid : ctx.args) {
            if (uuid.isEmpty() || isFixedReferenceUuid(uuid)) continue;
            if (sk->findGeometry(uuid)) boundary.append(uuid);
        }
        if (boundary.isEmpty()) {
            auto* cmdMgr = core::CommandLineManager::instance();
            if (cmdMgr) cmdMgr->printWarning("⚠️  No valid geometry in selection.");
            return CommandResult::Failure("No valid geometry in selection.");
        }
        m_boundaryEdges = boundary;
        setWaitingForInput();
        beginPickSegmentStage(sk);
        return CommandResult::Success("Waiting for object to extend...");
    }

    setWaitingForInput();
    beginSelectBoundaryStage(sk);
    return CommandResult::Success("Waiting for boundary edges...");
}

// ─────────────────────────────────────────────────────────────────────────
// 選取邊界邊
// ─────────────────────────────────────────────────────────────────────────

void ExtendCommand::beginSelectBoundaryStage(cad::Sketch* sketch)
{
    m_state = State::SelectingBoundaryEdges;

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetGeom);

    m_picker = new SketchSelectionPicker(this);
    connect(m_picker, &SketchSelectionPicker::confirmed,
            this, &ExtendCommand::onSelectionConfirmed);
    connect(m_picker, &SketchSelectionPicker::cancelled,
            this, &ExtendCommand::onSelectionCancelled);

    m_picker->begin(sketch, SketchSelectionPicker::Mode::PickMultiple,
                    "[EXTEND] Select boundary edges, or press Enter/right-click to use all objects:");
}

void ExtendCommand::onSelectionConfirmed(const QStringList& uuids)
{
    Sketch* sk = activeSketch();
    if (!sk) { cleanup(); return; }

    if (uuids.isEmpty()) {
        QStringList all;
        for (SketchGeometry* g : sk->geometriesRef())
            if (g) all.append(g->uuid);
        m_boundaryEdges = all;
    } else {
        m_boundaryEdges = uuids;
    }

    beginPickSegmentStage(sk);
}

void ExtendCommand::onSelectionCancelled()
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->printMessage("EXTEND cancelled.");
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// 逐次點選要延伸的物件
// ─────────────────────────────────────────────────────────────────────────

void ExtendCommand::beginPickSegmentStage(cad::Sketch* sketch)
{
    m_state = State::PickingSegment;

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) {
        cadView->setMode(view::InteractionMode::GetGeom);
        // 防呆：選取邊界邊階段若殘留一個尚未收尾的窗選（isBoxSelectArmed()
        // 仍是 true），這裡多保險一次明確清掉——見 CadView::setMode() 內
        // 對應的說明，兩者處理的是同一個問題，這裡是額外一層防呆。
        cadView->cancelActiveBoxSelect();
    }

    subscribeGeomPicked();
    subscribeConfirm();
    subscribeCancelled();
    subscribeHover();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt("[EXTEND] Select object to extend, or press Enter/right-click to finish:");
        cmdMgr->waitForInput(core::InputType::String);
    }
}

void ExtendCommand::subscribeGeomPicked()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::GEOM_PICKED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onGeomPicked(v); },
                                      Qt::QueuedConnection);
        });
}

void ExtendCommand::subscribeConfirm()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::STRING_INPUT, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onConfirm(v); },
                                      Qt::QueuedConnection);
        });
}

void ExtendCommand::subscribeCancelled()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onCancelled(v); },
                                      Qt::QueuedConnection);
        });
}

void ExtendCommand::subscribeHover()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::GEOM_HOVER, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onHover(v); },
                                      Qt::QueuedConnection);
        });
}

void ExtendCommand::onHover(const QVariant& payload)
{
    if (m_state != State::PickingSegment) return;

    const QVariantMap map = payload.toMap();
    const QString uuid = map.value("geomUuid").toString();

    Sketch* sk = activeSketch();
    if (!sk || uuid.isEmpty() || isFixedReferenceUuid(uuid)) {
        rb::disarm();
        return;
    }

    const QPointF ptF = map.value("point").value<QPointF>();
    const QVector2D hoverPt(float(ptF.x()), float(ptF.y()));

    const auto seg = trimext::previewExtendAt(sk, uuid, m_boundaryEdges, hoverPt);
    if (seg.valid)
        rb::showPolylinePreview(sk, seg.points);
    else
        rb::disarm();
}

void ExtendCommand::onGeomPicked(const QVariant& payload)
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

    // 點擊前的 hover 預覽是根據點擊前的幾何狀態算的，點擊後幾何已經
    // 被延伸，畫面上殘留的預覽線可能對到已經改變的位置，先收起來，等
    // 下一次滑鼠移動再重新算。
    rb::disarm();

    const bool ok = trimext::extendAt(sk, uuid, m_boundaryEdges, clickPt);
    if (ok) {
        ++m_extendCount;
        if (cmdMgr) cmdMgr->printSuccess("↔️  Extended.");
    } else {
        if (cmdMgr) {
            cmdMgr->printWarning(
                "⚠️  Cannot extend here (no boundary found in that direction, "
                "or unsupported geometry type).");
        }
    }
}

void ExtendCommand::onConfirm(const QVariant& /*data*/)
{
    if (m_state != State::PickingSegment) return;
    finishLoop();
}

void ExtendCommand::onCancelled(const QVariant&)
{
    if (m_state != State::PickingSegment) return;
    finishLoop();
}

void ExtendCommand::finishLoop()
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        if (m_extendCount > 0)
            cmdMgr->printSuccess(QString("✅ EXTEND finished, %1 object(s) extended.").arg(m_extendCount));
        else
            cmdMgr->printMessage("EXTEND finished, nothing was extended.");
    }
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// unsubscribeAll / cleanup
// ─────────────────────────────────────────────────────────────────────────

void ExtendCommand::unsubscribeAll()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->unsubscribe(core::Events::GEOM_PICKED,       this);
    bus->unsubscribe(core::Events::GEOM_HOVER,        this);
    bus->unsubscribe(core::Events::STRING_INPUT,      this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED, this);
}

void ExtendCommand::cleanup()
{
    unsubscribeAll();
    rb::disarm();

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
    m_boundaryEdges.clear();
    m_extendCount = 0;

    if (state() == CommandState::Running)
        complete(CommandResult::Success());
}

} // namespace command
} // namespace aicad
