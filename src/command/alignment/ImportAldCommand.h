/**
 * @file ImportAldCommand.h
 * @brief IMPORTALD — 直接選取單一 *.ALD 檔匯入水平線形（不透過 .prj 索引）。
 *
 * 與 ImportAlignmentCommand（IMPORTALIGNMENT，經 .prj 批次匯入多條線路的
 * H.ALD/V.ALD）不同：本命令只處理使用者選取的單一 ALD 檔，且只匯入水平
 * 線形，不嘗試比對／載入對應的 V.ALD（若也要垂直線形，仍請走批次匯入，或
 * 之後另外對同一條 TrackCenterLine 執行垂直匯入）。
 *
 * 流程
 * ────
 *   1. 選取單一 .ALD 檔（記憶上次目錄；與 IMPORTALIGNMENT 共用同一個
 *      設定鍵，因為兩者選檔的資料夾通常相同）。
 *   2. 讀取水平線形關鍵點（AldFileIO::readHorizontalALD）。
 *   3. 依檔名推導 TrackCenterLine 名稱：若檔名符合 "...H.ALD" 慣例則去掉
 *      結尾的 "H" 與副檔名；否則直接採用不含副檔名的檔名。若已存在同名
 *      TrackCenterLine 則就地更新，不產生重複線路。
 *   4. TM2 → Local 座標轉換（比照 IMPORTALIGNMENT，見 ProjectOrigin.h）。
 *   5. 匯入完成後開啟 Railway 資料夾的彙總 3D Alignment 顯示。
 *
 * 同步（非互動式狀態機）命令，風格與 ImportAlignmentCommand 一致。
 */
#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"

namespace aicad {
namespace command {

class ImportAldCommand : public Command
{
public:
    ImportAldCommand();

    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;
};

} // namespace command
} // namespace aicad
