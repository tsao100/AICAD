//src/ui/CommandInput.h

#pragma once
#include <QLineEdit>

namespace aicad::ui {

class CommandInput : public QLineEdit {
    Q_OBJECT
public:
    explicit CommandInput(QWidget* parent = nullptr);

private slots:
    void onReturnPressed();
};

}
