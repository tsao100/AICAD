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
#include "../view/RubberBand.h"

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

        // livePreviewEnabled() 的子類別（MOVE／COPY）額外啟動「拖曳目標」
        // 的即時搬移預覽，見 armLivePreview() 說明。
        armLivePreview();

        auto* cmdMgr = core::CommandLineManager::instance();
        if (cmdMgr) cmdMgr->showPrompt(secondPointPrompt());
        return;
    }

    if (m_state == State::WaitSecondPoint) {
        Sketch* sk = activeSketch();
        const QVector2D delta = pt - m_basePoint;
        const auto xf = cad::transform::Transform2D::translation(delta);

        // 確認前先把即時預覽期間「輕量套用」在真實幾何上的位移還原，
        // 讓 commit() 走的仍是與沒有預覽時完全相同的單次完整變換
        // （from 原始位置 → 最終點），避免重複疊加位移或求解結果與
        // 預覽期間的中間態不一致。
        revertLivePreview();

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
// 即時搬移預覽（livePreviewEnabled() == true，目前為 MoveCommand／
// CopyCommand 使用）
// ─────────────────────────────────────────────────────────────────────────
//
// 分工比照既有 Grip 拖曳（SketchGripProvider）：拖曳中的每一幀只做「輕量」
// 搬移（直接改幾何座標、不跑約束求解器），放開/確認時才跑一次完整求解。
// 這裡用「增量位移」實作：armLivePreview() 進場時 m_liveDelta 歸零、透過
// armLivePreviewTargets() 取得這次要被即時拖曳預覽的目標幾何 uuid 清單
// （MOVE 是原選取範圍本身；COPY 是另外現場複製出來、疊在原物件正上方的
// 「預覽用複製品」——見標頭檔內兩者對 armLivePreviewTargets() 的說明）。
// 之後每次 RubberBand::updated()（= 每次滑鼠移動）都算出「這一幀相對於
// 基準點的總位移」與「上一幀已套用的總位移」之差，只套用這個差值，並
// 更新 m_liveDelta；revertLivePreview() 則先套用 -m_liveDelta 把目標幾何
// 完全還原回進場前的原始位置，再呼叫 teardownLivePreviewTargets() 讓子
// 類別視需要清理（MOVE 不需要；COPY 會把預覽用複製品整個刪掉）。
// commit() 前一定會先呼叫 revertLivePreview()，確保正式送出的變換／複製
// 仍是「原始位置 → 最終點」的單一完整動作，行為與沒有預覽時完全一致。

void SketchTransformCommandBase::armLivePreview()
{
    if (!livePreviewEnabled()) return;

    m_liveDelta = QVector2D();
    m_livePreviewTargets.clear();

    Sketch* sk = activeSketch();
    if (!sk) return;
    m_livePreviewTargets = armLivePreviewTargets(sk);
    if (m_livePreviewTargets.isEmpty()) return;

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

void SketchTransformCommandBase::updateLivePreviewTo(const QVector2D& cursorPt)
{
    Sketch* sk = activeSketch();
    if (!sk || m_livePreviewTargets.isEmpty()) return;

    const QVector2D newDelta    = cursorPt - m_basePoint;
    const QVector2D incremental = newDelta - m_liveDelta;
    if (incremental.x() == 0.0f && incremental.y() == 0.0f) return;

    cad::transform::applyToSelection(sk, m_livePreviewTargets,
        cad::transform::Transform2D::translation(incremental),
        /*solveAfter=*/false);
    m_liveDelta = newDelta;
}

void SketchTransformCommandBase::revertLivePreview()
{
    QObject::disconnect(m_livePreviewConn);
    m_livePreviewConn = QMetaObject::Connection();

    Sketch* sk = activeSketch();

    if (sk && !m_livePreviewTargets.isEmpty()) {
        if (m_liveDelta.x() != 0.0f || m_liveDelta.y() != 0.0f) {
            cad::transform::applyToSelection(sk, m_livePreviewTargets,
                cad::transform::Transform2D::translation(-m_liveDelta),
                /*solveAfter=*/false);
        }
        teardownLivePreviewTargets(sk, m_livePreviewTargets);
    }
    m_liveDelta = QVector2D();
    m_livePreviewTargets.clear();
}

void SketchTransformCommandBase::cleanup()
{
    // ─────────────────────────────────────────────────────────────────
    // cleanup
    // ─────────────────────────────────────────────────────────────────
    // 防呆：正常路徑（確認/commit）在呼叫這裡之前就已經 revertLivePreview()
    // 過（m_liveDelta 早已歸零，這裡是 no-op）。取消路徑
    // （onCancelled/onSelectionCancelled）則直接經由 cleanup() 呼叫到這裡，
    // 確保「取消 MOVE」一定會把預覽期間搬動過的幾何還原，不留下未求解、
    // 只是視覺上被搬移過的殘留狀態（也避免 UIManager 的快照比對把這個
    // 已取消的操作誤記成一筆 undo）。
    revertLivePreview();

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
