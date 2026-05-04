#ifndef COMMANDLINEMANAGER_H
#define COMMANDLINEMANAGER_H

#include "PromptStep.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QQueue>

namespace aicad {
namespace core {

enum class InputType {
    None,
    Point,
    Number,
    String,
    Option,
    YesNo
};

enum class MessageType {
    Info,
    Warning,
    Error,
    Success,
    Command,
    Response
};

class CommandLineManager : public QObject {
    Q_OBJECT

public:
    static CommandLineManager* instance();

    // 命令執行
    void executeCommand(const QString& input);
    void executeScript(const QString& script);
    void cancelCommand();

    // 提示管理
    void showPrompt(const QString& prompt);
    void showOptions(const QStringList& options);
    void clearPrompt();

    // 訊息輸出
    void printMessage(const QString& msg, MessageType type = MessageType::Info);
    void printError(const QString& error);
    void printWarning(const QString& warning);
    void printSuccess(const QString& msg);

    // 輸入處理
    void waitForInput(InputType type);
    bool isWaitingForInput() const { return m_isWaitingForInput; }
    InputType expectedInputType() const { return m_expectedInputType; }

    // 歷史記錄
    QStringList commandHistory() const { return m_commandHistory; }
    void addToHistory(const QString& command);

    // 當前命令
    QString activeCommand() const { return m_activeCommand; }
    bool isCommandActive() const { return !m_activeCommand.isEmpty(); }

signals:
    void promptChanged(const QString& prompt);
    void optionsAvailable(const QStringList& options);
    void messageReceived(const QString& msg, MessageType type);
    void inputRequired(InputType type);
    void commandStarted(const QString& cmd);
    void commandFinished(const QString& cmd, bool success);
    void commandCancelled();

public slots:
    void onCommandInput(const QString& input);
    void onOptionSelected(const QString& option);
    void onEscapePressed();

private:
    explicit CommandLineManager(QObject* parent = nullptr);
    ~CommandLineManager() override;

    void processInput(const QString& input);
    void processCommand(const QString& cmd);
    void processQueue();

    static CommandLineManager* s_instance;

    QString m_currentPrompt;
    QStringList m_currentOptions;
    InputType m_expectedInputType;

    bool m_isWaitingForInput;
    QString m_activeCommand;

    QStringList m_commandHistory;
    QQueue<QString> m_commandQueue;

    static constexpr int MAX_HISTORY = 100;
};

} // namespace core
} // namespace aicad

#endif
