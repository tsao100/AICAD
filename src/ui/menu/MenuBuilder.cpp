// ui/menu/MenuBuilder.cpp

#include "ui/menu/MenuBuilder.h"

#include <QMainWindow>
#include <QMenu>
#include <QAction>
#include <QToolBar>

MenuBuilder::MenuBuilder(QMainWindow* mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
{
    Q_ASSERT(m_mainWindow);
    m_menuBar = m_mainWindow->menuBar();
}

void MenuBuilder::build(const QVector<UIMenuItem>& items,
                        ActionFactory actionFactory)
{
    Q_ASSERT(actionFactory);

    for (const UIMenuItem& item : items) {

        QAction* action = actionFactory(item);
        if (!action) {
            // Action factory may decide to skip
            continue;
        }

        if (item.type == "menu") {
            QMenu* menu = ensureMenu(item.group);
            menu->addAction(action);
        }
        else if (item.type == "toolbar") {
            QToolBar* toolbar = ensureToolBar(item.group);
            toolbar->addAction(action);
        }
    }
}

QMenu* MenuBuilder::ensureMenu(const QString& name)
{
    if (m_menus.contains(name)) {
        return m_menus.value(name);
    }

    QMenu* menu = new QMenu(name, m_mainWindow);
    m_menuBar->addMenu(menu);
    m_menus.insert(name, menu);
    return menu;
}

QToolBar* MenuBuilder::ensureToolBar(const QString& name)
{
    if (m_toolbars.contains(name)) {
        return m_toolbars.value(name);
    }

    QToolBar* toolbar = new QToolBar(name, m_mainWindow);
    m_mainWindow->addToolBar(toolbar);
    m_toolbars.insert(name, toolbar);
    return toolbar;
}
