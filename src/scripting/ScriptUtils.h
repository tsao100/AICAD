// src/scripting/ScriptUtils.h

#pragma once
#include <QString>

namespace aicad::scripting {

class ScriptUtils {
public:
    static void runFile(const QString& filePath);
};

}
