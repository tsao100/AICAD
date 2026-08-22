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
#include "../cad/sketch/SketchConstraint.h"
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
           "or run MIRROR then click objects (window/crossing/fence supported) "
           "and press Enter or right-click, "
           "then specify the mirror line by clicking two points or selecting "
           "an existing line, then choose whether to erase the source objects "
           "([Yes/No] <No>: type y/n or press Enter/right-click, right-click "
           "alone accepts the default No).";
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
    m_previewClone.clear();
    m_previewHasAxis = false;
    m_previewAxisP0 = QVector2D();
    m_previewAxisP1 = QVector2D();

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
                    "[MIRROR] Select objects, then press Enter or right-click:");
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
// 取鏡射軸：點兩點，或直接選一條既有線段
// ─────────────────────────────────────────────────────────────────────────

void MirrorCommand::beginAxisPoint1Stage()
{
    m_state = State::WaitAxisPoint1;

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetPoint);

    subscribePointAcquired();
    subscribeCancelled();
    subscribeHover();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt(
            "[MIRROR] Specify first point of mirror line, or select an existing line:");
    }
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

bool MirrorCommand::lineAxisFromUuid(const QString& uuid, QVector2D& p0, QVector2D& p1) const
{
    Sketch* sk = activeSketch();
    if (!sk || uuid.isEmpty()) return false;

    SketchGeometry* g = sk->findGeometry(uuid);
    if (!g || g->type != SketchGeometryType::Line) return false;

    auto* line = static_cast<SketchLine*>(g);
    SketchPoint* sp = sk->point(line->startUuid);
    SketchPoint* ep = sk->point(line->endUuid);
    if (!sp || !ep) return false;

    p0 = sp->pos;
    p1 = ep->pos;
    return true;
}

void MirrorCommand::onPointAcquired(const QVariant& payload)
{
    const QVariantMap map = payload.toMap();
    const QPointF ptF = map.value("point").value<QPointF>();
    const QVector2D pt(float(ptF.x()), float(ptF.y()));

    auto* cmdMgr = core::CommandLineManager::instance();

    if (m_state == State::WaitAxisPoint1) {
        // 方式 (b)：整條線被偵測到（geomHandle==WholeGeom，代表游標點在
        // 線本身上，而不是吸附到該線的端點等特定子元素——那種情況
        // geomHandle 會是 Start/End，仍視為方式 (a) 的一個普通點，走下
        // 面的一般路徑）→ 直接以該線兩端點定軸，不需要再指定第二點。
        const QString geomUuid   = map.value("geomUuid").toString();
        const int     geomHandle = map.value("geomHandle").toInt();
        QVector2D lp0, lp1;
        if (!geomUuid.isEmpty() &&
            geomHandle == static_cast<int>(cad::GeomHandle::WholeGeom) &&
            lineAxisFromUuid(geomUuid, lp0, lp1))
        {
            finalizeAxis(lp0, lp1);
            return;
        }

        // 方式 (a)：一般點——記錄為鏡射軸第一點，進入等待第二點階段，
        // 掛上橡皮筋參考線與滑鼠移動即時預覽。
        m_axisP0 = pt;
        m_state = State::WaitAxisPoint2;

        unsubscribeHover();
        armPreviewClone();

        if (Sketch* sk = activeSketch())
            rb::armLinePreview(sk, m_axisP0);
        subscribeLivePreviewMove();

        if (cmdMgr) {
            cmdMgr->showPrompt(
                "[MIRROR] Specify second point of mirror line, or select an existing line:");
        }
        return;
    }

    if (m_state == State::WaitAxisPoint2) {
        // 第二點也支援「改選一條既有線」直接定軸（比照第一點的行為，
        // 讓使用者中途改變主意時不必取消重來）。
        const QString geomUuid   = map.value("geomUuid").toString();
        const int     geomHandle = map.value("geomHandle").toInt();
        QVector2D lp0, lp1;
        if (!geomUuid.isEmpty() &&
            geomHandle == static_cast<int>(cad::GeomHandle::WholeGeom) &&
            lineAxisFromUuid(geomUuid, lp0, lp1))
        {
            finalizeAxis(lp0, lp1);
            return;
        }

        if ((pt - m_axisP0).length() < float(kAxisDegenerateTol)) {
            // 鏡射軸兩點重合，無法定義方向：提示重新指定，停留在同一狀態。
            if (cmdMgr) {
                cmdMgr->printWarning(
                    "⚠️  Mirror line points must not coincide. Specify second point again:");
            }
            return;
        }

        finalizeAxis(m_axisP0, pt);
    }
}

