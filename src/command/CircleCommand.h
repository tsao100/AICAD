#ifndef AICAD_COMMAND_CIRCLECOMMAND_H
#define AICAD_COMMAND_CIRCLECOMMAND_H

#include "Command.h"
#include "command/CommandFactory.h"
#include <QVector2D>

namespace aicad {
namespace command {

class CircleCommand : public Command {
    Q_OBJECT

public:
    explicit CircleCommand(QObject* parent = nullptr);
    ~CircleCommand() override;

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    void handlePointAcquired(QVector2D point);
    void handleCancelled();
    void cleanup() override;

    QVector2D m_centerPoint;
    bool m_hasCenterPoint;
    bool m_isFinishing;
};

REGISTER_COMMAND("circle", CircleCommand);

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_CIRCLECOMMAND_H
