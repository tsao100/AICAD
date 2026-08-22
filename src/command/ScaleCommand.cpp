/**
 * @file ScaleCommand.cpp
 * @brief 見 ScaleCommand.h 檔頭說明。
 */
#include "ScaleCommand.h"
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
#include <cmath>

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

/// 縮放倍率的下限防呆：避免拖曳到基準點正上方時倍率趨近 0，把幾何
/// 縮成退化的一個點（Circle/Ellipse 半徑歸零、Line 頭尾重合等）。
constexpr double kMinScaleFactor = 1e-4;
} // namespace

ScaleCommand::ScaleCommand()
    : Command("SCALE", "Scale selected sketch geometry about a base point, "
                        "with live preview while dragging (alias: SC)")
{
}

QString ScaleCommand::getUsage() const
{
    return "Usage: SCALE — select geometry first then run SCALE, "
           "or run SCALE then click objects (window/crossing/fence supported) "
           "and press Enter or right-click, then specify base point and "
           "scale factor: drag to preview (factor = distance from base point "
           "to cursor), click to confirm, or type a factor directly.";
}

cad::Sketch* ScaleCommand::activeSketch() const
{
    return core::Application::instance()->activeSketch();
}

// ─────────────────────────────────────────────────────────────────────────
// execute
// ─────────────────────────────────────────────────────────────────────────

CommandResult ScaleCommand::execute(const CommandContext& ctx)
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

    setWaitingForInput();
    beginSelection(sk);
    return CommandResult::Success("Waiting for selection...");
}

// ─────────────────────────────────────────────────────────────────────────
// 選取階段
// ─────────────────────────────────────────────────────────────────────────

void ScaleCommand::beginSelection(cad::Sketch* sketch)
{
    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetGeom);

    m_picker = new SketchSelectionPicker(this);
    connect(m_picker, &SketchSelectionPicker::confirmed,
            this, &ScaleCommand::onSelectionConfirmed);
    connect(m_picker, &SketchSelectionPicker::cancelled,
            this, &ScaleCommand::onSelectionCancelled);

    m_picker->begin(sketch, SketchSelectionPicker::Mode::PickMultiple,
                    "[SCALE] Select objects, then press Enter or right-click:");
}

void ScaleCommand::onSelectionConfirmed(const QStringList& uuids)
{
    if (uuids.isEmpty()) {
        onSelectionCancelled();
        return;
    }
    m_selection = uuids;
    beginBasePointStage();
}

void ScaleCommand::onSelectionCancelled()
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->printMessage("SCALE cancelled.");
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// 取基準點
// ─────────────────────────────────────────────────────────────────────────

void ScaleCommand::beginBasePointStage()
{
    m_state = State::WaitBasePoint;

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetPoint);

    subscribePointAcquired();
    subscribeCancelled();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->showPrompt("[SCALE] Specify base point:");
}

void ScaleCommand::subscribePointAcquired()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::POINT_ACQUIRED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onPointAcquired(v); },
                                      Qt::QueuedConnection);
        });
}

void ScaleCommand::onPointAcquired(const QVariant& payload)
{
    const QVariantMap map = payload.toMap();
    const QPointF ptF = map.value("point").value<QPointF>();
    const QVector2D pt(float(ptF.x()), float(ptF.y()));

    if (m_state == State::WaitBasePoint) {
        m_basePoint = pt;
        beginFactorStage();

        // 基準點→游標的橡皮筋線，線長即目前預覽的縮放倍率（與直接輸入
        // 倍率數字視覺上一致）。
        if (Sketch* sk = activeSketch())
            rb::armLinePreview(sk, m_basePoint);

        // 啟動「拖曳中即時縮放預覽」（見 armLivePreview() 說明）。
        armLivePreview();
        return;
    }

    if (m_state == State::WaitFactor) {
        // 用滑鼠點一個點來指定倍率：倍率＝基準點到該點的距離。與 ROTATE
        // 的「取第二點決定角度」概念類似，這裡取的是距離而不是方位角。
        if ((pt - m_basePoint).lengthSquared() < 1e-8f) {
            // 點在基準點正上方（幾乎重合），倍率無意義，忽略這次點擊，
            // 讓使用者可以繼續拖曳或改用數字輸入。
            return;
        }
        applyScale(factorAtPoint(pt));
    }
}

