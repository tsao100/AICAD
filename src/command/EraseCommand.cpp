/**
 * @file EraseCommand.cpp
 * @brief ERASE 命令實作
 */

#include "EraseCommand.h"
#include "../core/Application.h"
#include "../core/CommandLineManager.h"
#include "../core/EventBus.h"
#include "../cad/Sketch.h"
#include "../ui/UIManager.h"
#include "../view/CadView.h"
#include <QDebug>

namespace aicad {
namespace command {

using namespace cad;

namespace {

/// 實際執行刪除的邏輯，抽成不依賴 EraseCommand 實例的自由函式。
/// 模式 B 等待 Enter 確認時，EraseCommand 物件早已隨命令同步結束而被
/// CommandManager 銷毀，因此真正的刪除動作不能寄在該物件的成員函式上。
void doErase(const QStringList& uuids)
{
    auto* app    = core::Application::instance();
    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk   = app ? app->activeSketch() : nullptr;
    if (!sk) {
        if (cmdMgr) cmdMgr->printError("No active sketch.");
        return;
    }

    int erased = 0;
    for (const QString& uuid : uuids) {
        // 草圖平面參考幾何（X 軸 / Y 軸 / 原點）為固定參考，不可刪除
        if (uuid.startsWith("sketch_xaxis:")  ||
            uuid.startsWith("sketch_yaxis:")  ||
            uuid.startsWith("sketch_origin:"))
            continue;

        if (sk->removeGeometry(uuid))
            ++erased;
    }

    // 清除選取狀態，避免刪除後 AIS context 仍殘留無效的選取參照
    auto* uiMgr = app ? app->uiManager() : nullptr;
    view::CadView* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->clearSketchGeomSelection();

    if (cmdMgr) {
        if (erased > 0)
            cmdMgr->printSuccess(QString("✅ Erased %1 object(s).").arg(erased));
        else
            cmdMgr->printWarning("⚠️  No matching geometry found to erase.");
    }
}

} // namespace

EraseCommand::EraseCommand()
    : Command("ERASE", "Erase selected sketch geometry (alias: E)")
{
}

QString EraseCommand::getUsage() const
{
    return "Usage: ERASE — select geometry first then run ERASE, "
           "or run ERASE then select geometry in the viewport and press Enter.";
}

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
        doErase(ctx.args);
        return CommandResult::Success();
    }

    // ── 模式 B：尚未選取 — 提示使用者在視圖中選取後按 Enter 確認 ──────
    if (cmdMgr) {
        cmdMgr->showPrompt(
            "[ERASE] Select objects to erase in the viewport, then press Enter:");
        cmdMgr->waitForInput(core::InputType::String);
    }

    auto* bus = app ? app->eventBus() : nullptr;
    if (bus) {
        // 訂閱的 receiver 用永久存在的 CommandLineManager singleton，
        // 而不是 this（this 在 execute() 回傳後就會被 CommandManager
        // 銷毀），確保使用者按下 Enter 時 callback 仍然存活。
        auto* persistentReceiver = core::CommandLineManager::instance();

        bus->subscribe(core::Events::STRING_INPUT, persistentReceiver,
                       [persistentReceiver](const QVariant&) {
                           auto* bus2 = core::Application::instance()->eventBus();
                           if (bus2) {
                               bus2->unsubscribe(core::Events::STRING_INPUT, persistentReceiver);
                               bus2->unsubscribe(core::Events::COMMAND_CANCELLED, persistentReceiver);
                           }

                           auto* app2  = core::Application::instance();
                           auto* uiMgr = app2 ? app2->uiManager() : nullptr;
                           view::CadView* cadView = uiMgr ? uiMgr->cadView() : nullptr;
                           QStringList sel = cadView ? cadView->selectedGeomUuids()
                                                      : QStringList();

                           if (sel.isEmpty()) {
                               core::CommandLineManager::instance()->printWarning(
                                   "⚠️  No objects selected. Erase cancelled.");
                               return;
                           }

                           doErase(sel);
                       });

        // 使用者按 Esc 取消輸入（CommandLineManager::cancelCommand）時，
        // 一併取消這次等待，避免訂閱永遠留著等不到的 STRING_INPUT。
        bus->subscribe(core::Events::COMMAND_CANCELLED, persistentReceiver,
                       [persistentReceiver](const QVariant&) {
                           auto* bus2 = core::Application::instance()->eventBus();
                           if (bus2) {
                               bus2->unsubscribe(core::Events::STRING_INPUT, persistentReceiver);
                               bus2->unsubscribe(core::Events::COMMAND_CANCELLED, persistentReceiver);
                           }
                       });
    }

    return CommandResult::Success("Waiting for selection...");
}

} // namespace command
} // namespace aicad