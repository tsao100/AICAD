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
    if (input.isEmpty()) return;

    QString trimmed = input.trimmed();

    // 添加到歷史記錄
    addToHistory(trimmed);

    // 如果正在等待輸入
    if (m_isWaitingForInput) {
        processInput(trimmed);
        return;
    }

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

    m_isWaitingForInput = false;
    m_expectedInputType = InputType::None;
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

    emit inputRequired(type);
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
