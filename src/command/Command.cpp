/**
 * @file Command.cpp
 * @brief Command 類別實作
 * @author Kaufen
 * @date 2024-12-04
 */

#include "Command.h"
#include "CommandManager.h"

#include <QDebug>

namespace aicad {
namespace commands {

class Command::Private {
public:
    QString name;
    QString description;
    CommandState state;
    
    Private(const QString& n, const QString& desc)
        : name(n)
        , description(desc)
        , state(CommandState::Ready)
    {
    }
};

Command::Command(const QString& name,
                const QString& description,
                QObject* parent)
    : QObject(parent)
    , d(new Private(name, description))
{
    qDebug() << "[Command]" << name << "created";
}

Command::~Command() {
    qDebug() << "[Command]" << d->name << "destroyed";
    delete d;
}

QString Command::name() const {
    return d->name;
}

QString Command::description() const {
    return d->description;
}

CommandState Command::state() const {
    return d->state;
}

bool Command::initialize() {
    qDebug() << "[Command]" << d->name << "initializing...";
    setState(CommandState::Ready);
    return true;
}

void Command::cleanup() {
    qDebug() << "[Command]" << d->name << "cleaning up...";
}

void Command::cancel() {
    qDebug() << "[Command]" << d->name << "cancelled";
    setState(CommandState::Cancelled);
}

bool Command::canCancel() const {
    // 預設情況下，只有正在執行的命令可以取消
    return d->state == CommandState::Running;
}

QString Command::getUsage() const {
    return QString("Usage: %1").arg(d->name);
}

bool Command::validateParameters(const CommandContext& context) const {
    Q_UNUSED(context);
    // 預設不進行參數驗證
    return true;
}

void Command::setState(CommandState state) {
    if (d->state != state) {
        d->state = state;
        Q_EMIT stateChanged(state);
    }
}

void Command::outputMessage(const QString& message) {
    Q_EMIT messageOutput(message);
}

void Command::updateProgress(int current, int total, const QString& message) {
    Q_EMIT progressUpdated(current, total, message);
}

} // namespace core
} // namespace aicad
