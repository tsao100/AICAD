#include "ExtrudeCommand.h"
#include "CommandFactory.h"
#include "core/Application.h"
#include "core/DocumentManager.h"
#include "core/EventBus.h"
#include "core/CommandLineManager.h"
#include "cad/Document.h"
#include "cad/Sketch.h"
#include "cad/Extrude.h"
#include <QDebug>

using namespace aicad::core;

namespace aicad {
namespace command {

ExtrudeCommand::ExtrudeCommand(QObject* parent)
    : Command("extrude", "Extrude a sketch profile", parent)
{}

CommandResult ExtrudeCommand::execute(const CommandContext& context) {
    Application* app = Application::instance();
    EventBus* bus = app->eventBus();
    cad::Document* doc = app->documentManager()->currentDocument();

    if (!doc) {
        return CommandResult::Failure("No active document");
    }

    // 取得目前活動的 Sketch
    cad::Sketch* sketch = app->activeSketch();
    if (!sketch) {
        return CommandResult::Failure(
            "No active sketch. Please create and activate a sketch first.");
    }
    m_sketchId = sketch->id();

    // 若命令列已傳入 height 參數（非互動模式）
    if (!context.args.isEmpty()) {
        bool ok;
        double height = context.args[0].toDouble(&ok);
        if (!ok || height <= 0.0) {
            return CommandResult::Failure("Invalid height value");
        }
        // 直接發事件，讓 UIManager 側處理 extrude 建立
        QVariantMap data;
        data["sketchId"] = m_sketchId;
        data["height"]   = height;
        bus->publish("command.create-extrude", data);

        complete(CommandResult::Success(
            QString("Extrude created with height %1").arg(height)));
        return CommandResult::Success();
    }

    // 互動模式：等待使用者輸入高度
    setState(CommandState::Running);
    bus->publish(Events::COMMAND_PROMPT, "Enter extrude height:");
    bus->publish(Events::COMMAND_LOG,  "Enter extrude height:");

    // 透過 CommandLineManager 宣告等待數字輸入
    CommandLineManager::instance()->waitForInput(InputType::Number);

    // 訂閱 NUMBER_INPUT 事件
    bus->subscribe(Events::NUMBER_INPUT, this,
                   [this](const QVariant& data) {
                       QMetaObject::invokeMethod(this, [this, data]() {
                           this->handleHeightInput(data.toString());
                       }, Qt::QueuedConnection);
                   });

    // 訂閱取消事件
    bus->subscribe(Events::POINT_CANCELLED, this,
                   [this](const QVariant&) {
                       QMetaObject::invokeMethod(this, [this]() {
                           complete(CommandResult::Failure("Extrude cancelled"));
                       }, Qt::QueuedConnection);
                   });

    return CommandResult::Success("Waiting for height input");
}

void ExtrudeCommand::handleHeightInput(const QString& input) {
    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    bool ok;
    double height = input.toDouble(&ok);
    if (!ok || height <= 0.0) {
        bus->publish(Events::COMMAND_LOG, "Invalid height. Please enter a positive number:");
        CommandLineManager::instance()->waitForInput(InputType::Number);
        return;
    }

    // 發事件給 UIManager 執行實際建立
    QVariantMap data;
    data["sketchId"] = m_sketchId;
    data["height"]   = height;
    bus->publish("command.create-extrude", data);

    complete(CommandResult::Success(
        QString("Extrude created with height %1").arg(height)));
}

void ExtrudeCommand::cleanup() {
    Application::instance()->eventBus()->unsubscribeAll(this);
}

// 靜態自動註冊
REGISTER_COMMAND("extrude", ExtrudeCommand);

} // namespace command
} // namespace aicad
