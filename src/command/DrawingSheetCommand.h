/**
 * @file DrawingSheetCommand.h
 * @brief DRAWINGSHEET（alias: DSH）— 圖紙（Drawing Sheet）功能的操作入口
 *
 * 用法（命令列）：
 *   DSH            開啟目前文件的「作用中圖紙」對話框；文件尚無圖紙時
 *                  自動建立 A3 橫向圖紙，並放入 Front/Top/Right/Iso 四個視圖。
 *   DSH NEW        另外新增一張空白圖紙（含四個預設視圖）並開啟。
 *   DSH INFO       不開對話框，僅在命令列輸出診斷資訊（文件特徵、形體是否
 *                  為空、每個視圖的來源特徵、圖紙數量），用於除錯。
 *
 * 圖紙管理器（DrawingSheetManager）以 Document 為鍵、掛在 Document 之下
 * （QObject parent），文件關閉時一併釋放。目前圖紙「僅存在記憶體」，尚未
 * 隨 .aicad 一併存檔（DrawingSheet::save/load 已實作，但本命令未串接）。
 *
 * 同步（模態對話框）命令，風格同 AlignmentQuickTableCommand。
 */
#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"

namespace aicad {
namespace command {

class DrawingSheetCommand : public Command
{
public:
    DrawingSheetCommand();

    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;
};

} // namespace command
} // namespace aicad
