/**
 * @file AlignmentEditCommand.cpp
 * @brief Step 17 — AlignmentEditCommand 實作
 */

#include "command/alignment/AlignmentEditCommand.h"
#include "railway/AlignmentDocument.h"
#include "core/Application.h"
#include "ui/UIManager.h"

#include <QUndoStack>
#include <QDebug>

namespace aicad {
namespace command {

// ── Static factory ────────────────────────────────────────────────────────────

void AlignmentEditCommand::push(railway::AlignmentDocument* doc,
                                const QJsonObject& before,
                                const QJsonObject& after,
                                const QString& text)
{
    if (!doc) return;

    // 取得 UIManager 的 UndoStack
    auto* app = core::Application::instance();
    if (!app) return;
    auto* uiMgr = app->uiManager();
    if (!uiMgr) return;
    QUndoStack* stack = uiMgr->undoStack();
    if (!stack) return;

    stack->push(new AlignmentEditCommand(doc, before, after, text));
}

// ── Constructor ───────────────────────────────────────────────────────────────

AlignmentEditCommand::AlignmentEditCommand(railway::AlignmentDocument* doc,
                                           const QJsonObject& before,
                                           const QJsonObject& after,
                                           const QString& text,
                                           int mergeId,
                                           QUndoCommand* parent)
    : QUndoCommand(text, parent)
    , m_doc(doc)
    , m_before(before)
    , m_after(after)
    , m_mergeId(mergeId)
{
}

// ── undo ──────────────────────────────────────────────────────────────────────

void AlignmentEditCommand::undo()
{
    if (!m_doc) return;
    qDebug() << "[AlignmentEditCommand] undo:" << text();
    m_doc->fromJson(m_before);
    // fromJson() 內部已呼叫 horizontal()->solve() 和 vertical()->solve()
    // 並 emit changed()，AlignmentRenderer 會自動刷新。
}

// ── redo ──────────────────────────────────────────────────────────────────────

void AlignmentEditCommand::redo()
{
    if (!m_doc) return;
    qDebug() << "[AlignmentEditCommand] redo:" << text();
    m_doc->fromJson(m_after);
}

// ── mergeWith ─────────────────────────────────────────────────────────────────
//
// 當使用者連續拖曳同一個 PI 端點時，每次 mouseMoveEvent 都會推入
// 一個 movePI 命令。合併後 Undo 一次即可還原整段拖曳，而非逐 pixel 還原。
//
bool AlignmentEditCommand::mergeWith(const QUndoCommand* other)
{
    if (m_mergeId == -1) return false;
    if (other->id() != m_mergeId) return false;

    const auto* o = static_cast<const AlignmentEditCommand*>(other);
    if (o->m_doc != m_doc) return false;

    // 保留最初的 before，採用最新的 after
    m_after = o->m_after;
    return true;
}

} // namespace command
} // namespace aicad
