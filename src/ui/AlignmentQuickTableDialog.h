/**
 * @file AlignmentQuickTableDialog.h
 * @brief 「快速輸入／編輯線形資料表」對話框 — ALIGNMENTQUICKTABLE（alias: AQT）用。
 *
 * 與既有的 AlignmentDataTableDialog（編輯一條「已存在、已求解」的
 * TrackCenterLine 之 EditableElement 鏈）不同，本對話框直接在「稠密關鍵點
 * 序列」（railway::AlignmentPoint，與 tcl->horizontal()->rawPoints() 同一種
 * 資料模型）的層級上工作：使用者比照 Excel 表格習慣，只輸入每個關鍵點的
 * 點位代碼（TSC）、長度、半徑／緩和曲線類型等少量必要資料，其餘的
 * Easting/Northing/Chainage/Azimuth 一律由「計算」按鈕正算補齊（見
 * railway::computeQuickAlignmentTable()，移植自使用者提供的 Excel VBA
 * CommandButton5_Click）。
 *
 * 座標單位／座標系統
 * ──────────────────
 * 表格內顯示與輸入的 Easting/Northing 一律是 **TM2（Global）座標**，不是
 * OCCT/CAD 內部使用的 Local 座標——比照 core::geometry::ProjectOrigin.h
 * 的既定架構原則：「TM2 大數值只存在於輸入框／顯示文字／檔案 I/O」。
 * 由於正算過程全程只用到座標差值與角度（平移不變），在 TM2 座標系底下
 * 直接計算與在 Local 座標系底下計算結果完全等價，因此本對話框刻意全程停留
 * 在 TM2 座標，讓使用者看到、輸入的都是熟悉的 TM2 數字；TM2 → Local 的轉換
 * 只在跨越到 TrackCenterLine／OCCT 那一側的邊界（AlignmentQuickTableCommand
 * 呼叫 tcl->loadHorizontal() 之前）才發生一次，比照 ImportAldCommand 的既定
 * 慣例（core::geometry::ProjectOrigin::ensureDefault() + toLocal()/toGlobal()）。
 *
 * 表格欄位配置比照 AlignmentDataTableDialog 的 16 欄平面線形資料表
 * （點位｜Easting｜Northing｜Chainage｜ContChainage｜Azimuth｜length｜
 *  曲線類型/半徑｜圓曲線編號｜超高｜軌距加寬｜速限｜備註一｜備註二｜
 *  數值一｜數值二），並重用同一份 HAlignTableWidget（「點位間」欄位下移
 *  半格繪製）與 azimuthToDMS() 顯示格式，讓兩個對話框視覺與資料語意一致。
 * 差異：AlignmentDataTableDialog 的「曲線類型/半徑」欄唯讀（資料來自既有
 * 元素鏈)；本對話框中該欄可自由編輯——輸入可解析為數字者視為圓弧半徑，
 * 否則視為緩和曲線類型 token（大小寫不拘，未知 token 視為 "SPIRAL"）。
 * 除第一列（起點）外，所有列的所有輸入欄位皆可自由編輯（本對話框的用途
 * 正是要讓使用者從零鍵入或修改這些資料）。
 *
 * 兩種使用模式
 * ────────────
 *   1. 「新增」模式（預設建構子）：表格從一個空白的兩列骨架開始。
 *   2. 「編輯既有線形」模式（另一個建構子，傳入既有的關鍵點序列與名稱）：
 *      表格以既有資料預先填好（Easting/Northing 已由呼叫端轉成 TM2），
 *      名稱欄鎖定唯讀（避免與 tclId 對應關係混淆，另開指令處理更名）。
 *
 * 表格支援標準 Excel 風格的選取／清除／剪下／複製/貼上（Ctrl+C/X/V、
 * Delete），見 HAlignTableWidget。
 *
 * 使用流程
 * ────────
 *   1.（編輯模式）表格已預先填好既有資料；（新增模式）於起點列輸入
 *      Easting/Northing/Chainage/ContChainage/Azimuth（十進位度）。
 *   2. 逐列輸入／修改 TSC 代碼／長度／半徑或緩和曲線類型／輔助欄位。
 *   3. 按「計算」：呼叫 computeQuickAlignmentTable() 正算補齊其餘各列，
 *      並將結果（唯讀）顯示於 Easting/Northing/Chainage/ContChainage/
 *      Azimuth 欄。計算失敗時於狀態列顯示錯誤列號與原因，不關閉對話框。
 *   4. 按「確定」：僅在最近一次「計算」成功且之後未再編輯輸入欄位時可用，
 *      將結果（TM2 座標）透過 resultPoints() 交給呼叫端
 *      （AlignmentQuickTableCommand）建立或更新 TrackCenterLine。
 *
 * @see railway::computeQuickAlignmentTable()
 * @see AlignmentDataTableDialog（後續於資料表中檢視／編輯本對話框產生的
 *      TrackCenterLine 時使用的既有對話框；本對話框不直接呼叫它）
 * @see HAlignTableWidget
 * @author AICAD Team
 */
