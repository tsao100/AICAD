/**
 * @file CompoundChainCalcDialog.h
 * @brief 「複合緩和曲線鏈結試算」對話框 — S0 C0 S1 C1 ... Sn（N≥2 弧）。
 *
 * 對應 SCS複合線形求解昇級計畫.md Phase 7.1：提供數據輸入、試算、有解後
 * 自動套用（顯示圖形）。與計畫文件原先設想的差異：本對話框沒有「勾選
 * 未知數」選項，因為 solveCompoundChain() 是封閉解（所有緩和曲線長度都
 * 需要直接給定，見 AlignmentDocument.h CompoundChainSpec 的說明），沒有
 * 可反算的未知數；「試算」在這裡的意思是「輸入完整參數後、正式寫入線形
 * 前，先看到節點座標／里程預覽」，功能上等同 SCSCHAIN 指令
 * WaitingForConfirm 階段的文字預覽，但用表格呈現、可重複調整參數。
 *
 * 使用方式：
 *   1. 由 AlignmentSCSChainCommand 在使用者於畫面上點選入/出切線之後建構
 *      本對話框（見下方「三參數」建構子），入/出切線下拉選單會預先選定
 *      並鎖定（不可更改，避免與畫面上已選取的切線不一致）；仍保留無鎖定
 *      的建構子供獨立測試或未來其他呼叫端使用，此時下拉選單可自由選擇
 *      （沿用舊行為）。
 *   2. 設定圓弧數 N（≥2），表格列數會自動調整為 N+1 段緩和曲線 × N 段圓弧。
 *   3. 選擇螺線形式（全部緩和曲線段共用同一種類型；插入後仍可在
 *      AlignmentDataTableDialog 逐段個別修改）。
 *   4. 填入 L0..LN（緩和曲線長度，0=省略）、R1..RN（圓弧半徑），以及前
 *      N-1 段圓弧的弧長；最後一段圓弧的弧長不需輸入（欄位鎖定顯示
 *      「Auto」），由 solveCompoundChain() 依「總轉角 − 其餘各段轉角」自動
 *      算出（見 AlignmentSolver.h CompoundChainUnknown 上方 Δθ 方程式的
 *      說明：只要恰好留一段圓弧心角不釘死，該段就會精確吸收剩餘轉角，
 *      不需要額外的反解機制）。
 *   5. 按「試算」：呼叫 AlignmentSolver::solveCompoundChain() 並顯示每個
 *      節點（TS/SC/CS/.../ST）的座標、方位角、里程；若失敗顯示原因。
 *   6. 試算成功後按「套用」：呼叫 addCompoundChain() 寫入線形並 solve()、
 *      關閉對話框。
 *
 * @author AICAD Team
 */
#pragma once

#include <QDialog>
#include <QVector>

class QComboBox;
class QSpinBox;
class QTableWidget;
class QPushButton;
class QLabel;

namespace aicad {
namespace railway {
class AlignmentDocument;
enum class SpiralType;
} // namespace railway

namespace ui {

class CompoundChainCalcDialog : public QDialog
{
    Q_OBJECT

public:
    /** 舊介面：入/出切線由使用者在對話框內的下拉選單自行選擇。 */
    explicit CompoundChainCalcDialog(railway::AlignmentDocument* doc,
                                      QWidget* parent = nullptr);

    /**
     * @brief 由 AlignmentSCSChainCommand 使用：使用者已在畫面上點選入/出
     *        切線，直接以 EditableElement index 預先鎖定下拉選單。
     * @param presetEntryIdx 入切線 index（elements() 中的 Tangent index）。
     * @param presetExitIdx  出切線 index（必須與 presetEntryIdx 不同）。
     */
    CompoundChainCalcDialog(railway::AlignmentDocument* doc,
                            int presetEntryIdx, int presetExitIdx,
                            QWidget* parent = nullptr);

    ~CompoundChainCalcDialog() override = default;

Q_SIGNALS:
    /** 套用成功後發出，供外部（UIManager）整體刷新畫面。 */
    void chainApplied();

private Q_SLOTS:
    void onArcCountChanged(int n);
    void onCalculate();
    void onApply();

private:
    void init();   ///< 兩個建構子共用的初始化（UI 建構、訊號連接）。
    void rebuildInputTable();
    void populateTangentCombos();
    void lockTangentCombos(int entryIdx, int exitIdx);

    /**
     * @brief 讀取目前輸入表格內容。
     * @param outRadii     各段圓弧半徑，size = N。
     * @param outLens      各段緩和曲線長度，size = N+1。
     * @param outArcAngles 各段圓弧心角絕對值 [rad]，size = N；由使用者輸入
     *                     的弧長（前 N-1 段）換算而得（angle = length /
     *                     radius）；最後一段固定填 0.0，代表「交給
     *                     solveCompoundChain() 用剩餘轉角自動算出」，並非
     *                     使用者輸入值。
     */
    bool readInputs(QVector<double>& outRadii, QVector<double>& outLens,
                    QVector<double>& outArcAngles) const;

    /** 目前「螺線形式」下拉選單所選的類型（套用到全部 N+1 段緩和曲線）。 */
    railway::SpiralType selectedSpiralType() const;

    railway::AlignmentDocument* m_doc = nullptr;

    QComboBox*    m_entryTangentCombo = nullptr;
    QComboBox*    m_exitTangentCombo  = nullptr;
    QComboBox*    m_spiralTypeCombo   = nullptr;   ///< 螺線形式（全段共用）
    QSpinBox*     m_arcCountSpin      = nullptr;
    QTableWidget* m_inputTable        = nullptr;   ///< N+1 行：Lk / Rk / 弧長（最後一行只有 Lk；
                                                    ///< 最後一段圓弧弧長欄鎖定顯示 Auto）
    QTableWidget* m_resultTable       = nullptr;   ///< 試算結果：節點序列
    QLabel*       m_statusLabel       = nullptr;
    QPushButton*  m_calcButton        = nullptr;
    QPushButton*  m_applyButton       = nullptr;

    bool m_lastCalcValid  = false;   ///< 上次「試算」是否成功（決定「套用」是否可按）
    bool m_tangentsLocked = false;   ///< true = 建構時已預選入/出切線，下拉選單鎖定不可改
};

} // namespace ui
} // namespace aicad