void MirrorCommand::finalizeAxis(const QVector2D& p0, const QVector2D& p1)
{
    m_axisP0 = p0;
    m_axisP1 = p1;

    // 軸已確定：不論來自兩點還是既有線段，都不再需要 POINT_ACQUIRED /
    // hover / 滑鼠移動這幾路預覽輸入。
    auto* bus = core::Application::instance()->eventBus();
    if (bus) bus->unsubscribe(core::Events::POINT_ACQUIRED, this);
    unsubscribeHover();
    unsubscribeLivePreviewMove();

    // 軸線橡皮筋收起——後續等待 Yes/No 期間不再顯示（見 MirrorCommand.h
    // 說明）；被鏡射物件的預覽複製品則保留顯示。
    rb::disarm();

    armPreviewClone();
    updatePreviewClone(m_axisP0, m_axisP1);

    beginEraseOptionStage();
}

// ─────────────────────────────────────────────────────────────────────────
// hover 到既有線段的即時預覽（WaitAxisPoint1）
// ─────────────────────────────────────────────────────────────────────────

void MirrorCommand::subscribeHover()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::GEOM_HOVER, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onHover(v); },
                                      Qt::QueuedConnection);
        });
}

void MirrorCommand::unsubscribeHover()
{
    auto* bus = core::Application::instance()->eventBus();
    if (bus) bus->unsubscribe(core::Events::GEOM_HOVER, this);
}

void MirrorCommand::onHover(const QVariant& payload)
{
    if (m_state != State::WaitAxisPoint1) return;

    const QVariantMap map = payload.toMap();
    const QString geomUuid = map.value("geomUuid").toString();
    const int     handle   = map.value("handle").toInt();

    // 只有整條線被偵測到（WholeGeom）才視為候選鏡射軸；沒有 hover 到
    // 候選線段時維持上一幀畫面（不強制清空），避免游標移出線段瞬間預覽
    // 消失、之後又要等下一次有效 hover 才出現的閃爍感。
    QVector2D lp0, lp1;
    if (!geomUuid.isEmpty() &&
        handle == static_cast<int>(cad::GeomHandle::WholeGeom) &&
        lineAxisFromUuid(geomUuid, lp0, lp1))
    {
        armPreviewClone();
        updatePreviewClone(lp0, lp1);
    }
}

// ─────────────────────────────────────────────────────────────────────────
// 等第二點的滑鼠移動即時預覽（WaitAxisPoint2）
// ─────────────────────────────────────────────────────────────────────────

void MirrorCommand::subscribeLivePreviewMove()
{
    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    view::RubberBand* band = cadView ? cadView->rubberBand() : nullptr;
    if (!band) return;

    m_livePreviewConn = QObject::connect(
        band, &view::RubberBand::updated, this,
        [this, band] {
            if (m_state != State::WaitAxisPoint2) return;
            if (!band->hasCurrentPoint()) return;
            const QPointF cp = band->currentPoint();
            updatePreviewClone(m_axisP0, QVector2D(float(cp.x()), float(cp.y())));
        });
}

void MirrorCommand::unsubscribeLivePreviewMove()
{
    QObject::disconnect(m_livePreviewConn);
    m_livePreviewConn = QMetaObject::Connection();
}

// ─────────────────────────────────────────────────────────────────────────
// 預覽複製品（hover / 滑鼠移動 / 軸確定後維持顯示，共用同一份複製品）
// ─────────────────────────────────────────────────────────────────────────

