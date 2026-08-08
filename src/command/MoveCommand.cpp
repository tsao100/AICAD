/**
 * @file MoveCommand.cpp
 * @brief 見 MoveCommand.h 檔頭說明。
 */
#include "MoveCommand.h"

#include "../core/CommandLineManager.h"
#include "../cad/Sketch.h"

namespace aicad {
namespace command {

using namespace cad;

MoveCommand::MoveCommand()
    : SketchTransformCommandBase("MOVE", "Move selected sketch geometry (alias: M)")
{
}

QString MoveCommand::getUsage() const
{
    return "Usage: MOVE — select geometry first then run MOVE, "
           "or run MOVE then click objects (window/crossing/fence supported) "
           "and press Enter or right-click, "
           "then specify base point and second point.";
}

QString MoveCommand::selectPrompt() const
{
    return "[MOVE] Select objects, then press Enter or right-click:";
}

QString MoveCommand::basePointPrompt() const
{
    return "[MOVE] Specify base point:";
}

QString MoveCommand::secondPointPrompt() const
{
    return "[MOVE] Specify second point (displacement target):";
}

void MoveCommand::commit(cad::Sketch* sketch, const QStringList& selection,
                         const cad::transform::Transform2D& xf)
{
    transform::applyToSelection(sketch, selection, xf);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr)
        cmdMgr->printSuccess(QString("✅ Moved %1 object(s).").arg(selection.size()));
}

} // namespace command
} // namespace aicad
