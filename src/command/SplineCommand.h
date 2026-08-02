#ifndef AICAD_COMMAND_SPLINECOMMAND_H
#define AICAD_COMMAND_SPLINECOMMAND_H

#include "Command.h"
#include "command/CommandFactory.h"
#include <QVector2D>
#include <QVector>

namespace aicad {
namespace command {

class SplineCommand : public Command {
    Q_OBJECT  // ✅ MOC will process this header

public:
    explicit SplineCommand(QObject* parent = nullptr);
    ~SplineCommand() override;

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    // ✅ Renamed methods (not slots - using EventBus)
    void handlePointAcquired(QVector2D point);
    void handleCancelled();
    void cleanup() override;

    QVector<QVector2D> m_controlPoints;  // 存儲所有控制點
    bool m_isFinishing;

    static constexpr int MIN_POINTS = 3;  // 最少需要3個點
};

REGISTER_COMMAND("spline", SplineCommand);

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_SPLINECOMMAND_H
