// src/scripting/ScriptCommand.h

#pragma once
#include "command/Command.h"

namespace aicad::scripting {

class ScriptCommand : public Command {
public:
    explicit ScriptCommand(const QString& script);

    void execute() override;

private:
    QString m_script;
};

}
