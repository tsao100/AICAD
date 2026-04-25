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

    if (!doc)
        return CommandResult::Failure("No active document");

    cad::Sketch* sketch = app->activeSketch();
    if (!sketch)
        return CommandResult::Failure(
            "No active sketch. Please create and activate a sketch first.");

    m_sketchId = sketch->id();

    // 非互動模式（命令列直接帶高度參數）
    if (!context.args.isEmpty()) {
        bool ok;
        double height = context.args[0].toDouble(&ok);
        if (!ok || height <= 0.0)
            return CommandResult::Failure("Invalid height value");

        QVariantMap data;
        data["sketchId"] = m_sketchId;
        data["height"]   = height;
        bus->publish("command.create-extrude", data);
        complete(CommandResult::Success(
            QString("Extrude created with height %1").arg(height)));
        return CommandResult::Success();
    }

    // 互動模式
    setState(CommandState::Running);
    app->clearSelectedRegion();   // ← 確保舊的 region 選取被清除

    auto regions = sketch->detectRegions();
    if (regions.isEmpty())
        return CommandResult::Failure("Sketch has no closed region to extrude");

    // 通知 CadView 顯示所有可選 region（半透明預覽）
    // UIManager 訂閱此事件後呼叫 cadView->displaySketchRegions()
    QVariantMap regionData;
    regionData["sketchId"] = m_sketchId;
    bus->publish("command.show-sketch-regions", regionData);

    if (regions.size() == 1) {
        // 只有一個區域，直接選取並等待使用者確認（或自動進入高度輸入）
        app->setSelectedRegion(regions.first());
        bus->publish(Events::COMMAND_PROMPT,
                     "One region found, auto-selected. Enter extrude height:");
        bus->publish(Events::COMMAND_LOG,
                     "One region found, auto-selected. Enter extrude height:");
        promptForHeight();
    } else {
        promptForRegion(regions.size());
    }

    return CommandResult::Success("Waiting for input");
}

void ExtrudeCommand::promptForRegion(int regionCount) {
    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    bus->publish(Events::COMMAND_PROMPT,
                 QString("Found %1 regions. Click inside a region to select it:")
                     .arg(regionCount));
    bus->publish(Events::COMMAND_LOG,
                 QString("Found %1 regions. Click inside a region to select it:")
                     .arg(regionCount));

    // CadView 的 mousePressEvent 在 regionAisMap 非空時：
    //   1. 呼叫 highlightSketchRegion()
    //   2. emit sketchRegionPicked(region)
    // UIManager 的 connect 已將 sketchRegionPicked → app->setSelectedRegion()
    //
    // 所以 ExtrudeCommand 只需監聽 Application::selectedRegionChanged
    connect(app, &Application::selectedRegionChanged,
            this, [this]() {
                // 確認 region 已選取才繼續
                if (!core::Application::instance()->selectedRegion())
                    return;
                // 斷開此連接，避免重複觸發
                disconnect(core::Application::instance(),
                           &Application::selectedRegionChanged,
                           this, nullptr);
                promptForHeight();
            });

    // 取消事件
    EventBus* bus2 = app->eventBus();
    bus2->subscribe(Events::POINT_CANCELLED, this,
                    [this](const QVariant&) {
                        QMetaObject::invokeMethod(this, [this]() {
                            complete(CommandResult::Failure("Extrude cancelled"));
                        }, Qt::QueuedConnection);
                    });
}

void ExtrudeCommand::promptForHeight() {
    EventBus* bus = core::Application::instance()->eventBus();

    // 取消 region 的視覺顯示，進入高度輸入階段
    bus->publish("command.clear-sketch-regions", QVariant());

    bus->publish(Events::COMMAND_PROMPT, "Enter extrude height:");
    bus->publish(Events::COMMAND_LOG,    "Enter extrude height:");

    CommandLineManager::instance()->waitForInput(InputType::Number);

    bus->subscribe(Events::NUMBER_INPUT, this,
                   [this](const QVariant& data) {
                       QMetaObject::invokeMethod(this, [this, data]() {
                           this->handleHeightInput(data.toString());
                       }, Qt::QueuedConnection);
                   });

    // POINT_CANCELLED 可能已訂閱（多 region 路徑），subscribe 重複無害；
    // 單 region 路徑在此補上
    bus->subscribe(Events::POINT_CANCELLED, this,
                   [this](const QVariant&) {
                       QMetaObject::invokeMethod(this, [this]() {
                           complete(CommandResult::Failure("Extrude cancelled"));
                       }, Qt::QueuedConnection);
                   });
}

void ExtrudeCommand::handleHeightInput(const QString& input) {
    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    bool ok;
    double height = input.toDouble(&ok);
    if (!ok || height <= 0.0) {
        bus->publish(Events::COMMAND_LOG,
                     "Invalid height. Please enter a positive number:");
        CommandLineManager::instance()->waitForInput(InputType::Number);
        return;
    }

    QVariantMap data;
    data["sketchId"] = m_sketchId;
    data["height"]   = height;
    bus->publish("command.create-extrude", data);

    app->clearSelectedRegion();

    complete(CommandResult::Success(
        QString("Extrude created with height %1").arg(height)));
}

void ExtrudeCommand::cleanup() {
    // 清除 region 視覺顯示
    core::Application::instance()->eventBus()
        ->publish("command.clear-sketch-regions", QVariant());
    // 清除 selectedRegion 狀態
    core::Application::instance()->clearSelectedRegion();
    // 取消所有 EventBus 訂閱
    core::Application::instance()->eventBus()->unsubscribeAll(this);
    // 取消 Qt signal 連接（promptForRegion 中的 connect）
    disconnect(core::Application::instance(), nullptr, this, nullptr);
}

REGISTER_COMMAND("extrude", ExtrudeCommand);

} // namespace command
} // namespace aicad
