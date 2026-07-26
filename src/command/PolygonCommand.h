#ifndef AICAD_COMMAND_POLYGONCOMMAND_H
#define AICAD_COMMAND_POLYGONCOMMAND_H

#include "Command.h"
#include "command/CommandFactory.h"
#include <QVector2D>

namespace aicad {
namespace command {

class PolygonCommand : public Command {
    Q_OBJECT  // ✅ MOC will process this header

public:
    explicit PolygonCommand(QObject* parent = nullptr);
    ~PolygonCommand() override;

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    // ✅ Event handlers (not slots - using EventBus)
    void handlePointAcquired(QVector2D point);
    void handleCancelled();
    void cleanup() override;

    // Helper method to create polygon geometry
    void createPolygon(const QVector2D& center, double radius, int sides);

    QVector2D m_centerPoint;
    bool m_hasCenterPoint;
    int m_sides;
    bool m_isFinishing;
};

REGISTER_COMMAND("polygon", PolygonCommand);

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_POLYGONCOMMAND_H
