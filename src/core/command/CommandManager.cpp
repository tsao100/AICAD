#include "CommandManager.h"
#include "Command.h"

using namespace aicad::core;

CommandManager::CommandManager(QObject* parent)
    : QObject(parent)
{
}

CommandManager::~CommandManager()
{
    delete m_current;
}

void CommandManager::registerCommand(const QString& name,
                                     const QStringList& aliases,
                                     Factory factory)
{
    CommandInfo info{ factory };
    m_commands.insert(name.toLower(), info);

    for (const auto& a : aliases)
        m_commands.insert(a.toLower(), info);
}

bool CommandManager::execute(const QString& name)
{
    auto key = name.toLower();
    if (!m_commands.contains(key))
        return false;

    // 結束舊命令
    cancelCurrent();

    m_current = m_commands[key].factory();
    m_current->begin();

    emit commandStarted(key);
    return true;
}

void CommandManager::finishCurrent()
{
    if (!m_current) return;

    QString name = m_current->name();
    m_current->end();
    delete m_current;
    m_current = nullptr;

    emit commandFinished(name);
}

void CommandManager::cancelCurrent()
{
    if (!m_current) return;

    m_current->cancel();
    delete m_current;
    m_current = nullptr;
}

Command* CommandManager::currentCommand() const
{
    return m_current;
}
