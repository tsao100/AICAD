/**
 * @file ComponentInsertCommand.h
 * @brief INSERTCOMP — 插入一個 *.aicad 作為「3D 組件」，指定其安裝里程。
 *
 * 流程：
 *   1. 選取 *.aicad（內容為 3D 組件本體，以及該組件所需的空間）
 *   2. 以暫時的 Document 載入該檔，列出其中的實體 → 使用者指定每個實體是「組件」或「所需空間」
 *   3. 輸入安裝位置（線路、里程、橫向偏移…，可多個）
 *   4. 建立 ComponentInstall：依超高定位組件，並讓道版 Loft 在所需空間處開孔
 */
#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"

namespace aicad {
namespace command {

class ComponentInsertCommand : public Command
{
public:
    ComponentInsertCommand();

    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;
};

} // namespace command
} // namespace aicad
