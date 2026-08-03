/**
 * @file MoveCommand.h
 * @brief MOVE — 平移選取的草圖幾何（別名 M，已於 CommandAlias 內建立）。
 */
#pragma once

#include "SketchTransformCommandBase.h"

namespace aicad {
namespace command {

/**
 * @brief MOVE 命令
 *
 * 使用方式：
 *  - 模式 A：先在視圖中選取幾何，再輸入 MOVE → 直接進入「指定基準點」。
 *  - 模式 B：直接輸入 MOVE → 選取物件，Enter 確認 → 指定基準點 →
 *            指定第二點（位移目標）→ 完成，整個操作合併成一筆 Undo
 *            （由 UIManager 的 Sketch 通用快照式 undo/redo 自動處理，
 *            本命令不需要手動推入 SketchEditCommand）。
 *
 * 若選取範圍內含有幾何約束（例如與範圍外物件的 Coincident），移動後
 * 約束求解器會重新收斂——這是與既有 Grip 拖曳一致的參數化行為，見
 * 實作計畫 §5 的說明。
 */
class MoveCommand : public SketchTransformCommandBase {
    Q_OBJECT
public:
    MoveCommand();
    QString getUsage() const override;

protected:
    QString selectPrompt()      const override;
    QString basePointPrompt()   const override;
    QString secondPointPrompt() const override;

    void commit(cad::Sketch* sketch, const QStringList& selection,
               const cad::transform::Transform2D& xf) override;
};

} // namespace command
} // namespace aicad
