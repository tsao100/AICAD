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
 *   1. 從下拉選單選擇入/出邊界切線（Fixed Tangent）。
 *   2. 設定圓弧數 N（≥2），表格列數會自動調整為 N+1 段緩和曲線 × N 段圓弧。
 *   3. 填入 L0..LN（緩和曲線長度，0=省略）與 R1..RN（圓弧半徑）。
 *   4. 按「試算」：呼叫 AlignmentSolver::solveCompoundChain() 並顯示每個
 *      節點（TS/SC/CS/.../ST）的座標、方位角、里程；若失敗顯示原因。
 *   5. 試算成功後按「套用」：呼叫 addCompoundChain() 寫入線形並 solve()、
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
} // namespace railway

namespace ui {

class CompoundChainCalcDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CompoundChainCalcDialog(railway::AlignmentDocument* doc,
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
    void rebuildInputTable();
    void populateTangentCombos();

    /** 讀取目前輸入表格內容，成功時填入 outRadii/outLens。 */
    bool readInputs(QVector<double>& outRadii, QVector<double>& outLens) const;

    railway::AlignmentDocument* m_doc = nullptr;

    QComboBox*    m_entryTangentCombo = nullptr;
    QComboBox*    m_exitTangentCombo  = nullptr;
    QSpinBox*     m_arcCountSpin      = nullptr;
    QTableWidget* m_inputTable        = nullptr;   ///< N+1 行：Lk / Rk（最後一行只有 Lk）
    QTableWidget* m_resultTable       = nullptr;   ///< 試算結果：節點序列
    QLabel*       m_statusLabel       = nullptr;
    QPushButton*  m_calcButton        = nullptr;
    QPushButton*  m_applyButton       = nullptr;

    bool m_lastCalcValid = false;   ///< 上次「試算」是否成功（決定「套用」是否可按）
};

} // namespace ui
} // namespace aicad
