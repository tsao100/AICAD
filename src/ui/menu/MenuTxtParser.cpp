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
        QString line = in.readLine();
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
    QString line = rawLine.trimmed();

    // Ignore empty lines and comments
    if (line.isEmpty() || line.startsWith("#")) {
        return;
    }

    QStringList parts = line.split('|');

    // We only care about menu / toolbar
    const QString type = parts.value(0);

    if (type != "menu" && type != "toolbar") {
        // Ignore command section or unknown types
        return;
    }

    // Expected format:
    // TYPE|GROUP|ID|LABEL|ICON|SHORTCUT
    if (parts.size() < 6) {
        m_error = QString(
            "Invalid format at line %1: expected 6 fields, got %2")
                      .arg(lineNumber)
                      .arg(parts.size());
        return;
    }

    UIMenuItem item;
    item.type     = parts.value(0).trimmed();
    item.group    = parts.value(1).trimmed();
    item.id       = parts.value(2).trimmed();
    item.label    = parts.value(3).trimmed();
    item.icon     = parts.value(4).trimmed();
    item.shortcut = parts.value(5).trimmed();

    // Basic validation (UI-level only)
    if (item.group.isEmpty()) {
        m_error = QString("Empty menu/toolbar name at line %1").arg(lineNumber);
        return;
    }

    if (item.id.isEmpty()) {
        m_error = QString("Empty command id at line %1").arg(lineNumber);
        return;
    }

    m_items.push_back(item);
}
