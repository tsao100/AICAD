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

    // ⚠️ Qt Undo Framework 慣例：QUndoStack::push() 在把命令推入堆疊的當下，
    // 一定會立刻呼叫一次 redo()（這樣「push 完＝已執行」才成立）。但本類別
    // 的操作在 push() 之前，早就已經透過正常的 Command 路徑（例如
    // ArcCommand → Sketch::addArc）真的執行過一次了，push() 只是把
    // before/after 這兩份快照記錄下來、方便之後真正的 undo/redo 而已。
    //
    // 如果讓這第一次、由 push() 觸發的 redo() 也真的去跑
    // Sketch::fromJson(m_after)，等於是「明明已經做完的事，立刻無意義地再
    // 用 JSON 快照重建一次」——而 fromJson() 的 Arc 分支剛好是全專案最脆弱
    // 的一段程式碼（靠角度差的正負號分支去猜弧該往哪一邊繞，見
    // Sketch.cpp fromJson 內 Arc case 的長篇註解），curve 取樣值與
    // SketchPoint 直接寫入值之間哪怕只有極小的浮點誤差，都可能讓那個分支
    // 判斷翻面，重建出方向錯誤（繞過去的那一段弧不對）的弧——端點/半徑/
    // 圓心都對，但不再通過原本點的中間點。
    //
    // 修法比照 Qt 官方文件建議的標準做法：用旗標讓「push() 觸發的第一次
    // redo()」直接跳過（此時 sketch 早已是 after 狀態，不需要也不應該再
    // 重建），只有之後使用者真正按下 Redo（此時 sketch 已被 undo() 帶回
    // before 狀態）時，才需要、也才會真的呼叫 fromJson(m_after)。
    bool m_skipFirstRedo = true;
};

} // namespace command
} // namespace aicad
