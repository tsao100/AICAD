#pragma once

/**
 * @file AlignmentSCSChainCommand.h
 * @brief SCSCHAIN — 在兩條切線間插入 S0 C0 S1 C1 ... Sn 複合緩和曲線鏈結
 *        （N≥2 個圓弧）。
 *
 * 對應 SCS複合線形求解昇級計畫.md Phase 7。與既有 AlignmentSCSCommand
 * （單弧 SCS）的關係：N==1 時應改用既有的 SCS 指令；本指令只接受 N≥2。
 *
 * 互動流程（狀態機）：
 * ─────────────────────────────────────────────────────────────────────
 *  PickEntryTangent   — 點擊選取入切線
 *       ↓ POINT_ACQUIRED
 *  PickExitTangent    — 點擊選取出切線
 *       ↓ POINT_ACQUIRED *  WaitingForArcCount — 提示 "N=<整數>"（或純數字），N≥2
 *       ↓ NUMBER_INPUT
 *  WaitingForChainInput — 依序詢問 L0（入螺旋，0=省略）、R1、L1（中段螺
 *       旋）、R2、L2、…、RN、LN（出螺旋，0=省略），共 2N+1 個提示。
 *       任一格可輸入 "?" 取代數值，標記為「交給 solver 反解的未知數」
 *       （Phase 4，最多 1 個——見 AlignmentSolver.h CompoundChainUnknown：
 *       只有 1 條 Δθ 方程式可用）。
 *       ↓ 每次 NUMBER_INPUT 前進一格，全部填完後：
 *          若已標記未知數 → WaitingForArcAngles；否則 → WaitingForConfirm
 *  WaitingForArcAngles — 僅在標記了未知數時進入：依序詢問 A1..AN（每段
 *       圓弧心角，度）。指定未知數後角度不能再自動平分，必須全部釘死。
 *       ↓ 全部填完 → WaitingForConfirm
 *  WaitingForConfirm  — 顯示完整規格預覽；等待 Enter（空輸入）或
 *       POINT_ACQUIRED 確認；亦接受 "Rk=…"／"Lk=…"／"Ak=…" 重新輸入第 k
 *       個值（重新輸入不能切換哪一格是未知數；要改的話請取消重新執行）
 *       ↓ 確認
 *  → addCompoundChain(idx1, idx2, spec) → solve() → refresh()
 *
 * 簡化說明（相較於計畫文件 Phase 7 的逐步精靈）：本指令採用純文字循序
 * 輸入，不驅動 RubberBand 即時預覽圖形（AlignmentSCSCommand 的
 * RubberBand::scs 模式是針對固定 3 元素設計，N 可變的鏈結需要另外擴充
 * RubberBand 才能重用，超出本次修改範圍）；確認前的文字預覽已足以核對
 * 參數，正式插入後即可在畫面上看到最終幾何。
 *
 * 螺旋類型固定使用 Clothoid（預設）；如需個別指定其他類型，插入後可在
 * AlignmentDataTableDialog 的資料表對 TS/CS 列雙擊「曲線類型」欄修改
 * （見 AlignmentSolver.cpp Pass 2b / AlignmentDataTableDialog.cpp 的
 * isSingleArcSCSHead() 相關處理）。
 *
 * @see AlignmentSCSCommand （單弧版本，N==1 時請改用該指令）
 * @see HorizontalAlignmentEdit::addCompoundChain()
 * @see AlignmentFloatCurveCommand::nearestTangentIndex()（複用）
 */

#include "command/alignment/AlignmentCommandBase.h"
#include "command/alignment/AlignmentFloatCurveCommand.h"   // nearestTangentIndex
#include "command/CommandFactory.h"
#include "railway/AlignmentDocument.h"   // CompoundChainSpec, SpiralType
#include <QPointF>
#include <QVector>

namespace aicad {
namespace command {

class AlignmentSCSChainCommand : public AlignmentCommandBase
{
    Q_OBJECT

public:
    explicit AlignmentSCSChainCommand(QObject* parent = nullptr);
    ~AlignmentSCSChainCommand() override = default;

