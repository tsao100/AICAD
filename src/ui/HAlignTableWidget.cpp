/**
 * @file HAlignTableWidget.cpp
 * @brief Implementation of HAlignTableWidget / BetweenPointDelegate.
 *        See the header for provenance and design notes.
 */
#include "HAlignTableWidget.h"

#include <QPainter>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <algorithm>
#include <limits>

namespace aicad {
namespace ui {

// ============================================================================
//  HAlignTableWidget
// ============================================================================

HAlignTableWidget::HAlignTableWidget(QWidget* parent)
    : QTableWidget(parent)
{
    setShowGrid(false);   // 格線改由 paintEvent() 手動繪製
}

void HAlignTableWidget::setFirstBetweenColumn(int col)
{
    m_firstBetweenCol = col;
    viewport()->update();
}

QModelIndex HAlignTableWidget::indexAt(const QPoint& pos) const
{
    const QModelIndex idx = QTableWidget::indexAt(pos);
    if (!idx.isValid())
        return idx;

    const int cols = columnCount();
    const bool isBetweenCol = (m_firstBetweenCol >= 0 && m_firstBetweenCol < cols
                                && idx.column() >= m_firstBetweenCol);
    if (!isBetweenCol)
        return idx;

    const QRect cellRect = visualRect(idx);
    const bool inUpperHalf = pos.y() < cellRect.center().y();
    if (inUpperHalf && idx.row() > 0)
        return model()->index(idx.row() - 1, idx.column());
    return idx;
}

/*
 * Qt 的標準逐格繪製流程（QAbstractItemView）會將每個 delegate 的繪製動作
 * 裁切（clip）在該格「原始（未位移）」的矩形範圍內，即使 delegate 本身把
 * QStyleOptionViewItem::rect 位移半列高，超出原格範圍的下半部仍會被外層
 * 裁切掉——這正是「文字只顯示上半部」的成因。
 *
 * 因此「點位間」欄位（m_firstBetweenCol 以後）改為：
 *   1. BetweenPointDelegate 完全不繪製內容（僅保留可編輯性／選取狀態邏輯），
 *      避免在錯誤（未位移）位置留下殘影。
 *   2. 本函式在基底繪製完成後，直接以 viewport() 的 QPainter 手動畫出這些
 *      欄位的背景與文字，並整體下移半列高——因為是在 paintEvent 中直接畫、
 *      不經過 QAbstractItemView 的逐格裁切機制，文字可以完整跨越列邊界
 *      顯示，不會被裁掉下半部。
 *   3. 格線同樣手動繪製：一般欄位（點位屬性）對齊正常列邊界；點位間欄位
 *      對齊下移半列高的位置，恰好銜接相鄰列，形成「錯位半格」的視覺效果；
 *      最後一列無下一點，故不繪製、也不延伸至該列。
 */
void HAlignTableWidget::paintEvent(QPaintEvent* event)
{
    QTableWidget::paintEvent(event);

    const int rows = rowCount();
    const int cols = columnCount();
    if (rows == 0 || cols == 0)
        return;

    const int firstBetween = (m_firstBetweenCol >= 0 && m_firstBetweenCol < cols)
                                  ? m_firstBetweenCol
                                  : cols;   // < 0 或超界 → 視為「沒有點位間欄位」

    QPainter painter(viewport());

    // ── 手動繪製「點位間」欄位的背景 + 文字（下移半列高）───────────────
    for (int row = 0; row < rows; ++row) {
        for (int c = firstBetween; c < cols; ++c) {
            QTableWidgetItem* it = item(row, c);
            if (!it) continue;

            QRect r = visualItemRect(it);
            r.translate(0, r.height() / 2);

            painter.fillRect(r, it->background());
            if (!it->text().isEmpty()) {
                painter.setPen(Qt::black);
                painter.setFont(it->font());
                painter.drawText(r.adjusted(4, 0, -4, 0),
                                  Qt::AlignLeft | Qt::AlignVCenter, it->text());
            }
        }
    }

    // ── 格線 ─────────────────────────────────────────────────────────
    painter.setPen(QPen(QColor(215, 215, 215)));

    const int leftX    = columnViewportPosition(0);
    const int betweenX = columnViewportPosition(qMin(firstBetween, cols - 1));
    const int rightX   = columnViewportPosition(cols - 1) + columnWidth(cols - 1);

    // ── 垂直欄分隔線（貫穿全表高度）──────────────────────────────────
    int x = leftX;
    for (int c = 0; c < cols; ++c) {
        painter.drawLine(x, rowViewportPosition(0), x, rowViewportPosition(rows - 1) + rowHeight(rows - 1));
        x += columnWidth(c);
    }
    painter.drawLine(x, rowViewportPosition(0), x, rowViewportPosition(rows - 1) + rowHeight(rows - 1));

    // ── 一般欄位（點位本身屬性）：正常列邊界 ─────────────────────────
    for (int row = 0; row <= rows; ++row) {
        const int y = (row < rows) ? rowViewportPosition(row)
                                    : rowViewportPosition(rows - 1) + rowHeight(rows - 1);
        painter.drawLine(leftX, y, betweenX, y);
    }

    // ── 點位間欄位：下移半列高的格線（首尾各少畫半格，最後一列不延伸）──
    if (firstBetween < cols) {
        for (int row = 0; row < rows; ++row) {
            const int y = rowViewportPosition(row) + rowHeight(row) / 2;
            painter.drawLine(betweenX, y, rightX, y);
        }
    }
}

// ============================================================================
//  Clipboard support
// ============================================================================

void HAlignTableWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Copy)) {
        copySelectionToClipboard();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Cut)) {
        copySelectionToClipboard();
        clearSelectedCellsContent();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        pasteClipboardAtSelection();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        clearSelectedCellsContent();
        event->accept();
        return;
    }
    QTableWidget::keyPressEvent(event);
}

