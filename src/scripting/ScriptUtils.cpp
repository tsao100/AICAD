// src/scripting/ScriptUtils.cpp

#include "ScriptUtils.h"
#include "core/Application.h"
#include <QFile>
#include <QTextStream>

namespace aicad::scripting {

void ScriptUtils::runFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&file);
    auto* lisp = aicadApp->lispEngine();

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(";"))
            continue;
        lisp->eval(line);
    }
}

}
