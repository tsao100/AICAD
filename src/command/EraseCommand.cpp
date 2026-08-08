/**
 * @file EraseCommand.cpp
 * @brief ERASE 命令實作
 *
 * 互動式取幾何（模式 B）的架構與 GeneralDimCommand::execute() / 
 * AlignmentSCSCommand::handlePointAcquired() 相同：命令保持 Running 狀態，
 * 將 CadView 切到 GetGeom 模式，訂閱 GEOM_PICKED 累積使用者點擊的幾何，
 * 訂閱 STRING_INPUT 處理 Enter 確認，訂閱 COMMAND_CANCELLED 處理 Esc 取消。
 */

#include "EraseCommand.h"
#include "../core/Application.h"
#include "../core/CommandLineManager.h"
#include "../core/EventBus.h"
#include "../cad/Sketch.h"
#include "../cad/sketch/DimensionLineAIS.h"
#include "../cad/sketch/ConstraintSymbolAIS.h"
#include "../cad/sketch/ConstraintOverlayManager.h"
#include "../ui/UIManager.h"
#include "../ui/SketchPanel.h"
#include "../view/CadView.h"
#include <QDebug>

namespace aicad {
namespace command {

using namespace cad;

namespace {
bool isFixedReferenceUuid(const QString& uuid)
{
    // 草圖平面參考幾何（X 軸 / Y 軸 / 原點）為固定參考，不可刪除/選取
    return uuid.startsWith("sketch_xaxis:")  ||
           uuid.startsWith("sketch_yaxis:")  ||
           uuid.startsWith("sketch_origin:");
}

/// 統一的單一物件刪除入口：先當作一般幾何刪除，找不到再當作約束刪除
/// （例如點擊尺寸線文字選到的是 constraint UUID，而不是幾何 UUID）。
bool eraseOne(cad::Sketch* sk, const QString& uuid)
{
    if (!sk || uuid.isEmpty() || isFixedReferenceUuid(uuid))
        return false;
    if (sk->removeGeometry(uuid))
        return true;
    if (sk->removeConstraint(uuid))
        return true;
    return false;
}
} // namespace

EraseCommand::EraseCommand()
    : Command("ERASE", "Erase selected sketch geometry (alias: E)")
{
}

QString EraseCommand::getUsage() const
{
    return "Usage: ERASE — select geometry first then run ERASE, "
           "or run ERASE then click geometry in the viewport "
           "(window/crossing/fence supported) and press Enter or right-click.";
}

// ─────────────────────────────────────────────────────────────────────────────
// execute
// ─────────────────────────────────────────────────────────────────────────────

CommandResult EraseCommand::execute(const CommandContext& ctx)
{
    auto* app    = core::Application::instance();
    auto* cmdMgr = core::CommandLineManager::instance();

    Sketch* sk = app ? app->activeSketch() : nullptr;
    if (!sk) {
        if (cmdMgr) cmdMgr->printError("No active sketch. Enter sketch edit mode first.");
        return CommandResult::Failure("No active sketch.");
    }

    // ── 模式 A：呼叫時已帶有選取的 UUID ───────────────────────────────
    // （Delete 鍵、或「先選取再輸入 ERASE / 按按鈕」皆會由 UIManager
    //   經 COMMAND_EXECUTE_REQUEST 把目前選取塞進 ctx.args）
    if (!ctx.args.isEmpty()) {
        int erased = 0;
        for (const QString& uuid : ctx.args) {
            if (eraseOne(sk, uuid)) ++erased;
        }

        auto* uiMgr = app->uiManager();
        view::CadView* cadView = uiMgr ? uiMgr->cadView() : nullptr;
        if (cadView) cadView->clearSketchGeomSelection();

        if (cmdMgr) {
            if (erased > 0)
                cmdMgr->printSuccess(QString("✅ Erased %1 object(s).").arg(erased));
            else
                cmdMgr->printWarning("⚠️  No matching geometry found to erase.");
        }
        return CommandResult::Success();
    }

    // ── 模式 B：尚未選取 — 進入互動取幾何模式 ─────────────────────────
    m_pending.clear();

    auto* uiMgr   = app->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) {
        cadView->setMode(view::InteractionMode::GetGeom);
        // 開啟窗選/穿越窗選/籬選/多邊形選取資格，見標頭檔說明與
        // CadView::setCommandBoxSelectEligible()。cleanup() 會對稱關閉。
        cadView->setCommandBoxSelectEligible(true);
    }

