// src/scripting/ScriptSerializer.cpp

#include "ScriptSerializer.h"
#include <QFile>
#include <QTextStream>

namespace aicad::scripting {

bool ScriptSerializer::save(const QString& path,
                            const QString& script)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out << script;
    return true;
}

}
