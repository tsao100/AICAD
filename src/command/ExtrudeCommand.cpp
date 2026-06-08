#include "ExtrudeCommand.h"
#include "CommandFactory.h"
#include "core/Application.h"
#include "core/DocumentManager.h"
#include "core/EventBus.h"
#include "core/CommandLineManager.h"
#include "ui/UIManager.h"
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

    // 非互動模式
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

    // ✅ 修正：先做所有前置檢查，確認可以繼續，再 setState(Running)
    app->clearSelectedRegion();

    auto regions = sketch->detectRegions();
    // ← 修正：圓 / arc+line 可能 detectRegions 為空但仍可擠出
    if (regions.isEmpty() && !sketch->hasExtrudableProfile())
        return CommandResult::Failure("Sketch has no closed region to extrude");

    setState(CommandState::Running);

    if (regions.size() == 1 || (regions.isEmpty() && sketch->hasExtrudableProfile())) {
        // 單一 region 或直接可擠出的輪廓（圓等）
        if (!regions.isEmpty())
            app->setSelectedRegion(regions.first());
        bus->publish(Events::COMMAND_PROMPT,
                     "Profile found. Enter extrude height:");
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

void ExtrudeCommand::promptForHeight()
{
    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    bus->publish("command.clear-sketch-regions", QVariant());

    // ── 先用預設高度建立 Extrude（0 height = placeholder）──────────
    QVariantMap createData;
    createData["sketchId"] = m_sketchId;
    createData["height"]   = 10.0;  // 初始預覽高度
    bus->publish("command.create-extrude", createData);

    // 等 Document 建立 Extrude 後，取得最新的 feature
    cad::Document* doc = app->documentManager()->currentDocument();
    if (!doc) {
        complete(CommandResult::Failure("No document"));
        return;
    }

    cad::Extrude* extrude = doc->lastExtrude();   // 需在 Document 新增此方法
    if (!extrude) {
        complete(CommandResult::Failure("Failed to create extrude"));
        return;
    }

    launchManipulator(extrude);
}

void ExtrudeCommand::handleHeightInput(const QString& input)
{
    Application* app = Application::instance();
    bool ok;
    double height = input.toDouble(&ok);
    if (!ok || height <= 0.0) {
        app->eventBus()->publish(Events::COMMAND_LOG,
                                 "Invalid height. Enter a positive number:");
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        return;
    }

    // 如果 Manipulator 存在，同步高度後直接 complete
    if (m_manipulator) {
        // Extrude 的 height 已由 Manipulator 即時更新，這裡只做最終確認
        m_manipulator->hide();
    } else {
        // 降級文字輸入路徑（同原本邏輯）
        QVariantMap data;
        data["sketchId"] = m_sketchId;
        data["height"]   = height;
        app->eventBus()->publish("command.create-extrude", data);
    }

    app->clearSelectedRegion();
    complete(CommandResult::Success(
        QString("Extrude height: %1 mm").arg(height)));
}

void ExtrudeCommand::launchManipulator(cad::Extrude* extrude)
{
    Application* app = Application::instance();

    ui::UIManager* uiMgr = app->uiManager();
    view::CadView* cadView = uiMgr->cadView();

    if (!cadView) {
        // fallback：降級為純文字輸入
        app->eventBus()->publish(Events::COMMAND_PROMPT, "Enter extrude height:");
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        app->eventBus()->subscribe(Events::NUMBER_INPUT, this,
                                   [this](const QVariant& data) {
                                       QMetaObject::invokeMethod(this, [this, data]() {
                                           handleHeightInput(data.toString());
                                       }, Qt::QueuedConnection);
                                   });
        return;
    }

    setState(CommandState::Running);

    m_manipulator = new manipulator::ExtrudeManipulator(extrude, cadView, this);
    m_manipulator->show();

    app->eventBus()->publish(Events::COMMAND_PROMPT,
                             "拖曳箭頭調整高度，或輸入數值後按 Enter 確認（Shift+點翻轉箭頭=對稱）");

    // height confirmed → complete command
    connect(m_manipulator, &manipulator::ExtrudeManipulator::heightConfirmed,
            this, [this, extrude](double h) {
                core::Application::instance()->eventBus()->publish(
                    Events::COMMAND_LOG,
                    QString("Extrude height set to %1 mm").arg(h, 0, 'f', 2));
                // 按 Enter 才真正 complete
            });

    // 監聽 Enter（從 CommandLine）確認完成
    core::Application::instance()->eventBus()->subscribe(
        Events::NUMBER_INPUT, this,
        [this](const QVariant& data) {
            QMetaObject::invokeMethod(this, [this, data]() {
                handleHeightInput(data.toString());
            }, Qt::QueuedConnection);
        });

    connect(m_manipulator, &manipulator::ExtrudeManipulator::cancelled,
            this, [this]() {
                QMetaObject::invokeMethod(this, [this]() {
                    complete(CommandResult::Failure("Extrude cancelled"));
                }, Qt::QueuedConnection);
            });
}

void ExtrudeCommand::cleanup() {
    if (m_manipulator) {
        m_manipulator->hide();
        m_manipulator->deleteLater();
        m_manipulator = nullptr;
    }
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
