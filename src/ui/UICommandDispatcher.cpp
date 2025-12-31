#include "ui/UICommandDispatcher.h"
#include <QDebug>

UICommandDispatcher::UICommandDispatcher(ICommandInvoker* invoker,
                                         QObject* parent)
    : QObject(parent), m_invoker(invoker)
{
}

void UICommandDispatcher::execute(const QString& command)
{
    // Sprint UI-1: stub implementation
    // UI System responsibility ends here

    qDebug() << "[UI] Dispatch command:" << command;

    // 下一個 Sprint 會在這裡：
    // - 呼叫 CommandManager
    if (m_invoker) {
        m_invoker->execute(command);
    }

    // - 或 publish EventBus
}
