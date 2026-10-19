/**
 * @file ProfileArraySegmentTableDialog.h
 * @brief 沿線斷面陣列的「分段資料表」— 可編輯
 *
 * 每次輸入（PROFILEARRAY 一次）= 表格的一列（一段）。固定欄位：
 *   段名稱 | 起點里程 | 終點里程 | 使用的 profile | Thick | w | 間距
 * 另可「新增欄位」：欄名 = profile 草圖內的參數名（例如 H、cant、d9），
 * 該欄有填值的段就以該值覆寫該參數；空白 = 不覆寫。
 *
 * 編輯功能：新增欄位、新增列、刪除列、清除（儲存格內容／整張表）、複製、貼上。
 *
 * 編輯是在「工作副本」（表格本身）上進行，按「套用」才會寫回陣列並重建
 * （放樣成本高，不在每次按鍵就重建）；「還原」會丟棄尚未套用的修改。
 */
#pragma once

#include <QDialog>
#include <QPointer>

class QTableWidget;
class QLabel;

namespace aicad {
namespace cad { class AlignedProfileArray; struct ProfileArraySegment; }
namespace ui {

class ProfileArraySegmentTableDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProfileArraySegmentTableDialog(cad::AlignedProfileArray* arr,
                                            QWidget* parent = nullptr);
    ~ProfileArraySegmentTableDialog() override = default;

private:
    // 資料 ⇄ 表格
    void loadFromArray();
    bool readSegments(QList<cad::ProfileArraySegment>& segs,
                      QStringList& extraColumns, QString& error) const;
    void apply();

    // 編輯動作
    void addRow();
    void deleteRows();
    void addColumn();
    void clearCells();
    void clearAll();
    void copySelection();
    void paste();
    void showStations();

    // 輔助
    int     appendDefaultRow(int baseRow = -1);   ///< 預設值沿用 baseRow（-1 = 最後一列）
    int     addExtraColumn(const QString& name);
    QString uniqueName(const QString& wanted, int ignoreRow) const;
    void    updateInfo();
    QList<QPair<QString, QString>> profiles() const;   ///< (名稱, feature id)

    QPointer<cad::AlignedProfileArray> m_array;
    QTableWidget* m_table = nullptr;
    QLabel*       m_info  = nullptr;
};

} // namespace ui
} // namespace aicad
