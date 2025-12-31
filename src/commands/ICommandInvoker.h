// commands/ICommandInvoker.h
#pragma once

#include <QObject>
#include <QString>
#include <QDebug>

class ICommandInvoker {
public:
    virtual ~ICommandInvoker() = default;
    virtual void execute(const QString& command) = 0;
};

class DummyCommandInvoker : public ICommandInvoker {
public:
    void execute(const QString& command) override {
        qDebug() << "[Command] Execute:" << command;
    }
};

