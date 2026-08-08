/**
 * @file EraseCommand.h
 * @brief ERASE 命令 — 刪除草圖中選取的幾何元素
 */

#ifndef AICAD_COMMAND_ERASECOMMAND_H
#define AICAD_COMMAND_ERASECOMMAND_H

#include "Command.h"
#include <QStringList>
#include <QVariant>

namespace aicad {
namespace command {

/**
 * @brief ERASE 命令（別名 E）
 *
 * 兩種使用方式：
 *  - 模式 A：先在視圖中選取幾何，再輸入 ERASE（或按 Delete 鍵）
 *            → UIManager 已將選取的 UUID 填入 ctx.args，直接刪除，
 *              命令同步完成，不進入 Running 狀態。
 *  - 模式 B：直接輸入 ERASE，尚未選取任何幾何
 *            → 命令進入 Running 狀態並切換 CadView 到 GetGeom 模式
 *              （與 GeneralDimCommand / AlignmentSCSCommand 的取點/取幾何
 *              架構相同）：
 *                訂閱 GEOM_PICKED → 每次點擊一個幾何，加入/移出待刪清單
 *                （並用 AIS_InteractiveContext::AddOrRemoveSelected 顯示
 *                高亮），訂閱 STRING_INPUT → 使用者按 Enter（空輸入）時
 *                確認並刪除待刪清單中的幾何，訂閱 COMMAND_CANCELLED →
 *                使用者按 Esc 時取消整個操作。
 *              點擊空白處另外支援窗選/穿越窗選/籬選/多邊形選取（拖曳出
 *              矩形，由左至右＝窗選須完全框住，由右至左＝穿越窗選碰到
 *              即算；或輸入 F/WP/CP 切換籬選/多邊形），一次圈選多個
 *              一般幾何加入待刪清單（訂閱 SKETCH_GEOM_SELECTED，見
 *              CadView::setCommandBoxSelectEligible()）；尺寸線/約束
 *              符號不在窗選命中範圍內，仍需逐一點擊。除了按 Enter 外，
 *              按滑鼠右鍵也能結束選取並確認刪除（等同 Enter，於
 *              CadView::mousePressEvent() 統一處理，見該處註解）。
 *            完成或取消後一律呼叫 cleanup()：取消訂閱、還原 CadView 為
 *            Sketching 模式、清空待刪清單、並以 complete() 結束命令。
 *
 * 僅在 Sketching 模式（有 active sketch）下有效；草圖平面參考幾何
 * （X 軸 / Y 軸 / 原點）為固定參考，不可被刪除/選取。
 */
class EraseCommand : public Command {
    Q_OBJECT

public:
    EraseCommand();

    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;

private:
    void subscribeAll();
    void unsubscribeAll();
    void cleanup();

    void onGeomPicked(const QVariant& data);
    void onConfirm(const QVariant& data);
    void onCancelled(const QVariant& data);

    /// CadView 窗選/穿越窗選/籬選/多邊形選取完成時觸發（見 execute()
    /// 模式 B 對 CadView::setCommandBoxSelectEligible() 的說明）。
    /// 只會回報一般幾何的 UUID（尺寸線/約束符號不在 CadView 的窗選命中
    /// 範圍內，比照原本設計，仍需逐一點擊才能刪除）。
    void onBoxSelected(const QVariant& data);

    void setHighlight(const QString& uuid, bool on);
    void updatePendingPrompt();

    QStringList m_pending;   ///< 已點選、等待確認刪除的幾何 UUID
};

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_ERASECOMMAND_H
