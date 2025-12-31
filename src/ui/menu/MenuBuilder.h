// ui/menu/MenuBuilder.h
#pragma once

#include "ui/menu/UIMenuItem.h"

#include <QMainWindow>
#include <QMap>
#include <QVector>

class UICommandDispatcher;
class QMenu;
class QToolBar;
class QAction;

class MenuBuilder
{
public:
    MenuBuilder(QMainWindow* mainWindow,
                UICommandDispatcher* dispatcher);

    void build(const QVector<UIMenuItem>& items);

    QAction* action(const QString& commandId) const;
    
    QToolBar* toolbar(const QString& name) const;

private:
    QMainWindow* m_mainWindow;
    UICommandDispatcher* m_dispatcher;

    QMap<QString, QMenu*> m_menus;
    QMap<QString, QToolBar*> m_toolbars;

    QMap<QString, QAction*> m_actions;

private:
    QMenu* getOrCreateMenu(const QString& name);
    QToolBar* getOrCreateToolBar(const QString& name);

    QAction* getOrCreateAction(const UIMenuItem& item);

    void addMenuItem(const UIMenuItem& item);
    void addToolBarItem(const UIMenuItem& item);
};
