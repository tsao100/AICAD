/**
 * @file LispEditorCommand.h
 * @brief LISPED（開啟 Lisp 編輯器）與 LOAD_LISP（載入並執行 Lisp 檔案）命令
 *
 * menu.txt 早就有 `load_lisp` 的選單項目與 command 宣告，但沒有任何命令實作，
 * 所以選了也沒有反應；這裡補上。REGISTER_COMMAND 放在 .cpp（避免 ODR 問題）。
 */
#pragma once

#include "Command.h"

namespace aicad {
namespace command {

/// lisped [file...] —— 顯示 Lisp 編輯器；若帶檔案路徑則一併開啟
class LispEditorCommand : public Command {
    Q_OBJECT
public:
    explicit LispEditorCommand(QObject* parent = nullptr);
    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;
};

/// load_lisp [file] —— 載入並執行 Lisp 檔案；沒有給路徑時跳出檔案對話框
class LoadLispCommand : public Command {
    Q_OBJECT
public:
    explicit LoadLispCommand(QObject* parent = nullptr);
    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;
};

} // namespace command
} // namespace aicad