    CommandResult execute(const CommandContext& context) override;
    bool          isInteractive() const override { return true; }
    QString       getUsage()      const override;

private:
    enum class Step {
        PickEntryTangent,     ///< 等待使用者點選入切線
        PickExitTangent,      ///< 等待使用者點選出切線
        WaitingForArcCount,    ///< 等待 N=<整數>（或純數字），N≥2
        WaitingForChainInput,  ///< 依序詢問 L0/R1/L1/R2/.../RN/LN；任一格可輸入 "?" 標記為未知數
        WaitingForArcAngles,   ///< 若標記了未知數：依序詢問 A1..AN（弧心角，度）——見
                               ///< AlignmentSolver.h CompoundChainUnknown：指定未知數時
                               ///< 每段圓弧心角都必須釘死，不可再用自動平分
        WaitingForConfirm      ///< 全部參數就緒；等待確認
    };

    /** WaitingForChainInput 內部子階段：目前正在等待哪一種數值。 */
    enum class ChainField { SpiralLength, ArcRadius };

    void handlePointAcquired(const QPointF& point);
    void handleNumberInput(const QString& text);
    void handleCancelled();

    void commitChain();
    void cleanup() override;
    void goToConfirm();

    /** 高亮或清除指定切線元素（idx=-1 清除全部）。 */
    void highlightTangent(int elemIdx);

    /** 依目前 m_lens/m_radii 進度，發出下一個提示訊息。 */
    void promptNextChainField();

    /**
     * @brief L0..LN 全部填完後呼叫：若已標記未知數（m_hasUnknown），進入
     *        WaitingForArcAngles 依序詢問 A1..AN；否則直接 goToConfirm()。
     */
    void startArcAnglesOrConfirm();

    /**
     * @brief 嘗試解析 WaitingForConfirm 階段的重新輸入語法（Rk=.../Lk=...）。
     * @return true = 已處理（重新進入對應輸入階段）；false = 非重新輸入語法。
     */
    bool tryParseReentry(const QString& text);

    // ── 狀態 ─────────────────────────────────────────────────────────────────
    railway::AlignmentDocument* m_alignDoc    = nullptr;
    Step                        m_step        = Step::PickEntryTangent;
    bool                        m_isFinishing = false;

    int m_idx1 = -1;   ///< 入切線 EditableElement index
    int m_idx2 = -1;   ///< 出切線 EditableElement index
    int m_arcCount = 0; ///< N（圓弧數，≥2）

    QVector<double> m_lens;    ///< size = N+1，逐步填入（L0..LN）
    QVector<double> m_radii;   ///< size = N，逐步填入（R1..RN）

    int        m_fillIdx = 0;                          ///< 目前填到第幾個（L 或 R 的自身索引）
    ChainField m_fillField = ChainField::SpiralLength;  ///< 目前等待的欄位種類

    // ── Phase 4：讓使用者把「恰好一個」Rk/Lk 標記為未知數（輸入 "?"）───────
    //  見 AlignmentSolver.h CompoundChainUnknown 的說明：只有 1 條 Δθ
    //  方程式可用，指定未知數時每段圓弧心角都必須釘死（不可再自動平分）。
    bool m_hasUnknown      = false; ///< 是否已標記一個未知數
    bool m_unknownIsSpiral = false; ///< true = 未知數是某段緩和曲線長度；false = 某段圓弧半徑
    int  m_unknownIndex    = -1;    ///< 緩和曲線 index（0..N）或圓弧 index（0..N-1）

    QVector<double> m_arcAnglesDeg; ///< size = N，WaitingForArcAngles 階段依序填入（度）；
                                     ///< 只在 m_hasUnknown==true 時使用
    int m_angleFillIdx = 0;         ///< WaitingForArcAngles 目前填到第幾個弧（0..N-1）
};

} // namespace command
} // namespace aicad
