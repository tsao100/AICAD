#ifndef AICAD_COMMAND_LINECOMMAND_H
#define AICAD_COMMAND_LINECOMMAND_H

#include "Command.h"
#include "command/CommandFactory.h"
#include "view/CadView.h"
#include "cad/Plane.h"
#include "view/RubberBand.h"
#include <QVector2D>

namespace aicad {
namespace command {

class LineCommand : public Command {
    Q_OBJECT  // ✅ MOC will process this header
    
public:
    explicit LineCommand(QObject* parent = nullptr);
    ~LineCommand() override;
    
    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;
    void cleanup(view::CadView* cadView, view::RubberBand* rubber);

private:
    // ✅ Renamed methods (not slots - using EventBus)
    void handlePointAcquired(QVector2D point);
    void handleCancelled();
    void cleanup() override;
    CommandResult createLine(double x1, double y1, double x2, double y2);

    view::CustomPlane convertPlane(const cad::Plane& plane);

    QVector2D m_startPoint;
    bool m_hasStartPoint;
    bool m_isFinishing;
};

REGISTER_COMMAND("line", LineCommand);

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_LINECOMMAND_H
