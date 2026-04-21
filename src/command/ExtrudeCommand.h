#ifndef AICAD_COMMAND_EXTRUDECOMMAND_H
#define AICAD_COMMAND_EXTRUDECOMMAND_H

#include "Command.h"

namespace aicad {
namespace command {

class ExtrudeCommand : public Command {
    Q_OBJECT
public:
    explicit ExtrudeCommand(QObject* parent = nullptr);
    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    void cleanup() override;

private:
    void handleHeightInput(const QString& input);
    QString m_sketchId;
};

} // namespace command
} // namespace aicad

#endif
