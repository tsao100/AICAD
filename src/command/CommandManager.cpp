/**
 * @file CommandManager.cpp
 * @brief CommandManager 類別實作
 * @author Kaufen
 * @date 2024-12-04
 */

#include "command/CommandManager.h"
#include "Command.h"
#include "core/EventBus.h"
#include "core/Application.h"

#include <QDebug>
#include <QHash>
#include <QStringList>
#include <QVector>
#include <algorithm>

namespace aicad {
namespace core {

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

CommandResult CommandManager::executeCommand(const QString& commandName,
                                            const CommandContext& context)
{
    QString canonicalName = getCanonicalName(commandName);
    
    if (canonicalName.isEmpty()) {
        QString msg = QString("Unknown command: %1").arg(commandName);
        qWarning() << "[CommandManager]" << msg;
        return CommandResult::Failure(msg);
    }
    
    // 檢查是否有命令正在執行
    if (d->currentCommand) {
        QString msg = QString("Command '%1' is already running. Cancel it first.")
            .arg(d->currentCommand->name());
        qWarning() << "[CommandManager]" << msg;
        return CommandResult::Failure(msg);
    }
    
    // 取得命令資訊
    const CommandInfo& info = d->commands[canonicalName];
    
    // 建立命令實例
    d->currentCommand = info.factory();
    if (!d->currentCommand) {
        QString msg = QString("Failed to create command: %1").arg(canonicalName);
        qCritical() << "[CommandManager]" << msg;
        return CommandResult::Failure(msg);
    }
    
    qDebug() << "[CommandManager] Executing command:" << canonicalName 
             << "Args:" << context.args;
    
    // 連接命令信號
    connect(d->currentCommand, &Command::messageOutput,
            this, &CommandManager::commandMessage);
    
    // 發出命令開始事件
    Q_EMIT commandStarted(canonicalName);
    
    // 透過 EventBus 發布事件
    if (Application* app = Application::instance()) {
        if (EventBus* bus = app->eventBus()) {
            bus->publish(Events::COMMAND_STARTED, canonicalName);
        }
    }
    
    CommandResult result;
    
    try {
        // 初始化命令
        if (!d->currentCommand->initialize()) {
            result = CommandResult::Failure("Command initialization failed");
        } else {
            // 驗證參數
            if (!d->currentCommand->validateParameters(context)) {
                result = CommandResult::Failure("Invalid command parameters");
            } else {
                // 執行命令
                result = d->currentCommand->execute(context);
            }
        }
        
        // 清理命令
        d->currentCommand->cleanup();
        
    } catch (const std::exception& e) {
        QString msg = QString("Command execution failed: %1").arg(e.what());
        qCritical() << "[CommandManager]" << msg;
        result = CommandResult::Failure(msg);
    } catch (...) {
        QString msg = "Command execution failed: Unknown exception";
        qCritical() << "[CommandManager]" << msg;
        result = CommandResult::Failure(msg);
    }
    
    // 刪除命令實例
    delete d->currentCommand;
    d->currentCommand = nullptr;
    
    // 添加到歷史記錄
    if (result.success) {
        addToHistory(canonicalName, context.args);
    }
    
    // 發出命令完成事件
    Q_EMIT commandFinished(canonicalName, result);
    
    // 透過 EventBus 發布事件
    if (Application* app = Application::instance()) {
        if (EventBus* bus = app->eventBus()) {
            QString eventName = result.success ? 
                Events::COMMAND_EXECUTED : "command.failed";
            bus->publish(eventName, canonicalName);
        }
    }
    
    qDebug() << "[CommandManager] Command" << canonicalName 
             << (result.success ? "completed successfully" : "failed")
             << "Message:" << result.message;
    
    return result;
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
