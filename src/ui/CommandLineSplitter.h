#ifndef COMMANDLINESPLITTER_H
#define COMMANDLINESPLITTER_H

#include <QSplitter>
#include <QSplitterHandle>

namespace aicad {
namespace ui {

class CommandLineSplitterHandle : public QSplitterHandle {
    Q_OBJECT

public:
    explicit CommandLineSplitterHandle(Qt::Orientation orientation, QSplitter* parent);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEvent* event) override;
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
