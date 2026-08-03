/**
 * @file ChamferCommand.cpp
 * @brief 見 ChamferCommand.h 檔頭說明——2D／3D 兩種倒角模式的整合實作。
 *
 * REGISTER_COMMAND 刻意放在本 .cpp（而非標頭檔）——見專案慣例：巨集展開為
 * file-scope 的 static 初始化，若放在標頭檔，任何 #include 這個標頭的
 * 編譯單元都會各自產生一份同名 static 變數與註冊呼叫，有 ODR 風險且可能
 * 造成靜默的重複/遺漏註冊。
 */

#include "command/ChamferCommand.h"
#include "command/CommandFactory.h"
#include "command/CommandTypes.h"
#include "command/TrimExtendHelper.h"

#include "core/Application.h"
#include "core/CommandLineManager.h"
#include "core/DocumentManager.h"
#include "core/EventBus.h"

#include "cad/Sketch.h"
#include "cad/Document.h"
#include "cad/Feature.h"
#include "cad/ChamferSolid.h"

#include "ui/UIManager.h"
#include "view/CadView.h"

#include <QMetaObject>
#include <QPointF>
#include <QMap>
#include <QDebug>

using namespace aicad::core;

namespace aicad {
namespace command {

using namespace cad;

namespace {
bool isFixedReferenceUuid(const QString& uuid)
{
    return uuid.startsWith("sketch_xaxis:") ||
           uuid.startsWith("sketch_yaxis:") ||
           uuid.startsWith("sketch_origin:");
}
} // namespace

ChamferCommand::ChamferCommand(QObject* parent)
    : Command("chamfer",
              "Chamfer two sketch lines, or selected edges of a solid feature "
              "(Extrude/Loft/…), depending on current editing context (alias: CHA)",
              parent)
{
}

QString ChamferCommand::getUsage() const
{
    return "Usage: CHAMFER — inside a sketch: specify two chamfer distances "
           "(0,0 = trim to intersection), then select two straight lines. "
           "Outside a sketch: select any number of solid edges, then enter a "
           "single chamfer distance.";
}

cad::Sketch* ChamferCommand::activeSketch() const
{
    return core::Application::instance()->activeSketch();
}

// ─────────────────────────────────────────────────────────────────────────
// execute — 依目前是否在編輯 Sketch 決定走 2D 或 3D 模式
// ─────────────────────────────────────────────────────────────────────────

CommandResult ChamferCommand::execute(const CommandContext& context)
{
    m_is3DMode = (activeSketch() == nullptr);

    if (m_is3DMode)
        return begin3D(context);
    return begin2D();
}

// ═════════════════════════════════════════════════════════════════════════
//  2D 模式：草圖內兩直線倒角
// ═════════════════════════════════════════════════════════════════════════

CommandResult ChamferCommand::begin2D()
{
    m_state2D = State2D::Idle;
    m_dist1 = m_dist2 = 0.0;
    m_line1Uuid.clear();
    m_clickPt1 = QVector2D();

    Sketch* sk = activeSketch();
    if (!sk) {
        auto* cmdMgr = core::CommandLineManager::instance();
        if (cmdMgr) cmdMgr->printError("No active sketch. Enter sketch edit mode first.");
        return CommandResult::Failure("No active sketch.");
    }

    // CHAMFER 2D 模式一律走互動流程，不支援模式 A（理由同 FILLET，見其檔頭說明）。
    setWaitingForInput();
    beginDist1Stage();
    return CommandResult::Success("Waiting for first chamfer distance...");
}

void ChamferCommand::beginDist1Stage()
{
    m_state2D = State2D::WaitDist1;

    subscribeNumberInput2D();
    subscribeCancelled2D();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt("[CHAMFER] Specify first chamfer distance (0 = trim to intersection):");
        cmdMgr->waitForInput(core::InputType::Number);
    }
}

void ChamferCommand::beginDist2Stage()
{
    m_state2D = State2D::WaitDist2;

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt("[CHAMFER] Specify second chamfer distance:");
        cmdMgr->waitForInput(core::InputType::Number);
    }
}

