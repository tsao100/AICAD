/**
 * @file HAlignTableWidget.h
 * @brief 支援「點位間」欄位下移半格繪製、以及選取/清除/剪下/複製/貼上的
 *        平面線形資料表格元件。
 *
 * 抽出自 AlignmentDataTableDialog.cpp 原本的匿名 namespace 內部類別
 * （HAlignTableWidget／BetweenPointDelegate），改為可重用的公開元件，供
 * AlignmentQuickTableDialog 等其他「平面線形資料表」共用同一份實作，
 * 確保視覺與操作行為一致。
 *
 * ⚠️ AlignmentDataTableDialog.cpp 本身仍保留它自己的（匿名 namespace、
 * internal linkage）舊版拷貝，並未改用這份共用元件——避免修改既有、已上線
 * 測試過的檔案而引入回歸風險。兩份實作內容相同但完全獨立，不會有 ODR 衝突
 * （不同 namespace／不同 TU）。日後若要收斂成單一份，可以再讓
 * AlignmentDataTableDialog.cpp 改用這裡的版本。
 *
 * 「點位間」欄位下移半格的原理，見 HAlignTableWidget::paintEvent() 的實作
 * 註解（原始版本的完整說明）。
 *
 * 剪貼簿支援（Ctrl+C/X/V、Delete）：以選取範圍的外接矩形為準，複製/貼上
 * TSV（Tab 分隔）文字；貼上與清除一律略過不可編輯（唯讀／計算結果）的
 * 儲存格，因此可以安全地用在混合「可編輯輸入欄」與「唯讀計算欄」的表格上。
 */
#pragma once

#include <QTableWidget>
#include <QStyledItemDelegate>
#include <QModelIndex>
#include <QPoint>

class QKeyEvent;
class QPaintEvent;

namespace aicad {
namespace ui {

class HAlignTableWidget : public QTableWidget
{
    Q_OBJECT

public:
    explicit HAlignTableWidget(QWidget* parent = nullptr);

    /**
     * @brief 設定「點位間」欄位（代表本點→下一點之間的線元資訊，例如
     *        length／radius／curveType）的起始欄索引；此欄（含）以後的內容
     *        整體下移半列高繪製。傳入 < 0 或 >= columnCount() 表示「不使用
     *        下移繪製」，整張表退化為一般格線。
     */
    void setFirstBetweenColumn(int col);

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

    /**
     * @brief 「點位間」欄位的內容視覺上下移半列高（見 paintEvent()），但
     *        Qt 的選取／點擊/拖曳選取一律透過 indexAt() 把像素位置換算成
     *        model index，且是以「未位移」的格線幾何計算——若不修正，滑鼠
     *        點在視覺上看到的文字上，實際選到的卻是下一列的儲存格，導致
     *        選取／清除／剪下／複製／貼上作用在錯誤的列。
     *
     *        修正方式：對「點位間」欄位，落在某格「上半部」的點擊視覺上
     *        顯示的其實是「上一列」下移後溢出的內容，因此換算成上一列的
     *        index；落在「下半部」則已經是本列自己下移後的內容，維持不變。
     *        因為 QAbstractItemView 的點擊、雙擊、拖曳範圍選取全部經由
     *        indexAt() 做座標→index 的轉換，這裡覆寫一處即可讓上述所有
     *        互動方式（含 Ctrl+C/X/V、Delete 所依賴的 selectedRanges()／
     *        selectedItems()）都自動得到正確、與視覺一致的結果。
     */
    QModelIndex indexAt(const QPoint& pos) const override;

private:
    void copySelectionToClipboard() const;
    void clearSelectedCellsContent();
    void pasteClipboardAtSelection();

    int m_firstBetweenCol = -1;
};

/**
 * @brief 「點位間」欄位的委派：本身不繪製任何內容（實際顯示由
 *        HAlignTableWidget::paintEvent() 手動處理，見該類別註解），但雙擊
 *        編輯時把編輯器位置一併下移半列高，與視覺顯示對齊。
 */
class BetweenPointDelegate : public QStyledItemDelegate
{
public:
    explicit BetweenPointDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const override;
};

} // namespace ui
} // namespace aicad
