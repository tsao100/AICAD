#pragma once

/**
 * @file SetOriginCommand.h
 * @brief SETORIGIN (alias: SO) — 設定 TM2 二度分帶座標原點
 *
 * 設計：模仿 AlignmentSCSCommand，只用 NUMBER_INPUT 接收所有文字輸入，
 * 每步結束呼叫 waitForInput(Number)，避免 CommandLineManager 的
 * m_expectedInputType 被提前清除的問題。
 */

#include "command/Command.h"
#include "command/CommandFactory.h"
#include "command/CommandTypes.h"
#include <QPointF>

namespace aicad {
namespace core { class EventBus; }
namespace command {

class SetOriginCommand : public Command
{
    Q_OBJECT

public:
    explicit SetOriginCommand(QObject* parent = nullptr);
    ~SetOriginCommand() override = default;

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    enum class Step {
        WaitingForEasting,
        WaitingForNorthing,
        WaitingForModelPoint
    };

    void handleNumberInput(const QString& text);
    void applyOffset(const QPointF& modelPt);
    void promptModelPoint(core::EventBus* bus);
    void cleanup() override;

    Step   m_step        = Step::WaitingForEasting;
    double m_easting     = 0.0;
    double m_northing    = 0.0;
    bool   m_isFinishing = false;
};

REGISTER_COMMAND("setorigin", SetOriginCommand);

} // namespace command
} // namespace aicad
