/**
 * @file AssemblyCommands.h
 * @brief 零件組合（Assembly）指令：
 *
 *   INSERTPART（別名 IPART）— 插入一個 *.aicad 作為組合中的零件（單位縮放、初始位置／轉角、是否固定）
 *   MATE（別名 AM）         — 新增配合：選兩個幾何參照（零件的基準／面／邊／頂點，或世界座標軸／平面，
 *                              可在 3D 視圖點選），指定共線、共面、距離、夾角…；新增後立即求解
 *   SOLVEASM（別名 ASOLVE） — 重新求解整個組合
*   ASMACTION               — Feature Browser 右鍵選單的動作分派（ctx.data["action"]、["itemId"]）：
 *                              insertPart／insertPartVoid／moveToVoid／moveToAssembly／addMate／addMateForPart／solve／editPart／toggleFixed／syncPart／
 *                              openSource／editMate／toggleSuppress
 */
#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"

namespace aicad {
namespace command {

class InsertPartCommand : public Command {
public:
    InsertPartCommand();
    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;
};

class MateCommand : public Command {
public:
    MateCommand();
    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;
};

class SolveAssemblyCommand : public Command {
public:
    SolveAssemblyCommand();
    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;
};

class AssemblyActionCommand : public Command {
public:
    AssemblyActionCommand();
    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;
};

} // namespace command
} // namespace aicad
