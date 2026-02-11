#ifndef AICAD_COMMAND_RECTCOMMAND_H
#define AICAD_COMMAND_RECTCOMMAND_H

#include "Command.h"
#include "command/CommandFactory.h"
#include <QVector2D>

namespace aicad {
namespace command {

class RectCommand : public Command {
    Q_OBJECT

public:
    explicit RectCommand(QObject* parent = nullptr);
    ~RectCommand() override;

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    void handlePointAcquired(QVector2D point);
    void handleCancelled();
    void cleanup() override;

    QVector2D m_firstCorner;   // 第一個對角點
    bool m_hasFirstCorner;
    bool m_isFinishing;
};

REGISTER_COMMAND("rect", RectCommand);

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_RECTCOMMAND_H
