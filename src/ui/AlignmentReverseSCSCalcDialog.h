/**
 * @file AlignmentReverseSCSCalcDialog.h
 * @brief 「反向 SCS+SCS 曲線試算」對話框（ALIGNMENTREVSCS / RSCS）。
 *
 * 與 CompoundChainCalcDialog 同一套模式（見該檔頭註解）：輸入完整參數後、
 * 正式寫入線形前，先呼叫 AlignmentSolver::solveReverseSCS() 產生節點序列
 * 預覽，供使用者確認後「套用」——不是「試算未知數」（RSCS 沒有可勾選的
 * 未知數：R1/L1/R2/L2 皆為輸入已知量，只有中間反向對的 Lm1/Lm2 恆定交給
 * solveReverseSCS() 內部的 solveReverseSpiral(EqualLength) 自動反解，見
 * AlignmentDocument.h ReverseSCSSpec 的完整設計動機說明）。
 *
 * 使用方式：
 *   1. 由 AlignmentReverseSCSCommand 在使用者於畫面上點選入/出切線之後
 *      建構本對話框（三參數建構子），入/出切線下拉選單會預先選定並鎖定；
 *      仍保留無鎖定的建構子供獨立測試或未來其他呼叫端使用。
 *   2. 填入 R1/L1、R2/L2（半徑必填 >0；長度可為 0 = 省略該側入/出螺旋），
 *      選擇入螺旋類型 T1、出螺旋類型 T2、反向對類型 TM（三者各自獨立，
 *      預設皆為 Clothoid）。
 *   3. 按「試算」：呼叫 AlignmentSolver::solveReverseSCS()（不寫入文件）
 *      並顯示 TS/SC/CS/ST/TS/SC/CS/ST 節點序列的座標、方位角、下一段
 *      長度、半徑；失敗顯示原因（多半是兩弧圓心距離超出反向對可搭接的
 *      範圍 —— 半徑或切線間距不合理時常見）。
 *   4. 試算成功後按「套用」：呼叫 addReverseSCS() 寫入線形並 solve()、
 *      關閉對話框。
 *
 * 記住上次輸入（GetSetting／SaveSetting 風格）：
 *   R1/L1/T1/R2/L2/T2/TM 這 7 個欄位在每次按「試算」時就會寫入 QSettings
 *   （不論試算成功或失敗都存——即使這次沒解出反向曲線，下次打開對話框
 *   還是要帶回剛剛打的值，不然使用者等於白打）；「套用」成功時會再存
 *   一次，涵蓋「算完後又手動改欄位、沒重按試算就直接套用」的情況。
 *   QSettings 用 ("AICAD","AICAD")，比照 ExportAlignmentCommand.cpp／
 *   BasicCommands.cpp 既有慣例——Windows 落地在登錄機碼
 *   HKEY_CURRENT_USER\Software\AICAD\AICAD，等同 VBA SaveSetting() 的
 *   落地位置。下次開啟本對話框時（loadLastSettings()，見下方該函式
 *   註解）自動帶回做為預設值。入/出切線選擇「不」還原——理由見
 *   loadLastSettings() 註解。
 *
 * @see CompoundChainCalcDialog（同一套「試算→套用」模式的另一個既有範例）
 * @see AlignmentReverseSCSCommand（互動指令；對話框取消時退回文字循序輸入）
 * @see HorizontalAlignmentEdit::addReverseSCS(const ReverseSCSSpec&)
 * @see AlignmentSolver::solveReverseSCS()
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

class AlignmentReverseSCSCalcDialog : public QDialog
{
    Q_OBJECT

public:
    /** 舊介面：入/出切線由使用者在對話框內的下拉選單自行選擇。 */
    explicit AlignmentReverseSCSCalcDialog(railway::AlignmentDocument* doc,
                                           QWidget* parent = nullptr);

    /**
     * @brief 由 AlignmentReverseSCSCommand 使用：使用者已在畫面上點選
     *        入/出切線，直接以 EditableElement index 預先鎖定下拉選單。
     * @param presetEntryIdx 入切線 index（elements() 中的 Tangent index）。
     * @param presetExitIdx  出切線 index（必須與 presetEntryIdx 不同）。
     */
    AlignmentReverseSCSCalcDialog(railway::AlignmentDocument* doc,
                                  int presetEntryIdx, int presetExitIdx,
                                  QWidget* parent = nullptr);

    ~AlignmentReverseSCSCalcDialog() override = default;

