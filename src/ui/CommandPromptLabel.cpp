//src/ui/CommandPromptLabel.cpp

#include "CommandPromptLabel.h"

namespace aicad::ui {

CommandPromptLabel::CommandPromptLabel(QWidget* parent)
    : QLabel(parent)
{
    setText("Command:");
    setStyleSheet(R"(
        color: #E0E0E0;
        padding-left: 4px;
    )");
}

}