void ChamferCommand::subscribeNumberInput2D()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::NUMBER_INPUT, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onNumberInput2D(v); },
                                      Qt::QueuedConnection);
        });
}

void ChamferCommand::onNumberInput2D(const QVariant& payload)
{
    if (m_is3DMode) return;
    if (m_state2D != State2D::WaitDist1 && m_state2D != State2D::WaitDist2) return;

    bool ok = false;
    const double v = payload.toString().trimmed().toDouble(&ok);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (!ok || v < 0.0) {
        if (cmdMgr) {
            cmdMgr->printError("Invalid distance. Enter a non-negative number:");
            cmdMgr->waitForInput(core::InputType::Number);
        }
        return;  // 停留在同一階段
    }

    if (m_state2D == State2D::WaitDist1) {
        m_dist1 = v;
        beginDist2Stage();
        return;
    }

    // State2D::WaitDist2
    m_dist2 = v;

    auto* bus = core::Application::instance()->eventBus();
    if (bus) bus->unsubscribe(core::Events::NUMBER_INPUT, this);

    beginFirstObjectStage();
}

void ChamferCommand::beginFirstObjectStage()
{
    m_state2D = State2D::WaitFirstObject;
    m_line1Uuid.clear();
    m_clickPt1 = QVector2D();

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetGeom);

    subscribeGeomPicked2D();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->showPrompt("[CHAMFER] Select first line:");
}

void ChamferCommand::subscribeGeomPicked2D()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::GEOM_PICKED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onGeomPicked2D(v); },
                                      Qt::QueuedConnection);
        });
}

void ChamferCommand::onGeomPicked2D(const QVariant& payload)
{
    if (m_is3DMode) return;
    if (m_state2D != State2D::WaitFirstObject && m_state2D != State2D::WaitSecondObject) return;

    const QVariantMap map = payload.toMap();
    const QString uuid = map.value("geomUuid").toString();
    const QPointF ptF = map.value("point").value<QPointF>();
    const QVector2D clickPt(float(ptF.x()), float(ptF.y()));

    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk = activeSketch();

    if (uuid.isEmpty() || isFixedReferenceUuid(uuid) || !sk) {
        if (cmdMgr) cmdMgr->printWarning("⚠️  No selectable geometry at that point.");
        return;
    }

    if (!dynamic_cast<SketchLine*>(sk->findGeometry(uuid))) {
        if (cmdMgr) cmdMgr->printWarning("⚠️  CHAMFER currently only supports straight lines.");
        return;
    }

    if (m_state2D == State2D::WaitFirstObject) {
        m_line1Uuid = uuid;
        m_clickPt1  = clickPt;
        m_state2D = State2D::WaitSecondObject;
        if (cmdMgr) cmdMgr->showPrompt("[CHAMFER] Select second line:");
        return;
    }

    // State2D::WaitSecondObject
    if (uuid == m_line1Uuid) {
        if (cmdMgr) cmdMgr->printWarning("⚠️  Select a different line for the second object.");
        return;
    }

    const bool ok = trimext::chamferAt(sk, m_line1Uuid, uuid, m_dist1, m_dist2, m_clickPt1, clickPt);
    if (cmdMgr) {
        if (ok)
            cmdMgr->printSuccess(
                QString("✅ Chamfered (D1=%1, D2=%2).").arg(m_dist1).arg(m_dist2));
        else
            cmdMgr->printWarning(
                "⚠️  Cannot chamfer: lines are parallel/collinear, or distances are unreasonable.");
    }

    cleanup2D();
}

void ChamferCommand::subscribeCancelled2D()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onCancelled2D(v); },
                                      Qt::QueuedConnection);
        });
}

void ChamferCommand::onCancelled2D(const QVariant&)
{
    if (m_is3DMode) return;
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->printMessage("CHAMFER cancelled.");
    cleanup2D();
}

void ChamferCommand::unsubscribeAll2D()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->unsubscribe(core::Events::NUMBER_INPUT,      this);
    bus->unsubscribe(core::Events::GEOM_PICKED,       this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED, this);
}

