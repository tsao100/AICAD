#pragma once
#include <QObject>
#include <QHash>
#include <functional>

namespace aicad::core {

class Command;

class CommandManager : public QObject
{
    Q_OBJECT
public:
    using Factory = std::function<Command*()>;

    explicit CommandManager(QObject* parent = nullptr);
    ~CommandManager();

    /// 註冊命令
    void registerCommand(const QString& name,
                         const QStringList& aliases,
                         Factory factory);

    /// 執行命令
    bool execute(const QString& name);

    /// 結束目前命令
    void finishCurrent();

    /// 取消目前命令
    void cancelCurrent();

    Command* currentCommand() const;

signals:
    void commandStarted(const QString& name);
    void commandFinished(const QString& name);

private:
    struct CommandInfo {
        Factory factory;
    };

    QHash<QString, CommandInfo> m_commands;
    Command* m_current = nullptr;
};

} // namespace aicad::core
