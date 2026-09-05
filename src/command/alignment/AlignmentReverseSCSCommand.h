#pragma once

/**
 * @file AlignmentReverseSCSCommand.h
 * @brief ALIGNMENTREVSCS（alias: RSCS）— 在兩條切線間插入反向 SCS+SCS
 *        曲線（左彎接右彎或右彎接左彎）。
 *
 * 結構（見 AlignmentDocument.h ReverseSCSSpec、AlignmentSolver.h
 * solveReverseSCS() 的完整推導與設計動機說明）：
 *
 *   Tangent(entry) → SpiralIn(L1) → CircularArc(R1)
 *                  → [反向對 Lm1/Lm2，EqualLength 自動反解，過零交會]
 *                  → CircularArc(R2) → SpiralOut(L2) → Tangent(exit)
 *
 *  R1/L1/R2/L2 皆為使用者輸入（比照既有 SCS 指令的 R=/L1=/L2= 提示）；
 *  只有中間反向對的 Lm1/Lm2 交給 solveReverseSCS() 自動反解。兩弧轉向
 *  （左彎/右彎）由兩切線的相對幾何自動判斷，不需使用者額外選擇。
 *
 * 互動流程（狀態機）：
 * ─────────────────────────────────────────────────────────────────────
 *  PickEntryTangent    — 點擊選取入切線；高亮顯示
 *       ↓ POINT_ACQUIRED
 *  PickExitTangent     — 點擊選取出切線；高亮顯示
 *       ↓ POINT_ACQUIRED
 *  WaitingForRadius1   — 提示 "R1=<數字>"
 *       ↓ NUMBER_INPUT
 *  WaitingForL1        — 提示 "L1=<數字>"（0 = 無入螺旋）
 *       ↓ NUMBER_INPUT
 *  WaitingForType1     — 提示入螺旋類型（Enter = Clothoid，L1=0 時跳過）
 *       ↓ NUMBER_INPUT
 *  WaitingForRadius2   — 提示 "R2=<數字>"
 *       ↓ NUMBER_INPUT
 *  WaitingForL2        — 提示 "L2=<數字>"（0 = 無出螺旋）
 *       ↓ NUMBER_INPUT
 *  WaitingForType2     — 提示出螺旋類型（Enter = Clothoid，L2=0 時跳過）
 *       ↓ NUMBER_INPUT
 *  WaitingForMidType   — 提示中間反向對類型（Enter = Clothoid，套用於
 *                        Lm1、Lm2 兩段——EqualLength 策略下兩段等長，
 *                        沿用同一種類型是合理預設）
 *       ↓ NUMBER_INPUT
 *  WaitingForConfirm   — 顯示參數摘要；等待 Enter（空輸入）或 POINT_ACQUIRED
 *       ↓ 確認
 *  → addReverseSCS(spec) → solve() → refresh()
 *
 *  無即時橡皮筋預覽（rubberBandMode="none"，比照 SCSChAIN 指令）：
 *  中間反向對的實際幾何要到 solve() 才會算出，命令執行期間無法即時畫出
 *  正確預覽，確認前只顯示文字參數摘要。
 *
 *  選好出切線後，優先跳出 AlignmentReverseSCSCalcDialog（試算對話框，
 *  可重複調整參數看節點座標預覽再套用；比照 AlignmentSCSChainCommand
 *  對 CompoundChainCalcDialog 的整合方式）；使用者取消/關閉對話框則
 *  退回本檔案原本的文字循序輸入流程（WaitingForRadius1 起）。
 *
 * @see AlignmentCommandBase
 * @see AlignmentSCSCommand（螺旋類型輸入語法完全相同，直接複用
 *      parseSpiralType()/spiralTypeName() 的邏輯）
 * @see AlignmentReverseSCSCalcDialog（試算對話框，主要互動路徑）
 * @see HorizontalAlignmentEdit::addReverseSCS(const ReverseSCSSpec&)
 * @see AlignmentFloatCurveCommand::nearestTangentIndex()（複用）
 */

#include "command/alignment/AlignmentCommandBase.h"
#include "command/alignment/AlignmentFloatCurveCommand.h"   // nearestTangentIndex
#include "command/CommandFactory.h"
#include "railway/AlignmentDocument.h"   // SpiralType
#include <QPointF>
#include <QWidget>

namespace aicad {
namespace command {

class AlignmentReverseSCSCommand : public AlignmentCommandBase
{
    Q_OBJECT

public:
    explicit AlignmentReverseSCSCommand(QObject* parent = nullptr);
    ~AlignmentReverseSCSCommand() override = default;

    CommandResult execute(const CommandContext& context) override;
    bool          isInteractive() const override { return true; }
    QString       getUsage()      const override;

private:
    enum class Step {
        PickEntryTangent,
        PickExitTangent,
        WaitingForRadius1,
        WaitingForL1,
        WaitingForType1,
        WaitingForRadius2,
        WaitingForL2,
        WaitingForType2,
        WaitingForMidType,
        WaitingForConfirm
    };

    void handlePointAcquired(const QPointF& point);
    void handleNumberInput(const QString& text);
    void handleCancelled();

    /** 開啟 AlignmentReverseSCSCalcDialog；取消/關閉則退回文字循序輸入。 */
    void openCalcDialog();

    void commitRSCS();
    void cleanup() override;

    /** 高亮或清除指定切線元素（idx=-1 清除全部）。 */
    void highlightTangent(int elemIdx);

    /** 依當前所有參數組出 WaitingForConfirm 的提示文字。 */
    QString confirmPrompt() const;

    static bool    parseSpiralType(const QString& text, railway::SpiralType& out);
    static QString spiralTypeName(railway::SpiralType t);

    // ── 狀態 ─────────────────────────────────────────────────────────────────
    railway::AlignmentDocument* m_alignDoc    = nullptr;
    QWidget*                    m_parentWidget = nullptr;  ///< AlignmentReverseSCSCalcDialog 的父視窗（來自 CommandContext::cadView）
    Step                        m_step        = Step::PickEntryTangent;
    bool                        m_isFinishing = false;

    int    m_idx1    = -1;     ///< 入切線 EditableElement index
    int    m_idx2    = -1;     ///< 出切線 EditableElement index
    double m_radius1 = 0.0;    ///< R1
    double m_L1      = 0.0;    ///< 入螺旋長度
    double m_radius2 = 0.0;    ///< R2
    double m_L2      = 0.0;    ///< 出螺旋長度

    railway::SpiralType m_type1  = railway::SpiralType::Clothoid;  ///< 入螺旋（L1）類型
    railway::SpiralType m_type2  = railway::SpiralType::Clothoid;  ///< 出螺旋（L2）類型
    railway::SpiralType m_typeM  = railway::SpiralType::Clothoid;  ///< 中間反向對（Lm1/Lm2）類型
};

REGISTER_COMMAND("alignmentrevscs", AlignmentReverseSCSCommand);

} // namespace command
} // namespace aicad
