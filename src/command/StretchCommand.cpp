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
#include "../view/RubberBand.h"

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
    m_liveDelta = QVector2D();
    m_liveDeltaApplied = false;

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

        // 從矩形預覽切換成基準點→游標的線預覽（位移向量）＋即時拉伸預覽。
        if (Sketch* sk = activeSketch())
            rb::armLinePreview(sk, m_basePoint);
        armLivePreview();

        if (cmdMgr) cmdMgr->showPrompt("[STRETCH] Specify second point (displacement target):");
        return;

    case State::WaitSecondPoint: {
        Sketch* sk = activeSketch();
        const QVector2D delta = pt - m_basePoint;

        // 確認前先把即時預覽期間「輕量套用」在真實幾何上的位移還原，讓
        // 最終送出的位移仍是「原始座標 → 使用者指定的最終位移」單一完整
        // 動作，避免與預覽期間的累計增量重複疊加（比照 ROTATE 的
        // revertLivePreview() 用法）。
        revertLivePreview();

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
// 即時位移預覽（WaitSecondPoint 階段）
// ─────────────────────────────────────────────────────────────────────────

void StretchCommand::armLivePreview()
{
    m_liveDelta = QVector2D();
    m_liveDeltaApplied = false;

    Sketch* sk = activeSketch();
    if (!sk) return;
    if (!m_usePreselected && m_corner1 == m_corner2) return;  // 窗選矩形退化，不應發生但防呆一下

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    view::RubberBand* band = cadView ? cadView->rubberBand() : nullptr;
    if (!band) return;

    m_livePreviewConn = QObject::connect(
        band, &view::RubberBand::updated, this,
        [this, band] {
            if (m_state != State::WaitSecondPoint) return;
            if (!band->hasCurrentPoint()) return;
            const QPointF cp = band->currentPoint();
            updateLivePreviewTo(QVector2D(float(cp.x()), float(cp.y())));
        });
}

void StretchCommand::updateLivePreviewTo(const QVector2D& cursorPt)
{
    if (m_state != State::WaitSecondPoint) return;

    Sketch* sk = activeSketch();
    if (!sk) return;

    const QVector2D newDelta = cursorPt - m_basePoint;
    if (m_liveDeltaApplied && newDelta == m_liveDelta) return;

    if (m_usePreselected) {
        // 模式 A：固定的既選集合，跟 MOVE 完全一樣——復原上一幀位移、
        // 套用這一幀位移即可（哪些物件受影響不會因為位移而改變）。
        if (m_liveDeltaApplied) {
            cad::transform::applyToSelection(
                sk, m_preSelected, cad::transform::Transform2D::translation(-m_liveDelta), false);
        }
        cad::transform::applyToSelection(
            sk, m_preSelected, cad::transform::Transform2D::translation(newDelta), false);
    } else {
        // 窗選模式：哪些端點落在窗內是根據「呼叫當下的座標」判斷的，
        // 每一幀都必須先用上一幀位移的反向量復原回原始座標，才能保證
        // 這一幀重新判斷出來的受影響點集合跟第一幀一致（見
        // stretchWithinRect() 的 solveAfter 參數說明）。
        const QVector2D rectMin(qMin(m_corner1.x(), m_corner2.x()),
                                qMin(m_corner1.y(), m_corner2.y()));
        const QVector2D rectMax(qMax(m_corner1.x(), m_corner2.x()),
                                qMax(m_corner1.y(), m_corner2.y()));
        if (m_liveDeltaApplied)
            cad::transform::stretchWithinRect(sk, rectMin, rectMax, -m_liveDelta, false);
        cad::transform::stretchWithinRect(sk, rectMin, rectMax, newDelta, false);
    }

    m_liveDelta = newDelta;
    m_liveDeltaApplied = true;
}

void StretchCommand::revertLivePreview()
{
    QObject::disconnect(m_livePreviewConn);
    m_livePreviewConn = QMetaObject::Connection();

    if (!m_liveDeltaApplied) return;

    Sketch* sk = activeSketch();
    if (sk) {
        if (m_usePreselected) {
            cad::transform::applyToSelection(
                sk, m_preSelected, cad::transform::Transform2D::translation(-m_liveDelta), false);
        } else {
            const QVector2D rectMin(qMin(m_corner1.x(), m_corner2.x()),
                                    qMin(m_corner1.y(), m_corner2.y()));
            const QVector2D rectMax(qMax(m_corner1.x(), m_corner2.x()),
                                    qMax(m_corner1.y(), m_corner2.y()));
            cad::transform::stretchWithinRect(sk, rectMin, rectMax, -m_liveDelta, false);
        }
    }
    m_liveDelta = QVector2D();
    m_liveDeltaApplied = false;
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
    // 防呆：正常路徑（WaitSecondPoint 分支）在呼叫這裡之前就已經
    // revertLivePreview() 過（m_liveDeltaApplied 早已是 false，這裡是
    // no-op）。取消路徑（onCancelled）則直接經由 cleanup() 呼叫到這裡，
    // 確保「取消 STRETCH」一定會把預覽期間搬動過的幾何還原。
    revertLivePreview();

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
