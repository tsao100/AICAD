/**
 * @file LispEditorDock.cpp
 */
#include "LispEditorDock.h"
#include "LispEditorWidget.h"
#include "LispCodeEdit.h"

#include "core/Application.h"
#include "core/EventBus.h"
#include "scripting/LispEngine.h"

#include <QApplication>
#include <QMainWindow>

namespace aicad {
namespace ui {

QMainWindow* LispEditorDock::findMainWindow()
{
    QMainWindow* fallback = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (auto* mw = qobject_cast<QMainWindow*>(w)) {
            if (mw->isVisible()) return mw;
            if (!fallback) fallback = mw;
        }
    }
    return fallback;
}

LispEditorDock* LispEditorDock::instance(QMainWindow* mainWindow)
{
    if (!mainWindow) return nullptr;
    if (auto* existing = mainWindow->findChild<LispEditorDock*>(QStringLiteral("LispEditorDock")))
        return existing;
    return new LispEditorDock(mainWindow);
}

LispEditorDock::LispEditorDock(QMainWindow* mainWindow)
    : QDockWidget(tr("Lisp 編輯器"), mainWindow)
{
    setObjectName(QStringLiteral("LispEditorDock"));
    setAllowedAreas(Qt::AllDockWidgetAreas);

    m_editor = new LispEditorWidget(this);
    setWidget(m_editor);

    core::Application* app = core::Application::instance();
    if (app) {
        m_editor->setEngine(app->lispEngine());

        // (print-message "...") 會發布這個事件；原本沒有任何人訂閱，訊息等於被丟掉
        if (core::EventBus* bus = app->eventBus()) {
            bus->subscribe(QStringLiteral("scripting.print-message"), this, [this](const QVariant& v) {
                m_editor->appendMessage(v.toString(), LispEditorWidget::MessageKind::Info);
            });
        }
    }

    m_editor->restoreSession();
    connect(qApp, &QCoreApplication::aboutToQuit, m_editor, &LispEditorWidget::saveSession);

    mainWindow->addDockWidget(Qt::BottomDockWidgetArea, this);
    setFloating(true);
    resize(980, 700);
}

void LispEditorDock::showAndFocus()
{
    show();
    raise();
    activateWindow();
    if (m_editor && m_editor->currentEditor()) m_editor->currentEditor()->setFocus();
}

void LispEditorDock::toggle()
{
    if (isVisible()) hide();
    else             showAndFocus();
}

} // namespace ui
} // namespace aicad
