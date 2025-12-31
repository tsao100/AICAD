// ui/menu/MenuTxtParser.h
#pragma once

#include "ui/menu/UIMenuItem.h"

#include <QString>
#include <QVector>

class MenuTxtParser
{
public:
    explicit MenuTxtParser(const QString& filePath);

    // 讀取並解析 menu.txt
    bool parse();

    // 解析結果（UI-only）
    QVector<UIMenuItem> items() const;

    // 錯誤資訊（for UI debug / log）
    QString errorString() const;

private:
    QString m_filePath;
    QString m_error;
    QVector<UIMenuItem> m_items;

private:
    void parseLine(const QString& line, int lineNumber);
};
