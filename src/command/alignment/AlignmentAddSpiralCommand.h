#pragma once

/**
 * @file AlignmentAddSpiralCommand.h
 * @brief ALIGNMENTADDSPIRAL (alias: AS) — 在已知直線與已知弧之間插入長度未知的
 *        Clothoid（LC 群組），或在已知弧與已知直線之間插入（CA 群組）。
 *
 * 求解流程
 * ─────────
 * LC 群組（直線 → Clothoid → 弧）：
 *   1. 使用者點擊選取 Fixed Tangent（入切線）。
 *   2. 使用者點擊選取 Fixed CircularArc（目標弧，位於螺旋之後）。
 *   3. （選用）輸入螺旋線類型 T=CLOTHOID / HALFSINE / PARABOLA / CUBICJPN / CUBICECI。
 *   4. Enter 確認 → addLC(tangentIdx, arcIdx, spiralType) → solve() → refresh()。
 *      Solver 以二分法自動求解 Clothoid 長度 Ls；弧的 PC 更新為 SC 點。
 *
 * CA 群組（弧 → Clothoid → 直線）：
 *   1. 使用者點擊選取 Fixed CircularArc（目標弧，位於螺旋之前）。
 *   2. 使用者點擊選取 Fixed Tangent（出切線）。
 *   3. 其餘同 LC。
 *      Solver 求解 Ls；弧的 PT 更新為 CS 點。
 *
 * 互動狀態機
 * ──────────
 *   PickMode         — 詢問群組方向：LC（直線→弧）或 CA（弧→直線）
 *      ↓ NUMBER_INPUT "LC"/"CA"  or POINT_ACQUIRED（自動偵測）
 *   PickFirst        — 點擊第一個元素（LC: 切線；CA: 弧）
 *      ↓ POINT_ACQUIRED
 *   PickSecond       — 點擊第二個元素（LC: 弧；CA: 切線）
 *      ↓ POINT_ACQUIRED
 *   WaitingForType   — 輸入螺旋類型（Enter = Clothoid）
 *      ↓ NUMBER_INPUT（或空 Enter）
 *   WaitingForConfirm — 顯示求解預覽；Enter 確認
 *      ↓ NUMBER_INPUT ""  or POINT_ACQUIRED
 *
 * 自動偵測：PickFirst 階段若使用者點擊切線 → 切換為 LC 模式；
 *           若點擊弧 → 切換為 CA 模式（無需手動輸入 LC/CA）。
 *
 * @see AlignmentSolver::solveLC(), AlignmentSolver::solveCA()
 * @see HorizontalAlignmentEdit::addLC(), addCA()
 * @see AlignmentSCSCommand（結構參考）
 */

#include "command/alignment/AlignmentCommandBase.h"
#include "command/alignment/AlignmentFloatCurveCommand.h"  // nearestTangentIndex
#include "command/CommandFactory.h"
#include "railway/AlignmentDocument.h"
#include "railway/AlignmentSolver.h"

#include <QVector2D>

namespace aicad {
namespace command {

class AlignmentAddSpiralCommand : public AlignmentCommandBase
{
    Q_OBJECT

public:
    explicit AlignmentAddSpiralCommand(QObject* parent = nullptr);
    ~AlignmentAddSpiralCommand() override = default;

    CommandResult execute(const CommandContext& context) override;
    bool          isInteractive() const override { return true; }
    QString       getUsage()      const override;

private:
    // ── 群組方向 ──────────────────────────────────────────────────────────────
    enum class GroupMode {
        Unknown,  ///< 尚未決定；由第一次點擊自動偵測
        LC,       ///< 直線 → Clothoid → 弧
        CA,       ///< 弧 → Clothoid → 直線
        ACA       ///< 弧₁ → Clothoid → 弧₂ (兩弧均 Fixed，長度未知)
    };

    // ── 狀態機 ────────────────────────────────────────────────────────────────
    enum class Step {
        PickFirst,          ///< 點擊第一個元素（切線 or 弧，視 GroupMode）
        PickSecond,         ///< 點擊第二個元素
        WaitingForType,     ///< 等待螺旋類型輸入（Enter = Clothoid）
        WaitingForConfirm   ///< 參數就緒；等待 Enter 確認或重輸入
    };

    void handlePointAcquired(const QVector2D& point);
    void handleNumberInput(const QString& text);
    void handleCancelled();

    void commitSpiral();
    void cleanup() override;
    void goToConfirm();

    // ── 元素選取工具 ──────────────────────────────────────────────────────────

    /**
     * @brief 找出距離 clickPt 最近的 Fixed CircularArc，回傳其 index；
     *        找不到時回傳 -1。距離以點到弧弦的最短距離近似（足夠精確）。
     */
    static int nearestFixedArcIndex(
        const QVector2D&                            clickPt,
        const railway::HorizontalAlignmentEdit*     edit);

    /**
     * @brief 綜合偵測：回傳最近元素的 index 及其類型（Tangent / CircularArc）。
     *        優先回傳距離在 searchRadius 內的元素；切線與弧同時存在時選最近的。
     * @param[out] outType  EditableElementType::Tangent or CircularArc
     * @return index ≥ 0 on success, -1 if nothing within searchRadius.
     */
    static int nearestTangentOrArc(
        const QVector2D&                            clickPt,
        const railway::HorizontalAlignmentEdit*     edit,
        railway::EditableElementType&               outType);

    // ── 高亮輔助 ──────────────────────────────────────────────────────────────
    void highlightElement(int elemIdx);

    // ── Spiral type helpers ───────────────────────────────────────────────────
    static bool   parseSpiralType(const QString& text, railway::SpiralType& out);
    static QString spiralTypeName(railway::SpiralType t);

    // ── 求解預覽（輸出到命令行）──────────────────────────────────────────────
    /**
     * @brief 執行試算並輸出 Ls 結果，但不提交到 document。
     *        用於 WaitingForConfirm 階段讓使用者確認求解結果。
     */
    void showSolverPreview();

    // ── 狀態 ─────────────────────────────────────────────────────────────────
    railway::AlignmentDocument* m_alignDoc    = nullptr;
    Step                        m_step        = Step::PickFirst;
    GroupMode                   m_mode        = GroupMode::Unknown;
    bool                        m_isFinishing = false;

    int m_tangentIdx = -1;   ///< Fixed Tangent 的 index（LC/CA 使用）
    int m_arcIdx     = -1;   ///< Fixed CircularArc 的 index（LC: 後方弧；CA: 前方弧；ACA: Arc₁）
    int m_arc2Idx    = -1;   ///< ACA 群組：Arc₂ 的 index（螺旋後方的第二段弧）

    railway::SpiralType m_spiralType = railway::SpiralType::Clothoid;
};

REGISTER_COMMAND("alignmentaddspiral", AlignmentAddSpiralCommand);

} // namespace command
} // namespace aicad