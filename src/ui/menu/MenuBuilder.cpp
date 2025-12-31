// ui/menu/MenuBuilder.cpp

#include "ui/menu/MenuBuilder.h"
#include "ui/UICommandDispatcher.h"

#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QAction>
#include <QIcon>

MenuBuilder::MenuBuilder(QMainWindow* mainWindow,
                         UICommandDispatcher* dispatcher)
    : m_mainWindow(mainWindow),
      m_dispatcher(dispatcher)
{
}

void MenuBuilder::build(const QVector<UIMenuItem>& items)
{
    for (const auto& item : items) {
        if (item.type == "menu") {
            addMenuItem(item);
        }
        else if (item.type == "toolbar") {
            addToolBarItem(item);
        }
    }
}

QAction* MenuBuilder::action(const QString& commandId) const
{
    return m_actions.value(commandId, nullptr);
}

QToolBar* MenuBuilder::toolbar(const QString& name) const
{
    return m_toolbars.value(name, nullptr);
}

// ========================
// Menu handling
// ========================

QMenu* MenuBuilder::getOrCreateMenu(const QString& name)
{
    if (!m_menus.contains(name)) {
        QMenu* menu = m_mainWindow->menuBar()->addMenu(name);
        m_menus.insert(name, menu);
    }
    return m_menus.value(name);
}

void MenuBuilder::addMenuItem(const UIMenuItem& item)
{
    QMenu* menu = getOrCreateMenu(item.group);

    if (item.id == "separator") {
        menu->addSeparator();
        return;
    }

    QAction* action = getOrCreateAction(item);

    menu->addAction(action);
}

// ========================
// Toolbar handling
// ========================

QToolBar* MenuBuilder::getOrCreateToolBar(const QString& name)
{
    if (!m_toolbars.contains(name)) {
        QToolBar* tb = new QToolBar(name, m_mainWindow);
        m_mainWindow->addToolBar(tb);
        m_toolbars.insert(name, tb);
    }
    return m_toolbars.value(name);
}

QAction* MenuBuilder::getOrCreateAction(const UIMenuItem& item)
{
    if (m_actions.contains(item.id)) {
        return m_actions.value(item.id);
    }

    QAction* action = new QAction(item.label, m_mainWindow);

    if (!item.icon.isEmpty()) {
        action->setIcon(QIcon(item.icon));
    }

    if (!item.shortcut.isEmpty()) {
        action->setShortcut(QKeySequence(item.shortcut));
    }

    if (m_dispatcher) {
        QString commandId = item.id;
        QObject::connect(action, &QAction::triggered,
                         m_mainWindow,
                         [this, commandId]() {
                             m_dispatcher->execute(commandId);
                         });
    }

    m_actions.insert(item.id, action);
    return action;
}

void MenuBuilder::addToolBarItem(const UIMenuItem& item)
{
    QToolBar* tb = getOrCreateToolBar(item.group);

    if (item.id == "separator") {
        tb->addSeparator();
        return;
    }

    QAction* action = getOrCreateAction(item);

    tb->addAction(action);
}
