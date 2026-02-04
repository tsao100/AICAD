
//src/ui/ResultPopup.h
#pragma once
#include <QWidget>
#include <QLabel>
#include <QTimer>

namespace aicad::ui {

class ResultPopup : public QWidget {
    Q_OBJECT
public:
    explicit ResultPopup(QWidget* parent = nullptr);

private:
    QLabel* m_label;
    QTimer  m_timer;

    void showMessage(const QString& msg, bool error);
};

}
