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
#include "cad/FeatureGroup.h"
#include <QDebug>
#include <QApplication>
#include <QInputDialog>
#include <QStringList>

using namespace aicad::core;

namespace aicad {
namespace command {

ExtrudeCommand::ExtrudeCommand(QObject* parent)
    : Command("extrude", "Extrude a sketch profile", parent)
{}

ExtrudeCommand::ExtrudeCommand(const QString& name, const QString& description, int operation,
                               QObject* parent)
    : Command(name, description, parent), m_operation(operation)
{}

QVariantMap ExtrudeCommand::makeCreateData(double height) const
{
    QVariantMap data;
    data["sketchId"]  = m_sketchId;
    data["height"]    = height;
    data["operation"] = m_operation;
    data["targetId"]  = m_targetId;
    return data;
}

bool ExtrudeCommand::pickTarget(QString* err)
{
    m_targetId.clear();
    if (m_operation == 0) return true;

    cad::Document* doc = Application::instance()->documentManager()->currentDocument();
    const QList<cad::Feature*> cands = doc ? doc->solidFeatureCandidates() : QList<cad::Feature*>();
    if (cands.isEmpty()) {
        if (err) *err = QStringLiteral("文件裡沒有可以挖除／運算的實體");
        return false;
    }
    if (cands.size() == 1) {            // 只有一個就直接用，不多問
        m_targetId = cands.first()->id();
        return true;
    }
    QStringList names;
    for (cad::Feature* f : cands) names << f->name();
    bool ok = false;
    const QString chosen = QInputDialog::getItem(QApplication::activeWindow(),
                               QStringLiteral("目標實體"), QStringLiteral("對哪個實體運算？"),
                               names, 0, false, &ok);
    if (!ok) {
        if (err) *err = QStringLiteral("已取消");
        return false;
    }
    m_targetId = cands.at(names.indexOf(chosen))->id();
    return true;
}

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

    {
        QString perr;
        if (!pickTarget(&perr))
            return CommandResult::Failure(perr);
    }

    // 非互動模式
    if (!context.args.isEmpty()) {
        bool ok;
        double height = context.args[0].toDouble(&ok);
        if (!ok || height <= 0.0)
            return CommandResult::Failure("Invalid height value");
        bus->publish("command.create-extrude", makeCreateData(height));
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
    bus->publish("command.create-extrude", makeCreateData(10.0));   // 初始預覽高度

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

bool ExtrudeCommand::applyTargetKeyword(const QString& text)
{
    cad::Document* doc = Application::instance()->documentManager()->currentDocument();
    if (!doc || !m_extrude) return false;

    const QString t = text.trimmed();
    cad::FeatureGroup g;
    if (t.compare(QStringLiteral("P"), Qt::CaseInsensitive) == 0 || t.compare(QStringLiteral("Part"), Qt::CaseInsensitive) == 0)
        g = cad::FeatureGroup::Part;
    else if (t.compare(QStringLiteral("A"), Qt::CaseInsensitive) == 0 || t.compare(QStringLiteral("Assembly"), Qt::CaseInsensitive) == 0)
        g = cad::FeatureGroup::Assembly;
    else if (t.compare(QStringLiteral("V"), Qt::CaseInsensitive) == 0 || t.compare(QStringLiteral("Void"), Qt::CaseInsensitive) == 0)
        g = cad::FeatureGroup::Void;
    else
        return false;

    doc->setFeatureGroup(m_extrude, g);       // 立即生效（Void 以半透明紅色顯示）
    doc->setActiveGroup(g);                   // 接下來新建的擠出也放進這個根
    Application::instance()->eventBus()->publish(
        Events::COMMAND_LOG,
        QString("Extrude 放入 %1（之後新建的特徵也會放進 %1）").arg(cad::Document::groupLabel(g)));
    showTargetPrompt();
    return true;
}

void ExtrudeCommand::showTargetPrompt()
{
    const QString cur = m_extrude ? cad::Document::groupLabel(m_extrude->group()) : QStringLiteral("Part");
    if (auto* clm = core::CommandLineManager::instance())
        clm->showPrompt(QString("拖曳箭頭調整高度，或輸入數值後按 Enter 確認（Shift+點翻轉箭頭=對稱）。"
                                "放入 [Part(P)/Assembly(A)/Void(V)]（目前：%1）").arg(cur));
}

void ExtrudeCommand::handleHeightInput(const QString& input)
{
    Application* app = Application::instance();
    if (applyTargetKeyword(input)) return;          // 輸入 P／A／V：只切換放入的根，繼續等高度
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
        app->eventBus()->publish("command.create-extrude", makeCreateData(height));
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

    m_extrude = extrude;
    m_manipulator = new manipulator::ExtrudeManipulator(extrude, cadView, this);
    m_manipulator->show();

    // 提示列的 [Part(P)/Assembly(A)/Void(V)] 會變成可點選的選項：新建擠出時直接決定放進哪個根
    showTargetPrompt();
    app->eventBus()->subscribe(Events::OPTION_SELECTED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v]() { applyTargetKeyword(v.toString()); },
                                      Qt::QueuedConnection);
        });

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
REGISTER_COMMAND("extrudecut", ExtrudeCutCommand);

} // namespace command
} // namespace aicad