void HAlignTableWidget::copySelectionToClipboard() const
{
    const auto ranges = selectedRanges();
    if (ranges.isEmpty())
        return;

    int topRow = std::numeric_limits<int>::max();
    int bottomRow = -1, leftCol = std::numeric_limits<int>::max(), rightCol = -1;
    for (const auto& r : ranges) {
        topRow    = qMin(topRow, r.topRow());
        bottomRow = qMax(bottomRow, r.bottomRow());
        leftCol   = qMin(leftCol, r.leftColumn());
        rightCol  = qMax(rightCol, r.rightColumn());
    }
    if (bottomRow < 0 || rightCol < 0)
        return;

    QString text;
    for (int r = topRow; r <= bottomRow; ++r) {
        QStringList cells;
        for (int c = leftCol; c <= rightCol; ++c) {
            const QTableWidgetItem* it = item(r, c);
            cells << (it ? it->text() : QString());
        }
        text += cells.join(QLatin1Char('\t'));
        if (r < bottomRow)
            text += QLatin1Char('\n');
    }
    QGuiApplication::clipboard()->setText(text);
}

void HAlignTableWidget::clearSelectedCellsContent()
{
    const auto items = selectedItems();
    for (auto* it : items) {
        if (it && (it->flags() & Qt::ItemIsEditable))
            it->setText(QString());
    }
}

void HAlignTableWidget::pasteClipboardAtSelection()
{
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty())
        return;

    int startRow = currentRow();
    int startCol = currentColumn();
    if (startRow < 0 || startCol < 0) {
        const auto ranges = selectedRanges();
        if (ranges.isEmpty())
            return;
        startRow = ranges.first().topRow();
        startCol = ranges.first().leftColumn();
    }

    QStringList lines = text.split(QLatin1Char('\n'));
    // 去掉最後一個空行（許多來源複製時會多帶一個結尾換行）。
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    for (int dr = 0; dr < lines.size(); ++dr) {
        QString line = lines[dr];
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        const QStringList cells = line.split(QLatin1Char('\t'));
        for (int dc = 0; dc < cells.size(); ++dc) {
            const int r = startRow + dr;
            const int c = startCol + dc;
            if (r < 0 || c < 0 || r >= rowCount() || c >= columnCount())
                continue;
            QTableWidgetItem* it = item(r, c);
            if (it && (it->flags() & Qt::ItemIsEditable))
                it->setText(cells[dc]);
        }
    }
}

// ============================================================================
//  BetweenPointDelegate
// ============================================================================

BetweenPointDelegate::BetweenPointDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{}

void BetweenPointDelegate::paint(QPainter* /*painter*/, const QStyleOptionViewItem& /*option*/,
                                  const QModelIndex& /*index*/) const
{
    // 刻意不繪製任何東西：Qt 會將本函式的繪製結果裁切在「原始（未位移）」
    // 格子範圍內，若在此處畫下移後的內容，超出原格範圍的部分會被裁掉。
    // 實際可見內容改由 HAlignTableWidget::paintEvent() 直接在 viewport()
    // 上繪製，不受此裁切限制。
}

void BetweenPointDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                                                 const QModelIndex& index) const
{
    QStyleOptionViewItem opt(option);
    opt.rect.translate(0, opt.rect.height() / 2);
    QStyledItemDelegate::updateEditorGeometry(editor, opt, index);
}

} // namespace ui
} // namespace aicad
