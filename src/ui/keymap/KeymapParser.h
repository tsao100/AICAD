//ui/keymap/KeymapParser.h
#pragma once

#include <QString>
#include <QHash>

class KeymapParser
{
public:
    explicit KeymapParser(const QString& filePath);

    bool parse();
    QHash<QString, QString> mappings() const;

private:
    QString m_filePath;
    QHash<QString, QString> m_map;
};
