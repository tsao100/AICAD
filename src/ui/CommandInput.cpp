//src/ui/CommandInput.cpp
#include "CommandInput.h"
#include "core/Application.h"
#include "core/EventBus.h"

using namespace aicad::core;

namespace aicad::ui {

CommandInput::CommandInput(QWidget* parent)
    : QLineEdit(parent)
{
    setPlaceholderText("Enter command...");
    connect(this, &QLineEdit::returnPressed,
            this, &CommandInput::onReturnPressed);
}

void CommandInput::onReturnPressed()
{
    QString cmd = text().trimmed();
    if (cmd.isEmpty())
        return;

    auto* bus = Application::instance()->eventBus();

    bus->publish(Events::COMMAND_LOG,
        QVariant::fromValue("Command: " + cmd));
    bus->publish(Events::COMMAND_EXECUTE_REQUEST, cmd);

    clear();
}

}