Q_SIGNALS:
    /** 套用成功後發出，供外部（UIManager）整體刷新畫面。 */
    void curveApplied();

private Q_SLOTS:
    void onCalculate();
    void onApply();

private:
    void init();   ///< 兩個建構子共用的初始化（UI 建構、訊號連接）。
    void populateTangentCombos();
    void lockTangentCombos(int entryIdx, int exitIdx);
    void addSpiralTypeItems(QComboBox* combo) const;

    /**
     * @brief 以 QSettings 讀回上次成功「套用」時的 R1/L1/T1/R2/L2/T2/TM，
     *        填入對應欄位做為本次開啟的預設值——比照 VBA 的
     *        GetSetting()／SaveSetting()：Windows 上落地在
     *        HKEY_CURRENT_USER\Software\AICAD\AICAD 登錄機碼（QSettings
     *        使用 (kSettingsOrg, kSettingsApp) = ("AICAD","AICAD")，與
     *        ExportAlignmentCommand.cpp／BasicCommands.cpp 既有慣例相同），
     *        Linux/macOS 上則是 QSettings 對應平台的預設落地位置（ini／
     *        plist）。若尚無記錄（第一次使用、或找不到機碼），保留呼叫端
     *        目前已設定好的硬編碼預設值不動。
     *
     *        刻意不還原「入/出切線」選擇：切線 index 是特定文件當下的
     *        元素順序，換一個文件、甚至同一份文件經過編輯後 index 都可能
     *        對應到完全不同的切線，貿然還原容易誤套用到錯誤的切線而不
     *        自知，比找不到上次選擇更危險，因此只還原 R/L/Type 這些與
     *        「文件結構」無關的純數值/列舉參數。
     */
    void loadLastSettings();

    /**
     * @brief 寫回本次的 R1/L1/T1/R2/L2/T2/TM 供下次開啟沿用。在 onCalculate()
     *        一開始就會呼叫（不論試算成功或失敗），onApply() 成功時也會
     *        再呼叫一次（見 .cpp 兩處呼叫點的註解）。
     */
    void saveLastSettings() const;

    /** 目前 R1/L1/R2/L2/T1/T2/TM 欄位是否皆為合法值；失敗時填入原因訊息。 */
    bool readInputs(double& r1, double& l1, railway::SpiralType& t1,
                    double& r2, double& l2, railway::SpiralType& t2,
                    railway::SpiralType& tm, QString& errorOut) const;

    railway::AlignmentDocument* m_doc = nullptr;

    QComboBox*      m_entryTangentCombo = nullptr;
    QComboBox*      m_exitTangentCombo  = nullptr;
    QDoubleSpinBox* m_radius1Spin       = nullptr;
    QDoubleSpinBox* m_length1Spin       = nullptr;
    QComboBox*      m_type1Combo        = nullptr;
    QDoubleSpinBox* m_radius2Spin       = nullptr;
    QDoubleSpinBox* m_length2Spin       = nullptr;
    QComboBox*      m_type2Combo        = nullptr;
    QComboBox*      m_typeMCombo        = nullptr;   ///< 反向對（Lm1/Lm2）共用類型

    QTableWidget* m_resultTable   = nullptr;   ///< 試算結果：節點序列
    QLabel*       m_statusLabel   = nullptr;
    QPushButton*  m_calcButton    = nullptr;
    QPushButton*  m_applyButton   = nullptr;

    bool m_lastCalcValid  = false;   ///< 上次「試算」是否成功（決定「套用」是否可按）
    bool m_tangentsLocked = false;   ///< true = 建構時已預選入/出切線，下拉選單鎖定不可改
};

} // namespace ui
} // namespace aicad
