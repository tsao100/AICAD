/**
 * @file DefinePlaneCommand.cpp
 */
#include "command/DefinePlaneCommand.h"

#include "core/Application.h"
#include "core/DocumentManager.h"
#include "cad/DefinedPlane.h"
#include "cad/Document.h"
#include "ui/DefinePlaneDialog.h"

#include <QApplication>

namespace aicad {
namespace command {

using core::Application;

DefinePlaneCommand::DefinePlaneCommand()
    : Command("defplane", "Define Plane")
{}

QString DefinePlaneCommand::getUsage() const
{
    return "Usage: defplane — 定義平面（基準平面 + 沿法向量偏移），可用來建立草圖";
}

CommandResult DefinePlaneCommand::execute(const CommandContext& context)
{
    cad::Document* doc = Application::instance()->documentManager()->currentDocument();
    if (!doc) return CommandResult::Failure("No active document");

    // 從 Feature Browser 的 XY/XZ/YZ 項目右鍵進來時，context.data["baseRef"] 帶有預選基準
    const QString defBase = context.data.value("baseRef", QStringLiteral("XY")).toString();

    ui::DefinePlaneDialog dlg(doc, nullptr, defBase, QApplication::activeWindow());
    if (dlg.exec() != QDialog::Accepted) return CommandResult::Failure("Define plane cancelled");

    QString err;
    cad::DefinedPlane* dp = doc->createDefinedPlane(dlg.definition(), dlg.planeName(), &err);
    if (!dp) return CommandResult::Failure(QString("定義平面失敗：%1").arg(err));
    return CommandResult::Success(QString("已定義平面「%1」（%2）").arg(dp->name(), dp->description()));
}

REGISTER_COMMAND("defplane", DefinePlaneCommand);

} // namespace command
} // namespace aicad
