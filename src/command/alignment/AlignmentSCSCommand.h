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
 *       ↓ POINT_ACQUIRED — 兩條切線都選定後，立即以 Modal 開啟
 *         SCSCalcDialog（見 openCalcDialog()），入/出切線下拉選單已預先
 *         鎖定為 m_idx1/m_idx2；使用者在對話框內輸入 R／L1／T1／L2／T2，
 *         按「試算」核對節點座標、「套用」寫入線形。對話框「套用」成功
 *         （QDialog::Accepted）時，本指令直接以 Success 結束（addSCS()/
 *         solve() 已由對話框完成）。
 *       ↓ 對話框被取消（QDialog::Rejected，例如按「取消」或直接關閉）
 *  WaitingForRadius    — 退回原本的純文字循序輸入模式；提示 "R=<數字>"
 *       或純數字；更新 RubberBand::SCS
 *       ↓ NUMBER_INPUT（解析 R=600 或 600）
 *  WaitingForL1        — 提示 "L1=<數字>" 或純數字（0 = 無入螺旋）
 *       ↓ NUMBER_INPUT（解析 L1=150 或 150 或 L=150）
 *  WaitingForType1     — 提示選擇入螺旋類型（預設 Enter = Clothoid）
 *       ↓ NUMBER_INPUT（解析 T1=CLOTHOID / HALFSINE / PARABOLA / CUBICJPN / CUBICECI）
 *  WaitingForL2        — 提示 "L2=<數字>" 或純數字（0 = 無出螺旋）
 *       ↓ NUMBER_INPUT（解析 L2=150 或 150 或 L=150）
 *  WaitingForType2     — 提示選擇出螺旋類型（預設 Enter = Clothoid）
 *       ↓ NUMBER_INPUT（解析 T2=CLOTHOID / HALFSINE / … 或空 Enter）
 *  WaitingForConfirm   — 顯示預覽；等待 Enter（空輸入）或 POINT_ACQUIRED
 *       ↓ 確認
 *  → addSCS(idx1, idx2, radius, L1, L2, type1, type2) → solve() → refresh()
 *
 * 特殊退化情況：
 *  L1=L2=0         → 等同 AFC（Floating CircularArc）
 *  L1>0, L2=0      → SC 型（只有入螺旋 + 圓弧）
 *  L1=0, L2>0      → CS 型（圓弧 + 出螺旋）
 *  L1=L2>0         → 對稱 SCS
 *  L1=0            → 跳過 WaitingForType1
 *  L2=0            → 跳過 WaitingForType2
 *
 * 螺旋類型輸入語法：
 *  T1=CLOTHOID  / T1=C    → Clothoid（預設）
 *  T1=HALFSINE  / T1=HS   → HalfSine
 *  T1=PARABOLA  / T1=P    → Parabola（三次拋物線）
 *  T1=CUBICJPN  / T1=JPN  → CubicJPN（日本 JIS）
 *  T1=CUBICECI  / T1=ECI  → CubicECI（CECI）
 *  T1=SINUSOIDAL / T1=SIN → Sinusoidal
 *  T1=COSINE     / T1=COS → Cosine
 *  T1=BLOSS      / T1=BL  → Bloss
 *  T1=LEMNISCATE / T1=LEM → Lemniscate
 *  T1=WIENERBOGEN/ T1=WB  → WienerBogen
 *  T1=RADIOID    / T1=RAD → Radioid
 *  T1=LOGARITHMIC/ T1=LOG → Logarithmic
 *  T1=HYPERBOLIC / T1=HYP → Hyperbolic
 *  T1=POLYNOMIAL / T1=POLY→ Polynomial
 *  T1=QUINTIC    / T1=QNT → Quintic
 *  T1=BIQUADRATIC/ T1=BIQ → Biquadratic
 *  T1=SPLINE     / T1=SPL → Spline
 *  T1=BLOSSEULERHYBRID / T1=BEH → BlossEulerHybrid
 *  Enter（空輸入）         → 沿用預設（Clothoid）
 *  T2=…                   → 對應出螺旋類型（語法同上）
 *
 * @see AlignmentCommandBase
 * @see HorizontalAlignmentEdit::addSCS(int,int,double,double,double,SpiralType,SpiralType)
 * @see AlignmentFloatCurveCommand::nearestTangentIndex()（複用）
 * @see aicad::ui::SCSCalcDialog
 */

