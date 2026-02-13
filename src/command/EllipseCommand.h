#ifndef AICAD_COMMAND_ELLIPSECOMMAND_H
#define AICAD_COMMAND_ELLIPSECOMMAND_H

#include "Command.h"
#include "command/CommandFactory.h"
#include <QVector2D>

namespace aicad {
namespace command {

class EllipseCommand : public Command {
    Q_OBJECT  // ✅ MOC will process this header

public:
    explicit EllipseCommand(QObject* parent = nullptr);
    ~EllipseCommand() override;

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    // ✅ Event handlers (using EventBus)
    void handlePointAcquired(QVector2D point);
    void handleCancelled();
    void cleanup() override;

    enum class InputState {
        WaitingForCenter,
        WaitingForMajorAxis,
        WaitingForMinorAxis
    };

    InputState m_inputState;
    QVector2D m_center;
    QVector2D m_majorAxisEnd;
    bool m_isFinishing;
};

REGISTER_COMMAND("ellipse", EllipseCommand);

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_ELLIPSECOMMAND_H
