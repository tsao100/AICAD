#ifndef AICAD_COMMAND_RectCOMMAND_H
#define AICAD_COMMAND_RectCOMMAND_H

#include "Command.h"
#include "command/CommandFactory.h"
#include <QVector2D>

namespace aicad {
namespace command {

class RectCommand : public Command {
    Q_OBJECT  // ✅ MOC will process this header

public:
    explicit RectCommand(QObject* parent = nullptr);
    ~RectCommand() override;

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    // ✅ Renamed methods (not slots - using EventBus)
    void handlePointAcquired(QVector2D point);
    void handleCancelled();
    void cleanup() override;

    QVector2D m_startPoint;
    bool m_hasStartPoint;
    bool m_isFinishing;
};

REGISTER_COMMAND("rect", RectCommand);

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_RectCOMMAND_H
