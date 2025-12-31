// ui/menu/UIMenuItem.h
#pragma once

#include <QString>

struct UIMenuItem
{
    QString type;      // "menu" | "toolbar"
    QString group;     // menu name or toolbar name (e.g. "File", "main")
    QString id;        // command id or "separator"
    QString label;     // display text
    QString icon;      // icon path (toolbar)
    QString shortcut;  // shortcut string
};
