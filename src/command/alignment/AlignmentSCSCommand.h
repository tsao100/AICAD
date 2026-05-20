#pragma once

/**
 * @file AlignmentSCSCommand.h
 * @brief ALIGNMENTSCS（alias: SCS）— 在兩條切線間插入 SCS（Spiral-Circular-Spiral）曲線。
 *
 * 互動流程（狀態機）：
 * ─────────────────────────────────────────────────────────────────────
 *  PickEntryTangent    — 點擊選取入切線；高亮顯示
 *       ↓ POINT_ACQUIRED
 *  PickExitTangent     — 點擊選取出切線；高亮顯示
 *       ↓ POINT_ACQUIRED
 *  WaitingForRadius    — 提示 "R=<數字>"  或純數字；更新 RubberBand::SCS
 *       ↓ NUMBER_INPUT（解析 R=600 或 600）
 *  WaitingForL1        — 提示 "L1=<數字>" 或純數字（0 = 無入螺旋）
 *       ↓ NUMBER_INPUT（解析 L1=150 或 150 或 L=150）
 *  WaitingForL2        — 提示 "L2=<數字>" 或純數字（0 = 無出螺旋）
 *       ↓ NUMBER_INPUT（解析 L2=150 或 150 或 L=150）
 *  WaitingForConfirm   — 顯示預覽；等待 Enter（空輸入）或 POINT_ACQUIRED
 *       ↓ 確認
 *  → addSCS(idx1, idx2, radius, L1, L2) → solve() → refresh()
 *
 * 特殊退化情況：
 *  L1=L2=0         → 等同 AFC（Floating CircularArc）
 *  L1>0, L2=0      → SC 型（只有入螺旋 + 圓弧）
 *  L1=0, L2>0      → CS 型（圓弧 + 出螺旋）
 *  L1=L2>0         → 對稱 SCS
 *
 * 輸入解析擴充：
 *  "R=<num>"   → 半徑
 *  "L1=<num>"  → 入螺旋長度
 *  "L2=<num>"  → 出螺旋長度
 *  "L=<num>"   → 同時設定 L1 與 L2
 *  "@x,y"      → 相對座標（由 InputParser 處理，點選後 POINT_ACQUIRED 發出）
 *  "@dist<ang" → 相對極座標
 *
 * @see AlignmentCommandBase
 * @see HorizontalAlignmentEdit::addSCS(int,int,double,double,double)
 * @see AlignmentFloatCurveCommand::nearestTangentIndex()（複用）
 */

#include "command/alignment/AlignmentCommandBase.h"
#include "command/alignment/AlignmentFloatCurveCommand.h"   // nearestTangentIndex
#include "command/CommandFactory.h"
#include <QVector2D>

namespace aicad {
namespace command {

class AlignmentSCSCommand : public AlignmentCommandBase
{
    Q_OBJECT

public:
    explicit AlignmentSCSCommand(QObject* parent = nullptr);
    ~AlignmentSCSCommand() override = default;

    CommandResult execute(const CommandContext& context) override;
    bool          isInteractive() const override { return true; }
    QString       getUsage()      const override;

private:
    // ── 狀態機 ────────────────────────────────────────────────────────────────
    enum class Step {
        PickEntryTangent,   ///< 等待使用者點選入切線
        PickExitTangent,    ///< 等待使用者點選出切線
        WaitingForRadius,   ///< 等待 R=<num> 或純數字
        WaitingForL1,       ///< 等待 L1=<num> / L=<num> / 純數字
        WaitingForL2,       ///< 等待 L2=<num> / L=<num> / 純數字
        WaitingForConfirm   ///< 全部參數就緒；等待確認（Enter / 點擊）
    };

    void handlePointAcquired(const QVector2D& point);
    void handleNumberInput(const QString& text);
    void handleCancelled();

    void commitSCS();
    void cleanup() override;

    /** 高亮或清除指定切線元素（idx=-1 清除全部）。 */
    void highlightTangent(int elemIdx);

    /** 依當前 m_radius / m_L1 / m_L2 更新 RubberBand SCS 預覽。 */
    void updateRubberBandPreview();

    // ── 狀態 ─────────────────────────────────────────────────────────────────
    railway::AlignmentDocument* m_alignDoc    = nullptr;
    Step                        m_step        = Step::PickEntryTangent;
    bool                        m_isFinishing = false;

    int    m_idx1    = -1;     ///< 入切線 EditableElement index
    int    m_idx2    = -1;     ///< 出切線 EditableElement index
    double m_radius  = 0.0;   ///< 圓弧半徑 [m]
    double m_L1      = 0.0;   ///< 入螺旋長度 L1 [m]
    double m_L2      = 0.0;   ///< 出螺旋長度 L2 [m]
};

REGISTER_COMMAND("alignmentscs", AlignmentSCSCommand);

} // namespace command
} // namespace aicad
