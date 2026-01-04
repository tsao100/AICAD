// ui/menu/MenuTxtParser.cpp

#include "ui/menu/MenuTxtParser.h"

#include <QFile>
#include <QTextStream>
#include <QStringList>

MenuTxtParser::MenuTxtParser(const QString& filePath)
    : m_filePath(filePath)
{
}

bool MenuTxtParser::parse()
{
    m_items.clear();
    m_error.clear();

    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_error = QString("Cannot open menu file: %1").arg(m_filePath);
        return false;
    }

    QTextStream in(&file);
    int lineNumber = 0;

    while (!in.atEnd()) {
        const QString line = in.readLine();
        ++lineNumber;

        parseLine(line, lineNumber);
        if (!m_error.isEmpty()) {
            return false;
        }
    }

    return true;
}

QVector<UIMenuItem> MenuTxtParser::items() const
{
    return m_items;
}

QString MenuTxtParser::errorString() const
{
    return m_error;
}

void MenuTxtParser::parseLine(const QString& rawLine, int lineNumber)
{
    const QString line = rawLine.trimmed();

    // Ignore empty lines and comments
    if (line.isEmpty() || line.startsWith('#')) {
        return;
    }

    const QStringList parts = line.split('|');
    const QString type = parts.value(0).trimmed();

    // ------------------------------------------------------------
    // UI-4 Responsibility Boundary
    //
    // UI system ONLY parses visual structure:
    //   - menu
    //   - toolbar
    //
    // Other sections (command / script / keymap / etc.)
    // are owned by their corresponding subsystems.
    // ------------------------------------------------------------
    if (isIgnoredSection(type)) {
        return;
    }

    // Expected UI format:
    // TYPE | GROUP | COMMAND_ID | LABEL | ICON | SHORTCUT
    if (parts.size() < 6) {
        m_error = QString(
            "Invalid UI menu format at line %1: expected 6 fields, got %2")
                      .arg(lineNumber)
                      .arg(parts.size());
        return;
    }

    UIMenuItem item;
    item.type     = type;
    item.group    = parts.value(1).trimmed();
    item.id       = parts.value(2).trimmed();   // command id
    item.label    = parts.value(3).trimmed();
    item.icon     = parts.value(4).trimmed();
    item.shortcut = parts.value(5).trimmed();

    // ---------------------------
    // UI-level validation only
    // ---------------------------
    if (item.group.isEmpty()) {
        m_error = QString("Empty menu/toolbar group at line %1").arg(lineNumber);
        return;
    }

    if (item.id.isEmpty()) {
        m_error = QString("Empty command id at line %1").arg(lineNumber);
        return;
    }

    m_items.push_back(item);
}

bool MenuTxtParser::isIgnoredSection(const QString& type) const
{
    // UI system only understands menu / toolbar
    if (type == "menu" || type == "toolbar") {
        return false;
    }

    // Explicitly ignored sections (documented)
    if (type == "command" ||
        type == "script"  ||
        type == "keymap")
    {
        return true;
    }

    // Unknown sections are ignored silently (forward compatibility)
    return true;
}
