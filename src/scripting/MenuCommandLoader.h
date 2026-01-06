// src/scripting/MenuCommandLoader.h
#pragma once
#include <QString>
#include <QHash>

namespace aicad::scripting {

struct CommandMeta {
    QString id;
    QString alias;
    QString category;
    QString scriptTemplate;
};

class MenuCommandLoader {
public:
    static void load(const QString& filePath);

    static const QHash<QString, CommandMeta>& metadata();
};

}
