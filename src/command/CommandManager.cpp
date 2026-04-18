/**
 * @file CommandManager.cpp
 * @brief CommandManager 類別實作
 * @author Kaufen
 * @date 2024-12-04
 */

#include "command/CommandManager.h"
#include "command/Command.h"
#include "core/EventBus.h"
#include "core/Application.h"

#include <QDebug>
#include <QHash>
#include <QStringList>
#include <QVector>
#include <algorithm>

using namespace aicad::core;

namespace aicad {
namespace command {

/**
 * @brief 命令註冊資訊
 */
struct CommandInfo {
    QString name;
    QStringList aliases;
    CommandManager::CommandFactory factory;
    QString description;
    
    CommandInfo() {}
    
    CommandInfo(const QString& n, 
                const QStringList& a,
                CommandManager::CommandFactory f)
        : name(n), aliases(a), factory(f) {}
};

class CommandManager::Private {
public:
    // 命令名稱 -> 命令資訊
    QHash<QString, CommandInfo> commands;
    
    // 別名 -> 標準命令名稱
    QHash<QString, QString> aliasMap;
    
    // 命令歷史（最近執行的命令）
    QVector<QString> history;
    
    // 當前執行的命令
    Command* currentCommand;
    
    // 歷史記錄最大數量
    static const int MAX_HISTORY = 100;
    
    Private()
        : currentCommand(nullptr)
    {
    }
    
    ~Private() {
        if (currentCommand) {
            delete currentCommand;
        }
    }
    
