// src/scripting/ScriptSerializer.h

#pragma once
#include <QString>

namespace aicad::scripting {

class ScriptSerializer {
public:
    static bool save(const QString& filePath,
                     const QString& script);
};

}
