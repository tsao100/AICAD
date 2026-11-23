/**
 * @file ProfileArrayCommand.cpp
 * @brief Implementation of PROFILEARRAY / EDITPROFILEARRAY — see ProfileArrayCommand.h.
 */
#include "command/alignment/ProfileArrayCommand.h"

#include "core/Application.h"
#include "core/DocumentManager.h"
#include "core/CommandLineManager.h"
#include "core/EventBus.h"
#include "cad/Document.h"
#include "cad/Sketch.h"
#include "cad/AlignedProfileArray.h"
#include "cad/ProfileLoftSolid.h"
#include "railway/RailwayAlignment.h"
#include "ui/UIManager.h"
#include "view/CadView.h"

#include <QDebug>

using namespace aicad::core;

namespace aicad {
namespace command {

// ============================================================================
//  CreateProfileArrayCommand
// ============================================================================

CreateProfileArrayCommand::CreateProfileArrayCommand(QObject* parent)
    : Command("profilearray", "Create Aligned Profile Array", parent)
{}

QString CreateProfileArrayCommand::getUsage() const
{
    return
        "PROFILEARRAY\n"
        "\n"
        "Place copies of the active sketch (containing 'cant'/'H' named\n"
        "parameters) at uniform chainage intervals along a TrackCenterLine.\n"
        "\n"
        "Prerequisite: activate the master profile sketch first.\n"
        "\n"
        "Step 1 — Track name (skipped automatically if only one TCL exists)\n"
        "Step 2 — Start chainage [m]\n"
        "Step 3 — Length [m] (end chainage = start + length)\n"
        "\n"
        "The station interval is automatic: about 1.5 m on curves (arcs and\n"
        "spirals) and about 3 m on straights; leftover length is distributed\n"
        "evenly over the sub-intervals of each straight/curve zone.\n"
        "\n"
        "Note: dimensions that must vary per station should reference\n"
        "'cant'/'H' directly in their own constraint expression (e.g.\n"
        "\"H + 250\"), not through an intermediate master-level named\n"
        "parameter — see the implementation plan §2.4 for why.";
}

CommandResult CreateProfileArrayCommand::execute(const CommandContext& /*context*/)
{
    Application* app = Application::instance();
    EventBus* bus = app->eventBus();
    cad::Document* doc = app->documentManager()->currentDocument();

    if (!doc)
        return CommandResult::Failure("No active document");

    // ── 找候選 master 草圖：優先用目前 active sketch，否則列出文件內所有 Sketch ──
    QList<cad::Sketch*> sketches;
    for (cad::Feature* f : doc->features())
        if (auto* sk = qobject_cast<cad::Sketch*>(f)) sketches.append(sk);

    cad::Sketch* master = app->activeSketch();
    if (!master && sketches.isEmpty())
        return CommandResult::Failure(
            "Document has no sketch to use as master profile. Create a sketch first.");

    if (doc->trackCenterLines().isEmpty())
        return CommandResult::Failure("Document has no TrackCenterLine to array along.");

    m_masterSketchId.clear();
    m_tcl = nullptr;
    m_isFinishing = false;
    m_start = 0.0; m_end = 0.0; m_interval = 20.0;

    bus->subscribe(Events::STRING_INPUT, this,
        [this](const QVariant& data) {
            const QString text = data.toString();
            QMetaObject::invokeMethod(this, [this, text]() {
                handleStringInput(text);
            }, Qt::QueuedConnection);
        });

    bus->subscribe(Events::NUMBER_INPUT, this,
        [this](const QVariant& data) {
            const QString text = data.toString();
            QMetaObject::invokeMethod(this, [this, text]() {
                handleNumberInput(text);
            }, Qt::QueuedConnection);
        });

    bus->subscribe(Events::POINT_CANCELLED, this,
        [this](const QVariant&) {
            QMetaObject::invokeMethod(this, [this]() {
                m_isFinishing = true;
                complete(CommandResult::Success("ProfileArray cancelled"));
            }, Qt::QueuedConnection);
        });

    // ✅ 選草圖/選線路的步驟也接受在 FeatureBrowser 上直接點選對應節點，
    //    不用只能打字輸入名稱。
    bus->subscribe(Events::FEATURE_SELECTED, this,
        [this](const QVariant& data) {
            const QString itemId = data.toString();
            QMetaObject::invokeMethod(this, [this, itemId]() {
                handleFeatureClicked(itemId);
            }, Qt::QueuedConnection);
        });

    setState(CommandState::Running);

    if (master) {
        m_masterSketchId = master->id();
        outputMessage(QString("Using active sketch: %1").arg(master->name()));
        resolveTrackOrPrompt();
    } else if (sketches.size() == 1) {
        m_masterSketchId = sketches.first()->id();
        outputMessage(QString("Using sketch: %1").arg(sketches.first()->name()));
        resolveTrackOrPrompt();
    } else {
        m_step = Step::WaitingForSketchName;
        QStringList names;
        for (auto* sk : sketches) if (sk) names << sk->name();
        outputMessage(QString("No active sketch. Sketches found: %1").arg(names.join(", ")));
        bus->publish(Events::COMMAND_PROMPT,
            tr("Enter master sketch name (or click it in the Feature Browser):"));
        CommandLineManager::instance()->waitForInput(core::InputType::String);
    }

    return CommandResult::Success("Waiting for input");
}

void CreateProfileArrayCommand::resolveTrackOrPrompt()
{
    EventBus* bus = Application::instance()->eventBus();
    cad::Document* doc = Application::instance()->documentManager()->currentDocument();

    if (doc->trackCenterLines().size() == 1) {
        m_tcl = doc->trackCenterLines().first();
        outputMessage(QString("Using track: %1").arg(m_tcl->name()));
        promptStart();
    } else {
        m_step = Step::WaitingForTclName;
        QStringList names;
        for (auto* tcl : doc->trackCenterLines())
            if (tcl) names << tcl->name();
        outputMessage(QString("Multiple tracks found: %1").arg(names.join(", ")));
        bus->publish(Events::COMMAND_PROMPT,
            tr("Enter track name (or click it in the Feature Browser):"));
        CommandLineManager::instance()->waitForInput(core::InputType::String);
    }
}

void CreateProfileArrayCommand::handleStringInput(const QString& text)
{
    if (m_isFinishing) return;

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();
    cad::Document* doc = app->documentManager()->currentDocument();
    const QString t = text.trimmed();

    if (m_step == Step::WaitingForSketchName) {
        cad::Sketch* found = nullptr;
        for (cad::Feature* f : doc->features()) {
            if (auto* sk = qobject_cast<cad::Sketch*>(f)) {
                if (sk->name() == t) { found = sk; break; }
            }
        }
        if (!found) {
            outputMessage(QString("No sketch named \"%1\" — try again:").arg(t));
            bus->publish(Events::COMMAND_PROMPT, tr("Enter master sketch name (or click it in the Feature Browser):"));
            CommandLineManager::instance()->waitForInput(core::InputType::String);
            return;
        }
        m_masterSketchId = found->id();
        outputMessage(QString("Using sketch: %1").arg(found->name()));
        resolveTrackOrPrompt();
        return;
    }

    if (m_step == Step::WaitingForTclName) {
        for (auto* tcl : doc->trackCenterLines()) {
            if (tcl && tcl->name() == t) { m_tcl = tcl; break; }
        }
        if (!m_tcl) {
            outputMessage(QString("No track named \"%1\" — try again:").arg(t));
            bus->publish(Events::COMMAND_PROMPT, tr("Enter track name (or click it in the Feature Browser):"));
            CommandLineManager::instance()->waitForInput(core::InputType::String);
            return;
        }
        outputMessage(QString("Using track: %1").arg(m_tcl->name()));
        promptStart();
    }
}

void CreateProfileArrayCommand::handleFeatureClicked(const QString& itemId)
{
    if (m_isFinishing || itemId.isEmpty()) return;

    cad::Document* doc = Application::instance()->documentManager()->currentDocument();
    if (!doc) return;

    if (m_step == Step::WaitingForSketchName) {
        auto* sk = qobject_cast<cad::Sketch*>(doc->findFeature(itemId));
        if (!sk) return;  // 點到別的東西（不是 Sketch），忽略、繼續等待
        m_masterSketchId = sk->id();
        outputMessage(QString("Using sketch: %1").arg(sk->name()));
        resolveTrackOrPrompt();
        return;
    }

    if (m_step == Step::WaitingForTclName) {
        for (auto* tcl : doc->trackCenterLines()) {
            if (tcl && tcl->id() == itemId) {
                m_tcl = tcl;
                outputMessage(QString("Using track: %1").arg(m_tcl->name()));
                promptStart();
                return;
            }
        }
        // 點到別的東西（不是 TrackCenterLine），忽略、繼續等待
    }
}

void CreateProfileArrayCommand::promptStart()
{
    m_step = Step::WaitingForStart;
    Application::instance()->eventBus()->publish(
        Events::COMMAND_PROMPT, tr("Enter start chainage [m]:"));
    CommandLineManager::instance()->waitForInput(core::InputType::Number);
}

void CreateProfileArrayCommand::promptEnd()
{
    m_step = Step::WaitingForEnd;
    Application::instance()->eventBus()->publish(
        Events::COMMAND_PROMPT, tr("Enter length [m]:"));
    CommandLineManager::instance()->waitForInput(core::InputType::Number);
}

void CreateProfileArrayCommand::promptInterval()
{
    m_step = Step::WaitingForInterval;
    Application::instance()->eventBus()->publish(
        Events::COMMAND_PROMPT, tr("Enter interval [m]:"));
    CommandLineManager::instance()->waitForInput(core::InputType::Number);
}

void CreateProfileArrayCommand::handleNumberInput(const QString& text)
{
    if (m_isFinishing) return;

    EventBus* bus = Application::instance()->eventBus();
    bool ok = false;
    const double v = text.trimmed().toDouble(&ok);

    if (m_step == Step::WaitingForStart) {
        if (!ok) {
            outputMessage("Cannot parse value — enter a number for start chainage:");
            bus->publish(Events::COMMAND_PROMPT, tr("Enter start chainage [m]:"));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        m_start = v;
        promptEnd();
        return;
    }

    if (m_step == Step::WaitingForEnd) {
        if (!ok || v <= 0.0) {
            outputMessage("Invalid value — length must be a positive number:");
            bus->publish(Events::COMMAND_PROMPT, tr("Enter length [m]:"));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        m_end = m_start + v;          // 終點 = 起始 + 長度；間距由線形自動決定
        createArray();
        return;
    }

    if (m_step == Step::WaitingForInterval) {
        if (!ok || v <= 0.0) {
            outputMessage("Invalid value — interval must be a positive number:");
            bus->publish(Events::COMMAND_PROMPT, tr("Enter interval [m]:"));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        m_interval = v;
        createArray();
    }
}

void CreateProfileArrayCommand::createArray()
{
    if (m_isFinishing) return;
    m_isFinishing = true;

    Application* app = Application::instance();
    cad::Document* doc = app->documentManager()->currentDocument();
    cad::Sketch* master = qobject_cast<cad::Sketch*>(doc->findFeature(m_masterSketchId));

    if (!master || !m_tcl) {
        complete(CommandResult::Failure("Master sketch or track no longer available"));
        return;
    }

    // 每次輸入 = 分段表格的一列：
    //   同一條 TCL 上已有沿線陣列 → 把這次輸入「加成一段」到該陣列（表格多一列）；
    //   沒有 → 建立新陣列（第一段 Seg1）。
    cad::AlignedProfileArray* arr = nullptr;
    for (cad::Feature* f : doc->features()) {
        auto* cand = qobject_cast<cad::AlignedProfileArray*>(f);
        if (cand && cand->trackCenterLine() == m_tcl) arr = cand;   // 取最後建立的
    }

    QString segName;
    if (arr) {
        cad::ProfileArraySegment seg;
        seg.name      = arr->uniqueSegmentName();
        seg.start     = m_start;
        seg.end       = m_end;
        seg.interval  = m_interval;
        seg.autoInterval = true;      // 間距依線形自動取值
        seg.profileId = master->id();
        segName       = seg.name;

        QList<cad::ProfileArraySegment> segs = arr->segments();
        segs.append(seg);
        arr->commitSegments(segs, arr->extraColumns());
    } else {
        arr = doc->createAlignedProfileArray(master, m_tcl, m_start, m_end, m_interval,
                                             QString(), /*autoInterval=*/true);
        segName = (arr && !arr->segments().isEmpty()) ? arr->segments().first().name : QString();
    }

    if (!arr) {
        complete(CommandResult::Failure("Failed to create profile array"));
        return;
    }
    if (!arr->errorMessage().isEmpty()) {
        complete(CommandResult::Failure("Profile array build failed: " + arr->errorMessage()));
        return;
    }

    // 放樣：每個分段各有一個 Loft。沿用既有陣列時 commitSegments() 已為新的分段建立；
    // 新建陣列則在這裡補建。找出「這次輸入的那一段」的 Loft 供回報。
    arr->syncSegmentLofts();
    cad::ProfileLoftSolid* loft = nullptr;
    {
        QString segId;
        for (const cad::ProfileArraySegment& s : arr->segments())
            if (s.name == segName) { segId = s.id; break; }
        for (cad::Feature* f : doc->features()) {
            auto* cand = qobject_cast<cad::ProfileLoftSolid*>(f);
            if (cand && cand->sourceArray() == arr && cand->segmentId() == segId) { loft = cand; break; }
        }
    }
    if (!loft) {
        complete(CommandResult::Failure(
            "Profile array created but loft failed to start"));
        return;
    }
    if (!loft->errorMessage().isEmpty()) {
        complete(CommandResult::Failure(
            QString("Profile array updated (%1 stations) but loft failed: %2")
                .arg(arr->stationCount())
                .arg(loft->errorMessage())));
        return;
    }

    // ✅ 放樣完常常在畫面之外，直接 fitAll() 讓使用者馬上看得到結果。
    if (ui::UIManager* uiMgr = app->uiManager()) {
        if (view::CadView* cadView = uiMgr->cadView())
            cadView->fitAll();
    }

    complete(CommandResult::Success(
        QString("Segment '%1' (%2 -> %3 m, auto interval) added to %4: "
                "%5 segment(s), %6 station(s) in total -> %7")
            .arg(segName)
            .arg(m_start, 0, 'f', 3)
            .arg(m_end,   0, 'f', 3)
            .arg(arr->name())
            .arg(arr->segmentCount())
            .arg(arr->stationCount())
            .arg(loft->name())));
}

void CreateProfileArrayCommand::cleanup()
{
    Application::instance()->eventBus()->unsubscribeAll(this);
    m_step = Step::WaitingForStart;
    m_isFinishing = false;
    m_tcl = nullptr;
}

// ============================================================================
//  EditProfileArrayCommand
// ============================================================================

EditProfileArrayCommand::EditProfileArrayCommand(QObject* parent)
    : Command("editprofilearray", "Edit Aligned Profile Array", parent)
{}

QString EditProfileArrayCommand::getUsage() const
{
    return "EDITPROFILEARRAY <featureId> <startChainage> <endChainage> <interval>\n"
           "Change the range/interval of the FIRST segment of an existing profile array and\n"
           "rebuild it. (Use the profile array's segment table to edit other segments.)";
}

CommandResult EditProfileArrayCommand::execute(const CommandContext& context)
{
    if (context.args.size() < 4)
        return CommandResult::Failure(
            "Usage: editprofilearray <featureId> <startChainage> <endChainage> <interval>");

    cad::Document* doc = Application::instance()->documentManager()->currentDocument();
    if (!doc)
        return CommandResult::Failure("No active document");

    auto* arr = qobject_cast<cad::AlignedProfileArray*>(doc->findFeature(context.args[0]));
    if (!arr)
        return CommandResult::Failure("No AlignedProfileArray with id: " + context.args[0]);

    bool okS = false, okE = false, okI = false;
    const double start    = context.args[1].toDouble(&okS);
    const double end      = context.args[2].toDouble(&okE);
    const double interval = context.args[3].toDouble(&okI);
    if (!okS || !okE || !okI || interval <= 0.0 || end < start)
        return CommandResult::Failure("Invalid start/end/interval values");

    arr->setRange(start, end, interval);
    if (!arr->rebuild())
        return CommandResult::Failure("Rebuild failed: " + arr->errorMessage());

    return CommandResult::Success(
        QString("Profile array updated: %1 station(s)").arg(arr->stationCount()));
}

// ============================================================================
//  LoftProfileArrayCommand
// ============================================================================

LoftProfileArrayCommand::LoftProfileArrayCommand(QObject* parent)
    : Command("profileloft", "Loft Profile Array to Solid", parent)
{}

QString LoftProfileArrayCommand::getUsage() const
{
    return "PROFILELOFT [featureId]\n"
           "Loft an AlignedProfileArray's station profiles into a solid.\n"
           "If featureId is omitted: auto-selects when only one profile array\n"
           "exists, otherwise prompts for its name.";
}

CommandResult LoftProfileArrayCommand::execute(const CommandContext& context)
{
    Application* app = Application::instance();
    cad::Document* doc = app->documentManager()->currentDocument();
    if (!doc)
        return CommandResult::Failure("No active document");

    if (!context.args.isEmpty()) {
        auto* arr = qobject_cast<cad::AlignedProfileArray*>(doc->findFeature(context.args[0]));
        if (!arr)
            return CommandResult::Failure("No AlignedProfileArray with id: " + context.args[0]);
        return doLoft(arr);
    }

    QList<cad::AlignedProfileArray*> arrays;
    for (cad::Feature* f : doc->features())
        if (auto* a = qobject_cast<cad::AlignedProfileArray*>(f)) arrays.append(a);

    if (arrays.isEmpty())
        return CommandResult::Failure("Document has no AlignedProfileArray to loft. Run PROFILEARRAY first.");

    if (arrays.size() == 1)
        return doLoft(arrays.first());

    // ── 超過一個：以 STRING_INPUT 請使用者輸入名稱（此時才進入非同步流程）──────
    m_isFinishing = false;
    EventBus* bus = app->eventBus();

    bus->subscribe(Events::STRING_INPUT, this,
        [this](const QVariant& data) {
            const QString text = data.toString();
            QMetaObject::invokeMethod(this, [this, text]() {
                handleStringInput(text);
            }, Qt::QueuedConnection);
        });
    bus->subscribe(Events::POINT_CANCELLED, this,
        [this](const QVariant&) {
            QMetaObject::invokeMethod(this, [this]() {
                m_isFinishing = true;
                complete(CommandResult::Success("Profile loft cancelled"));
            }, Qt::QueuedConnection);
        });
    bus->subscribe(Events::FEATURE_SELECTED, this,
        [this](const QVariant& data) {
            const QString itemId = data.toString();
            QMetaObject::invokeMethod(this, [this, itemId]() {
                handleFeatureClicked(itemId);
            }, Qt::QueuedConnection);
        });

    setState(CommandState::Running);

    QStringList names;
    for (auto* a : arrays) if (a) names << a->name();
    outputMessage(QString("Multiple profile arrays found: %1").arg(names.join(", ")));
    bus->publish(Events::COMMAND_PROMPT,
        tr("Enter profile array name (or click it in the Feature Browser):"));
    CommandLineManager::instance()->waitForInput(core::InputType::String);

    return CommandResult::Success("Waiting for input");
}

void LoftProfileArrayCommand::handleStringInput(const QString& text)
{
    if (m_isFinishing) return;

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();
    cad::Document* doc = app->documentManager()->currentDocument();
    const QString t = text.trimmed();

    cad::AlignedProfileArray* found = nullptr;
    for (cad::Feature* f : doc->features()) {
        if (auto* a = qobject_cast<cad::AlignedProfileArray*>(f)) {
            if (a->name() == t) { found = a; break; }
        }
    }
    if (!found) {
        outputMessage(QString("No profile array named \"%1\" — try again:").arg(t));
        bus->publish(Events::COMMAND_PROMPT,
            tr("Enter profile array name (or click it in the Feature Browser):"));
        CommandLineManager::instance()->waitForInput(core::InputType::String);
        return;
    }

    m_isFinishing = true;
    complete(doLoft(found));
}

void LoftProfileArrayCommand::handleFeatureClicked(const QString& itemId)
{
    if (m_isFinishing || itemId.isEmpty()) return;

    cad::Document* doc = Application::instance()->documentManager()->currentDocument();
    if (!doc) return;

    auto* arr = qobject_cast<cad::AlignedProfileArray*>(doc->findFeature(itemId));
    if (!arr) return;  // 點到別的東西，忽略、繼續等待

    m_isFinishing = true;
    complete(doLoft(arr));
}

CommandResult LoftProfileArrayCommand::doLoft(cad::AlignedProfileArray* arr)
{
    Application* app = Application::instance();
    cad::Document* doc = app->documentManager()->currentDocument();

    // 每個分段各有一個 Loft：補建缺少的（已有的不會重複建立）
    arr->syncSegmentLofts();
    QList<cad::ProfileLoftSolid*> lofts;
    for (cad::Feature* f : doc->features()) {
        auto* cand = qobject_cast<cad::ProfileLoftSolid*>(f);
        if (cand && cand->sourceArray() == arr) lofts.append(cand);
    }
    if (lofts.isEmpty())
        return CommandResult::Failure("Failed to create profile loft");
    for (cad::ProfileLoftSolid* l : lofts)
        if (!l->errorMessage().isEmpty())
            return CommandResult::Failure(
                QString("Loft '%1' failed: %2").arg(l->name(), l->errorMessage()));
    cad::ProfileLoftSolid* loft = lofts.first();

    // ✅ 放樣完常常在畫面之外（測站沿線分布，範圍可能跟目前視角差很多），
    //    直接 fitAll() 讓使用者馬上看得到結果，不用自己找。
    if (ui::UIManager* uiMgr = app->uiManager()) {
        if (view::CadView* cadView = uiMgr->cadView())
            cadView->fitAll();
    }

    return CommandResult::Success(
        QString("%1 profile loft solid(s) for %2: %3")
            .arg(lofts.size()).arg(arr->name()).arg(loft->name() + (lofts.size() > 1 ? " ..." : "")));
}

void LoftProfileArrayCommand::cleanup()
{
    Application::instance()->eventBus()->unsubscribeAll(this);
    m_isFinishing = false;
}

} // namespace command
} // namespace aicad
