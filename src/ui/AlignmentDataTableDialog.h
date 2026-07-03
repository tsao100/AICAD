/**
 * @file AlignmentDataTableDialog.h
 * @brief 「線形資料表」對話框 — 平面 / 縱斷面線形資料表，支援表格內直接編輯。
 *
 * 平面線形資料表（7 欄）：
 *   點位(tsc) | Easting | Northing | Chainage | 方位角° | 長度(m) | 曲線類型/半徑
 *   可編輯：TS/CS 行的「長度」與「曲線類型」；SC/CC 行的「半徑」
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
        int  elemIdx   = -1;    ///< 對應的 EditableElement 索引；-1 表示唯讀
        bool editLen   = false; ///< 可編輯長度（SpiralIn/SpiralOut）
        bool editRad   = false; ///< 可編輯半徑（CircularArc）
        bool editType  = false; ///< 可編輯緩和曲線類型
    };

    void buildUi();
    void populateHorizontalTable();
    void populateVerticalTable();

    /// 初始化 m_doc->vertical() 的 VIP 記錄：當 VIP 為空時，
    /// 嘗試從 m_tcl->vertical()->points() 反推 PVI 資料。
    void initVerticalVipsIfEmpty();

    railway::AlignmentDocument* m_doc = nullptr;
    railway::TrackCenterLine*   m_tcl = nullptr;

    QTabWidget*    m_tabs   = nullptr;
    QTableWidget*  m_hTable = nullptr;
    QTableWidget*  m_vTable = nullptr;

    QVector<HRowMeta> m_hMeta;  ///< 與 m_hTable 列數一一對應

    bool m_populating = false;  ///< 防止 populate 時觸發 itemChanged 迴圈
};

} // namespace ui
} // namespace aicad
