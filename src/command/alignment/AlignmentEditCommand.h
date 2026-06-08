/**
 * @file AlignmentEditCommand.h
 * @brief Step 17 — Undo/Redo 整合
 *
 * AlignmentEditCommand 包裝每一次對 AlignmentDocument 的可撤銷操作。
 * 每次 mutating API 呼叫（addFixedTangent、addSCS、movePI …）在執行前後
 * 各序列化一次 JSON 快照；undo/redo 透過 fromJson() 完整還原狀態。
 *
 * 使用方式（在 AlignmentDocument 的 mutating API 中）：
 *   QJsonObject before = toJson();
 *   // …執行操作…
 *   QJsonObject after  = toJson();
 *   AlignmentEditCommand::push(this, before, after, "Add Fixed Tangent");
 */
#pragma once

#include <QUndoCommand>
#include <QJsonObject>
#include <QString>

namespace aicad {
namespace railway { class AlignmentDocument; }

namespace command {

class AlignmentEditCommand : public QUndoCommand
{
public:
    /**
     * @brief 建立 undo/redo 命令並推入 UndoStack。
     *
     * 若找不到 UIManager 或 undoStack，命令不建立（操作仍會執行，
     * 只是無法 Undo）。
     *
     * @param doc     AlignmentDocument 指標（存活期由呼叫端保證）
     * @param before  操作前的 JSON 快照
     * @param after   操作後的 JSON 快照
     * @param text    顯示在 Edit 選單的動作名稱
     */
    static void push(railway::AlignmentDocument* doc,
                     const QJsonObject& before,
                     const QJsonObject& after,
                     const QString& text = QString());

    // ── QUndoCommand interface ────────────────────────────────────────────
    void undo() override;
    void redo() override;

    /**
     * @brief 合併相鄰的 movePI 操作（連續拖曳只產生一筆 undo 記錄）。
     *
     * 只有 id 相同且 m_mergeId != -1 的連續命令才會合併。
     */
    bool mergeWith(const QUndoCommand* other) override;
    int  id() const override { return m_mergeId; }

    // ── Constructor (public for QUndoStack::push) ─────────────────────────
    AlignmentEditCommand(railway::AlignmentDocument* doc,
                         const QJsonObject& before,
                         const QJsonObject& after,
                         const QString& text,
                         int mergeId = -1,
                         QUndoCommand* parent = nullptr);

private:
    railway::AlignmentDocument* m_doc;
    QJsonObject m_before;
    QJsonObject m_after;
    int         m_mergeId = -1;  ///< -1 = 不合併
};

} // namespace command
} // namespace aicad