void MirrorCommand::armPreviewClone()
{
    if (!m_previewClone.isEmpty()) return;   // 已建立，不重複複製

    Sketch* sk = activeSketch();
    if (!sk || m_selection.isEmpty()) return;

    // 以零位移建立一份與原物件完全重疊的預覽複製品（約束一併複製，見
    // cloneAndTransform() 說明）。之後每一幀只用輕量 applyToSelection()
    // 重新定位這份既有的複製品，不重新呼叫 cloneAndTransform()——避免
    // 每幀都重新複製約束，造成效能問題與幾何/約束物件暴增。
    m_previewClone = cad::transform::cloneAndTransform(sk, m_selection,
        cad::transform::Transform2D::translation(QVector2D()));
    m_previewHasAxis = false;
}

void MirrorCommand::updatePreviewClone(const QVector2D& p0, const QVector2D& p1)
{
    if (m_previewClone.isEmpty()) return;
    if ((p1 - p0).length() < float(kAxisDegenerateTol)) return;   // 退化軸，維持上一幀畫面

    Sketch* sk = activeSketch();
    if (!sk) return;

    if (m_previewHasAxis) {
        // 鏡射變換是自反的（同一個鏡射軸連續套用兩次＝還原），用同一個
        // 變換再套用一次，就能把預覽複製品從上一幀的鏡射位置移回「與
        // 原物件重疊」的狀態，不需要額外的反變換函式。
        const auto undoXf =
            cad::transform::Transform2D::mirror(m_previewAxisP0, m_previewAxisP1);
        cad::transform::applyToSelection(sk, m_previewClone, undoXf, /*solveAfter=*/false);
    }

    const auto newXf = cad::transform::Transform2D::mirror(p0, p1);
    cad::transform::applyToSelection(sk, m_previewClone, newXf, /*solveAfter=*/false);

    m_previewAxisP0  = p0;
    m_previewAxisP1  = p1;
    m_previewHasAxis = true;
}

void MirrorCommand::teardownPreviewClone()
{
    unsubscribeHover();
    unsubscribeLivePreviewMove();

    Sketch* sk = activeSketch();
    if (sk && !m_previewClone.isEmpty()) {
        // removeGeometry() 會一併清掉預覽複製品身上（含複製約束時新增
        // 出來的）約束/標註，比照 CopyCommand::teardownLivePreviewTargets()
        // 的既有作法。
        for (const QString& uuid : m_previewClone)
            sk->removeGeometry(uuid);
        Q_EMIT sk->rebuildRequested();
    }
    m_previewClone.clear();
    m_previewHasAxis = false;
    m_previewAxisP0 = QVector2D();
    m_previewAxisP1 = QVector2D();
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
    // 空字串（直接按 Enter 或單純按滑鼠右鍵）比照提示文字的預設值 <No>；
    // 先打 "y" 再按 Enter 或滑鼠右鍵（CadView::tryConfirmYesNoViaRightClick()
    // 會把輸入框內容一併送出）＝ Yes。
    const bool eraseSource = (a == "y" || a == "yes");

    // 預覽複製品只是暫時的視覺回饋：正式送出前整個刪除，改用最終軸線
    // 重新走一次正式流程，避免預覽期間的中間狀態與正式結果產生落差
    // （比照 COPY commit() 的既有原則，見 MirrorCommand.h 說明）。
    teardownPreviewClone();

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
    bus->unsubscribe(core::Events::GEOM_HOVER,        this);
    bus->unsubscribe(core::Events::YESNO_INPUT,       this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED, this);
}

void MirrorCommand::cleanup()
{
    // 防呆：正常路徑（onYesNoInput）在呼叫這裡之前就已經
    // teardownPreviewClone() 過（m_previewClone 早已清空，這裡是
    // no-op）。取消路徑（onSelectionCancelled/onCancelled）則直接經由
    // cleanup() 呼叫到這裡，確保「取消 MIRROR」一定會把預覽複製品連同
    // 其複製出來的約束一起清乾淨，不留下殘留幾何。
    teardownPreviewClone();

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
