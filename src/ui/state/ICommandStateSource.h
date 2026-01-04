// ui/state/ICommandStateSource.h

#pragma once

#include <QObject>
#include <QString>

struct CommandState
{
    bool enabled = true;
    bool checked = false;
    bool visible = true;
};

/**
 * @brief Abstract source of command state for UI-5
 *
 * Implemented later by CommandManager / Script / Test stub
 */
class ICommandStateSource : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual ~ICommandStateSource() = default;

Q_SIGNALS:
    void commandStateChanged(const QString& commandId,
                             const CommandState& state);
};