void ChamferCommand::cleanup2D()
{
    unsubscribeAll2D();

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->clearPrompt();

    m_state2D = State2D::Idle;
    m_dist1 = m_dist2 = 0.0;
    m_line1Uuid.clear();
    m_clickPt1 = QVector2D();

    if (state() == CommandState::Running)
        complete(CommandResult::Success());
}

// ═════════════════════════════════════════════════════════════════════════
//  3D 模式：實體邊選取倒角
// ═════════════════════════════════════════════════════════════════════════

CommandResult ChamferCommand::begin3D(const CommandContext& context)
{
    m_ctx = context;
    m_isFinishing = false;

    if (!context.cadView) {
        const QString msg = "Internal error: no CadView in context.";
        outputMessage(msg);
        return CommandResult::Failure(msg);
    }

    auto* docMgr = Application::instance()->documentManager();
    cad::Document* doc = docMgr ? docMgr->currentDocument() : nullptr;
    if (!doc) {
        const QString msg = "No active document.";
        outputMessage(msg);
        return CommandResult::Failure(msg);
    }

    // ── 切到 PickEdge 模式，對所有已顯示的實體 Feature 開啟邊子形選取 ──────
    //
    // 依設計決議：CHAMFER 3D 模式一啟動就直接開放畫面上所有實體 Feature 的
    // 邊選取（不需要使用者先點選來源實體整體），見 CadView::beginEdgePicking()。
    m_prevMode    = context.cadView->mode();
    m_modeChanged = true;
    context.cadView->beginEdgePicking();

    connect(context.cadView, &view::CadView::edgePicked,
            this, &ChamferCommand::onEdgePicked3D, Qt::UniqueConnection);

    EventBus* bus = Application::instance()->eventBus();

    bus->subscribe(Events::NUMBER_INPUT, this,
        [this](const QVariant& data) {
            QString text = data.toString();
            QMetaObject::invokeMethod(this, [this, text]() {
                handleDistanceInput3D(text);
            }, Qt::QueuedConnection);
        });

    setState(CommandState::Running);

    outputMessage("CHAMFER — 點選要倒角的邊（可重複點選以取消選取），"
                  "輸入倒角距離（正數，套用於所有選取邊）後按 Enter 完成：");
    bus->publish(Events::COMMAND_PROMPT,
                 tr("點選要倒角的邊，輸入倒角距離後按 Enter："));
    CommandLineManager::instance()->waitForInput(core::InputType::Number);

    return CommandResult::Success("Waiting for edge selection / distance input");
}

void ChamferCommand::onEdgePicked3D()
{
    if (!m_is3DMode) return;
    if (m_isFinishing || !m_ctx.cadView) return;

    const int n = m_ctx.cadView->pickedEdges().size();
    const QString prompt = tr("已選取 %1 條邊 — 輸入倒角距離後按 Enter：").arg(n);

    outputMessage(prompt);
    Application::instance()->eventBus()->publish(Events::COMMAND_PROMPT, prompt);

    // ⚠️ 不需要重新呼叫 waitForInput()：邊的點擊走的是 CadView::mousePressEvent
    //    的 PickEdge 分支，不經過 CommandLineManager::processInput()，命令列
    //    的「等待輸入」狀態不會被這個點擊動作關閉。
}

void ChamferCommand::handleDistanceInput3D(const QString& text)
{
    if (!m_is3DMode) return;
    if (m_isFinishing) return;

    EventBus* bus = Application::instance()->eventBus();
    const QString trimmed = text.trimmed();

    bool ok = false;
    const double distance = trimmed.toDouble(&ok);

    if (!ok || distance <= 0.0) {
        const QString msg = "無法辨識的倒角距離 — 請輸入一個正數：";
        outputMessage(msg);
        bus->publish(Events::COMMAND_PROMPT, tr("請輸入倒角距離（正數）："));
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        return;
    }

    commitAll3D(distance);
}

