/**
 * @file SketchEditCommand.h
 * @brief Sketch 編輯的通用快照式 undo/redo 指令。
 *
 * 架構完全比照既有的 railway::AlignmentEditCommand：不逐指令手刻
 * undo/redo，而是在一次「使用者操作」（畫線、加束制、刪除幾何……）
 * 前後各序列化一次 Sketch::toJson()，undo/redo 時整個以 fromJson()
 * 回灌。這與 Sketch 早已具備完整 JSON 序列化能力（存檔/讀檔即用它）
 * 的既有架構完全一致，不需要替每個 Command 個別手刻 undo。
 *
 * 由 UIManager 掛在 CommandManager::commandStarted / commandFinished /
 * commandCancelled 訊號上自動產生（見 UIManager::initialize() 中
 * 「Sketch 通用 undo/redo」區塊），一般不需要在個別 Command 中手動使用。
 *
 * 唯一例外：grip 拖曳（GripMoveCommand）與畫線指令的自動束制邏輯不經過
 * CommandManager 的 commandStarted/commandFinished（拖曳是連續互動，
 * 不是一次性的具名 Command），因此繼續各自維持既有的 undo 機制，
 * 兩者互不衝突。
 */
#pragma once

#include <QUndoCommand>
#include <QJsonObject>
#include <QString>
#include <QPointer>

namespace aicad {
namespace cad { class Sketch; }

namespace command {

class SketchEditCommand : public QUndoCommand
{
public:
    /**
     * @brief 建立 undo/redo 命令並推入 UndoStack。
     *
     * 若找不到 UIManager 或 undoStack，命令不建立（操作仍會執行，
     * 只是無法 Undo）。呼叫端應先自行比較 before/after 是否相同，
     * 相同（no-op）就不要呼叫，避免產生空白的 undo 記錄。
     */
    static void push(cad::Sketch* sketch,
                     const QJsonObject& before,
                     const QJsonObject& after,
                     const QString& text = QString());

    void undo() override;
    void redo() override;

    SketchEditCommand(cad::Sketch* sketch,
                      const QJsonObject& before,
                      const QJsonObject& after,
                      const QString& text,
                      QUndoCommand* parent = nullptr);

private:
    QPointer<cad::Sketch> m_sketch;
    QJsonObject m_before;
    QJsonObject m_after;
};

} // namespace command
} // namespace aicad
