/**
 * @file MirrorCommand.cpp
 * @brief 見 MirrorCommand.h 檔頭說明。
 */
#include "MirrorCommand.h"
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

/// 鏡射軸兩點退化（重合）的容許誤差。與 Transform2D::mirror() 內部的
/// len2 < 1e-12（平方值）門檻一致，這裡用未平方的線性距離方便判讀。
constexpr double kAxisDegenerateTol = 1e-6;
} // namespace

MirrorCommand::MirrorCommand()
    : Command("MIRROR", "Mirror selected sketch geometry about a line (alias: MI)")
{
}

QString MirrorCommand::getUsage() const
{
    return "Usage: MIRROR — select geometry first then run MIRROR, "
           "or run MIRROR then click objects and press Enter, "
           "then specify two points defining the mirror line, "
           "then choose whether to erase the source objects.";
}

cad::Sketch* MirrorCommand::activeSketch() const
{
    return core::Application::instance()->activeSketch();
}

// ─────────────────────────────────────────────────────────────────────────
// execute
// ─────────────────────────────────────────────────────────────────────────

CommandResult MirrorCommand::execute(const CommandContext& ctx)
{
    m_state = State::Idle;
    m_selection.clear();
    m_axisP0 = QVector2D();
    m_axisP1 = QVector2D();

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
        setWaitingForInput();
        onSelectionConfirmed(preSelected);
        return CommandResult::Success("Waiting for mirror line...");
    }

    setWaitingForInput();
    beginSelection(sk);
    return CommandResult::Success("Waiting for selection...");
}

// ─────────────────────────────────────────────────────────────────────────
// 選取階段
// ─────────────────────────────────────────────────────────────────────────

void MirrorCommand::beginSelection(cad::Sketch* sketch)
{
    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetGeom);

    m_picker = new SketchSelectionPicker(this);
    connect(m_picker, &SketchSelectionPicker::confirmed,
            this, &MirrorCommand::onSelectionConfirmed);
    connect(m_picker, &SketchSelectionPicker::cancelled,
            this, &MirrorCommand::onSelectionCancelled);

    m_picker->begin(sketch, SketchSelectionPicker::Mode::PickMultiple,
                    "[MIRROR] Select objects, then press Enter:");
}

void MirrorCommand::onSelectionConfirmed(const QStringList& uuids)
{
    if (uuids.isEmpty()) {
        onSelectionCancelled();
        return;
    }
    m_selection = uuids;
    beginAxisPoint1Stage();
}

void MirrorCommand::onSelectionCancelled()
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->printMessage("MIRROR cancelled.");
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// 取鏡射軸兩點
// ─────────────────────────────────────────────────────────────────────────

void MirrorCommand::beginAxisPoint1Stage()
{
    m_state = State::WaitAxisPoint1;

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetPoint);

    subscribePointAcquired();
    subscribeCancelled();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->showPrompt("[MIRROR] Specify first point of mirror line:");
}

void MirrorCommand::subscribePointAcquired()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::POINT_ACQUIRED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onPointAcquired(v); },
                                      Qt::QueuedConnection);
        });
}

void MirrorCommand::onPointAcquired(const QVariant& payload)
{
    const QVariantMap map = payload.toMap();
    const QPointF ptF = map.value("point").value<QPointF>();
    const QVector2D pt(float(ptF.x()), float(ptF.y()));

    auto* cmdMgr = core::CommandLineManager::instance();

    if (m_state == State::WaitAxisPoint1) {
        m_axisP0 = pt;
        m_state = State::WaitAxisPoint2;

        // 鏡射軸第一點→游標的橡皮筋線，就是目前預覽的鏡射軸方向。
        if (Sketch* sk = activeSketch())
            rb::armLinePreview(sk, m_axisP0);

        if (cmdMgr) cmdMgr->showPrompt("[MIRROR] Specify second point of mirror line:");
        return;
    }

    if (m_state == State::WaitAxisPoint2) {
        if ((pt - m_axisP0).length() < float(kAxisDegenerateTol)) {
            // 鏡射軸兩點重合，無法定義方向：提示重新指定，停留在同一狀態。
            if (cmdMgr) {
                cmdMgr->printWarning(
                    "⚠️  Mirror line points must not coincide. Specify second point again:");
            }
            return;
        }

        m_axisP1 = pt;

        // 兩點都取得了，不再需要 POINT_ACQUIRED。
        auto* bus = core::Application::instance()->eventBus();
        if (bus) bus->unsubscribe(core::Events::POINT_ACQUIRED, this);

        beginEraseOptionStage();
    }
}

// ─────────────────────────────────────────────────────────────────────────
// 是否刪除原物件
// ─────────────────────────────────────────────────────────────────────────

void MirrorCommand::beginEraseOptionStage()
{
    m_state = State::WaitEraseOption;

    subscribeYesNoInput();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt("[MIRROR] Erase source objects? [Yes/No] <No>:");
        cmdMgr->waitForInput(core::InputType::YesNo);
    }
}

void MirrorCommand::subscribeYesNoInput()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::YESNO_INPUT, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onYesNoInput(v); },
                                      Qt::QueuedConnection);
        });
}

void MirrorCommand::onYesNoInput(const QVariant& payload)
{
    if (m_state != State::WaitEraseOption) return;

    const QString a = payload.toString().trimmed().toLower();
    // 空字串（直接按 Enter）比照提示文字的預設值 <No>。
    const bool eraseSource = (a == "y" || a == "yes");

    Sketch* sk = activeSketch();
    auto* cmdMgr = core::CommandLineManager::instance();

    if (sk && !m_selection.isEmpty()) {
        const auto xf = cad::transform::Transform2D::mirror(m_axisP0, m_axisP1);

        if (eraseSource) {
            cad::transform::applyToSelection(sk, m_selection, xf);
            if (cmdMgr)
                cmdMgr->printSuccess(
                    QString("✅ Mirrored %1 object(s) (source erased).").arg(m_selection.size()));
        } else {
            const QStringList created = cad::transform::cloneAndTransform(sk, m_selection, xf);
            if (cmdMgr) {
                if (!created.isEmpty())
                    cmdMgr->printSuccess(
                        QString("✅ Mirrored %1 object(s) (copy).").arg(created.size()));
                else
                    cmdMgr->printWarning("⚠️  Nothing was mirrored.");
            }
        }
    }

    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// 取消
// ─────────────────────────────────────────────────────────────────────────

void MirrorCommand::subscribeCancelled()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onCancelled(v); },
                                      Qt::QueuedConnection);
        });
}

void MirrorCommand::onCancelled(const QVariant&)
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->printMessage("MIRROR cancelled.");
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// unsubscribeAll / cleanup
// ─────────────────────────────────────────────────────────────────────────

void MirrorCommand::unsubscribeAll()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->unsubscribe(core::Events::POINT_ACQUIRED,    this);
    bus->unsubscribe(core::Events::YESNO_INPUT,       this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED, this);
}

void MirrorCommand::cleanup()
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
    m_selection.clear();
    m_axisP0 = QVector2D();
    m_axisP1 = QVector2D();

    if (state() == CommandState::Running)
        complete(CommandResult::Success());
}

} // namespace command
} // namespace aicad
