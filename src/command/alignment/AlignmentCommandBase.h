#pragma once

/**
 * @file AlignmentCommandBase.h
 * @brief Thin base class for railway-alignment commands.
 *
 * Provides only alignDoc() helper.
 * Subclasses manage their own EventBus subscriptions following the
 * LineCommand pattern (subscribe once, unsubscribeAll in cleanup()).
 */

#include "command/Command.h"
#include "command/CommandTypes.h"
#include "railway/AlignmentDocument.h"

namespace aicad {
namespace command {

class AlignmentCommandBase : public Command
{
    Q_OBJECT

public:
    explicit AlignmentCommandBase(const QString& name,
                                  const QString& description,
                                  QObject* parent = nullptr)
        : Command(name, description, parent)
    {}

protected:
    static railway::AlignmentDocument* alignDoc(const CommandContext& ctx)
    {
        return ctx.alignmentDoc;
    }
};

} // namespace command
} // namespace aicad