void ChamferCommand::commitAll3D(double distance)
{
    auto* docMgr = Application::instance()->documentManager();
    cad::Document* doc = docMgr ? docMgr->currentDocument() : nullptr;

    if (!doc || !m_ctx.cadView) {
        m_isFinishing = true;
        complete(CommandResult::Failure("Internal error: no current document."));
        return;
    }

    const QVector<view::PickedEdgeRef> edges = m_ctx.cadView->pickedEdges();
    if (edges.isEmpty()) {
        outputMessage("尚未選取任何邊 — CHAMFER 已取消。");
        m_isFinishing = true;
        complete(CommandResult::Success("Chamfer cancelled: no edges selected"));
        return;
    }

    // 依來源 Feature 分組：同時挑了多個來源 Feature 的邊時，各自建立一個
    // ChamferSolid（單一距離仍套用於每一組全部的邊，見類別註解）。
    // ⚠️ 先分組原始 TopoDS_Edge、找到 src 之後才呼叫 makeSignature(edge, src)：
    //    角點定址反查（loopIndex/cornerIndex）需要知道邊屬於哪個來源 Feature
    //    才能查表比對，不能在還沒查出 src 之前就計算 signature。
    QMap<QString, QVector<TopoDS_Edge>> rawByFeature;
    for (const view::PickedEdgeRef& ref : edges) {
        if (ref.featureId.isEmpty() || ref.edge.IsNull()) continue;
        rawByFeature[ref.featureId].append(ref.edge);
    }

    int created = 0, failed = 0;
    for (auto it = rawByFeature.constBegin(); it != rawByFeature.constEnd(); ++it) {
        cad::Feature* src = doc->findFeature(it.key());
        if (!src) { ++failed; continue; }

        QVector<cad::EdgeSignature> sigs;
        sigs.reserve(it.value().size());
        for (const TopoDS_Edge& e : it.value())
            sigs.append(cad::ChamferSolid::makeSignature(e, src));

        cad::ChamferSolid* cf = doc->createChamferSolid(src, distance, sigs);
        if (cf && !cf->hasError()) {
            ++created;
        } else {
            ++failed;
            if (cf) {
                outputMessage(QString("倒角失敗（%1）：%2")
                                  .arg(src->name(), cf->errorMessage()));
            }
        }
    }

    doc->setModified(true);

    outputMessage(QString("CHAMFER 完成：建立 %1 個倒角特徵，%2 個失敗。距離 = %3")
                      .arg(created).arg(failed).arg(distance, 0, 'f', 3));

    m_isFinishing = true;
    complete(created > 0
                 ? CommandResult::Success("Chamfer completed")
                 : CommandResult::Failure("Chamfer failed for all selected edges"));
}

void ChamferCommand::cleanup3D()
{
    EventBus* bus = Application::instance()->eventBus();
    bus->unsubscribeAll(this);

    if (m_ctx.cadView) {
        disconnect(m_ctx.cadView, &view::CadView::edgePicked,
                   this, &ChamferCommand::onEdgePicked3D);
        m_ctx.cadView->endEdgePicking();

        if (m_modeChanged) {
            m_ctx.cadView->setMode(m_prevMode);
        }
    }
    m_modeChanged = false;
    m_isFinishing = false;
}

// ═════════════════════════════════════════════════════════════════════════
//  cleanup — 依 execute() 當時判定的模式，分派到對應的還原邏輯
// ═════════════════════════════════════════════════════════════════════════

void ChamferCommand::cleanup()
{
    if (m_is3DMode)
        cleanup3D();
    else
        cleanup2D();
}

// ─────────────────────────────────────────────────────────────────────────────
// Static registration — 指令 "chamfer" 觸發 ChamferCommand（別名 CHA，見 menu.txt）。
// 刻意放在 .cpp 檔案作用域內（namespace 內、未加 aicad::command:: 完整限定
// 名）——REGISTER_COMMAND 巨集以 token-pasting 組出 static 變數名稱
// （_reg_##CLASS），CLASS 前面不能帶 "::"，否則無法通過前置處理器展開。
REGISTER_COMMAND("chamfer", ChamferCommand);

} // namespace command
} // namespace aicad
