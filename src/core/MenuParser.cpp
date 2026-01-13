//src/core/MenuParser.cpp

/**
 * @file MenuParser.cpp
 * @brief MenuParser 實作
 */

#include "MenuParser.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

namespace aicad {
namespace core {

class MenuParser::Private {
public:
    bool loaded;
    
    // 按選單/工具列分組的項目
    QMap<QString, QVector<MenuItem>> menuItems;
    QMap<QString, QVector<MenuItem>> toolbarItems;
    
    // 命令定義
    QMap<QString, CommandDef> commands;
    
    // 別名映射
    QMap<QString, QString> aliasToCommand;
    
    Private() : loaded(false) {}
};

MenuParser::MenuParser(QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    qDebug() << "[MenuParser] Created";
}

MenuParser::~MenuParser() {
    delete d;
}

bool MenuParser::load(const QString& filePath) {
    qDebug() << "[MenuParser] Loading:" << filePath;
    
    clear();
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString error = QString("Cannot open file: %1").arg(filePath);
        qWarning() << "[MenuParser]" << error;
        Q_EMIT errorOccurred(error);
        return false;
    }
    
    QTextStream in(&file);
    int lineNumber = 0;
    int validLines = 0;
    
    while (!in.atEnd()) {
        lineNumber++;
        QString line = in.readLine().trimmed();
        
        // 跳過空行和註解
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        
        if (parseLine(line, lineNumber)) {
            validLines++;
        }
    }
    
    file.close();
    
    d->loaded = true;
    
    qDebug() << "[MenuParser] Loaded successfully";
    qDebug() << "  - Valid lines:" << validLines;
    qDebug() << "  - Menus:" << d->menuItems.size();
    qDebug() << "  - Toolbars:" << d->toolbarItems.size();
    qDebug() << "  - Commands:" << d->commands.size();
    
    Q_EMIT loaded();
    return true;
}

bool MenuParser::parseLine(const QString& line, int lineNumber) {
    QStringList parts = line.split('|');
    
    if (parts.isEmpty()) {
        return false;
    }
    
    QString type = parts[0].toLower();
    
    if (type == "menu") {
        return parseMenuItem(parts, lineNumber);
    } else if (type == "toolbar") {
        return parseToolbarItem(parts, lineNumber);
    } else if (type == "command") {
        return parseCommand(parts, lineNumber);
    } else {
        qWarning() << "[MenuParser] Line" << lineNumber 
                   << "Unknown type:" << type;
        return false;
    }
}

bool MenuParser::parseMenuItem(const QStringList& parts, int lineNumber) {
    // menu|MENU|ID|LABEL|ICON|SHORTCUT
    if (parts.size() < 3) {
        qWarning() << "[MenuParser] Line" << lineNumber 
                   << "Invalid menu format";
        return false;
    }
    
    MenuItem item;
    item.type = MenuItemType::Menu;
    item.parent = parts[1].trimmed();
    item.id = parts[2].trimmed();
    
    if (item.id == "separator") {
        item.type = MenuItemType::Separator;
    } else {
        if (parts.size() > 3) item.label = parts[3].trimmed();
        if (parts.size() > 4) item.icon = parts[4].trimmed();
        if (parts.size() > 5) item.shortcut = parts[5].trimmed();
    }
    
    d->menuItems[item.parent].append(item);
    
    return true;
}

bool MenuParser::parseToolbarItem(const QStringList& parts, int lineNumber) {
    // toolbar|TOOLBAR|ID|LABEL|ICON|SHORTCUT
    if (parts.size() < 3) {
        qWarning() << "[MenuParser] Line" << lineNumber 
                   << "Invalid toolbar format";
        return false;
    }
    
    MenuItem item;
    item.type = MenuItemType::Toolbar;
    item.parent = parts[1].trimmed();
    item.id = parts[2].trimmed();
    
    if (item.id == "separator") {
        item.type = MenuItemType::Separator;
    } else {
        if (parts.size() > 3) item.label = parts[3].trimmed();
        if (parts.size() > 4) item.icon = parts[4].trimmed();
        if (parts.size() > 5) item.shortcut = parts[5].trimmed();
    }
    
    d->toolbarItems[item.parent].append(item);
    
    return true;
}

bool MenuParser::parseCommand(const QStringList& parts, int lineNumber) {
    // command|ID|ALIAS|EXPECTED_ARGS
    if (parts.size() < 4) {
        qWarning() << "[MenuParser] Line" << lineNumber 
                   << "Invalid command format";
        return false;
    }
    
    CommandDef cmd;
    cmd.id = parts[1].trimmed();
    
    QString aliasStr = parts[2].trimmed();
    if (!aliasStr.isEmpty()) {
        cmd.aliases = aliasStr.split(',', Qt::SkipEmptyParts);
        for (QString& alias : cmd.aliases) {
            alias = alias.trimmed();
            d->aliasToCommand[alias] = cmd.id;
        }
    }
    
    bool ok;
    cmd.expectedArgs = parts[3].trimmed().toInt(&ok);
    if (!ok) {
        cmd.expectedArgs = -1; // 可變參數
    }
    
    d->commands[cmd.id] = cmd;
    
    return true;
}

QVector<MenuItem> MenuParser::getMenuItems(const QString& menuName) const {
    return d->menuItems.value(menuName);
}

QVector<MenuItem> MenuParser::getToolbarItems(const QString& toolbarName) const {
    return d->toolbarItems.value(toolbarName);
}

QStringList MenuParser::getAllMenuNames() const {
    return d->menuItems.keys();
}

QStringList MenuParser::getAllToolbarNames() const {
    return d->toolbarItems.keys();
}

CommandDef MenuParser::getCommand(const QString& commandId) const {
    return d->commands.value(commandId);
}

QVector<CommandDef> MenuParser::getAllCommands() const {
    return d->commands.values().toVector();
}

QString MenuParser::findCommandByAlias(const QString& alias) const {
    return d->aliasToCommand.value(alias);
}

bool MenuParser::isLoaded() const {
    return d->loaded;
}

void MenuParser::clear() {
    d->menuItems.clear();
    d->toolbarItems.clear();
    d->commands.clear();
    d->aliasToCommand.clear();
    d->loaded = false;
}

} // namespace core
} // namespace aicad