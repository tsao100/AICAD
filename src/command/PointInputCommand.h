/**
 * @file PointInputCommand.h
 * @brief 需要連續點輸入的命令抽象基類
 *
 * 封裝「訂閱 EventBus 點事件 → 蒐集點 → 通知子類」的通用流程。
 * 子類只需實作三個純虛函數，不必自行管理事件訂閱 / 退訂。
 *
 * 結束方式：
 *  - m_requiredPoints > 0：蒐集滿指定點數後自動呼叫 onFinished()
 *  - m_requiredPoints = -1（預設，無限模式）：
 *      * RMB 或 Space → Events::COMMAND_FINISH  → onCommandFinishing(UserFinished)
 *      * ESC           → Events::POINT_CANCELLED → onCommandFinishing(UserCancelled)
 *
 * 典型使用方式：
 * @code
 * class LineCommand : public PointInputCommand {
 * public:
 *     LineCommand() : PointInputCommand("line", "繪製連續直線") {
 *         m_requiredPoints = -1;   // 無限模式，RMB/Space 結束
 *     }
 * protected:
 *     void onPointAdded(const QVector<QVector2D>& pts) override { ... }
 *     void onFinished(const QVector<QVector2D>& pts)  override { }
 *     QString promptForNextPoint(int index) const override { ... }
 *     void onCommandFinishing(FinishReason reason) override { ... }
 * };
 * @endcode
 */

#pragma once

#include "command/Command.h"
#include <QVector>
#include <QVector2D>

namespace aicad {
namespace command {

/**
 * @brief 命令結束的原因
 *
 * 用於區分使用者「主動完成」與「取消中止」，
 * 子類可在 onCommandFinishing() 中依此採取不同行為。
 */
enum class FinishReason {
    UserFinished,   ///< RMB 或 Space：主動完成，已建立的幾何應保留
    UserCancelled   ///< ESC：取消中止，子類可選擇撤銷已建立的幾何
};

class PointInputCommand : public Command {
    Q_OBJECT

public:
    explicit PointInputCommand(const QString& name,
                               const QString& description,
                               QObject* parent = nullptr);

    ~PointInputCommand() override;

protected:
    // ---------------------------------------------------------------
    // 共用狀態
    // ---------------------------------------------------------------

    /** 已蒐集的點序列，依輸入順序排列 */
    QVector<QVector2D> m_points;

    /**
     *  -1 = 無限制（預設），直到 RMB / Space / ESC 觸發結束
     *  >0 = 蒐集滿即自動呼叫 onFinished()
     */
    int  m_requiredPoints = -1;

    /** 防止重入旗標：進入任何結束流程後立即設為 true */
    bool m_isFinishing    = false;

    // ---------------------------------------------------------------
    // 子類必須實作的純虛函數
    // ---------------------------------------------------------------

    /**
     * @brief 每次新增一個點後呼叫
     * @param points 目前已蒐集的所有點（含本次新增）
     *
     * 無限模式下，建議在此函數中即時建立幾何（例如每兩點畫一段線），
     * 而非等到 onFinished()。
     */
    virtual void onPointAdded(const QVector<QVector2D>& points) = 0;

    /**
     * @brief 點數達到 m_requiredPoints 時呼叫（僅限定點數模式）
     *
     * 無限模式（m_requiredPoints = -1）不會呼叫此函數，
     * 子類可保留空實作 `{}`。
     */
    virtual void onFinished(const QVector<QVector2D>& points) = 0;

    /**
     * @brief 取得下一個點的提示文字
     * @param index 下一個點的 0-based 索引
     */
    virtual QString promptForNextPoint(int index) const = 0;

    /**
     * @brief 命令即將結束時的 hook（無限模式專用）
     * @param reason UserFinished（RMB/Space）或 UserCancelled（ESC）
     *
     * 預設為空實作，子類可 override 以輸出統計、發布收尾事件，
     * 或依 reason 決定是否撤銷已建立的幾何。
     *
     * ⚠ 此函數返回後，基類會立即發出 finished() 信號。
     */
    virtual void onCommandFinishing(FinishReason reason);

    // ---------------------------------------------------------------
    // 供子類在 execute() 中呼叫
    // ---------------------------------------------------------------

    /**
     * @brief 訂閱以下三個 EventBus 事件：
     *  - Events::POINT_ACQUIRED   → 每個左鍵點擊
     *  - Events::COMMAND_FINISH   → RMB 或 Space（主動完成）
     *  - Events::POINT_CANCELLED  → ESC（取消）
     *
     * 所有 callback 均透過 QueuedConnection 確保在主執行緒執行。
     */
    void subscribePointEvents();

    /** @brief 退訂所有 EventBus 事件（cleanup() 中自動呼叫） */
    void unsubscribePointEvents();

private:
    void handlePointAcquired(QVector2D point);
    void handleFinishRequested();   ///< 對應 COMMAND_FINISH（RMB / Space）
    void handleCancelled();         ///< 對應 POINT_CANCELLED（ESC）

    /** 共用的結束流程，避免重複程式碼 */
    void doFinish(FinishReason reason);
};

} // namespace command
} // namespace aicad
