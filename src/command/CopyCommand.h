/**
 * @file CopyCommand.h
 * @brief COPY — 複製並平移選取的草圖幾何（別名 CO，已於 CommandAlias 內建立）。
 */
#pragma once

#include "SketchTransformCommandBase.h"

namespace aicad {
namespace command {

/**
 * @brief COPY 命令
 *
 * 互動流程與 MOVE 完全相同（選取物件 → 指定基準點 → 指定第二點），差異
 *僅在收尾時呼叫 cad::transform::cloneAndTransform() 而非
 * applyToSelection()：原物件保持不動，變換套用在複製出來的新幾何上。
 *
 * 選取範圍內部共用的端點（例如矩形相鄰兩邊共用的角點）複製後仍然共用
 * 同一個新端點，不會在角落裂開（見 SketchGeomTransformUtil::
 * cloneAndTransform() 的實作說明）。
 *
 * 新複製出來的幾何不會複製原本掛在來源幾何上的 SketchConstraint／
 * SketchAnnotation（見實作計畫 §3.2 的設計理由：複製出來的幾何預設是
 * 自由的，是否重新標註/約束由使用者自行決定）。
 *
 * MVP 範圍：目前僅支援「單次複製」（一個基準點 + 一個第二點 → 一份
 * 複製）。AutoCAD 式「多重複製」（同一基準點連續點擊產生多份拷貝）留待
 * 後續視需要再擴充。
 */
class CopyCommand : public SketchTransformCommandBase {
    Q_OBJECT
public:
    CopyCommand();
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
