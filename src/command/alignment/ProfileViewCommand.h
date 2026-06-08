/**
 * @file ProfileViewCommand.h
 * @brief Step 19 — PROFILEVIEW 命令
 *
 * 執行時顯示並置頂 VAlignEditorDockWidget。
 * 若 Dock 已可見則僅 raise()。
 *
 * 使用方式：鍵入 PROFILEVIEW（或別名 PV）
 */
#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"
#include "command/CommandTypes.h"
#include "core/Application.h"
#include "ui/UIManager.h"
#include "ui/VAlignEditorDockWidget.h"

#include <QDebug>

namespace aicad {
namespace command {

class ProfileViewCommand : public Command
{
    Q_OBJECT

public:
    explicit ProfileViewCommand(QObject* parent = nullptr)
        : Command("profileview", "Show Vertical Alignment Profile View", parent)
    {}

    CommandResult execute(const CommandContext& /*ctx*/) override
    {
        auto* app    = core::Application::instance();
        auto* uiMgr  = app ? app->uiManager() : nullptr;
        auto* dock   = uiMgr ? uiMgr->vAlignDockWidget() : nullptr;

        if (!dock) {
            return CommandResult::Failure(
                "VAlignEditorDockWidget 未初始化 — 無法開啟縱斷面視圖。");
        }

        dock->setVisible(true);
        dock->raise();
        // 確保 dock 取得焦點（若嵌入為浮動視窗則 activateWindow）
        dock->activateWindow();

        outputMessage("縱斷面視圖已開啟。");
        return CommandResult::Success("ProfileView shown");
    }

    bool        isInteractive() const override { return false; }
    QString     getUsage()      const override
    {
        return "Usage: PROFILEVIEW\n"
               "  顯示縱斷面（Vertical Alignment）編輯器視窗。";
    }
};

REGISTER_COMMAND("profileview", ProfileViewCommand);

} // namespace command
} // namespace aicad
