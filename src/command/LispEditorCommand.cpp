/**
 * @file LispEditorCommand.cpp
 */
#include "command/LispEditorCommand.h"
#include "command/CommandTypes.h"
#include "command/CommandFactory.h"
#include "core/Application.h"
#include "core/CommandLineManager.h"
#include "scripting/LispEngine.h"
#include "ui/LispEditorDock.h"
#include "ui/LispEditorWidget.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>

namespace aicad {
namespace command {

// ── lisped ───────────────────────────────────────────────────────────────

LispEditorCommand::LispEditorCommand(QObject* parent)
    : Command("lisped", "Open Lisp Editor", parent)
{
}

QString LispEditorCommand::getUsage() const
{
    return QStringLiteral("lisped [file.lisp ...]");
}

CommandResult LispEditorCommand::execute(const CommandContext& context)
{
    QMainWindow* mw = ui::LispEditorDock::findMainWindow();
    ui::LispEditorDock* dock = ui::LispEditorDock::instance(mw);
    if (!dock) return CommandResult::Failure("Main window not available");

    for (const QString& path : context.args) {
        if (!path.trimmed().isEmpty()) dock->editor()->openFile(path);
    }
    dock->showAndFocus();
    return CommandResult::Success();
}

// ── load_lisp ────────────────────────────────────────────────────────────

LoadLispCommand::LoadLispCommand(QObject* parent)
    : Command("load_lisp", "Load Lisp File", parent)
{
}

QString LoadLispCommand::getUsage() const
{
    return QStringLiteral("load_lisp [file.lisp]");
}

CommandResult LoadLispCommand::execute(const CommandContext& context)
{
    core::Application* app = core::Application::instance();
    scripting::LispEngine* lisp = app ? app->lispEngine() : nullptr;
    core::CommandLineManager* cl = core::CommandLineManager::instance();

    if (!lisp || !lisp->isInitialized()) {
        const QString msg = QStringLiteral("Lisp engine not available");
        if (cl) cl->printError(msg);
        return CommandResult::Failure(msg);
    }

    // 路徑含空白時，命令列會把它拆成多個參數；接回去
    QString path = context.args.join(QLatin1Char(' ')).trimmed();
    if (path.isEmpty()) {
        QSettings s("AICAD", "AICAD");
        path = QFileDialog::getOpenFileName(
            nullptr, QObject::tr("載入 Lisp 檔案"),
            s.value(QStringLiteral("LispEditor/lastDir")).toString(),
            QObject::tr("Lisp 檔案 (*.lisp *.lsp *.cl);;所有檔案 (*)"));
        if (path.isEmpty()) return CommandResult::Failure("Cancelled");
        s.setValue(QStringLiteral("LispEditor/lastDir"), QFileInfo(path).absolutePath());
    }

    QString lastError;
    const QMetaObject::Connection cOut = QObject::connect(lisp, &scripting::LispEngine::output,
        [cl](const QString& text) { if (cl) cl->printMessage(text.trimmed()); });
    const QMetaObject::Connection cErr = QObject::connect(lisp, &scripting::LispEngine::errorOccurred,
        [&lastError](const QString& msg) { lastError = msg; });

    const bool ok = lisp->loadFile(path);

    QObject::disconnect(cOut);
    QObject::disconnect(cErr);

    if (!ok) {
        const QString msg = lastError.isEmpty() ? QStringLiteral("Failed to load: ") + path : lastError;
        if (cl) cl->printError(msg);
        return CommandResult::Failure(msg);
    }
    if (cl) cl->printSuccess(QStringLiteral("Loaded: ") + path);
    return CommandResult::Success(path);
}

REGISTER_COMMAND("lisped",    LispEditorCommand);
REGISTER_COMMAND("load_lisp", LoadLispCommand);

} // namespace command
} // namespace aicad