#pragma once

#include <QDialog>
#include <QVector>

#include "railway/RailwayAlignment.h"   // AlignmentPoint

class QTableWidgetItem;
class QLabel;
class QLineEdit;
class QPushButton;

namespace aicad {
namespace ui {

class HAlignTableWidget;

class AlignmentQuickTableDialog : public QDialog
{
    Q_OBJECT

public:
    /** 「新增」模式：表格從空白兩列骨架開始。 */
    explicit AlignmentQuickTableDialog(QWidget* parent = nullptr);

    /**
     * @brief 「編輯既有線形」模式：表格以既有關鍵點序列預先填好。
     * @param existingPointsTM2  既有關鍵點序列，Easting/Northing 須已是
     *                            TM2（Global）座標（呼叫端負責用
     *                            ProjectOrigin::toGlobal() 轉換過）。
     * @param existingName       該 TrackCenterLine 現有名稱，僅供顯示
     *                            （名稱欄鎖定唯讀）。
     */
    AlignmentQuickTableDialog(const QVector<railway::AlignmentPoint>& existingPointsTM2,
                               const QString& existingName,
                               QWidget* parent = nullptr);

    ~AlignmentQuickTableDialog() override = default;

    /** 使用者輸入的新 TrackCenterLine 名稱（新增模式下可能為空）。 */
    QString trackName() const;

    /**
     * @brief 最近一次「計算」成功後的完整關鍵點序列（TM2 座標）。
     * 僅在 exec()/accept() 回傳 Accepted 之後有效。
     */
    const QVector<railway::AlignmentPoint>& resultPoints() const { return m_computed; }

private Q_SLOTS:
    void onAddRow();
    void onRemoveRow();
    void onCalculate();
    void onCellChanged(QTableWidgetItem* item);
    void onAccept();

private:
    void buildUi(bool editMode);
    void appendRow(const QString& tsc = QStringLiteral("TT"), bool isFirst = false);
    void loadFromPoints(const QVector<railway::AlignmentPoint>& pts);

    /** 由表格目前的輸入欄位（不含計算結果欄）組出待正算的 AlignmentPoint 序列。 */
    QVector<railway::AlignmentPoint> collectInputPoints(QString* errorOut) const;

    /** 將 computeQuickAlignmentTable() 的結果寫回表格的唯讀計算結果欄。 */
    void showComputedResults(const QVector<railway::AlignmentPoint>& pts);

    QLineEdit*           m_nameEdit    = nullptr;
    HAlignTableWidget*   m_table       = nullptr;
    QLabel*              m_statusLabel = nullptr;
    QPushButton*         m_okButton    = nullptr;

    QVector<railway::AlignmentPoint> m_computed;  ///< 最近一次成功計算的結果（TM2）
    bool m_resultValid = false;  ///< m_computed 是否仍與目前表格輸入一致（尚未被編輯打髒）
    bool m_populating  = false;  ///< 防止程式化寫入儲存格觸發 onCellChanged
};

} // namespace ui
} // namespace aicad
