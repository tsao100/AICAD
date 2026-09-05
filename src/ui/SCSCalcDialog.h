/**
 * @file SCSCalcDialog.h
 * @brief 「SCS（Spiral-Circular-Spiral）試算」對話框 — 單一圓弧版本
 *        （TS S1 SC C1 CS S2 ST）。
 *
 * 對應 CompoundChainCalcDialog 之於 SCSCHAIN 指令：本對話框是
 * AlignmentSCSCommand（SCS 指令，N==1 固定圓弧數）的 GUI 化版本。與
 * CompoundChainCalcDialog 的差異：
 *   - 圓弧數固定為 1，不需要「圓弧數 N」欄位／表格式輸入；R / L1 / L2
 *     以個別欄位（QDoubleSpinBox）呈現。
 *   - 入螺旋與出螺旋可各自選擇不同的螺線形式（T1 / T2），而非
 *     CompoundChainCalcDialog 的「全段共用同一種類型」。
 *   - 沒有 Phase 4 未知數反解選項（該功能仍只保留在 AlignmentSCSCommand
 *     的文字循序輸入模式；本對話框與 CompoundChainCalcDialog 一樣是
 *     封閉解限定：R / L1 / L2 都需要直接給定）。
 *
 * 使用方式：
 *   1. 由 AlignmentSCSCommand 在使用者於畫面上點選入/出切線之後建構
 *      本對話框（見下方「三參數」建構子），入/出切線下拉選單會預先選定
 *      並鎖定；仍保留無鎖定的建構子供獨立測試或未來其他呼叫端使用。
 *   2. 輸入圓弧半徑 R；入螺旋長度 L1（0=無入螺旋）與其類型 T1；出螺旋
 *      長度 L2（0=無出螺旋）與其類型 T2。
 *   3. 按「試算」：呼叫 AlignmentSolver::solveSCS() 並顯示 TS/SC/CS/ST
 *      四個節點的座標、方位角、下一段長度；若失敗顯示原因。座標顯示採
 *      ProjectOrigin::toGlobal() 換算後的 TM2 全域座標（非 Local CAD
 *      座標，見 ProjectOrigin.h 的架構原則）；方位角採 azimuthToDMS()
 *      的 ddd°mm'ss.sss" 格式（與 AlignmentDataTableDialog.cpp 一致）。
 *   4. 試算成功後按「套用」：呼叫 addSCS() 寫入線形並 solve()、關閉
 *      對話框。
 *
 * @author AICAD Team
 * @see CompoundChainCalcDialog （複合鏈結版本，N≥2）
 * @see aicad::command::AlignmentSCSCommand
 *
 * @note 參數記憶：對話框會記住「上一次成功套用」的 R／L1／T1／L2／T2
 *       （以及未鎖定建構子情境下的入/出切線），下次開啟時自動回填成
 *       預設值，方便連續繪製相似的 SCS 曲線。此設定透過 QSettings 寫入
 *       磁碟（等同 VBA GetSetting()/SaveSetting() 的效果：Windows 登錄檔
 *       HKCU\Software\AICAD\AICAD，或 Linux 上的
 *       ~/.config/AICAD/AICAD.conf），重開 AICAD 之後仍會保留，且與任何
 *       .aicad 檔案無關（不隨檔案存檔／載入，所有 .aicad 檔案共用同一份
 *       設定）。僅於「套用」成功時寫入；試算失敗或使用者取消對話框不會
 *       覆蓋既有設定。
 */
#pragma once

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QTableWidget;
class QPushButton;
class QLabel;

namespace aicad {
namespace railway {
class AlignmentDocument;
enum class SpiralType;
} // namespace railway

namespace ui {

class SCSCalcDialog : public QDialog
{
    Q_OBJECT

public:
    /** 舊介面：入/出切線由使用者在對話框內的下拉選單自行選擇。 */
    explicit SCSCalcDialog(railway::AlignmentDocument* doc,
                           QWidget* parent = nullptr);

    /**
     * @brief 由 AlignmentSCSCommand 使用：使用者已在畫面上點選入/出
     *        切線，直接以 EditableElement index 預先鎖定下拉選單。
     * @param presetEntryIdx 入切線 index（elements() 中的 Tangent index）。
     * @param presetExitIdx  出切線 index（必須與 presetEntryIdx 不同）。
     */
    SCSCalcDialog(railway::AlignmentDocument* doc,
                 int presetEntryIdx, int presetExitIdx,
                 QWidget* parent = nullptr);

    ~SCSCalcDialog() override = default;

Q_SIGNALS:
    /** 套用成功後發出，供外部（UIManager）整體刷新畫面。 */
    void scsApplied();

private Q_SLOTS:
    void onCalculate();
    void onApply();

private:
    void init();   ///< 兩個建構子共用的初始化（UI 建構、訊號連接）。
    void populateTangentCombos();
    void lockTangentCombos(int entryIdx, int exitIdx);

    /** 目前 T1 / T2 下拉選單所選的螺線類型。 */
    railway::SpiralType selectedType1() const;
    railway::SpiralType selectedType2() const;

    railway::AlignmentDocument* m_doc = nullptr;

    QComboBox*      m_entryTangentCombo = nullptr;
    QComboBox*      m_exitTangentCombo  = nullptr;
    QDoubleSpinBox*  m_radiusSpin        = nullptr;   ///< R
    QDoubleSpinBox*  m_l1Spin            = nullptr;   ///< L1（入螺旋，0=省略）
    QComboBox*      m_type1Combo        = nullptr;   ///< 入螺旋形式
    QDoubleSpinBox*  m_l2Spin            = nullptr;   ///< L2（出螺旋，0=省略）
    QComboBox*      m_type2Combo        = nullptr;   ///< 出螺旋形式

    QTableWidget*    m_resultTable       = nullptr;   ///< 試算結果：TS/SC/CS/ST 節點序列
    QLabel*          m_statusLabel       = nullptr;
    QPushButton*     m_calcButton        = nullptr;
    QPushButton*     m_applyButton       = nullptr;

    bool m_lastCalcValid  = false;   ///< 上次「試算」是否成功（決定「套用」是否可按）
    bool m_tangentsLocked = false;   ///< true = 建構時已預選入/出切線，下拉選單鎖定不可改
};

} // namespace ui
} // namespace aicad
