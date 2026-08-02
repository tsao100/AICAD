/**
 * @file ProfileArrayStationTableDialog.h
 * @brief 「站位資料表」對話框 — AlignedProfileArray 各測站 chainage/cant/H 唯讀表格
 *        （對應 ProfileArrayAlongAlignment_ImplementationPlan.md §2.5 Step 8）
 *
 * 比照 AlignmentDataTableDialog 的表格呈現方式，但本表全部欄位皆唯讀：
 * 純粹讓設計者在建立/調整 AlignedProfileArray 前後檢查各測站的 chainage、
 * cant、H、方位角、坡度是否符合預期，不提供編輯。
 */
#pragma once

#include <QDialog>

class QTableWidget;

namespace aicad {
namespace cad { class AlignedProfileArray; }
namespace ui {

class ProfileArrayStationTableDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProfileArrayStationTableDialog(cad::AlignedProfileArray* arr,
                                             QWidget* parent = nullptr);
    ~ProfileArrayStationTableDialog() override = default;

private:
    void populateTable();

    cad::AlignedProfileArray* m_array = nullptr;
    QTableWidget* m_table = nullptr;
};

} // namespace ui
} // namespace aicad
