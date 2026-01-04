// ui/menu/MenuTxtParser.h
#pragma once

#include "ui/menu/UIMenuItem.h"

#include <QString>
#include <QVector>

/**
 * @brief Parse menu.txt into UI-only menu/toolbar layout items.
 *
 * Responsibility:
 * - Parse ONLY menu / toolbar sections
 * - Ignore command / script / keymap sections
 * - Provide UI layout description (no behavior)
 *
 * NOTE:
 * - Command definitions are owned by CommandManager
 * - UI system only consumes command id
 */
class MenuTxtParser
{
public:
    explicit MenuTxtParser(const QString& filePath);

    // Parse menu.txt
    bool parse();

    // Parsed UI layout items (menu / toolbar only)
    QVector<UIMenuItem> items() const;

    // Error message for UI debug / log
    QString errorString() const;

private:
    QString m_filePath;
    QString m_error;
    QVector<UIMenuItem> m_items;

private:
    void parseLine(const QString& rawLine, int lineNumber);
    bool isIgnoredSection(const QString& type) const;
};
