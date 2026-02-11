/**
 * @file RectCommand.h
 * @brief 繪製矩形命令
 *
 * 繼承 PointInputCommand，蒐集兩個對角點後建立軸對齊矩形（4 條直線）。
 * 使用定點數模式（m_requiredPoints = 2）。
 *
 * ⚠ 關鍵行為（勿修改前請先理解）：
 *   基類 handlePointAcquired() 對「每一個點」都會呼叫 onPointAdded()，
 *   包含最後一個觸發 onFinished() 的點。
 *   因此：
 *     - onPointAdded()  負責視覺回饋（橡皮筋、提示訊息），對 size==1 及 size==2 都必須安全
 *     - onFinished()    負責幾何建立，在 onPointAdded() 之後被呼叫，points.size() 保證 == 2
 *
 * 互動流程：
 *   1. execute()            → 視圖切草繪模式、訂閱事件、提示「Specify first corner:」
 *   2. 點擊第 1 點（起角）  → onPointAdded(size=1)：設橡皮筋錨點，提示「Specify opposite corner:」
 *   3. 點擊第 2 點（對角）  → onPointAdded(size=2)：無視覺動作
 *                            → onFinished(size=2)：建立矩形 4 條邊，命令自動結束
 *   4. RMB / Space（提前）  → onCommandFinishing(UserFinished)：點數不足，略過建立，輸出警告
 *   5. ESC                  → onCommandFinishing(UserCancelled)：取消，輸出訊息
 *
 * 非互動模式（context.args >= 4，順序 x1 y1 x2 y2）：
 *   直接呼叫 createRect()，不訂閱任何事件。
 */

#ifndef AICAD_COMMAND_RECTCOMMAND_H
#define AICAD_COMMAND_RECTCOMMAND_H

#include "command/PointInputCommand.h"
#include "command/CommandFactory.h"

namespace aicad {
namespace command {

class RectCommand : public PointInputCommand {
    Q_OBJECT

public:
    explicit RectCommand(QObject* parent = nullptr);
    ~RectCommand() override;

    // ---------------------------------------------------------------
    // Command 介面
    // ---------------------------------------------------------------

    /**
     * @brief 執行命令
     *
     * - args >= 4：非互動模式，直接以 (x1,y1)-(x2,y2) 建立矩形後返回
     * - args < 4 ：互動模式，等待兩個對角點
     */
    CommandResult execute(const CommandContext& context) override;

    /** @return true，此為互動式命令 */
    bool isInteractive() const override { return true; }

    QString getUsage() const override;

    /**
     * @brief 清理資源
     *
     * 退訂所有 EventBus 事件，清除橡皮筋。
     * 由 CommandManager 在命令結束後呼叫，不應手動呼叫。
     */
    void cleanup() override;

protected:
    // ---------------------------------------------------------------
    // PointInputCommand 純虛函數實作
    // ---------------------------------------------------------------

    /**
     * @brief 每新增一個點後呼叫（含第 2 點）
     *
     * ⚠ 此函數對 size==1 及 size==2 都必須安全，不可假設 size 一定是 1：
     *   - size == 1：設定橡皮筋錨點，輸出「Specify opposite corner:」
     *   - size == 2：不做任何事（onFinished() 隨後會處理幾何建立）
     *   - size > 2 ：防禦性 early return（正常情況不應出現）
     */
    void onPointAdded(const QVector<QVector2D>& points) override;

    /**
     * @brief 兩個對角點蒐集完畢後呼叫，建立矩形 4 條邊
     *
     * 由基類保證呼叫時 points.size() == m_requiredPoints == 2。
     * 幾何建立委託給 createRect()。
     */
    void onFinished(const QVector<QVector2D>& points) override;

    /**
     * @brief 回傳第 index 個點的提示文字
     * @param index 0 = 起角，1 = 對角，其他 = 空字串（防禦）
     */
    QString promptForNextPoint(int index) const override;

    /**
     * @brief 提前結束時的 hook（RMB/Space 或 ESC 在取得 2 點之前觸發）
     *
     * 定點數模式下，正常完成走 onFinished()，此函數僅處理「異常提前結束」：
     *   - UserFinished（RMB/Space）：點數不足，不建立幾何，輸出警告
     *   - UserCancelled（ESC）：輸出取消訊息
     */
    void onCommandFinishing(FinishReason reason) override;

private:
    /**
     * @brief 建立軸對齊矩形的 4 條邊
     *
     * 兩點的位置任意（不限定哪個是左下），函數內部以 qMin/qMax 計算正確 BBox。
     * 4 條邊依逆時針順序發布 command.create-sketch-line 事件：
     *   底邊(BL→BR) → 右邊(BR→TR) → 頂邊(TR→TL) → 左邊(TL→BL)
     *
     * 供 onFinished()（互動）與 execute()（非互動）共用，避免重複程式碼。
     *
     * @pre corner1 != corner2（零面積矩形由呼叫端自行過濾）
     */
    void createRect(const QVector2D& corner1, const QVector2D& corner2);
};

REGISTER_COMMAND("rect", RectCommand);

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_RECTCOMMAND_H
