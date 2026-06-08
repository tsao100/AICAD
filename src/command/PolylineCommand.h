#ifndef AICAD_COMMAND_POLYLINECOMMAND_H
#define AICAD_COMMAND_POLYLINECOMMAND_H

#include "Command.h"
#include "command/CommandFactory.h"
#include <QVector>
#include <QVector2D>

namespace aicad {
namespace command {

class PolylineCommand : public Command {
    Q_OBJECT

public:
    explicit PolylineCommand(QObject* parent = nullptr);
    ~PolylineCommand() override;

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    void handlePointAcquired(QVector2D point);
    void handleCancelled();
    void cleanup() override;

    // Commits the collected vertices as a polyline (requires >= 2 points).
    void finalise();

    QVector<QVector2D> m_points;  // all collected vertices in order
    bool m_isFinishing;
};

REGISTER_COMMAND("polyline", PolylineCommand);

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_POLYLINECOMMAND_H
