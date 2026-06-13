#pragma once

/**
 * @file AlignmentFixCurveCommand.h
 * @brief ALIGNMENTFIXCURVE (alias: FC) — 3-point arc 模式新增 Fixed CircularArc。
 *
 * 互動流程：
 *   1. 點擊起點   (WaitingForStart)
 *   2. 點擊弧上點 (WaitingForMid)
 *   3. 點擊終點   (WaitingForEnd) → 計算外接圓 → addFixedCurve() + solve()
 *
 * 圓心 / 半徑求法：
 *   以垂直平分線聯立方程式（等同 ArcCommand::calcCircleFrom3Points）：
 *     (x2-x1)·cx + (y2-y1)·cy = [(x2²-x1²)+(y2²-y1²)] / 2
 *     (x3-x1)·cx + (y3-y1)·cy = [(x3²-x1²)+(y3²-y1²)] / 2
 *   三點共線時（|det| < 1e-9）視為錯誤，等待使用者重選終點。
 *
 * @see AlignmentCommandBase
 * @see HorizontalAlignmentEdit::addFixedCurve()
 */

#include "command/alignment/AlignmentCommandBase.h"
#include "command/CommandFactory.h"
#include <QPointF>

namespace aicad {
namespace command {

class AlignmentFixCurveCommand : public AlignmentCommandBase
{
    Q_OBJECT

public:
    explicit AlignmentFixCurveCommand(QObject* parent = nullptr);
    ~AlignmentFixCurveCommand() override = default;

    CommandResult execute(const CommandContext& context) override;
    bool          isInteractive() const override { return true; }
    QString       getUsage()      const override;

private:
    // ── 互動狀態 ─────────────────────────────────────────────────────────────
    enum class PickState {
        WaitingForStart,
        WaitingForMid,
        WaitingForEnd
    };

    void handlePointAcquired(const QPointF& point);
    void handleCancelled();
    void cleanup() override;

    /**
     * @brief 由三點求外接圓心與半徑。
     *
     * outCenter 以 QPointF（double 精度）回傳，避免 QVector2D float 截斷誤差。
     * @return 成功回傳 true；三點共線時回傳 false。
     */
    static bool circumcircle(const QPointF& p1,
                             const QPointF& p2,
                             const QPointF& p3,
                             QPointF& outCenter,
                             double&  outRadius);

    // ── 狀態 ─────────────────────────────────────────────────────────────────
    railway::AlignmentDocument* m_alignDoc   = nullptr;
    PickState                   m_pickState  = PickState::WaitingForStart;
    bool                        m_isFinishing = false;

    QPointF m_startPoint;
    QPointF m_midPoint;
};

REGISTER_COMMAND("alignmentfixcurve", AlignmentFixCurveCommand);

} // namespace command
} // namespace aicad