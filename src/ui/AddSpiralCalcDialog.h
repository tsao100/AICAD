/**
 * @file AddSpiralCalcDialog.h
 * @brief 「插入未知長度緩和曲線試算」對話框 — LC / CA / ACA 群組。
 *
 * 對應 AlignmentAddSpiralCommand（AS 指令）的計算對話框支援，架構比照
 * CompoundChainCalcDialog／AlignmentSCSChainCommand 的既有模式：
 *
 *   - 由 AlignmentAddSpiralCommand 在使用者於畫面上依序點選完兩個 Fixed
 *     元素（LC: 切線＋弧；CA: 弧＋切線；ACA: 弧₁＋弧₂）、群組方向已自動
 *     偵測完成之後，Modal 開啟本對話框（見 openCalcDialog()）；已選定的
 *     元素 index 直接由建構子帶入，對話框內不重新選取元素。
 *   - 對話框內只需選擇螺旋線類型（下拉選單，涵蓋 AlignmentAddSpiralCommand
 *     ::parseSpiralType() 支援的全部類型），按「試算」呼叫
 *     AlignmentSolver::solveLC() / solveCA() / solveACA() 顯示解出的 Ls、
 *     關鍵點座標／方位角、修剪後弧長（ACA 另含等效半徑 Req）；找不到解時
 *     顯示與 AlignmentAddSpiralCommand::showSolverPreview() 相同精神的
 *     診斷訊息（D<R 或兩圓心距離不足）。
 *   - 按「套用」呼叫 HorizontalAlignmentEdit::addLC()/addCA()/addACA() 寫入
 *     線形、solve()、關閉對話框（QDialog::Accepted）。
 *   - 使用者取消／關閉對話框（QDialog::Rejected）→ 呼叫端（
 *     AlignmentAddSpiralCommand）退回原本的純文字循序輸入模式
 *     （WaitingForType → WaitingForConfirm），與 SCSCHAIN 指令對
 *     CompoundChainCalcDialog 取消時的處理方式一致。
 *
 * 與 CompoundChainCalcDialog 的差異：LC/CA/ACA 只有「螺旋線類型」一個真正
 * 的輸入自由度（Ls 本身是 solver 反解的未知數，不像複合鏈結每段長度都要
 * 使用者輸入），所以沒有輸入表格，只有一個下拉選單＋試算結果的唯讀預覽。
 *
 * 記憶上次選取（比照 VBA GetSetting/SaveSetting）
 * ──────────────────────────────────────────────
 * 對話框開啟時（restoreSettings()，見 init()）以 QSettings 讀回「上一次
 * 這個群組方向（LC/CA/ACA 各自獨立一把 key）所選的螺旋線類型」並預先選好
 * ，取代每次都固定回到 Clothoid；對話框關閉時（saveSettings()，見
 * done()——不論按「套用」或取消／關閉視窗都會存檔，語意上等同 VBA
 * SaveSetting 在表單關閉時寫回，而不是只在「成功執行」才寫回）寫回目前的
 * 選取，供下次開啟沿用。儲存位置與專案既有慣例一致：
 *   QSettings("AICAD", "AICAD")，鍵值 "AddSpiralCalcDialog/spiralType_LC"
 *   / "..._CA" / "..._ACA"（見 ImportAlignmentCommand.cpp 等既有用法）。
 *
 * @author AICAD Team
 */
#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QTableWidget;
class QPushButton;

namespace aicad {
namespace railway {
class AlignmentDocument;
enum class SpiralType;
} // namespace railway

namespace ui {

class AddSpiralCalcDialog : public QDialog
{
    Q_OBJECT

public:
    /** 群組方向，與 AlignmentAddSpiralCommand::GroupMode 一一對應（獨立
     *  定義於此，避免對話框依賴命令類別的私有巢狀 enum）。 */
    enum class GroupMode {
        LC,   ///< 直線 → Clothoid → 弧（弧的 PC 更新為 SC）
        CA,   ///< 弧 → Clothoid → 直線（弧的 PT 更新為 CS）
        ACA   ///< 弧₁ → Clothoid → 弧₂（兩弧分別於 SC₁/SC₂ 修剪）
    };

    /**
     * @param doc         目標 AlignmentDocument。
     * @param mode        群組方向（呼叫端已自動偵測完成）。
     * @param tangentIdx  Fixed Tangent 的 index；LC/CA 使用，ACA 忽略（-1）。
     * @param arcIdx      Fixed CircularArc 的 index；LC: 後方弧；CA: 前方弧；
     *                    ACA: Arc₁。
     * @param arc2Idx     ACA 群組 Arc₂ 的 index；LC/CA 忽略（-1）。
     */
    AddSpiralCalcDialog(railway::AlignmentDocument* doc, GroupMode mode,
                        int tangentIdx, int arcIdx, int arc2Idx,
                        QWidget* parent = nullptr);
    ~AddSpiralCalcDialog() override = default;

    /** 套用成功後，新建立的 SpiralIn/SpiralOut 元素 index；套用失敗或尚未
     *  套用時為 -1。僅在 exec() 回傳 QDialog::Accepted 後有意義。 */
    int resultElementIndex() const { return m_resultIdx; }

Q_SIGNALS:
    /** 套用成功後發出，供外部（UIManager）整體刷新畫面。 */
    void spiralApplied();

private Q_SLOTS:
    void onCalculate();
    void onApply();

protected:
    /** 覆寫 QDialog::done()：不論以 accept()／reject()／關閉視窗結束，都
     *  先呼叫 saveSettings() 存回目前選取，再交回基底類別處理。確保取消
     *  對話框時，剛才試過的選擇下次開啟仍會帶入（而非只有「套用」成功
     *  才記住）。 */
    void done(int result) override;

private:
    void init();
    railway::SpiralType selectedSpiralType() const;
    QString modeLabelText() const;

    /** 從 QSettings 讀回目前群組方向（m_mode）上次使用的螺旋線類型，若有
     *  找到且該類型仍在下拉選單中，設為目前選取；找不到則維持預設
     *  （Clothoid，下拉選單的第一項）。在 init() 中、connect() 訊號之前
     *  呼叫，避免觸發一次多餘的自動重算。 */
    void restoreSettings();

    /** 將目前選取的螺旋線類型寫回 QSettings（見 done()）。 */
    void saveSettings() const;

    railway::AlignmentDocument* m_doc = nullptr;
    GroupMode m_mode;
    int m_tangentIdx = -1;
    int m_arcIdx     = -1;
    int m_arc2Idx    = -1;

    QLabel*       m_modeLabel       = nullptr;   ///< 唯讀顯示群組方向與已選元素
    QComboBox*    m_spiralTypeCombo = nullptr;
    QTableWidget* m_previewTable    = nullptr;   ///< 試算結果：Key/Value 兩欄
    QLabel*       m_statusLabel     = nullptr;
    QPushButton*  m_calcButton      = nullptr;
    QPushButton*  m_applyButton     = nullptr;

    bool m_lastCalcValid = false;   ///< 上次「試算」是否成功
    int  m_resultIdx     = -1;      ///< 套用成功後的新元素 index
};

} // namespace ui
} // namespace aicad
