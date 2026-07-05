/**
 * @file ImportAlignmentCommand.h
 * @brief IMPORTALIGNMENT — 匯入舊系統 .prj 專案索引檔 + *H.ALD / *V.ALD 線形資料。
 *
 * 流程
 * ────
 *   1. 選取 .prj 檔（記憶上次目錄）。
 *   2. 解析 .prj，取得依序排列的 *H.ALD 檔名清單。
 *   3. 解析 ALD 檔案實際所在資料夾：
 *        - 優先嘗試與 .prj 相同目錄；
 *        - 其次嘗試上次記憶的 ALD 資料夾；
 *        - 皆找不到對應檔案時，跳出資料夾選擇對話框，並記憶使用者的選擇。
 *   4. 對每個 *H.ALD 項目：
 *        - 讀取水平線形，建立（或更新同名）TrackCenterLine；
 *        - 依檔名推導對應 *V.ALD（同前綴 + "V.ALD"），若存在則一併載入垂直線形。
 *   5. 匯入完成後開啟 Railway 資料夾的彙總 3D Alignment 顯示。
 *
 * 這是一個同步（非互動式狀態機）命令，模仿 BasicCommands.cpp 中 load/save 的
 * 實作風格：直接在 execute() 內完成所有工作並回傳結果。
 */
#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"

namespace aicad {
namespace command {

class ImportAlignmentCommand : public Command
{
public:
    ImportAlignmentCommand();

    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;
};

} // namespace command
} // namespace aicad
