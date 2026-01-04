//ui/state/DummyCommandStateSource.h
#pragma once

#include "ui/state/ICommandStateSource.h"

/**
 * @brief Temporary stub for UI-5
 *
 * Will be replaced by real CommandManager later
 */
class DummyCommandStateSource : public ICommandStateSource
{
    Q_OBJECT
public:
    using ICommandStateSource::ICommandStateSource;

    void setEnabled(const QString& commandId, bool enabled)
    {
        CommandState s;
        s.enabled = enabled;
        Q_EMIT commandStateChanged(commandId, s);
    }
};
