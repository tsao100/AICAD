/**
 * @file DefinePlaneCommand.h
 * @brief DEFPLANE — 定義平面（基準平面 + 沿法向量偏移）。新平面成為 Feature Browser 的一個項目，可用來建立草圖。
 */
#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"

namespace aicad {
namespace command {

class DefinePlaneCommand : public Command
{
public:
    DefinePlaneCommand();
    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;
};

} // namespace command
} // namespace aicad
