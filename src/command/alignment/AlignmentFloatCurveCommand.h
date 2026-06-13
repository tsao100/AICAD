#pragma once

/**
 * @file AlignmentFloatCurveCommand.h
 * @brief ALIGNMENTFLOATCURVE (alias: AFC) — 在兩條已知切線之間插入 Floating CircularArc。
 *
 * 互動流程：
 *   1. "Select first tangent"  → pickAlignmentElement() → idx1
 *   2. "Select second tangent" → pickAlignmentElement() → idx2
 *   3. "Radius:"               → getDouble (NUMBER_INPUT event)
 *   4. 設定 RubberBand::Arc 即時預覽
 *   5. Enter 確認 → addFloatingCurve(idx1, idx2, radius) → solve()
 *
 * pickAlignmentElement()
 * ──────────────────────
 *   訂閱 POINT_ACQUIRED，接收點擊後遍歷
 *   m_alignDoc->horizontal()->elements() 中 type==Tangent 的元素，
 *   計算點到線段的最短距離，回傳最近切線元素的 index。
 *
 *   此函式（以相同邏輯）供 AlignmentSCSCommand 複用：
 *     static int nearestTangentIndex(const QVector2D& clickPt,
 *                                    const railway::HorizontalAlignmentEdit* edit);
 *
 * @see AlignmentCommandBase
 * @see HorizontalAlignmentEdit::addFloatingCurve()
 */

#include "command/alignment/AlignmentCommandBase.h"
#include "command/CommandFactory.h"
#include <QPointF>

namespace aicad {
namespace command {

class AlignmentFloatCurveCommand : public AlignmentCommandBase
{
    Q_OBJECT

public:
    explicit AlignmentFloatCurveCommand(QObject* parent = nullptr);
    ~AlignmentFloatCurveCommand() override = default;

    CommandResult execute(const CommandContext& context) override;
    bool          isInteractive() const override { return true; }
    QString       getUsage()      const override;

    // ── 公開靜態工具函式（供 SCSCommand 等後續命令複用）────────────────────────
    /**
     * @brief 在 edit 的 EditableElement 列表中，找出距離 clickPt 最近的
     *        TangentElement，回傳其 index；找不到時回傳 -1。
     *
     * 距離定義：點到線段（startPI → endPI）的最短歐幾里得距離。
     */
    static int nearestTangentIndex(
        const QPointF&                              clickPt,
        const railway::HorizontalAlignmentEdit*     edit);

private:
    // ── 命令狀態機 ───────────────────────────────────────────────────────────
    enum class Step {
        PickFirstTangent,
        PickSecondTangent,
        WaitingForRadius,
        WaitingForConfirm   ///< Radius 已取得，顯示預覽，等待 Enter 或下一個點
    };

    void handlePointAcquired(const QPointF& point);
    void handleNumberInput(const QString& text);
    void handleCancelled();
    void commitCurve();
    void cleanup() override;

    /**
     * @brief 向使用者高亮顯示已選定的切線（透過 EventBus 發佈）。
     * @param elemIdx 切線元素在 elements() 中的 index（-1 = 清除高亮）
     */
    void highlightTangent(int elemIdx);

    // ── 狀態 ─────────────────────────────────────────────────────────────────
    railway::AlignmentDocument* m_alignDoc    = nullptr;
    Step                        m_step        = Step::PickFirstTangent;
    bool                        m_isFinishing = false;

    int    m_idx1   = -1;   ///< 第一條切線的 EditableElement index
    int    m_idx2   = -1;   ///< 第二條切線的 EditableElement index
    double m_radius = 0.0;  ///< 使用者輸入的半徑
};

REGISTER_COMMAND("alignmentfloatcurve", AlignmentFloatCurveCommand);

} // namespace command
} // namespace aicad
