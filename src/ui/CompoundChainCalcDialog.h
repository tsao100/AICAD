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
 *   7. 螺線形式、圓弧數 N、輸入表格內容（Lk/Rk/Dk），以及在未鎖定切線
 *      下拉選單時的入/出切線選取，會在對話框關閉時透過 QSettings 記錄，
 *      下次開啟時自動帶入（比照 VBA GetSetting/SaveSetting 的用法，見
 *      loadSettings()/saveSettings()）；鎖定切線的情況（由
 *      AlignmentSCSChainCommand 建構）不記錄切線選取，因為切線由呼叫端
 *      的畫面選取決定，不該被「上次選擇」覆蓋。
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

protected:
    /**
     * @brief 對話框關閉（無論 accept 或 reject／按右上角 X）都會經過這裡，
     *        用來把目前的選取／輸入資料寫回 QSettings，讓下次開啟時
     *        （見 loadSettings()）可以帶入這次的內容——類似 VBA
     *        SaveSetting/GetSetting 的持久化模式，但「儲存」這一半選在
     *        對話框真正關閉的時候做一次即可，不需要每次編輯儲存格都寫
     *        磁碟。
     */
    void done(int result) override;

private Q_SLOTS:
    void onArcCountChanged(int n);
    void onCalculate();
    void onApply();

private:
    void init();   ///< 兩個建構子共用的初始化（UI 建構、訊號連接）。

    /**
     * @brief 依目前圓弧數 N 重建輸入表格（N+1 列）。
     *
     * N 改變時（使用者調整 spinbox，或 loadSettings() 還原上次的 N）不會
     * 把表格內容整個洗掉重來，也不會因為 N 反覆縮小又放大就遺失中間縮小
     * 時被砍掉的那些列的資料：重建前會先把目前每一列的文字寫回
     * m_cachedLens/m_cachedRadii/m_cachedArcLens（這三個快取只增不減，
     * 縮小 N 也不會清掉裡面已經記住的列），重建表格時優先從快取依「同一
     * 列索引」取值——比目前 N 多出來的舊列資料留在快取裡，N 之後再放大、
     * 同一個列索引重新出現時可以再次取用；快取裡沒有資料的欄位，才填入
     * 0（Lk／Rk）或留白（Dk）。「Auto」（最後一段圓弧的弧長，鎖定唯讀）
     * 與「—」（row==n 那一列的 N/A 佔位符號）都不當成使用者資料寫入快取。
     */
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

    /**
     * @brief 從 QSettings 還原上一次執行時的選取／輸入資料（螺線形式、
     *        圓弧數 N、輸入表格各欄、以及在未鎖定切線下拉選單時的入/出
     *        切線選取），比照 VBA GetSetting 的用法。由兩個建構子分別在
     *        init()（以及鎖定切線建構子的 lockTangentCombos()）之後呼叫
     *        ——必須晚於 lockTangentCombos()，m_tangentsLocked 才會是
     *        正確的值，才能判斷要不要還原切線選取（見兩個建構子內的
     *        說明）。若對應鍵不存在或內容已不適用於目前文件（例如上次
     *        儲存的切線 index 在這次的下拉選單裡找不到），該項目直接
     *        跳過、維持既有預設值，不會報錯。
     */
    void loadSettings();

    /**
     * @brief 把目前的選取／輸入資料寫回 QSettings，比照 VBA SaveSetting
     *        的用法。由 done() 在對話框關閉時呼叫一次。
     */
    void saveSettings() const;

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

    /**
     * @brief 依「列索引」快取使用者曾經在該列輸入過的值，供 N 縮小又放大
     *        時取用（見 rebuildInputTable() 說明）。
     *
     * 索引就是表格的列（row）索引，跟目前 N 底下這張表格實際有沒有那麼
     * 多列無關；大小只增不減——N 縮小時，即將被砍掉的列在真的縮小之前
     * 會先寫回這裡，資料不會因為表格列數變少而消失；N 之後再放大、同一
     * 個列索引重新出現時，rebuildInputTable() 會優先從這裡取值，而不是
     * 填回預設的 0／留白。
     *
     * m_cachedRadii／m_cachedArcLens 只到「N-1」（每段圓弧一個值，比
     * m_cachedLens 少一個，因為 Lk 是 N+1 個緩和曲線、Rk/Dk 是 N 個圓弧）
     * ——這跟 m_inputTable 每一列的欄位定義一致（見 rebuildInputTable()
     * 內 `row < n` 的判斷）。
     */
    QVector<QString> m_cachedLens;
    QVector<QString> m_cachedRadii;
    QVector<QString> m_cachedArcLens;
};

} // namespace ui
} // namespace aicad
