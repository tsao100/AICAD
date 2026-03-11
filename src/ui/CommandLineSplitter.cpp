#include "CommandLineSplitter.h"
#include <QPainter>
#include <QMouseEvent>

namespace aicad {
namespace ui {

// CommandLineSplitterHandle

CommandLineSplitterHandle::CommandLineSplitterHandle(Qt::Orientation orientation, QSplitter* parent)
    : QSplitterHandle(orientation, parent)
    , m_hovered(false)
    , m_pressed(false)
{
    setMouseTracking(true);
    setCursor(orientation == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
}

void CommandLineSplitterHandle::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor bgColor = m_pressed ? QColor(0, 122, 204) :
                         m_hovered ? QColor(63, 63, 70) :
                         QColor(45, 45, 48);

    painter.fillRect(rect(), bgColor);

    if (m_hovered || m_pressed) {
        painter.setPen(QPen(QColor(180, 180, 180), 1));

        QRect r = rect();
        int centerX = r.center().x();
        int centerY = r.center().y();

        if (orientation() == Qt::Horizontal) {
            for (int i = -4; i <= 4; i += 4) {
                painter.drawLine(centerX + i, centerY - 8,
                                 centerX + i, centerY + 8);
            }
        } else {
            for (int i = -4; i <= 4; i += 4) {
                painter.drawLine(centerX - 8, centerY + i,
                                 centerX + 8, centerY + i);
            }
        }
    }
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void CommandLineSplitterHandle::enterEvent(QEnterEvent* event) {
#else
void CommandLineSplitterHandle::enterEvent(QEvent* event) {
#endif
    m_hovered = true;
    update();
    QSplitterHandle::enterEvent(event);
}

void CommandLineSplitterHandle::leaveEvent(QEvent* event) {
    m_hovered = false;
    update();
    QSplitterHandle::leaveEvent(event);
}

void CommandLineSplitterHandle::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        update();
    }
    QSplitterHandle::mousePressEvent(event);
}

void CommandLineSplitterHandle::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_pressed = false;
        update();
    }
    QSplitterHandle::mouseReleaseEvent(event);
}

// CommandLineSplitter

CommandLineSplitter::CommandLineSplitter(Qt::Orientation orientation, QWidget* parent)
    : QSplitter(orientation, parent)
{
    setHandleWidth(4);
    setChildrenCollapsible(false);
}

QSplitterHandle* CommandLineSplitter::createHandle() {
    return new CommandLineSplitterHandle(orientation(), this);
}

void CommandLineSplitter::collapseWidget(int index) {
    if (index < 0 || index >= count()) return;

    QList<int> sizes = this->sizes();
    sizes[index] = 0;
    setSizes(sizes);
}

void CommandLineSplitter::expandWidget(int index) {
    if (index < 0 || index >= count()) return;
    equalizeWidgets();
}

void CommandLineSplitter::equalizeWidgets() {
    int totalSize = (orientation() == Qt::Horizontal) ? width() : height();
    int widgetCount = count();
    int sizePerWidget = totalSize / widgetCount;

    QList<int> sizes;
    for (int i = 0; i < widgetCount; ++i) {
        sizes.append(sizePerWidget);
    }

    setSizes(sizes);
    emit sizesAdjusted();
}

} // namespace ui
} // namespace aicad
