/**
 * @file LineCommand.h
 * @brief 連續繪製直線命令（折線模式）
 *
 * 繼承 PointInputCommand，支援連續點擊產生多段折線，
 * 直到使用者按下 RMB 或 Space 鍵才結束。
 *
 * 互動流程：
 *   1. execute()         → 訂閱點事件，提示「Specify first point:」
 *   2. 第 1 個點         → onPointAdded()：記錄起點，更新橡皮筋錨點
 *   3. 第 2…N 個點       → onPointAdded()：建立前一點到本點的線段，
 *                          更新橡皮筋錨點至本點，繼續等待下一點
 *   4. RMB / Space       → onCommandFinishing(UserFinished)：輸出完成統計
 *   5. ESC               → onCommandFinishing(UserCancelled)：輸出取消訊息
 *
 * 非互動模式（context.args >= 4）：直接以座標建立單段直線，不進入互動流程。
 */

#ifndef AICAD_COMMAND_LINECOMMAND_H
#define AICAD_COMMAND_LINECOMMAND_H

#include "command/PointInputCommand.h"
#include "command/CommandFactory.h"

namespace aicad {
namespace command {

class LineCommand : public PointInputCommand {
    Q_OBJECT

public:
    explicit LineCommand(QObject* parent = nullptr);
    ~LineCommand() override;

    // ---------------------------------------------------------------
    // Command 介面
    // ---------------------------------------------------------------

    /**
     * @brief 執行命令
     *
     * - 有座標參數（args >= 4）：非互動模式，直接建立單段直線
     * - 無參數：互動模式，進入無限點輸入流程（RMB/Space 結束）
     */
    CommandResult execute(const CommandContext& context) override;

    /** @return true，此為互動式命令 */
    bool isInteractive() const override { return true; }

    QString getUsage() const override;

    /**
     * @brief 清理資源
     *
     * 退訂所有 EventBus 事件，通知視圖清除橡皮筋。
     */
    void cleanup() override;

protected:
    // ---------------------------------------------------------------
    // PointInputCommand 純虛函數實作
    // ---------------------------------------------------------------

    /**
     * @brief 每新增一個點時呼叫
     *
     * - 第 1 點（points.size() == 1）：設定橡皮筋錨點，輸出下一步提示
     * - 第 2…N 點（points.size() >= 2）：建立前一點 → 本點的線段，
     *   將橡皮筋錨點移至本點
     */
    void onPointAdded(const QVector<QVector2D>& points) override;

    /**
     * @brief 無限模式下不會被呼叫，保留空實作
     *
     * 若日後有需要定點數的子類，可 override 此函數。
     */
    void onFinished(const QVector<QVector2D>& points) override;

    /**
     * @brief 回傳第 index 個點的命令列提示
     * @param index 0 = 起點，1+ = 後續端點
     */
    QString promptForNextPoint(int index) const override;

    /**
     * @brief 命令即將結束時呼叫
     * @param reason UserFinished（RMB/Space）或 UserCancelled（ESC）
     *
     * 輸出已建立線段數的統計，並視 reason 顯示對應訊息。
     */
    void onCommandFinishing(FinishReason reason) override;
};

REGISTER_COMMAND("line", LineCommand);

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_LINECOMMAND_H
