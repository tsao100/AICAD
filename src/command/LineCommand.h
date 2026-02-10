/**
 * @file LineCommand.h
 * @brief 繪製直線命令
 *
 * 繼承 PointInputCommand，負責蒐集兩個端點後建立直線幾何。
 * 互動流程：
 *   1. execute()  → 訂閱點事件，提示「指定起點」
 *   2. onPointAdded() (index=1) → 記錄起點，更新橡皮筋，提示「指定終點」
 *   3. onFinished() (index=2)   → 透過 EventBus 建立直線
 *
 * 支援非互動模式：execute() 收到 context.args（x1 y1 x2 y2）時
 * 直接發布事件建立直線，不進入互動流程。
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
     * - 有座標參數（args >= 4）：直接透過 EventBus 建立直線並返回
     * - 無參數：進入互動模式，訂閱點事件等待使用者輸入
     */
    CommandResult execute(const CommandContext& context) override;

    /** @return true，此命令為互動式命令 */
    bool isInteractive() const override { return true; }

    QString getUsage() const override;

    /**
     * @brief 清理資源
     *
     * 退訂所有 EventBus 事件，清除橡皮筋，重置狀態。
     */
    void cleanup() override;

protected:
    // ---------------------------------------------------------------
    // PointInputCommand 純虛函數實作
    // ---------------------------------------------------------------

    /**
     * @brief 每新增一個點時呼叫
     *
     * - 第 1 點（index 0 → 1）：記錄起點，更新橡皮筋錨點，輸出提示
     * - 第 2 點以上（連續多段線模式）：建立線段，更新橡皮筋至新端點
     *
     * @note 目前 m_requiredPoints = 2，故第 2 點會觸發 onFinished()，
     *       此函數在多段線延伸時才有意義（預留給未來擴充）。
     */
    void onPointAdded(const QVector<QVector2D>& points) override;

    /**
     * @brief 點數達到 m_requiredPoints（2）時呼叫
     *
     * 發布 command.create-sketch-line 事件建立最後一段直線。
     *
     * @param points 包含起點與終點的完整序列
     */
    void onFinished(const QVector<QVector2D>& points) override;

    /**
     * @brief 回傳第 index 個點的命令列提示文字
     * @param index 下一個點的 0-based 索引（0 = 起點, 1 = 終點）
     */
    QString promptForNextPoint(int index) const override;
};

// 向 CommandFactory 自動注冊 "line" → LineCommand
REGISTER_COMMAND("line", LineCommand);

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_LINECOMMAND_H
