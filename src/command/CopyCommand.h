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
 * 選取範圍「內部」的約束（兩端都在複製範圍內）會一併複製到新幾何上；
 * 只要有一端指向複製範圍外的既有幾何，該約束就不會被複製。GDIM 標註
 * （尺寸線）目前不會一併複製（見 cloneAndTransform() 說明）。
 *
 * ✅ 在「指定第二點」等待期間即時預覽複製結果：進入該階段時會先以零
 * 位移呼叫 cloneAndTransform() 建立一份疊在原物件正上方的「預覽用
 * 複製品」（連同前述的約束複製規則），之後每一幀只把這份複製品跟著
 * 游標做輕量搬移（不重新求解，見 SketchTransformCommandBase 的說明）。
 * 原選取範圍全程不受影響。確認或取消時，這份預覽複製品都會被整個刪除
 * ——確認時由 commit() 重新呼叫一次 cloneAndTransform() 建立正式的最終
 * 複製，取消時則單純作廢，兩種情況下都不會留下重複或殘留的幾何。
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

    bool livePreviewEnabled() const override { return true; }

    QStringList armLivePreviewTargets(cad::Sketch* sketch) override;
    void teardownLivePreviewTargets(cad::Sketch* sketch, const QStringList& targets) override;
};

} // namespace command
} // namespace aicad
