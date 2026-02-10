/**
 * @file PointInputCommand.h
 * @brief 需要連續點輸入的命令抽象基類
 *
 * 封裝「訂閱 EventBus 點事件 → 蒐集點 → 通知子類」的通用流程。
 * 子類只需實作三個純虛函數，不必自行管理事件訂閱/退訂。
 *
 * 典型使用方式：
 * @code
 * class LineCommand : public PointInputCommand {
 * public:
 *     LineCommand() : PointInputCommand("line", "繪製直線") {
 *         m_requiredPoints = 2;   // 需要兩個點就結束
 *     }
 * protected:
 *     void onPointAdded(const QVector<QVector2D>& pts) override { ... }
 *     void onFinished(const QVector<QVector2D>& pts)  override { ... }
 *     QString promptForNextPoint(int index) const override { ... }
 * };
 * @endcode
 */

#pragma once

#include "command/Command.h"
#include <QVector>
#include <QVector2D>

namespace aicad {
namespace command {

class PointInputCommand : public Command {
    Q_OBJECT

public:
    /**
     * @brief 建構子
     * @param name        命令名稱（對應 CommandFactory 的鍵值）
     * @param description 命令描述（顯示於 UI）
     * @param parent      Qt 父物件
     */
    explicit PointInputCommand(const QString& name,
                               const QString& description,
                               QObject* parent = nullptr);

    /** @brief 析構子，確保退訂所有 EventBus 事件 */
    ~PointInputCommand() override;

protected:
    // ---------------------------------------------------------------
    // 共用狀態（子類可直接讀取，不應直接修改）
    // ---------------------------------------------------------------

    /** 已蒐集的點序列，依輸入順序排列 */
    QVector<QVector2D> m_points;

    /**
     * 需要蒐集的點數上限。
     *  -1 = 無限制，直到使用者按 ESC 取消
     *  >0 = 蒐集滿即自動呼叫 onFinished()
     */
    int m_requiredPoints = -1;

    /** 防止重入旗標：handleCancelled / onFinished 執行後設為 true */
    bool m_isFinishing = false;

    // ---------------------------------------------------------------
    // 子類必須實作的純虛函數
    // ---------------------------------------------------------------

    /**
     * @brief 每次新增一個點後呼叫（不含最後一點到達上限時）
     * @param points 目前已蒐集的所有點（含本次新增）
     *
     * 子類可在此更新橡皮筋、輸出提示，或進行即時預覽。
     */
    virtual void onPointAdded(const QVector<QVector2D>& points) = 0;

    /**
     * @brief 點數達到 m_requiredPoints 時呼叫（無限模式不會觸發）
     * @param points 最終蒐集的所有點
     *
     * 子類應在此執行實際建立幾何的邏輯。
     * 呼叫結束後基類會自動發出 finished() 信號。
     */
    virtual void onFinished(const QVector<QVector2D>& points) = 0;

    /**
     * @brief 取得下一個點的提示文字
     * @param index 下一個點的索引（0-based），例如 0 → "指定起點"
     * @return 顯示在命令列的提示字串
     */
    virtual QString promptForNextPoint(int index) const = 0;

    // ---------------------------------------------------------------
    // 供子類在 execute() 中呼叫的輔助函數
    // ---------------------------------------------------------------

    /**
     * @brief 訂閱 POINT_ACQUIRED / POINT_CANCELLED 事件
     *
     * 子類的 execute() 初始化完成後呼叫此函數，
     * 之後點擊事件即會自動路由至 handlePointAcquired / handleCancelled。
     */
    void subscribePointEvents();

    /**
     * @brief 退訂所有 EventBus 事件（cleanup() 中自動呼叫亦可手動呼叫）
     */
    void unsubscribePointEvents();

private:
    // ---------------------------------------------------------------
    // 內部事件處理（由 subscribePointEvents 的 lambda 透過
    // QMetaObject::invokeMethod 安全地派送到主執行緒執行）
    // ---------------------------------------------------------------

    /**
     * @brief 處理 POINT_ACQUIRED 事件
     *
     * 將點加入 m_points，呼叫 onPointAdded()；
     * 若已達 m_requiredPoints，再呼叫 onFinished() 並發出 finished()。
     */
    void handlePointAcquired(QVector2D point);

    /**
     * @brief 處理 POINT_CANCELLED 事件（使用者按 ESC）
     *
     * 設定 m_isFinishing = true 並發出 finished() 信號。
     */
    void handleCancelled();
};

} // namespace command
} // namespace aicad