#include "command/alignment/AlignmentCommandBase.h"
#include "command/alignment/AlignmentFloatCurveCommand.h"   // nearestTangentIndex
#include "command/CommandFactory.h"
#include "railway/AlignmentDocument.h"   // SpiralType
#include <QPointF>
#include <QVector2D>

class QWidget;

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
        WaitingForType1,    ///< 等待入螺旋類型（T1=… 或 Enter 跳過預設）
        WaitingForL2,       ///< 等待 L2=<num> / L=<num> / 純數字
        WaitingForType2,    ///< 等待出螺旋類型（T2=… 或 Enter 跳過預設）
        WaitingForConfirm   ///< 全部參數就緒；等待確認（Enter / 點擊）
    };

    void handlePointAcquired(const QPointF& point);
    void handleNumberInput(const QString& text);
    void handleCancelled();

    void commitSCS();
    void cleanup() override;
    void goToConfirm();   ///< 進入 WaitingForConfirm 並發出確認提示

    /**
     * @brief 兩條切線都選定後呼叫：Modal 開啟 SCSCalcDialog（入/出切線
     *        已鎖定為 m_idx1/m_idx2）。使用者按「套用」成功
     *        （QDialog::Accepted）→ 本指令直接結束（Success）；取消/關閉
     *        （QDialog::Rejected）→ 退回文字循序輸入模式（Step::
     *        WaitingForRadius）。
     */
    void openCalcDialog();

    /** 高亮或清除指定切線元素（idx=-1 清除全部）。 */
    void highlightTangent(int elemIdx);

    /** 依當前 m_radius / m_L1 / m_L2 更新 RubberBand SCS 預覽。 */
    void updateRubberBandPreview();

    /**
     * @brief 嘗試從輸入字串解析螺旋類型。
     *
     * 接受格式：
     *   T1=CLOTHOID / T1=C / T1=HALFSINE / T1=HS / T1=PARABOLA / T1=P /
     *   T1=CUBICJPN / T1=JPN / T1=CUBICECI / T1=ECI
     *   （T1= 或 T2= 前綴皆接受；亦接受無前綴的純類型名）
     *   空字串 → out 不改變，回傳 false（呼叫方保持預設值）。
     *
     * @param text  使用者輸入的原始字串（已 trimmed）
     * @param out   解析成功時填入類型；失敗時不改變
     * @return      true = 成功解析；false = 無效或空輸入
     */
    static bool parseSpiralType(const QString& text, railway::SpiralType& out);

    /** 將 SpiralType 轉為使用者易讀的顯示名稱。 */
    static QString spiralTypeName(railway::SpiralType t);

    // ── 狀態 ─────────────────────────────────────────────────────────────────
    railway::AlignmentDocument* m_alignDoc    = nullptr;
    QWidget*                    m_parentWidget = nullptr;  ///< SCSCalcDialog 的父視窗（來自 CommandContext::cadView）
    Step                        m_step        = Step::PickEntryTangent;
    bool                        m_isFinishing = false;

    int    m_idx1    = -1;     ///< 入切線 EditableElement index
    int    m_idx2    = -1;     ///< 出切線 EditableElement index
    double m_radius  = 0.0;   ///< 圓弧半徑 [m]
    double m_L1      = 0.0;   ///< 入螺旋長度 L1 [m]
    double m_L2      = 0.0;   ///< 出螺旋長度 L2 [m]

    railway::SpiralType m_type1 = railway::SpiralType::Clothoid;  ///< 入螺旋類型
    railway::SpiralType m_type2 = railway::SpiralType::Clothoid;  ///< 出螺旋類型
};

REGISTER_COMMAND("alignmentscs", AlignmentSCSCommand);

} // namespace command
} // namespace aicad
