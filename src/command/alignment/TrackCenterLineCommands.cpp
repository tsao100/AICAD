/**
 * @file TrackCenterLineCommands.cpp
 * @brief Implementation of TrackCenterLine CRUD commands.
 */

#include "TrackCenterLineCommands.h"

#include "core/Application.h"
#include "core/DocumentManager.h"
#include "cad/Document.h"
#include "railway/RailwayAlignment.h"

#include <QDebug>

namespace aicad {
namespace commands {

// ─────────────────────────────────────────────────────────────────────────────
// AddTrackCenterLineCommand
// ─────────────────────────────────────────────────────────────────────────────

CommandResult AddTrackCenterLineCommand::execute(const CommandContext& /*ctx*/)
{
    auto* doc = core::Application::instance()->documentManager()->currentDocument();
    if (!doc)
        return CommandResult::Failure("No active document");

    // 若命令列有引數則作為名稱
    QString name;  // 留空讓 Document 自動命名

    railway::TrackCenterLine* tcl = doc->addTrackCenterLine(name);
    m_createdId = tcl->id();

    qDebug() << "[AddTrackCenterLineCommand] Created:" << tcl->name() << tcl->id();
    return CommandResult::Success(QStringLiteral("已建立線路中心線: %1").arg(tcl->name()));
}

// ─────────────────────────────────────────────────────────────────────────────
// DeleteTrackCenterLineCommand
// ─────────────────────────────────────────────────────────────────────────────

CommandResult DeleteTrackCenterLineCommand::execute(const CommandContext& /*ctx*/)
{
    auto* doc = core::Application::instance()->documentManager()->currentDocument();
    if (!doc)
        return CommandResult::Failure("No active document");

    railway::TrackCenterLine* tcl = doc->findTrackCenterLine(m_tclId);
    if (!tcl)
        return CommandResult::Failure(QStringLiteral("找不到線路: %1").arg(m_tclId));

    // 先拍快照供 undo
    m_snapshot = tcl->toJson();

    doc->removeTrackCenterLine(m_tclId);
    qDebug() << "[DeleteTrackCenterLineCommand] Deleted:" << m_tclId;
    return CommandResult::Success("線路已刪除");
}

// ─────────────────────────────────────────────────────────────────────────────
// RenameTrackCommand
// ─────────────────────────────────────────────────────────────────────────────

CommandResult RenameTrackCommand::execute(const CommandContext& /*ctx*/)
{
    auto* doc = core::Application::instance()->documentManager()->currentDocument();
    if (!doc)
        return CommandResult::Failure("No active document");

    railway::TrackCenterLine* tcl = doc->findTrackCenterLine(m_tclId);
    if (!tcl)
        return CommandResult::Failure(QStringLiteral("找不到線路: %1").arg(m_tclId));

    m_oldName = tcl->name();
    tcl->setName(m_newName);
    doc->setModified(true);
    // TrackCenterLine::setName() already emits nameChanged; force tree refresh:
    QMetaObject::invokeMethod(doc, "treeStructureChanged", Qt::QueuedConnection);

    qDebug() << "[RenameTrackCommand]" << m_oldName << "->" << m_newName;
    return CommandResult::Success(QStringLiteral("已重新命名為: %1").arg(m_newName));
}

} // namespace commands
} // namespace aicad

// ─────────────────────────────────────────────────────────────────────────────
// Static registration — 指令 "TRACK" 觸發 AddTrackCenterLineCommand
// ─────────────────────────────────────────────────────────────────────────────
static void registerTrackCenterLineCommands()
{
    using namespace aicad::commands;
    aicad::command::CommandFactory::registerCreator("TRACK", []() -> aicad::command::Command* {
        return new AddTrackCenterLineCommand();
    });
}
Q_CONSTRUCTOR_FUNCTION(registerTrackCenterLineCommands)
