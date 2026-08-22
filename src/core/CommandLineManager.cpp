#include "CommandLineManager.h"
#include "Application.h"
#include "EventBus.h"
#include "command/InputParser.h"
#include <QTimer>
#include <QDebug>

namespace aicad {
namespace core {

CommandLineManager* CommandLineManager::s_instance = nullptr;

CommandLineManager* CommandLineManager::instance() {
    if (!s_instance) {
        s_instance = new CommandLineManager(Application::instance());
    }
    return s_instance;
}

CommandLineManager::CommandLineManager(QObject* parent)
    : QObject(parent)
    , m_expectedInputType(InputType::None)
    , m_isWaitingForInput(false)
{
    qDebug() << "[CommandLineManager] Created";
}

CommandLineManager::~CommandLineManager() {
    qDebug() << "[CommandLineManager] Destroyed";
}

void CommandLineManager::executeCommand(const QString& input) {
    // If a command is waiting for input, an empty string is a valid "Enter for
    // default" signal (e.g. accepting the default Clothoid spiral type).
    // Only reject empty input when the system is idle.
    if (input.isEmpty() && !m_isWaitingForInput) return;

    QString trimmed = input.trimmed();

    // 如果正在等待輸入
    if (m_isWaitingForInput) {
        // trimmed may be empty here — that is the "accept default" case.
        processInput(trimmed);
        return;
    }

    if (trimmed.isEmpty()) return;

    // 添加到歷史記錄
    addToHistory(trimmed);

    // 否則作為新命令執行
    processCommand(trimmed);
}

void CommandLineManager::processCommand(const QString& cmd) {
    m_activeCommand = cmd;

    qDebug() << "[CommandLineManager] Executing command:" << cmd;

    emit commandStarted(cmd);

    // 發送命令執行請求
    auto* bus = Application::instance()->eventBus();
    bus->publish(Events::COMMAND_EXECUTE_REQUEST, cmd);
}

void CommandLineManager::processInput(const QString& input) {
    qDebug() << "[CommandLineManager] Processing input:" << input
             << "Type:" << static_cast<int>(m_expectedInputType);

    // processInput 時比對 shortcut 或 label（case-insensitive）
    const auto& opts = m_currentOptions;
    for (const auto& po : opts) {
        if (input.compare(po.shortcut, Qt::CaseInsensitive) == 0 ||
            input.compare(po.label,    Qt::CaseInsensitive) == 0) {
            auto* bus = Application::instance()->eventBus();
            bus->publish(Events::OPTION_SELECTED, po.label);
            return;
        }
    }

    // 根據期望的輸入類型處理
    auto* bus = Application::instance()->eventBus();

    // ⚠️ 派送事件前先記下目前的「掛號世代」。派送過程中（例如
    // SketchSelectionPicker 收到自己的 STRING_INPUT，同步 unsubscribe
    // 自己、emit confirmed() 觸發下一階段命令緊接著同步呼叫
    // waitForInput() 重新掛號等待下一次輸入——TRIM/EXTEND 選完邊界、
    // 進入逐次點選階段就是這個情形），下游 handler 完全可能在同一個呼
    // 叫堆疊內就同步呼叫 waitForInput() 重新掛號。如果派送完直接無條件
    // 把 m_isWaitingForInput 設回 false，就會把剛剛才重新掛號好的新狀態
    // 蓋掉——這正是先前「TRIM/EXTEND 選完邊界進入下一階段後，右鍵按不
    // 掉命令」的根本原因。用世代編號判斷：只有在派送過程中沒有人重新呼
    // 叫過 waitForInput()，才代表這次輸入是「乾淨處理完、沒人接著要下
    // 一個輸入」，這時候才清掉等待狀態；否則保留派送過程中設定好的新
    // 狀態，不要覆蓋。
    const int generationBeforeDispatch = m_waitGeneration;

    switch (m_expectedInputType) {
    case InputType::Point:
        bus->publish(Events::COORDINATE_INPUT, input);
        break;

    case InputType::Number:
        bus->publish(Events::NUMBER_INPUT, input);
        break;

    case InputType::Option:
        bus->publish(Events::OPTION_SELECTED, input);
        break;

    case InputType::String:
        bus->publish(Events::STRING_INPUT, input);
        break;

    case InputType::YesNo:
        bus->publish(Events::YESNO_INPUT, input);
        break;

    default:
        break;
    }

    if (m_waitGeneration == generationBeforeDispatch) {
        m_isWaitingForInput = false;
        m_expectedInputType = InputType::None;
    }
}

void CommandLineManager::executeScript(const QString& script) {
    QStringList lines = script.split('\n', Qt::SkipEmptyParts);

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith(';')) {
            m_commandQueue.enqueue(trimmed);
        }
    }