    /**
     * @brief 格式化命令字串用於歷史記錄
     */
    QString formatCommandForHistory(const QString& name, const QStringList& args) {
        if (args.isEmpty()) {
            return name;
        }
        return QString("%1 %2").arg(name).arg(args.join(" "));
    }
};

CommandManager::CommandManager(QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    qDebug() << "[CommandManager] Created";

    // 訂閱事件


}

CommandManager::~CommandManager() {
    qDebug() << "[CommandManager] Destroying...";
    
    // 取消當前命令
    if (d->currentCommand) {
        d->currentCommand->cancel();
        delete d->currentCommand;
        d->currentCommand = nullptr;
    }
    
    delete d;
}

bool CommandManager::registerCommand(const QString& name,
                                     const QStringList& aliases,
                                     CommandFactory factory)
{
    if (name.isEmpty()) {
        qWarning() << "[CommandManager] Cannot register command with empty name";
        return false;
    }
    
    if (!factory) {
        qWarning() << "[CommandManager] Cannot register command with null factory";
        return false;
    }
    
    QString lowerName = name.toLower();
    
    // 檢查名稱是否已被使用
    if (d->commands.contains(lowerName)) {
        qWarning() << "[CommandManager] Command already registered:" << name;
        return false;
    }
    
    // 檢查別名是否已被使用
    for (const QString& alias : aliases) {
        QString lowerAlias = alias.toLower();
        if (d->aliasMap.contains(lowerAlias) || d->commands.contains(lowerAlias)) {
            qWarning() << "[CommandManager] Alias already in use:" << alias;
            return false;
        }
    }
    
    // 建立命令資訊
    CommandInfo info(lowerName, aliases, factory);
    
    // 嘗試建立命令實例以取得描述
    Command* cmd = factory();
    if (cmd) {
        info.description = cmd->description();
        delete cmd;
    }
    
    // 註冊命令
    d->commands[lowerName] = info;
    
    // 註冊別名
    for (const QString& alias : aliases) {
        d->aliasMap[alias.toLower()] = lowerName;
    }
    
    qDebug() << "[CommandManager] Registered command:" << name 
             << "Aliases:" << aliases;
    
    Q_EMIT commandCountChanged(d->commands.size());
    
    return true;
}

void CommandManager::unregisterCommand(const QString& name) {
    QString canonicalName = getCanonicalName(name);
    
    if (canonicalName.isEmpty()) {
        return;
    }
    
    // 移除別名映射
    if (d->commands.contains(canonicalName)) {
        const CommandInfo& info = d->commands[canonicalName];
        for (const QString& alias : info.aliases) {
            d->aliasMap.remove(alias.toLower());
        }
    }
    
    // 移除命令
    d->commands.remove(canonicalName);
    
    qDebug() << "[CommandManager] Unregistered command:" << canonicalName;
    
    Q_EMIT commandCountChanged(d->commands.size());
}

CommandResult CommandManager::executeCommand(
    const QString& commandName,
    const CommandContext& context)
{
    QString canonicalName = getCanonicalName(commandName);
    if (canonicalName.isEmpty()) {
        auto* bus = core::Application::instance()->eventBus();
        bus->publish(Events::COMMAND_PROMPT, "Unknown command");
        bus->publish(Events::COMMAND_LOG, "Unknown command");

        return CommandResult::Failure(
            QString("Unknown command: %1").arg(commandName));
    }

    // Cancel previous command
    if (d->currentCommand) {
        d->currentCommand->cancel();
        d->currentCommand->cleanup();
        d->currentCommand->deleteLater();
        d->currentCommand = nullptr;
    }

    const CommandInfo& info = d->commands[canonicalName];
    Command* cmd = info.factory();
    if (!cmd) {
        return CommandResult::Failure(
            QString("Failed to create command: %1").arg(canonicalName));
    }

    d->currentCommand = cmd;

    // 🔑 統一 finished 處理點（唯一）
    connect(cmd, &Command::finished,
            this, &CommandManager::onCommandFinished,
            Qt::UniqueConnection);

    Q_EMIT commandStarted(canonicalName);

    if (auto* bus = core::Application::instance()->eventBus()) {
        bus->publish(core::Events::COMMAND_STARTED, canonicalName);
    }

    // ---- 執行 ----
    CommandResult result;

    try {
        if (!cmd->initialize())
            result = CommandResult::Failure("Initialization failed");
        else if (!cmd->validateParameters(context))
            result = CommandResult::Failure("Invalid parameters");
        else
            result = cmd->execute(context);
    }
    catch (const std::exception& e) {
        result = CommandResult::Failure(e.what());
    }
    catch (...) {
        result = CommandResult::Failure("Unknown exception");
    }

    // 🔑 同步命令：主動結束（走同一條 finished 流）
    if (cmd->state() != CommandState::Running) {
        Q_EMIT cmd->finished(result);
    }

    return result;
}

void CommandManager::onCommandFinished(const CommandResult& result)
{
    Command* cmd = qobject_cast<Command*>(sender());
    if (!cmd || cmd != d->currentCommand)
        return;

    QString name = cmd->name();

    Q_EMIT commandFinished(name, result);

    if (auto* bus = core::Application::instance()->eventBus()) {
        bus->publish(
            result.success ? core::Events::COMMAND_EXECUTED
                           : core::Events::COMMAND_FAILED,
            name
            );
    }

    if (result.success) {
        auto* bus = core::Application::instance()->eventBus();
        bus->publish(Events::COMMAND_PROMPT, "");
        bus->publish(Events::COMMAND_LOG, "OK");
        addToHistory(name, QStringList());
    }

    cmd->cleanup();
    cmd->deleteLater();
    d->currentCommand = nullptr;

    qDebug() << "[CommandManager] Command finished:"
             << name << "success:" << result.success;
}


CommandResult CommandManager::executeCommand(const QString& commandName,
                                            const QStringList& args)
{
    CommandContext context;
    context.args = args;
    return executeCommand(commandName, context);
}

void CommandManager::cancelCurrentCommand() {
    if (!d->currentCommand) {
        qDebug() << "[CommandManager] No command to cancel";
        return;
    }
    
    QString cmdName = d->currentCommand->name();
    qDebug() << "[CommandManager] Cancelling command:" << cmdName;
    
    if (d->currentCommand->canCancel()) {
        d->currentCommand->cancel();
        
        Q_EMIT commandCancelled(cmdName);
        
        // 透過 EventBus 發布事件
        if (Application* app = Application::instance()) {
            if (EventBus* bus = app->eventBus()) {
                bus->publish(Events::COMMAND_CANCELLED, cmdName);
            }
        }
    } else {
        qWarning() << "[CommandManager] Command cannot be cancelled:" << cmdName;
    }
}

bool CommandManager::hasCommand(const QString& name) const {
    return !getCanonicalName(name).isEmpty();
}

QString CommandManager::getCanonicalName(const QString& nameOrAlias) const {
    QString lower = nameOrAlias.toLower();
    
    // 直接檢查是否為命令名稱
    if (d->commands.contains(lower)) {
        return lower;
    }
    
    // 檢查是否為別名
    if (d->aliasMap.contains(lower)) {
        return d->aliasMap[lower];
    }
    
    return QString();
}

QStringList CommandManager::getAllCommandNames() const {
    QStringList names;
    for (auto it = d->commands.constBegin(); it != d->commands.constEnd(); ++it) {
        names.append(it.key());
    }
    std::sort(names.begin(), names.end());
    return names;
}

QStringList CommandManager::getCommandAliases(const QString& name) const {
    QString canonicalName = getCanonicalName(name);
    
    if (canonicalName.isEmpty() || !d->commands.contains(canonicalName)) {
        return QStringList();
    }
    
    return d->commands[canonicalName].aliases;
}

QString CommandManager::getCommandDescription(const QString& name) const {
    QString canonicalName = getCanonicalName(name);
    
    if (canonicalName.isEmpty() || !d->commands.contains(canonicalName)) {
        return QString();
    }
    
    return d->commands[canonicalName].description;
}

int CommandManager::commandCount() const {
    return d->commands.size();
}

QStringList CommandManager::getCommandHistory(int maxCount) const {
    if (maxCount < 0 || maxCount > d->history.size()) {
        maxCount = d->history.size();
    }
    
    QStringList result;
    int startIdx = d->history.size() - maxCount;
    
    for (int i = startIdx; i < d->history.size(); ++i) {
        result.append(d->history[i]);
    }
    
    return result;
}

void CommandManager::clearHistory() {
    int count = d->history.size();
    d->history.clear();
    qDebug() << "[CommandManager] Cleared history. Removed" << count << "entries";
}

QStringList CommandManager::autoCompleteCommand(const QString& prefix) const {
    if (prefix.isEmpty()) {
        return getAllCommandNames();
    }
    
    QString lowerPrefix = prefix.toLower();
    QStringList matches;
    
    // 搜尋匹配的命令名稱
    for (auto it = d->commands.constBegin(); it != d->commands.constEnd(); ++it) {
        if (it.key().startsWith(lowerPrefix)) {
            matches.append(it.key());
        }
    }
    
    // 搜尋匹配的別名
    for (auto it = d->aliasMap.constBegin(); it != d->aliasMap.constEnd(); ++it) {
        if (it.key().startsWith(lowerPrefix)) {
            matches.append(it.key());
        }
    }
    
    matches.removeDuplicates();
    std::sort(matches.begin(), matches.end());
    
    return matches;
}

Command* CommandManager::currentCommand() const {
    return d->currentCommand;
}

bool CommandManager::isCommandRunning() const {
    return d->currentCommand != nullptr;
}

bool CommandManager::hasActiveCommand() const {
    return d->currentCommand != nullptr &&
           d->currentCommand->state() == CommandState::WaitingInput;
}

void CommandManager::addToHistory(const QString& commandName, 
                                  const QStringList& args)
{
    QString entry = d->formatCommandForHistory(commandName, args);
    
    // 移除重複的歷史記錄
    d->history.removeAll(entry);
    
    // 添加到末尾
    d->history.append(entry);
    
    // 限制歷史記錄數量
    while (d->history.size() > Private::MAX_HISTORY) {
        d->history.removeFirst();
    }
}

} // namespace core
} // namespace aicad
