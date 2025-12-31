// ui/state/UIStateController.cpp

#include "ui/state/UIStateController.h"
#include "ui/menu/MenuBuilder.h"

#include <QAction>
#include <QToolBar>

UIStateController::UIStateController(MenuBuilder* builder, QObject* parent)
    : QObject(parent)
    , m_builder(builder)
{
}

// =======================
// Command-driven UI
// =======================

void UIStateController::setCommandEnabled(const QString& commandId, bool enabled)
{
    QAction* action = m_builder->action(commandId);
    if (action) {
        action->setEnabled(enabled);
    }
}

void UIStateController::setCommandChecked(const QString& commandId, bool checked)
{
    QAction* action = m_builder->action(commandId);
    if (action) {
        action->setCheckable(true);
        action->setChecked(checked);
    }
}

// =======================
// Toolbar-driven UI
// =======================

void UIStateController::setToolbarVisible(const QString& toolbarName, bool visible)
{
    QToolBar* tb = m_builder->toolbar(toolbarName);
    if (tb) {
        tb->setVisible(visible);
    }
}

void UIStateController::setToolbarEnabled(const QString& toolbarName, bool enabled)
{
    QToolBar* tb = m_builder->toolbar(toolbarName);
    if (tb) {
        tb->setEnabled(enabled);
    }
}
