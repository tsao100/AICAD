// ui/menu/MenuBuilder.h
#pragma once

#include "ui/menu/UIMenuItem.h"

#include <QObject>
#include <QMenuBar>
#include <QToolBar>
#include <QHash>
#include <QVector>
#include <functional>

class QMainWindow;
class QAction;

/**
 * @brief Build menus and toolbars from UI-only layout items.
 *
 * UI-4 principles:
 * - MenuBuilder does NOT create QAction
 * - MenuBuilder does NOT know Command / Dispatcher
 * - QAction is provided by callback factory
 */
class MenuBuilder : public QObject
{
    Q_OBJECT
public:
    using ActionFactory = std::function<QAction*(const UIMenuItem&)>;

    explicit MenuBuilder(QMainWindow* mainWindow);

    // Build menus / toolbars from parsed items
    void build(const QVector<UIMenuItem>& items,
               ActionFactory actionFactory);

private:
    QMainWindow* m_mainWindow = nullptr;

    QMenuBar* m_menuBar = nullptr;
    QHash<QString, QMenu*> m_menus;
    QHash<QString, QToolBar*> m_toolbars;

private:
    QMenu* ensureMenu(const QString& name);
    QToolBar* ensureToolBar(const QString& name);
};
