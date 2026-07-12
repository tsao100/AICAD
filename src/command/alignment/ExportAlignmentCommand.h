/**
 * @file ExportAlignmentCommand.h
 * @brief EXPORTALIGNMENT — 匯出（更新）目前文件中所有 TrackCenterLine 為舊系統
 *        .prj 專案索引檔 + *H.ALD / *V.ALD 線形資料。
 *
 * 流程
 * ────
 *   1. 選取（或建立）.prj 檔（記憶上次目錄；預設沿用匯入時記憶的目錄）。
 *   2. 目的資料夾 = .prj 所在目錄。
 *   3. 對文件中每一條 TrackCenterLine：
 *        - hFileName = 線路名稱 + "H.ALD"（線路名稱即匯入時的 trackId）；
 *        - 寫出水平線形（rawPoints()）為 *H.ALD；
 *        - 若該線路含垂直線形（vertical()->points() 非空），寫出對應 *V.ALD
 *          （檔名由 verticalFileNameFor() 推導）。
 *   4. 依文件中線路的目前順序，寫出 .prj 檔（每行一個 *H.ALD 檔名）。
 *
 * 這是一個同步（非互動式狀態機）命令，鏡射 ImportAlignmentCommand 的實作風格：
 * 直接在 execute() 內完成所有工作並回傳結果。此為「更新」語意的匯出——沒有
 * 儲存線路清單者一律視為需要寫出，同名 *H.ALD or *V.ALD 檔案會被覆寫。
 */
#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"

namespace aicad {
namespace command {

class ExportAlignmentCommand : public Command
{
public:
    ExportAlignmentCommand();

    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;
};

} // namespace command
} // namespace aicad
