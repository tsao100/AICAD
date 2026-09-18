/**
 * @file AlignmentQuickTableCommand.h
 * @brief ALIGNMENTQUICKTABLE（alias: AQT）— 開啟 AlignmentQuickTableDialog，
 *        讓使用者以「部分關鍵點資料」表格輸入，正算出完整平面線形後建立
 *        新的 TrackCenterLine。
 *
 * 背景：使用者提供的 Excel VBA（CommandButton5_Click）示範了「只填起點
 * 座標＋每列點位代碼／長度／半徑／緩和曲線類型，其餘座標由公式逐列正算」
 * 的工作方式。本指令是這個工作方式在 AICAD 內的對應功能：
 *
 *   1. 開啟 AlignmentQuickTableDialog（模態）。
 *   2. 使用者輸入起點資料與各列 TSC／長度／半徑／緩和曲線類型，按「計算」
 *      正算補齊（railway::computeQuickAlignmentTable()），按「確定」關閉。
 *   3. 取得對話框算出的完整關鍵點序列，建立（或視名稱重複而更新）一條
 *      TrackCenterLine（比照 ImportAldCommand 的建立/更新慣例）。
 *   4. 開啟 Railway 資料夾的 3D Alignment 顯示，供使用者立即檢視結果；
 *      後續可透過既有的「線形資料表」（AlignmentDataTableDialog）右鍵選單
 *      進一步編輯／檢視此線路。
 *
 * 同步（非互動式狀態機）命令：對話框本身即是全部互動介面，不走
 * CommandLineWidget 的逐步文字輸入流程，風格與 ImportAldCommand 一致。
 *
 * @see railway::computeQuickAlignmentTable()
 * @see AlignmentQuickTableDialog
 */
#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"

namespace aicad {
namespace command {

class AlignmentQuickTableCommand : public Command
{
public:
    AlignmentQuickTableCommand();

    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;
};

} // namespace command
} // namespace aicad
