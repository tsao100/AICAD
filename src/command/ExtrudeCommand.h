#ifndef AICAD_COMMAND_EXTRUDECOMMAND_H
#define AICAD_COMMAND_EXTRUDECOMMAND_H

#include "Command.h"
#include "cad/Sketch.h"
#include "manipulator/ExtrudeManipulator.h"


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
    void promptForRegion(int regionCount);   // ← 移除 sketch* 參數（改用 Qt signal）
    void promptForHeight();
    void handleHeightInput(const QString& input);
    void launchManipulator(cad::Extrude* extrude);

    QString m_sketchId;
    manipulator::ExtrudeManipulator* m_manipulator = nullptr;
};

} // namespace command
} // namespace aicad

#endif
