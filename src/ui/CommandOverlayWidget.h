//src/ui/CommandOverlayWidget.h

#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QMouseEvent>


namespace aicad::ui {

class CommandPromptLabel;
class CommandInput;

class CommandOverlayWidget : public QWidget {
    Q_OBJECT
public:
    explicit CommandOverlayWidget(QWidget* parent);

    static constexpr int PromptMinWidth = 60;
    static constexpr int PromptMaxWidth = 600;
    static constexpr int PromptHPadding = 16;   // 左右 padding 總和

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

    void finishCommand();

    void mousePressEvent(QMouseEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void reposition();


private:
    QWidget* m_promptArea;
    QVBoxLayout* m_promptLayout;
    static constexpr int MaxPromptLines = 4;
    QList<QLabel*> m_promptLabels;

    QString m_activePrompt;              // 目前顯示在 CommandInput
    QStringList m_promptHistory;         // 尚未進 label 的歷史
    bool m_commandActive = false;

    int promptSpacing = 4;

    void adjustOverlayHeight();
    void repositionPrompts();
    void appendPromptLine(const QString& text);
    void handlePrompt(const QString& text);

    bool isOnRightBorder(const QPoint &pos) const {
        const int margin = 5;
        return pos.x() >= width() - margin;
    }

    bool m_resizing = false;
    QPoint m_dragStartPos;
    CommandInput* m_input;
    int m_startWidth = 0;

};

}
