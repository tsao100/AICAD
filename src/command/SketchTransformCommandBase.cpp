/**
 * @file SketchTransformCommandBase.cpp
 * @brief 見 SketchTransformCommandBase.h 檔頭說明。
 */
#include "SketchTransformCommandBase.h"
#include "CommandRubberBandHelper.h"

#include "../core/Application.h"
#include "../core/CommandLineManager.h"
#include "../core/EventBus.h"
#include "../cad/Sketch.h"
#include "../ui/UIManager.h"
#include "../view/CadView.h"

#include <QMetaObject>
#include <QPointF>
#include <QDebug>

namespace aicad {
namespace command {

using namespace cad;

namespace {
/// 比照 EraseCommand.cpp / SketchSelectionPicker.cpp 的
/// isFixedReferenceUuid()：草圖平面參考幾何（X 軸／Y 軸／原點）為固定
/// 參考，不可被選取／變換。
bool isFixedReferenceUuid(const QString& uuid)
{
    return uuid.startsWith("sketch_xaxis:") ||
           uuid.startsWith("sketch_yaxis:") ||
           uuid.startsWith("sketch_origin:");
}
} // namespace

SketchTransformCommandBase::SketchTransformCommandBase(const QString& name,
                                                        const QString& description)
    : Command(name, description)
{
}

cad::Sketch* SketchTransformCommandBase::activeSketch() const
{
    return core::Application::instance()->activeSketch();
}

// ─────────────────────────────────────────────────────────────────────────
// execute
// ─────────────────────────────────────────────────────────────────────────

CommandResult SketchTransformCommandBase::execute(const CommandContext& ctx)
{
    m_state = State::Idle;
    m_selection.clear();
    m_basePoint = QVector2D();

    Sketch* sk = activeSketch();
    if (!sk) {
        auto* cmdMgr = core::CommandLineManager::instance();
        if (cmdMgr) cmdMgr->printError("No active sketch. Enter sketch edit mode first.");
        return CommandResult::Failure("No active sketch.");
    }

    // ── 模式 A：呼叫時已帶有選取的 UUID（比照 EraseCommand）───────────
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

        setWaitingForInput();
        onSelectionConfirmed(preSelected);
        return CommandResult::Success("Waiting for base point...");
    }

    // ── 模式 B：互動選取 ─────────────────────────────────────────────
    setWaitingForInput();
    beginSelection(sk);
    return CommandResult::Success("Waiting for selection...");
}

// ─────────────────────────────────────────────────────────────────────────
// 選取階段
// ─────────────────────────────────────────────────────────────────────────

void SketchTransformCommandBase::beginSelection(cad::Sketch* sketch)
{
    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetGeom);

    m_picker = new SketchSelectionPicker(this);
    connect(m_picker, &SketchSelectionPicker::confirmed,
            this, &SketchTransformCommandBase::onSelectionConfirmed);
    connect(m_picker, &SketchSelectionPicker::cancelled,
            this, &SketchTransformCommandBase::onSelectionCancelled);

    m_picker->begin(sketch, SketchSelectionPicker::Mode::PickMultiple, selectPrompt());
}

void SketchTransformCommandBase::onSelectionConfirmed(const QStringList& uuids)
{
    if (uuids.isEmpty()) {
        onSelectionCancelled();
        return;
    }
    m_selection = uuids;
    beginBasePointStage();
}

void SketchTransformCommandBase::onSelectionCancelled()
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->printMessage(QString("%1 cancelled.").arg(name()));
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// 取基準點 / 取第二點
// ─────────────────────────────────────────────────────────────────────────

void SketchTransformCommandBase::beginBasePointStage()
{
    m_state = State::WaitBasePoint;

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    // GetPoint：單純點選平面上任一點（不需要選中既有幾何），與
    // LeaderNoteCommand::transitionToWaitAnchor() 的作法一致。
    if (cadView) cadView->setMode(view::InteractionMode::GetPoint);

    subscribePointAcquired();
    subscribeCancelled();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->showPrompt(basePointPrompt());
}

void SketchTransformCommandBase::subscribePointAcquired()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::POINT_ACQUIRED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onPointAcquired(v); },
                                      Qt::QueuedConnection);
        });
}

void SketchTransformCommandBase::subscribeCancelled()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onCancelled(v); },
                                      Qt::QueuedConnection);
        });
}

void SketchTransformCommandBase::unsubscribeAll()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->unsubscribe(core::Events::POINT_ACQUIRED,    this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED, this);
}

void SketchTransformCommandBase::onPointAcquired(const QVariant& payload)
{
    const QVariantMap map = payload.toMap();
    const QPointF ptF = map.value("point").value<QPointF>();
    const QVector2D pt(float(ptF.x()), float(ptF.y()));

    if (m_state == State::WaitBasePoint) {
        m_basePoint = pt;
        m_state = State::WaitSecondPoint;

        // 啟動「基準點→游標」的橡皮筋預覽線，讓使用者拖曳時能直接看到
        // 位移方向/距離（見 CommandRubberBandHelper.h 說明）。
        if (Sketch* sk = activeSketch())
            rb::armLinePreview(sk, m_basePoint);

        auto* cmdMgr = core::CommandLineManager::instance();
        if (cmdMgr) cmdMgr->showPrompt(secondPointPrompt());
        return;
    }

    if (m_state == State::WaitSecondPoint) {
        Sketch* sk = activeSketch();
        const QVector2D delta = pt - m_basePoint;
        const auto xf = cad::transform::Transform2D::translation(delta);

        if (sk && !m_selection.isEmpty())
            commit(sk, m_selection, xf);

        cleanup();
    }
}

void SketchTransformCommandBase::onCancelled(const QVariant&)
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->printMessage(QString("%1 cancelled.").arg(name()));
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// cleanup
// ─────────────────────────────────────────────────────────────────────────

void SketchTransformCommandBase::cleanup()
{
    unsubscribeAll();
    rb::disarm();

    if (m_picker) {
        m_picker->abortSilently();   // no-op（已 confirmed/cancelled 過的話 m_active 已是 false）
        m_picker->deleteLater();
        m_picker = nullptr;
    }

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->clearPrompt();

    m_state = State::Idle;
    m_selection.clear();
    m_basePoint = QVector2D();

    if (state() == CommandState::Running)
        complete(CommandResult::Success());
}

} // namespace command
} // namespace aicad
