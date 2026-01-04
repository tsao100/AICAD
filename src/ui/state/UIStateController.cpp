#include "MainWindow.h"
#include "ui/state/UIStateController.h"

#include <QAction>

UIStateController::UIStateController(MainWindow* mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
{
    Q_ASSERT(m_mainWindow);
}

void UIStateController::setEnabled(const QString& commandId, bool enabled)
{
    if (QAction* act = m_mainWindow->action(commandId)) {
        act->setEnabled(enabled);
    }
}

void UIStateController::setChecked(const QString& commandId, bool checked)
{
    if (QAction* act = m_mainWindow->action(commandId)) {
        act->setChecked(checked);
    }
}

void UIStateController::setVisible(const QString& commandId, bool visible)
{
    if (QAction* act = m_mainWindow->action(commandId)) {
        act->setVisible(visible);
    }
}
