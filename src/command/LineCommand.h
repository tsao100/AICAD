#ifndef AICAD_COMMAND_LINECOMMAND_H
#define AICAD_COMMAND_LINECOMMAND_H

#include "Command.h"
#include "command/CommandFactory.h"
#include "cad/Sketch.h"
#include <QVector2D>

namespace aicad {
namespace command {

class LineCommand : public Command {
    Q_OBJECT  // ✅ MOC will process this header

public:
    explicit LineCommand(QObject* parent = nullptr);
    ~LineCommand() override;

    void setRole(cad::GeomRole role) { m_role = role; }

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
    cad::GeomRole m_role = cad::GeomRole::Normal;
};

REGISTER_COMMAND("line", LineCommand);

// construction-line / centerline 用匿名 lambda 直接追加，繞過 macro 變數名稱衝突
namespace { const bool _reg_LineCommand_extra = []() {
    command::CommandFactory::registerCreator("construction-line",
                                             []() -> command::Command* {
                                                 auto* cmd = new command::LineCommand();
                                                 cmd->setRole(cad::GeomRole::Construction);
                                                 return cmd;
                                             });
    command::CommandFactory::registerCreator("centerline",
                                             []() -> command::Command* {
                                                 auto* cmd = new command::LineCommand();
                                                 cmd->setRole(cad::GeomRole::Centerline);
                                                 return cmd;
                                             });
    return true;
}(); }

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_LINECOMMAND_H
