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
#include "scripting/LispEngine.h"

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
    // ── Lisp 表達式分流 ──────────────────────────────────────────────
    // 若輸入（去除頭尾空白後）以 '(' 開頭，視為 Lisp 表達式，直接交給
    // LispEngine 求值，不進入一般具名命令的查找/生命週期流程。
    // （CommandInputEdit 已保證多行 Lisp 表達式的括弧會在送出前對應完成。）
    const QString trimmedInput = commandName.trimmed();
    if (trimmedInput.startsWith(QLatin1Char('('))) {
        return executeLispExpression(trimmedInput);
    }

    QString canonicalName = getCanonicalName(commandName);
    if (canonicalName.isEmpty()) {
        auto* bus = core::Application::instance()->eventBus();
        bus->publish(Events::COMMAND_PROMPT, "Unknown command");

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

    // Command::outputMessage() 發出的 messageOutput 訊號，在此之前整個專案
    // 沒有任何地方連接過——所有互動命令（V3D、SCS、AlignmentAddSpiral…）呼叫
    // outputMessage() 顯示的提示/結果訊息因此從未真正出現在命令列，屬於
    // 靜默遺失。統一轉發到 COMMAND_LOG，讓 UIManager 既有的命令列輸出訂閱
    // （見 connectCommandLineEvents() 對 COMMAND_LOG 的處理）能顯示出來。
    if (auto* bus = core::Application::instance()->eventBus()) {
        // ⚠️ 不可對 lambda 連線加 Qt::UniqueConnection：Qt 只能比較
        //    pointer-to-member-function 是否重複，對 lambda/functor 這個
        //    旗標不會有任何保護效果（見 ConstraintOverlayManager.cpp 同類註記）。
        //    此處也不需要靠它防重複——cmd 是 info.factory() 剛建立的新物件
        //    （見上方第 216 行），本來就不會被連接第二次。
        connect(cmd, &Command::messageOutput, this,
                [bus](const QString& message) {
                    bus->publish(core::Events::COMMAND_LOG, message);
                });
    }

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

namespace {

// 將 LispEngine::eval() 回傳的 QVariant 轉為適合顯示在命令列的字串。
// Lisp 的 list 會被轉為巢狀 QVariantList，這裡遞迴印成 "(a b c)" 的形式；
// 布林 T/NIL、以及沒有回傳值（NIL）的情況分別處理。
QString formatLispResultForDisplay(const QVariant& value) {
    if (!value.isValid()) {
        return QStringLiteral("NIL");
    }
    if (value.type() == QVariant::List) {
        QStringList parts;
        for (const QVariant& item : value.toList()) {
            parts << formatLispResultForDisplay(item);
        }
        return QLatin1Char('(') + parts.join(QLatin1Char(' ')) + QLatin1Char(')');
    }
    if (value.type() == QVariant::Bool) {
        return value.toBool() ? QStringLiteral("T") : QStringLiteral("NIL");
    }
    return value.toString();
}

} // anonymous namespace

CommandResult CommandManager::executeLispExpression(const QString& expr) {
    auto* app = core::Application::instance();
    auto* bus = app ? app->eventBus() : nullptr;
    scripting::LispEngine* lisp = app ? app->lispEngine() : nullptr;

    if (!lisp || !lisp->isInitialized()) {
        const QString msg = "Lisp engine not available";
        qWarning() << "[CommandManager]" << msg;
        if (bus) {
            bus->publish(Events::COMMAND_ERROR, msg);
            bus->publish(Events::COMMAND_FAILED, expr);
        }
        return CommandResult::Failure(msg);
    }

    // 取消目前正在執行的具名命令（若有），行為與一般命令分流一致。
    if (d->currentCommand) {
        d->currentCommand->cancel();
        d->currentCommand->cleanup();
        d->currentCommand->deleteLater();
        d->currentCommand = nullptr;
    }

    Q_EMIT commandStarted(expr);
    if (bus) {
        bus->publish(Events::COMMAND_STARTED, expr);
    }

    // eval() 內部並不會在成功時清除 lastError()，因此不能靠 lastError()
    // 事後判斷本次求值是否出錯；改用 errorOccurred 訊號在本次呼叫期間
    // 是否被觸發來判斷，訊號在同執行緒下是同步發出的。
    bool    hadError = false;
    QString errorMsg;
    QMetaObject::Connection conn = QObject::connect(
        lisp, &scripting::LispEngine::errorOccurred,
        [&hadError, &errorMsg](const QString& msg) {
            hadError = true;
            errorMsg = msg;
        });

    const QVariant evalResult = lisp->eval(expr);

    QObject::disconnect(conn);

    CommandResult result;

    if (hadError) {
        qWarning() << "[CommandManager] Lisp evaluation failed:" << errorMsg;
        if (bus) {
            bus->publish(Events::COMMAND_ERROR, errorMsg);
            bus->publish(Events::COMMAND_FAILED, expr);
        }
        result = CommandResult::Failure(errorMsg);
    } else {
        const QString display = formatLispResultForDisplay(evalResult);
        qDebug() << "[CommandManager] Lisp evaluation result:" << display;
        if (bus) {
            bus->publish(Events::COMMAND_LOG, display);
            bus->publish(Events::COMMAND_PROMPT, "");
            bus->publish(Events::COMMAND_EXECUTED, expr);
        }
        addToHistory(expr, QStringList());
        result = CommandResult::Success(display);
    }

    Q_EMIT commandFinished(expr, result);
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
        // 失敗時原本只發佈命令名稱（例如 "alignment3daddvprofile"），
        // ResultPopup 等訂閱者只能顯示這個名稱，看不到真正的失敗原因
        // （例如「尚未選取任何線路」），使用者因而完全不知道命令為何
        // 沒有反應。成功時仍維持發佈命令名稱（既有行為，COMMAND_EXECUTED
        // 訂閱者預期的是名稱），失敗時改為發佈 result.message（若為空則
        // 退回命令名稱，避免顯示空字串）。
        const QString failureText =
            result.message.isEmpty() ? name : result.message;
        bus->publish(
            result.success ? core::Events::COMMAND_EXECUTED
                           : core::Events::COMMAND_FAILED,
            result.success ? name : failureText
            );
    }

    if (result.success) {
        auto* bus = core::Application::instance()->eventBus();
        bus->publish(Events::COMMAND_PROMPT, "");
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

    Command* cmd = d->currentCommand;
    QString cmdName = cmd->name();
    qDebug() << "[CommandManager] Cancelling command:" << cmdName;

    if (cmd->canCancel()) {
        cmd->cancel();

        Q_EMIT commandCancelled(cmdName);

        // 透過 EventBus 發布事件
        if (Application* app = Application::instance()) {
            if (EventBus* bus = app->eventBus()) {
                bus->publish(Events::COMMAND_CANCELLED, cmdName);
                // ✅ 不論指令本身是否有另外訂閱 POINT_CANCELLED 之類的事件來
                //    自我收尾，這裡都要把命令列的提示重設回「等待下一個指令」，
                //    否則使用者會看到舊的提示文字卡著不動。
                bus->publish(Events::COMMAND_PROMPT, QString());
            }
        }

        // ✅ 過去這裡漏了完整收尾：只呼叫了 cancel()，沒有 cleanup()／
        //    deleteLater()／把 d->currentCommand 歸零，導致 CommandManager
        //    誤以為指令仍在執行中，後續指令送不進來。這裡比照
        //    onCommandFinished() 的收尾方式，確保「取消」等同於「指令徹底
        //    結束」，讓命令列真正回到可以接受下一個指令的狀態。
        cmd->cleanup();
        cmd->deleteLater();
        d->currentCommand = nullptr;
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
    return d->currentCommand != nullptr &&(
           d->currentCommand->state() == CommandState::WaitingInput ||
                                            d->currentCommand->state() == CommandState::Running );
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
