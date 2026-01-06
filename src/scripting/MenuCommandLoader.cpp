// src/scripting/MenuCommandLoader.cpp

#include "MenuCommandLoader.h"

#include "core/Application.h"
#include "command/CommandManager.h"
#include "scripting/ScriptCommand.h"

#include <QFile>
#include <QTextStream>

using namespace aicad::core;

namespace aicad::scripting {

static QHash<QString, CommandMeta> g_metadata;

static QString buildScriptTemplate(const QString& id, int argCount)
{
    QStringList args;
    for (int i = 1; i <= argCount; ++i)
        args << QString("$%1").arg(i);

    if (args.isEmpty())
        return QString("(%1)").arg(id);

    return QString("(%1 %2)").arg(id, args.join(" "));
}

void MenuCommandLoader::load(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("[MenuCommandLoader] Cannot open menu.txt");
        return;
    }

    QTextStream in(&file);
    auto* cmdMgr = aicadApp->commandManager();

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith("#"))
            continue;

        QStringList t = line.split("|");
        if (t.size() < 4 || t[0] != "command")
            continue;

        QString id       = t[1].trimmed();
        QString alias    = t[2].trimmed();
        int argCount     = t[3].toInt();
        QString category = (t.size() >= 5) ? t[4].trimmed() : "misc";

        QString scriptTemplate = buildScriptTemplate(id, argCount);

        // ---- 註冊 Command ----
        cmdMgr->registerCommand(id, [scriptTemplate]() {
            return new ScriptCommand(scriptTemplate);
        });

        // ---- 記錄 Metadata ----
        CommandMeta meta;
        meta.id = id;
        meta.alias = alias;
        meta.category = category;
        meta.scriptTemplate = scriptTemplate;

        g_metadata.insert(id, meta);

        // ---- Keymap stub ----
        if (!alias.isEmpty()) {
            // Phase 2.5: 只登記，不處理 UI
            // KeymapRegistry::registerKey(alias, id);
        }
    }

    qDebug("[MenuCommandLoader] Phase 2.5 loaded");
}

const QHash<QString, CommandMeta>& MenuCommandLoader::metadata()
{
    return g_metadata;
}

}
