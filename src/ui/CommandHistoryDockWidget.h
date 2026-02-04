//src/ui/CommandHistoryDockWidget.h

#pragma once
#include <QDockWidget>
#include <QTextEdit>

namespace aicad::ui {

class CommandHistoryDockWidget : public QDockWidget {
    Q_OBJECT
public:
    explicit CommandHistoryDockWidget(QWidget* parent = nullptr);

private:
    QTextEdit* m_text;
};

}
