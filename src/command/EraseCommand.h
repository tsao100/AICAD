/**
 * @file EraseCommand.h
 * @brief ERASE 命令 — 刪除草圖中選取的幾何元素
 */

#ifndef AICAD_COMMAND_ERASECOMMAND_H
#define AICAD_COMMAND_ERASECOMMAND_H

#include "Command.h"

namespace aicad {
namespace command {

/**
 * @brief ERASE 命令（別名 E）
 *
 * 兩種使用方式：
 *  - 模式 A：先在視圖中選取幾何，再輸入 ERASE（或按 Delete 鍵）
 *            → UIManager 已將選取的 UUID 填入 ctx.args，直接刪除。
 *  - 模式 B：直接輸入 ERASE，尚未選取任何幾何
 *            → 提示使用者在視圖中選取後按 Enter 確認。
 *            注意：此命令物件本身不會保持 Running 狀態，execute() 回傳後
 *            CommandManager 會立即將其視為完成並銷毀，讓 CadView 的滑鼠
 *            多選（SelectDetected）行為維持正常，不被誤判成繪圖命令的
 *            取點模式。真正等待 Enter 確認 / 執行刪除的邏輯改為訂閱在
 *            永久存在的 CommandLineManager singleton 上，不依賴本物件
 *            的生命週期。
 *
 * 僅在 Sketching 模式（有 active sketch）下有效；草圖平面參考幾何
 * （X 軸 / Y 軸 / 原點）為固定參考，不可被刪除。
 */
class EraseCommand : public Command {
    Q_OBJECT

public:
    EraseCommand();

    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;
};

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_ERASECOMMAND_H