// ─────────────────────────────────────────────────────────────────────────
// 取縮放倍率
// ─────────────────────────────────────────────────────────────────────────

double ScaleCommand::factorAtPoint(const QVector2D& pt) const
{
    return double((pt - m_basePoint).length());
}

void ScaleCommand::beginFactorStage()
{
    m_state = State::WaitFactor;

    // 基準點階段已經在 GetPoint 模式並訂閱 POINT_ACQUIRED，這裡不重複
    // 呼叫 subscribePointAcquired()（該訂閱一路延續到本階段，讓使用者可以
    // 直接點第二點決定倍率，也可以改成打數字——兩種輸入方式同時有效）。

    subscribeNumberInput();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt(
            "[SCALE] Specify scale factor: drag to preview, click to confirm, "
            "or type a factor:");
        cmdMgr->waitForInput(core::InputType::Number);
    }
}

void ScaleCommand::applyScale(double factor)
{
    Sketch* sk = activeSketch();
    auto* cmdMgr = core::CommandLineManager::instance();

    // 確認前先把即時預覽期間「輕量套用」在真實幾何上的縮放還原，讓最終
    // 套用的仍是與沒有預覽時完全相同的單次完整變換（原始幾何 → 最終
    // 倍率），避免重複疊加縮放或求解結果與預覽期間的中間態不一致。
    revertLivePreview();

    if (factor <= 0.0) {
        if (cmdMgr) cmdMgr->printError("Invalid scale factor: must be > 0.");
        cleanup();
        return;
    }

    if (sk && !m_selection.isEmpty()) {
        const auto xf = cad::transform::Transform2D::scale(m_basePoint, factor);
        cad::transform::applyToSelection(sk, m_selection, xf);

        if (cmdMgr)
            cmdMgr->printSuccess(
                QString("✅ Scaled %1 object(s) by %2×.").arg(m_selection.size()).arg(factor));
    }

    cleanup();
}

void ScaleCommand::subscribeNumberInput()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::NUMBER_INPUT, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onNumberInput(v); },
                                      Qt::QueuedConnection);
        });
}

void ScaleCommand::onNumberInput(const QVariant& payload)
{
    if (m_state != State::WaitFactor) return;

    bool ok = false;
    const double factor = payload.toString().trimmed().toDouble(&ok);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (!ok || factor <= 0.0) {
        if (cmdMgr) {
            cmdMgr->printError("Invalid scale factor. Enter a positive number:");
            cmdMgr->waitForInput(core::InputType::Number);
        }
        return;  // 停留在 WaitFactor，等待重新輸入
    }

    applyScale(factor);
}

// ─────────────────────────────────────────────────────────────────────────
// 即時縮放預覽（比照 SketchTransformCommandBase 內 MOVE/COPY 的
// livePreview 機制：拖曳中的每一幀只做「輕量」變換——直接改幾何座標／
// 半徑、不跑約束求解器，放開/確認時才跑一次完整求解）
// ─────────────────────────────────────────────────────────────────────────
//
// 用「累積倍率」實作：armLivePreview() 進場時 m_liveFactor 歸零重設為
// 1.0（代表目前真實幾何仍是「原始未縮放」狀態）。之後每次 RubberBand::
// updated()（＝每次滑鼠移動）都算出「這一幀相對於基準點的目標累積倍率」
// newFactor，與「上一幀已套用的累積倍率」m_liveFactor 的比值
// newFactor/m_liveFactor 就是這一幀需要「疊加」的增量倍率——因為對同一個
// 中心點連續做兩次等比縮放，等同一次縮放兩者的乘積（e.g. 對真實幾何先縮
// 2 倍、再縮 1.5 倍＝最終相對原始幾何縮了 3 倍），套用這個增量倍率即可讓
// 真實幾何從「上一幀的縮放結果」正確過渡到「這一幀的目標縮放結果」，不需
// 要每幀都先復原回原始大小再重新套用完整倍率。revertLivePreview() 則套用
// 1/m_liveFactor 把真實幾何完全還原回進場前的原始大小。
// applyScale() 前一定會先呼叫 revertLivePreview()，確保正式送出的縮放仍是
// 「原始幾何 → 最終倍率」的單一完整動作，行為與沒有預覽時完全一致。