    setWaitingForInput();   // ★ 必須設為 Running，命令才會持續存活等待使用者互動

    subscribeAll();

    if (cmdMgr) {
        cmdMgr->showPrompt(
            "[ERASE] Click objects to erase (click again to deselect), then press Enter or right-click:");
        cmdMgr->waitForInput(core::InputType::String);
    }

    return CommandResult::Success("Waiting for selection...");
}

// ─────────────────────────────────────────────────────────────────────────────
// subscribeAll / unsubscribeAll
// ─────────────────────────────────────────────────────────────────────────────

void EraseCommand::subscribeAll()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;

    bus->subscribe(core::Events::GEOM_PICKED, this,
                   [this](const QVariant& data) { onGeomPicked(data); });

    bus->subscribe(core::Events::STRING_INPUT, this,
                   [this](const QVariant& data) { onConfirm(data); });

    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
                   [this](const QVariant& data) { onCancelled(data); });

    bus->subscribe(core::Events::SKETCH_GEOM_SELECTED, this,
                   [this](const QVariant& data) { onBoxSelected(data); });
}

void EraseCommand::unsubscribeAll()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;

    bus->unsubscribe(core::Events::GEOM_PICKED,      this);
    bus->unsubscribe(core::Events::STRING_INPUT,     this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED, this);
    bus->unsubscribe(core::Events::SKETCH_GEOM_SELECTED, this);
}

// ─────────────────────────────────────────────────────────────────────────────
// onGeomPicked — 每次在視圖中點擊一個幾何時觸發
// ─────────────────────────────────────────────────────────────────────────────

void EraseCommand::onGeomPicked(const QVariant& data)
{
    QVariantMap map = data.toMap();
    QString uuid = map.value("geomUuid").toString();

    if (uuid.isEmpty()) {
        // 一般幾何沒命中 → 再檢查是否點到尺寸線（文字），取得其約束 UUID
        auto* app     = core::Application::instance();
        auto* uiMgr   = app ? app->uiManager() : nullptr;
        view::CadView* cadView = uiMgr ? uiMgr->cadView() : nullptr;
        if (cadView) uuid = cadView->detectedConstraintUuid();
    }

    if (uuid.isEmpty() || isFixedReferenceUuid(uuid)) {
        auto* cmdMgr = core::CommandLineManager::instance();
        if (cmdMgr) cmdMgr->printWarning("⚠️  No erasable geometry at that point — click closer.");
        return;
    }

    // 點選已在待刪清單中的幾何 → 取消選取（toggle）
    if (m_pending.contains(uuid)) {
        m_pending.removeAll(uuid);
        setHighlight(uuid, false);
    } else {
        m_pending.append(uuid);
        setHighlight(uuid, true);
    }

    updatePendingPrompt();
}

// ─────────────────────────────────────────────────────────────────────────────
// onConfirm — 使用者按 Enter（STRING_INPUT，空白輸入）確認刪除
// ─────────────────────────────────────────────────────────────────────────────

