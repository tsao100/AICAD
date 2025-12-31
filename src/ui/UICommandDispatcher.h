#pragma once

#include "commands/ICommandInvoker.h"

#include <QObject>
#include <QString>

class UICommandDispatcher : public QObject
{
    Q_OBJECT
public:
    explicit UICommandDispatcher(ICommandInvoker* invoker,
                                 QObject* parent = nullptr);
    void execute(const QString& command);

private:
    ICommandInvoker* m_invoker;

};


