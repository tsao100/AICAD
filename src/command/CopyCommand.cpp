/**
 * @file CopyCommand.cpp
 * @brief 見 CopyCommand.h 檔頭說明。
 */
#include "CopyCommand.h"

#include "../core/CommandLineManager.h"
#include "../cad/Sketch.h"

namespace aicad {
namespace command {

using namespace cad;

CopyCommand::CopyCommand()
    : SketchTransformCommandBase("COPY", "Copy and move selected sketch geometry (alias: CO)")
{
}

QString CopyCommand::getUsage() const
{
    return "Usage: COPY — select geometry first then run COPY, "
           "or run COPY then click objects and press Enter, "
           "then specify base point and second point.";
}

QString CopyCommand::selectPrompt() const
{
    return "[COPY] Select objects, then press Enter:";
}

QString CopyCommand::basePointPrompt() const
{
    return "[COPY] Specify base point:";
}

QString CopyCommand::secondPointPrompt() const
{
    return "[COPY] Specify second point (displacement target):";
}

void CopyCommand::commit(cad::Sketch* sketch, const QStringList& selection,
                         const cad::transform::Transform2D& xf)
{
    const QStringList created = transform::cloneAndTransform(sketch, selection, xf);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        if (!created.isEmpty())
            cmdMgr->printSuccess(QString("✅ Copied %1 object(s).").arg(created.size()));
        else
            cmdMgr->printWarning("⚠️  Nothing was copied.");
    }
}

} // namespace command
} // namespace aicad