void EraseCommand::onConfirm(const QVariant& /*data*/)
{
    auto* app    = core::Application::instance();
    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk   = app ? app->activeSketch() : nullptr;

    if (!sk || m_pending.isEmpty()) {
        if (cmdMgr) cmdMgr->printWarning("⚠️  No objects selected. Erase cancelled.");
        cleanup();
        return;
    }

    int erased = 0;
    for (const QString& uuid : m_pending) {
        if (eraseOne(sk, uuid)) ++erased;
    }

    if (cmdMgr) {
        if (erased > 0)
            cmdMgr->printSuccess(QString("✅ Erased %1 object(s).").arg(erased));
        else
            cmdMgr->printWarning("⚠️  No matching geometry found to erase.");
    }

    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────────
// onCancelled — 使用者按 Esc 取消（COMMAND_CANCELLED）
// ─────────────────────────────────────────────────────────────────────────────

void EraseCommand::onCancelled(const QVariant& /*data*/)
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->printMessage("ERASE cancelled.");
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────────
// onBoxSelected — CadView 窗選/穿越窗選/籬選/多邊形選取完成
// （Events::SKETCH_GEOM_SELECTED，見標頭檔說明）
// ─────────────────────────────────────────────────────────────────────────────

void EraseCommand::onBoxSelected(const QVariant& data)
{
    const QVariantMap map   = data.toMap();
    const QStringList uuids = map.value("uuids").toStringList();

    // uuids 是框選完成當下 AIS context 內「完整」的選取結果（CadView 一律
    // 以 additive 模式套用窗選，見 execute() 對
    // setCommandBoxSelectEligible() 的說明），直接覆蓋 m_pending 即可，
    // 並過濾掉固定參考幾何。
    m_pending.clear();
    for (const QString& uuid : uuids) {
        if (uuid.isEmpty() || isFixedReferenceUuid(uuid)) continue;
        m_pending.append(uuid);
    }

    updatePendingPrompt();

    // CadView 結束窗選時會呼叫 resetInputWait()，需要重新設定等待輸入，
    // 否則命令列會卡住，使用者按 Enter/繼續點選都不會有反應。
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->waitForInput(core::InputType::String);
}

// ─────────────────────────────────────────────────────────────────────────────
// setHighlight — 用 AIS_InteractiveContext 將待刪幾何標示為已選取
// ─────────────────────────────────────────────────────────────────────────────

void EraseCommand::setHighlight(const QString& uuid, bool on)
{
    auto* app     = core::Application::instance();
    auto* uiMgr   = app ? app->uiManager() : nullptr;
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    Sketch* sk    = app ? app->activeSketch() : nullptr;
    if (!cadView || !sk) return;

    auto context = cadView->context();
    if (context.IsNull()) return;

    auto toggle = [&](const Handle(AIS_InteractiveObject)& obj) -> bool {
        if (obj.IsNull()) return false;
        bool isSelected = context->IsSelected(obj);
        if (on != isSelected)
            context->AddOrRemoveSelected(obj, Standard_True);
        return true;
    };

    // 1) 先當作一般幾何（直線 / 點 / 圓…）處理
    const QList<QString>& uuids = sk->aisShapeUuids();
    QList<Handle(AIS_InteractiveObject)> shapes = sk->aisShapes();
    for (int i = 0; i < uuids.size() && i < shapes.size(); ++i) {
        if (uuids[i] == uuid) {
            toggle(shapes[i]);
            return;
        }
    }

    // 1b) ⚠️ 修正：建構線／弧／圓除了不參與輪廓外，其他功能都要與一般幾何
    // 相同——包含 ERASE 選取時的高亮回饋。建構幾何是獨立於 aisShapes()／
    // aisShapeUuids() 的另一組 parallel array（見 Sketch::constructionShapes()
    // 說明），原本這裡完全沒有查詢，導致點選建構幾何準備刪除時沒有高亮。
    const QList<QString>& ctorUuids = sk->constructionShapeUuids();
    const QList<Handle(AIS_Shape)> ctorShapes = sk->constructionShapes();
    for (int i = 0; i < ctorUuids.size() && i < ctorShapes.size(); ++i) {
        if (ctorUuids[i] == uuid) {
            toggle(ctorShapes[i]);
            return;
        }
    }

    // 2) 找不到對應幾何 → 嘗試當作尺寸約束（AIS_DimensionLine）處理
    auto* panel = uiMgr ? uiMgr->findChild<ui::SketchPanel*>() : nullptr;
    if (panel && panel->overlay()) {
        Handle(AIS_DimensionLine) dim = panel->overlay()->dimLineAISForConstraint(uuid);
        if (!dim.IsNull()) {
            toggle(dim);
            return;
        }
        // 3) 也找不到 → 嘗試當作幾何約束符號（AIS_ConstraintSymbol，
        //    如 Horizontal/Vertical/Coincident 等小圖示）處理
        Handle(cad::AIS_ConstraintSymbol) sym = panel->overlay()->symbolAISForConstraint(uuid);
        if (!sym.IsNull())
            toggle(sym);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// updatePendingPrompt
// ─────────────────────────────────────────────────────────────────────────────

void EraseCommand::updatePendingPrompt()
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (!cmdMgr) return;

    cmdMgr->showPrompt(
        QString("[ERASE] %1 object(s) selected. Click more, or press Enter to erase:")
            .arg(m_pending.size()));
}

// ─────────────────────────────────────────────────────────────────────────────
// cleanup
// ─────────────────────────────────────────────────────────────────────────────

void EraseCommand::cleanup()
{
    unsubscribeAll();
    m_pending.clear();

    auto* app     = core::Application::instance();
    auto* uiMgr   = app ? app->uiManager() : nullptr;
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) {
        cadView->clearSketchGeomSelection();
        cadView->setCommandBoxSelectEligible(false);
        cadView->setMode(view::InteractionMode::Sketching);
    }

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->clearPrompt();

    if (state() == CommandState::Running)
        complete(CommandResult::Success());
}

} // namespace command
} // namespace aicad
