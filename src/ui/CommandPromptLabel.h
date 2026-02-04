//src/ui/CommandPromptLabel.h

#pragma once
#include <QLabel>

namespace aicad::ui {

class CommandPromptLabel : public QLabel {
    Q_OBJECT
public:
    explicit CommandPromptLabel(QWidget* parent = nullptr);
};

}
