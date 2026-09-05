/**
 * @file AlignmentDataTableDialog.h
 * @brief 「線形資料表」對話框 — 平面 / 縱斷面線形資料表，支援表格內直接編輯。
 *
 * 平面線形資料表（16 欄）：
 *   點位 | Easting | Northing | Chainage | ContinuousChainage | Azimuth |
 *   length | RadiusCurveType | CircularCurveNo | Cant | GaugeWidenning |
 *   SpeedLimit | Text1 | Text2 | Real1 | Real2
 *   可編輯：第一列的 Chainage / ContinuousChainage（起始里程）；
 *          TS/CS 行的「長度」與「曲線類型」；SC/CC/TC 行的「半徑」；
 *          所有列（末列除外）的 CircularCurveNo/Cant/GaugeWidenning/
 *          SpeedLimit/Text1/Text2/Real1/Real2。
 *   length 欄（含）以後的資料代表「本點至下一點」之間的線元資訊，於表格中
 *   下移半格高顯示；末列因無下一點，不顯示這些欄位。
 *
 * 縱斷面線形資料表（5 欄）：
 *   # | PVI 里程 | PVI 高程 | 坡度% | Lvc(m) | K值
 *   可編輯：中間 VIP 的 PVI 里程、高程、Lvc
 *
 * @author AICAD Team
 */
#pragma once

#include <QDialog>
#include <QVector>

#include "railway/RailwayAlignment.h"  // AlignmentPoint 需為完整型別（QVector 成員/內聯解構）

class QTableWidget;
class QTableWidgetItem;
class QTabWidget;
class QPushButton;

namespace aicad {
namespace railway {
class AlignmentDocument;
class TrackCenterLine;
} // namespace railway

namespace ui {

/**
 * @brief 方位角（弧度，順時針由北）→ ddd°mm'ss.sss" 格式，不足位補零。
 *        例：方位角 123.7524° → "123°45'08.640\""。
 *
 * 定義在 AlignmentDataTableDialog.cpp，供所有需要顯示方位角的對話框
 * （AlignmentDataTableDialog 本身、CompoundChainCalcDialog…）共用同一份
 * 格式化邏輯。
 */
QString azimuthToDMS(double rad);

class AlignmentDataTableDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AlignmentDataTableDialog(railway::AlignmentDocument* doc,
                                       railway::TrackCenterLine*  tcl,
                                       QWidget* parent = nullptr);
    ~AlignmentDataTableDialog() override = default;

Q_SIGNALS:
    /** 任一儲存格編輯並成功套用後發出，供外部（UIManager）整體刷新。 */
    void dataCommitted();

private Q_SLOTS:
    void onHCellChanged(QTableWidgetItem* item);
    void onVCellChanged(QTableWidgetItem* item);
    void onHCellDoubleClicked(int row, int col);

private:
    // ── 輔助結構：水平資料表每列的可編輯性後設資料 ──────────────────────────
    struct HRowMeta {
        int  elemIdx    = -1;    ///< 對應的 EditableElement 索引（型別/半徑用）；-1 表示唯讀
        int  lenElemIdx = -1;    ///< 長度欄實際要寫入的 EditableElement 索引。
                                  ///< 多數情況與 elemIdx 相同，但 SCS 群組的 CS 列例外：
                                  ///< elemIdx 指向 SpiralIn（讀取 spiralType2 用），
                                  ///< 而出螺旋長度（L2）實際存在獨立的 SpiralOut 元素上，
                                  ///< 若誤用 elemIdx 寫入會覆蓋到 SpiralIn 的長度（L1）。
        bool editLen    = false; ///< 可編輯長度（SpiralIn/SpiralOut）
        bool editRad    = false; ///< 可編輯半徑（CircularArc）
        bool editType   = false; ///< 可編輯緩和曲線類型
        bool editStartChainage     = false; ///< 僅第一列：可編輯起始里程
        bool editStartContChainage = false; ///< 僅第一列：可編輯起始連續里程
        bool startChainageUsesSolver = false; ///< 僅第一列：true=透過 solver 偏移量套用；
                                               ///< false=尚未求解（如 ALD 直接匯入），直接平移 TCL 原始點位
        bool isLastPoint = false; ///< 是否為序列最後一點（無「下一點」，
                                   ///< length 以後的欄位代表點位間資訊，故不顯示）
    };

    void buildUi();
    void populateHorizontalTable();
    void populateVerticalTable();

    /// 初始化 m_doc->vertical() 的 VIP 記錄：當 VIP 為空時，
    /// 嘗試從 m_tcl->vertical()->points() 反推 PVI 資料。
    void initVerticalVipsIfEmpty();

    /**
     * @brief 將 m_hAux 中使用者編輯過的輔助欄位（CircularCurveNo/Cant/
     *        GaugeWidenning/SpeedLimit/Text1/Text2/Real1/Real2）合併寫回
     *        m_tcl->horizontal() 的原始點位清單（依里程比對），使其在存檔／
     *        匯出時生效。
     *
     * 背景：這些欄位並非幾何求解的一部分，solve() 每次都會以預設值（0 /
     * 空字串）重新產生 AlignmentPoint 序列；且 UIManager 在
     * HorizontalAlignmentEdit::changed() 時會用 solver 結果整批覆寫
     * m_tcl->horizontal()（參見 UIManager.cpp），因此每次幾何編輯後都需要
     * 重新套用一次本函式，才能保留使用者輸入的輔助資料。populateHorizontalTable()
     * 結尾固定呼叫本函式，確保兩者不會失步。
     *
     * @param displayPts 目前資料表顯示的（已去重複點）關鍵點序列，
     *                   與 m_hAux 一一對應。
     */
    void pushAuxToTcl(const QVector<railway::AlignmentPoint>& displayPts);

    railway::AlignmentDocument* m_doc = nullptr;
    railway::TrackCenterLine*   m_tcl = nullptr;

    QTabWidget*    m_tabs   = nullptr;
    QTableWidget*  m_hTable = nullptr;
    QTableWidget*  m_vTable = nullptr;

    QVector<HRowMeta> m_hMeta;  ///< 與 m_hTable 列數一一對應

    /// 輔助欄位快取：與目前顯示列一一對應，用來在 solver 重新產生
    /// AlignmentPoint（歸零輔助欄位）後，保留使用者先前的輸入。
    QVector<railway::AlignmentPoint> m_hAux;

    bool m_populating = false;  ///< 防止 populate 時觸發 itemChanged 迴圈
};

} // namespace ui
} // namespace aicad
