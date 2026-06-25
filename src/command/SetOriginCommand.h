/**
 * @file SetOriginCommand.h
 * @brief SETORIGIN 命令 — 設定 TM2 參考座標原點
 *
 * 使用者輸入東向（Easting）與北向（Northing）後，
 * 發布 "command.set-coordinate-origin" 事件，
 * UIManager 訂閱後呼叫 CadView::setCoordinateOffset(e, n)
 * 以更新 ViewGrid / OSnapManager 的座標偏移。
 *
 * 互動流程：
 *   SETORIGIN
 *   > 輸入 TM2 東向（Easting）參考座標，如 250000：
 *   > 輸入 TM2 北向（Northing）參考座標，如 2650000：
 *   > [訊息] 座標原點已設為 E=250000, N=2650000
 */

#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"
#include "command/CommandTypes.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "core/CommandLineManager.h"

#include <QString>

namespace aicad {
namespace command {

class SetOriginCommand : public Command
{
    Q_OBJECT

public:
    explicit SetOriginCommand(QObject* parent = nullptr)
        : Command("SETORIGIN", "設定 TM2 參考座標原點", parent)
    {}

    CommandResult execute(const CommandContext& /*ctx*/) override
    {
        m_state   = State::WaitEasting;
        m_easting = 0.0;

        auto* bus = core::Application::instance()->eventBus();
        bus->subscribe(core::Events::NUMBER_INPUT, this,
                       [this](const QVariant& data) {
                           QMetaObject::invokeMethod(this, [this, data]() {
                               handleNumberInput(data.toString());
                           }, Qt::QueuedConnection);
                       });

        bus->subscribe(core::Events::POINT_CANCELLED, this,
                       [this](const QVariant&) {
                           QMetaObject::invokeMethod(this, [this]() {
                               cleanup();
                               Q_EMIT finished(CommandResult::Failure("SETORIGIN cancelled"));
                           }, Qt::QueuedConnection);
                       });

        setState(CommandState::Running);

        auto* clm = core::CommandLineManager::instance();
        bus->publish(core::Events::COMMAND_PROMPT,
                     tr("輸入 TM2 東向（Easting）參考座標，如 250000："));
        clm->waitForInput(core::InputType::Number);

        return CommandResult::Success();
    }

    void cleanup() override
    {
        auto* bus = core::Application::instance()->eventBus();
        bus->unsubscribeAll(this);
    }

private:
    enum class State { WaitEasting, WaitNorthing } m_state = State::WaitEasting;
    double m_easting = 0.0;

    void handleNumberInput(const QString& text)
    {
        bool ok;
        double val = text.trimmed().toDouble(&ok);
        auto* bus  = core::Application::instance()->eventBus();
        auto* clm  = core::CommandLineManager::instance();

        if (!ok) {
            bus->publish(core::Events::COMMAND_PROMPT,
                         tr("輸入格式錯誤，請輸入數字。"));
            clm->waitForInput(core::InputType::Number);
            return;
        }

        if (m_state == State::WaitEasting) {
            m_easting = val;
            m_state   = State::WaitNorthing;
            bus->publish(core::Events::COMMAND_PROMPT,
                         tr("輸入 TM2 北向（Northing）參考座標，如 2650000："));
            clm->waitForInput(core::InputType::Number);
        } else {
            // 第二個數字 = Northing，送出並結束
            double northing = val;

            QVariantMap data;
            data["easting"]  = m_easting;
            data["northing"] = northing;
            bus->publish("command.set-coordinate-origin", data);

            QString msg = QString("座標原點已設為 E=%1, N=%2")
                              .arg(m_easting,  0, 'f', 3)
                              .arg(northing, 0, 'f', 3);
            bus->publish(core::Events::COMMAND_PROMPT, msg);

            cleanup();
            Q_EMIT finished(CommandResult::Success(msg));
        }
    }
};

// 靜態自動登記：CommandAlias "SO" → "SETORIGIN" → SetOriginCommand
REGISTER_COMMAND("setorigin", SetOriginCommand);

} // namespace command
} // namespace aicad

