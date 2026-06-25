/**
 * @file TrackCenterLineCommands.h
 * @brief Undo-able commands for TrackCenterLine CRUD operations.
 *
 * Commands
 * ─────────
 *   AddTrackCenterLineCommand    – create a new TCL; registered as "TRACK"
 *   DeleteTrackCenterLineCommand – delete with snapshot undo
 *   RenameTrackCommand           – rename with old/new name undo
 */
#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"

#include <QJsonObject>
#include <QString>

using namespace aicad::command;

namespace aicad {

namespace cad    { class Document; }
namespace railway{ class TrackCenterLine; }

namespace commands {

// ─────────────────────────────────────────────────────────────────────────────
// AddTrackCenterLineCommand
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 在 Document 新增一條 TrackCenterLine。
 *
 * 若 CommandContext 提供了字串引數，該引數作為線路名稱；否則使用預設名稱。
 * 指令 id: "TRACK"
 */
class AddTrackCenterLineCommand : public Command
{
public:
    AddTrackCenterLineCommand()
        : Command("TRACK", "Add Track Center Line") {}

    CommandResult execute(const CommandContext& ctx) override;

private:
    QString m_createdId;  ///< 記錄剛建立的 TCL id，供後續 undo 使用
};

// ─────────────────────────────────────────────────────────────────────────────
// DeleteTrackCenterLineCommand
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 刪除指定 id 的 TrackCenterLine，支援 undo（快照 JSON）。
 */
class DeleteTrackCenterLineCommand : public Command
{
public:
    explicit DeleteTrackCenterLineCommand(const QString& tclId)
        : Command("DELETE_TRACK", "Delete Track Center Line")
        , m_tclId(tclId) {}

    CommandResult execute(const CommandContext& ctx) override;

private:
    QString     m_tclId;
    QJsonObject m_snapshot;  ///< JSON snapshot before deletion
};

// ─────────────────────────────────────────────────────────────────────────────
// RenameTrackCommand
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 重新命名 TrackCenterLine，支援 undo（存舊名稱）。
 */
class RenameTrackCommand : public Command
{
public:
    explicit RenameTrackCommand(const QString& tclId, const QString& newName)
        : Command("RENAME_TRACK", "Rename Track")
        , m_tclId(tclId)
        , m_newName(newName) {}

    CommandResult execute(const CommandContext& ctx) override;

private:
    QString m_tclId;
    QString m_oldName;
    QString m_newName;
};

} // namespace commands
} // namespace aicad
