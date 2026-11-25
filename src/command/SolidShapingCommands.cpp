/**
 * @file SolidShapingCommands.cpp
 */
#include "SolidShapingCommands.h"
#include "CommandFactory.h"

#include "core/Application.h"
#include "core/DocumentManager.h"
#include "core/EventBus.h"
#include "core/CommandLineManager.h"
#include "cad/Document.h"
#include "cad/Sketch.h"
#include "cad/SolidModifier.h"
#include "cad/SolidOps.h"
#include "ui/SolidFeatureDialogs.h"

#include <QApplication>
#include <QMap>
#include <QVariantMap>

using namespace aicad::core;

namespace aicad {
namespace command {

namespace {
cad::SolidOp opFromInt(int v) { return static_cast<cad::SolidOp>(v); }
}

// ═════════════════════════ 對話框型指令 ═════════════════════════

SolidDialogCommand::SolidDialogCommand(const QString& name, const QString& description, QObject* parent)
    : Command(name, description, parent)
{
}

CommandResult SolidDialogCommand::execute(const CommandContext&)
{
    Application* app = Application::instance();
    cad::Document* doc = app->documentManager()->currentDocument();
    if (!doc)
        return CommandResult::Failure("No active document");

    cad::Sketch* active = app->activeSketch();
    const bool created = run(doc, active, QApplication::activeWindow());

    if (!created) {
        complete(CommandResult::Success("Cancelled"));
        return CommandResult::Success("Cancelled");
    }

    // 與擠出一致：建立完成後離開草圖編輯、回到 3D 等角視圖
    if (active) app->setActiveSketch(nullptr);
    QVariantMap viewData;
    viewData["mode"] = "iso";
    app->eventBus()->publish("command.request-view-setup", viewData);

    complete(CommandResult::Success(QString("%1 created").arg(name())));
    return CommandResult::Success();
}

bool RevolveCommand::run(cad::Document* doc, cad::Sketch* sk, QWidget* parent)
{
    return ui::runRevolveDialog(doc, sk, opFromInt(m_op), parent);
}

bool SweepCommand::run(cad::Document* doc, cad::Sketch* sk, QWidget* parent)
{
    return ui::runSweepDialog(doc, sk, opFromInt(m_op), parent);
}

bool LoftCommand::run(cad::Document* doc, cad::Sketch*, QWidget* parent)
{
    return ui::runLoftDialog(doc, opFromInt(m_op), parent);
}

bool PatternCommand::run(cad::Document* doc, cad::Sketch*, QWidget* parent)
{
    return ui::runPatternDialog(doc, parent);
}

bool Mirror3DCommand::run(cad::Document* doc, cad::Sketch*, QWidget* parent)
{
    return ui::runMirrorDialog(doc, parent);
}

bool HoleCommand::run(cad::Document* doc, cad::Sketch* sk, QWidget* parent)
{
    return ui::runHoleDialog(doc, sk, parent);
}

bool BooleanCommand::run(cad::Document* doc, cad::Sketch*, QWidget* parent)
{
    return ui::runBooleanDialog(doc, parent);
}

// ═════════════════════════ 選邊／選面型指令 ═════════════════════════

PickSubshapeCommand::PickSubshapeCommand(const QString& name, const QString& description,
                                         bool pickFaces, QObject* parent)
    : Command(name, description, parent), m_pickFaces(pickFaces)
{
}

int PickSubshapeCommand::pickedCount() const
{
    if (!m_ctx.cadView) return 0;
    return m_pickFaces ? m_ctx.cadView->pickedFaces().size()
                       : m_ctx.cadView->pickedEdges().size();
}

CommandResult PickSubshapeCommand::execute(const CommandContext& context)
{
    m_ctx = context;
    m_finishing = false;

    if (!context.cadView)
        return CommandResult::Failure("Internal error: no CadView in context.");

    Application* app = Application::instance();
    cad::Document* doc = app->documentManager()->currentDocument();
    if (!doc)
        return CommandResult::Failure("No active document.");
    if (app->activeSketch())
        return CommandResult::Failure("請先離開草圖編輯，再執行此指令。");

    m_prevMode = context.cadView->mode();
    m_modeChanged = true;
    if (m_pickFaces) context.cadView->beginFacePicking();
    else             context.cadView->beginEdgePicking();

    // 不用 UniqueConnection：lambda／成員函式指標連線在 Qt 裡 UniqueConnection 的行為不可靠，
    // 這裡每次 execute() 都是全新指令物件，只會連一次；cleanup() 會斷開。
    connect(context.cadView, &view::CadView::edgePicked, this, &PickSubshapeCommand::onPicked);

    // ⚠️ 先進 Running、再訂閱：訂閱後事件可能立刻到達
    setState(CommandState::Running);

    EventBus* bus = app->eventBus();
    bus->subscribe(Events::NUMBER_INPUT, this, [this](const QVariant& data) {
        const QString text = data.toString();
        QMetaObject::invokeMethod(this, [this, text]() { handleInput(text); }, Qt::QueuedConnection);
    });

    const QString prompt = promptText(0);
    outputMessage(prompt);
    bus->publish(Events::COMMAND_PROMPT, prompt);
    CommandLineManager::instance()->waitForInput(core::InputType::Number);

    return CommandResult::Success("Waiting for selection / value input");
}

void PickSubshapeCommand::onPicked()
{
    if (m_finishing || !m_ctx.cadView) return;
    const QString prompt = promptText(pickedCount());
    outputMessage(prompt);
    Application::instance()->eventBus()->publish(Events::COMMAND_PROMPT, prompt);
    // 點擊不經過 CommandLineManager，命令列的「等待輸入」狀態仍在，不必重新 waitForInput()
}

void PickSubshapeCommand::handleInput(const QString& text)
{
    if (m_finishing) return;

    bool ok = false;
    const double value = text.trimmed().toDouble(&ok);
    if (!ok || qAbs(value) < 1e-9) {
        const QString msg = QStringLiteral("無法辨識的數值，請輸入不為 0 的數字：");
        outputMessage(msg);
        Application::instance()->eventBus()->publish(Events::COMMAND_PROMPT, msg);
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        return;
    }

    cad::Document* doc = Application::instance()->documentManager()->currentDocument();
    if (!doc || pickedCount() == 0) {
        outputMessage(QStringLiteral("尚未選取任何%1 — 已取消。").arg(m_pickFaces ? "面" : "邊"));
        m_finishing = true;
        complete(CommandResult::Success("Cancelled: nothing selected"));
        return;
    }

    const bool ok2 = commit(doc, value);
    m_finishing = true;
    complete(ok2 ? CommandResult::Success("Completed")
                 : CommandResult::Failure("Operation failed"));
}

void PickSubshapeCommand::cleanup()
{
    Application::instance()->eventBus()->unsubscribeAll(this);

    if (m_ctx.cadView) {
        disconnect(m_ctx.cadView, &view::CadView::edgePicked, this, &PickSubshapeCommand::onPicked);
        if (m_pickFaces) m_ctx.cadView->endFacePicking();
        else             m_ctx.cadView->endEdgePicking();
        if (m_modeChanged) m_ctx.cadView->setMode(m_prevMode);
    }
    m_modeChanged = false;
    m_finishing = false;
}

// ── FILLET3D ──────────────────────────────────────────────────────────

QString Fillet3DCommand::promptText(int picked) const
{
    if (picked == 0)
        return QStringLiteral("FILLET3D — 點選要圓角的邊（再點一次取消選取），輸入半徑（正數）後按 Enter：");
    return QStringLiteral("已選取 %1 條邊 — 輸入圓角半徑後按 Enter：").arg(picked);
}

bool Fillet3DCommand::commit(cad::Document* doc, double radius)
{
    if (radius <= 0.0) {
        outputMessage(QStringLiteral("圓角半徑必須為正數。"));
        return false;
    }

    QMap<QString, QVector<cad::EdgeSignature>> byFeature;
    for (const view::PickedEdgeRef& ref : m_ctx.cadView->pickedEdges()) {
        if (ref.featureId.isEmpty() || ref.edge.IsNull()) continue;
        cad::Feature* src = doc->findFeature(ref.featureId);
        if (!src) continue;
        byFeature[ref.featureId].append(cad::ChamferSolid::makeSignature(ref.edge, src));
    }

    int created = 0, failed = 0;
    for (auto it = byFeature.constBegin(); it != byFeature.constEnd(); ++it) {
        cad::Feature* src = doc->findFeature(it.key());
        if (!src) { ++failed; continue; }

        auto* fl = new cad::FilletSolid(doc);
        fl->setSourceFeature(src);
        fl->setRadius(radius);
        fl->setEdgeSignatures(it.value());
        doc->registerSolidFeature(fl, QStringLiteral("Fillet"), {}, { src });

        if (fl->hasError()) {
            ++failed;
            outputMessage(QStringLiteral("圓角失敗（%1）：%2").arg(src->name(), fl->errorMessage()));
        } else {
            ++created;
        }
    }
    doc->setModified(true);
    outputMessage(QStringLiteral("FILLET3D 完成：建立 %1 個圓角特徵，%2 個失敗。半徑 = %3")
                      .arg(created).arg(failed).arg(radius, 0, 'f', 3));
    return created > 0;
}

// ── SHELL ─────────────────────────────────────────────────────────────

QString ShellCommand::promptText(int picked) const
{
    if (picked == 0)
        return QStringLiteral("SHELL — 點選要「開口」的面（再點一次取消選取；不選則為封閉空心），"
                              "輸入壁厚（正 = 向內，負 = 向外）後按 Enter：");
    return QStringLiteral("已選取 %1 個開口面 — 輸入壁厚（正 = 向內，負 = 向外）後按 Enter：").arg(picked);
}

bool ShellCommand::commit(cad::Document* doc, double thickness)
{
    QMap<QString, QVector<cad::FaceSignature>> byFeature;
    for (const view::PickedFaceRef& ref : m_ctx.cadView->pickedFaces()) {
        if (ref.featureId.isEmpty() || ref.face.IsNull()) continue;
        byFeature[ref.featureId].append(cad::solidops::makeFaceSignature(ref.face));
    }

    int created = 0, failed = 0;
    for (auto it = byFeature.constBegin(); it != byFeature.constEnd(); ++it) {
        cad::Feature* src = doc->findFeature(it.key());
        if (!src) { ++failed; continue; }

        auto* sh = new cad::ShellSolid(doc);
        sh->setSourceFeature(src);
        sh->setThickness(thickness);
        sh->setFaceSignatures(it.value());
        doc->registerSolidFeature(sh, QStringLiteral("Shell"), {}, { src });

        if (sh->hasError()) {
            ++failed;
            outputMessage(QStringLiteral("抽殼失敗（%1）：%2").arg(src->name(), sh->errorMessage()));
        } else {
            ++created;
        }
    }
    doc->setModified(true);
    outputMessage(QStringLiteral("SHELL 完成：建立 %1 個抽殼特徵，%2 個失敗。壁厚 = %3")
                      .arg(created).arg(failed).arg(thickness, 0, 'f', 3));
    return created > 0;
}

REGISTER_COMMAND("revolve",    RevolveCommand);
REGISTER_COMMAND("revolvecut", RevolveCutCommand);
REGISTER_COMMAND("sweep",      SweepCommand);
REGISTER_COMMAND("sweepcut",   SweepCutCommand);
REGISTER_COMMAND("loft",       LoftCommand);
REGISTER_COMMAND("loftcut",    LoftCutCommand);
REGISTER_COMMAND("boolean",    BooleanCommand);
REGISTER_COMMAND("pattern3d",  PatternCommand);
REGISTER_COMMAND("mirror3d",   Mirror3DCommand);
REGISTER_COMMAND("hole",       HoleCommand);
REGISTER_COMMAND("fillet3d",   Fillet3DCommand);
REGISTER_COMMAND("shell",      ShellCommand);

} // namespace command
} // namespace aicad