void ScaleCommand::armLivePreview()
{
    m_liveFactor = 1.0;

    Sketch* sk = activeSketch();
    if (!sk || m_selection.isEmpty()) return;

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    view::RubberBand* band = cadView ? cadView->rubberBand() : nullptr;
    if (!band) return;

    m_livePreviewConn = QObject::connect(
        band, &view::RubberBand::updated, this,
        [this, band] {
            if (!band->hasCurrentPoint()) return;
            const QPointF cp = band->currentPoint();
            updateLivePreviewTo(QVector2D(float(cp.x()), float(cp.y())));
        });
}

void ScaleCommand::updateLivePreviewTo(const QVector2D& cursorPt)
{
    Sketch* sk = activeSketch();
    if (!sk || m_selection.isEmpty()) return;

    double newFactor = factorAtPoint(cursorPt);
    if (newFactor < kMinScaleFactor) newFactor = kMinScaleFactor;
    if (std::abs(newFactor - m_liveFactor) < 1e-9) return;

    const double incremental = newFactor / m_liveFactor;
    cad::transform::applyToSelection(sk, m_selection,
        cad::transform::Transform2D::scale(m_basePoint, incremental),
        /*solveAfter=*/false);
    m_liveFactor = newFactor;
}

void ScaleCommand::revertLivePreview()
{
    QObject::disconnect(m_livePreviewConn);
    m_livePreviewConn = QMetaObject::Connection();

    Sketch* sk = activeSketch();
    if (sk && !m_selection.isEmpty() && std::abs(m_liveFactor - 1.0) > 1e-9) {
        cad::transform::applyToSelection(sk, m_selection,
            cad::transform::Transform2D::scale(m_basePoint, 1.0 / m_liveFactor),
            /*solveAfter=*/false);
    }
    m_liveFactor = 1.0;
}

// ─────────────────────────────────────────────────────────────────────────
// 取消
// ─────────────────────────────────────────────────────────────────────────

void ScaleCommand::subscribeCancelled()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onCancelled(v); },
                                      Qt::QueuedConnection);
        });
}

void ScaleCommand::onCancelled(const QVariant&)
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->printMessage("SCALE cancelled.");
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// unsubscribeAll / cleanup
// ─────────────────────────────────────────────────────────────────────────

void ScaleCommand::unsubscribeAll()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->unsubscribe(core::Events::POINT_ACQUIRED,    this);
    bus->unsubscribe(core::Events::NUMBER_INPUT,      this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED, this);
}

void ScaleCommand::cleanup()
{
    // 防呆：正常路徑（applyScale）在呼叫這裡之前就已經 revertLivePreview()
    // 過（m_liveFactor 早已歸 1.0，這裡是 no-op）。取消路徑
    // （onCancelled/onSelectionCancelled）則直接經由 cleanup() 呼叫到這裡，
    // 確保「取消 SCALE」一定會把預覽期間縮放過的幾何還原。
    revertLivePreview();

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
    m_basePoint = QVector2D();

    if (state() == CommandState::Running)
        complete(CommandResult::Success());
}

} // namespace command
} // namespace aicad