    processQueue();
}

void CommandLineManager::processQueue() {
    if (m_commandQueue.isEmpty()) return;

    QString cmd = m_commandQueue.dequeue();
    executeCommand(cmd);

    // 繼續處理隊列（在命令完成後）
    if (!m_commandQueue.isEmpty()) {
        QTimer::singleShot(100, this, &CommandLineManager::processQueue);
    }
}

void CommandLineManager::cancelCommand() {
    qDebug() << "[CommandLineManager] Command cancelled";

    m_activeCommand.clear();
    m_isWaitingForInput = false;
    m_expectedInputType = InputType::None;
    m_currentPrompt.clear();
    m_currentOptions.clear();
    emit promptOptionsChanged({});

    auto* bus = Application::instance()->eventBus();
    bus->publish(Events::COMMAND_CANCELLED, QVariant());

    emit commandCancelled();
}

void CommandLineManager::showPrompt(const QString& prompt) {
    m_currentPrompt = prompt;

    // 解析 prompt 內的 [Arc/Undo/...] → 更新按鈕列
    auto parsed = command::InputParser::parsePrompt(prompt);
    m_currentOptions = parsed.options;
    emit promptOptionsChanged(m_currentOptions);   // ← 新增

    auto* bus = Application::instance()->eventBus();
    bus->publish(Events::COMMAND_PROMPT, prompt);
    emit promptChanged(prompt);
}

void CommandLineManager::showOptions(const QStringList& options) {
    m_currentOptions.clear();
    for (const QString& opt : options) {
        command::InputParser::ParsedOption po;
        po.label    = opt;
        po.shortcut = opt.isEmpty() ? QString() : QString(opt[0].toUpper());
        m_currentOptions.append(po);
    }
    emit promptOptionsChanged(m_currentOptions);
}

void CommandLineManager::clearPrompt() {
    m_currentPrompt.clear();

    auto* bus = Application::instance()->eventBus();
    bus->publish(Events::COMMAND_PROMPT, QString());

    emit promptChanged(QString());
}

void CommandLineManager::printMessage(const QString& msg, MessageType type) {
    auto* bus = Application::instance()->eventBus();

    QString eventName;
    switch (type) {
    case MessageType::Error:
        eventName = Events::COMMAND_ERROR;
        break;
    case MessageType::Warning:
        eventName = Events::COMMAND_WARNING;
        break;
    case MessageType::Success:
    case MessageType::Info:
    default:
        eventName = Events::COMMAND_LOG;
        break;
    }

    bus->publish(eventName, msg);
    emit messageReceived(msg, type);
}

void CommandLineManager::printError(const QString& error) {
    printMessage(error, MessageType::Error);
}

void CommandLineManager::printWarning(const QString& warning) {
    printMessage(warning, MessageType::Warning);
}

void CommandLineManager::printSuccess(const QString& msg) {
    printMessage(msg, MessageType::Success);
}

void CommandLineManager::waitForInput(InputType type) {
    m_isWaitingForInput = true;
    m_expectedInputType = type;
    ++m_waitGeneration;

    emit inputRequired(type);
}

void CommandLineManager::resetInputWait() {
    m_isWaitingForInput = false;
    m_expectedInputType = InputType::None;
    m_currentOptions.clear();
    emit promptOptionsChanged({});
}

void CommandLineManager::addToHistory(const QString& command) {
    if (command.isEmpty()) return;

    m_commandHistory.removeAll(command);
    m_commandHistory.prepend(command);

    if (m_commandHistory.size() > MAX_HISTORY) {
        m_commandHistory.removeLast();
    }
}

void CommandLineManager::onCommandInput(const QString& input) {
    executeCommand(input);
}

void CommandLineManager::onOptionSelected(const QString& option) {
    // option 是 po.shortcut（如 "U"、"C"）
    auto* bus = core::Application::instance()->eventBus();
    bus->publish(Events::OPTION_SELECTED, option);
}

void CommandLineManager::onEscapePressed() {
    cancelCommand();
}

} // namespace core
} // namespace aicad