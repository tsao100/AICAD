//ui/keymap/KeymapParser.cpp
#include "ui/keymap/KeymapParser.h"

#include <QFile>
#include <QTextStream>

KeymapParser::KeymapParser(const QString& filePath)
    : m_filePath(filePath)
{
}

bool KeymapParser::parse()
{
    m_map.clear();

    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        const QStringList parts = line.split('|');
        if (parts.size() != 2)
            continue;

        m_map.insert(parts[0].trimmed(),
                     parts[1].trimmed());
    }
    return true;
}

QHash<QString, QString> KeymapParser::mappings() const
{
    return m_map;
}
