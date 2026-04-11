#ifndef COMMANDHISTORYPOPUP_H
#define COMMANDHISTORYPOPUP_H

#include <QWidget>
#include <QTextEdit>
#include <QPropertyAnimation>

namespace aicad {
namespace ui {

// 按▲或 F2 從命令輸入區上緣滑出的歷程視窗
class CommandHistoryPopup : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int slideHeight READ slideHeight WRITE setSlideHeight)

public:
    explicit CommandHistoryPopup(QWidget* parent);

    void appendLine(const QString& text, bool isPrompt = false);
    void slideIn(QWidget* anchor);
    void slideOut();
    void toggle(QWidget* anchor);
    void setContent(const QString& html);

    int  slideHeight() const { return height(); }
    void setSlideHeight(int h);

private:
    QTextEdit*          m_textEdit  = nullptr;
    QPropertyAnimation* m_anim      = nullptr;
    QWidget*            m_anchor    = nullptr;
    int                 m_targetH   = 200;

    void reposition();
};

} // namespace ui
} // namespace aicad

#endif
