#ifndef COMMANDLINESPLITTER_H
#define COMMANDLINESPLITTER_H

#include <QSplitter>
#include <QSplitterHandle>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QEnterEvent>
#endif

namespace aicad {
namespace ui {

class CommandLineSplitterHandle : public QSplitterHandle {
    Q_OBJECT

public:
    explicit CommandLineSplitterHandle(Qt::Orientation orientation, QSplitter* parent);

protected:
    void paintEvent(QPaintEvent* event) override;
    // ✅ Qt 5/6 兼容
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent* event) override;
#else
    void enterEvent(QEvent* event) override;
#endif
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    bool m_hovered;
    bool m_pressed;
};

class CommandLineSplitter : public QSplitter {
    Q_OBJECT

public:
    explicit CommandLineSplitter(Qt::Orientation orientation, QWidget* parent = nullptr);

    void collapseWidget(int index);
    void expandWidget(int index);
    void equalizeWidgets();

protected:
    QSplitterHandle* createHandle() override;

signals:
    void sizesAdjusted();
};

} // namespace ui
} // namespace aicad

#endif
