// src/scripting/ScriptCommand.cpp

#include "ScriptCommand.h"
#include "scripting/LispEngine.h"
#include "core/Application.h"

using namespace aicad::core;

namespace aicad::scripting {

ScriptCommand::ScriptCommand(const QString& script)
    : m_script(script)
{
}

void ScriptCommand::execute()
{
    auto* lisp = aicadApp->lispEngine();
    lisp->eval(m_script);
}

}
