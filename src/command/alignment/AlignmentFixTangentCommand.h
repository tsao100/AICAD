#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"
#include "command/CommandTypes.h"
#include "railway/AlignmentDocument.h"
#include <QVector2D>

namespace aicad {
namespace command {

/**
 * ALIGNMENTFIXTANGENT (alias: FT)
 *
 * Continuous mode (mirrors LineCommand):
 *   click  → set start point, rubber band anchors
 *   click  → addFixedTangent(p1, p2) + solve(); p2 becomes new p1
 *   right-click (POINT_CANCELLED) → finish
 */
class AlignmentFixTangentCommand : public Command
{
    Q_OBJECT

public:
    explicit AlignmentFixTangentCommand(QObject* parent = nullptr);
    ~AlignmentFixTangentCommand() override = default;

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    void handlePointAcquired(const QVector2D& point);
    void handleCancelled();
    void cleanup() override;

    // model
    railway::AlignmentDocument* m_alignDoc = nullptr;

    // interaction state
    QVector2D m_startPoint;
    bool      m_hasStartPoint = false;
    bool      m_isFinishing   = false;
};

REGISTER_COMMAND("alignmentfixtangent", AlignmentFixTangentCommand);

} // namespace command
} // namespace aicad
