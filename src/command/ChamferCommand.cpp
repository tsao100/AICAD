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

// static — 見 ChamferCommand.h 對 s_lastDist1/s_lastDist2 的說明：跨指令
// 呼叫存活，記住使用者上一次設定的倒角距離。應用程式啟動後、第一次執行
// CHAMFER 之前維持 0.0（對應「D1、D2 預設值為零」）。
double ChamferCommand::s_lastDist1 = 0.0;
double ChamferCommand::s_lastDist2 = 0.0;

QString ChamferCommand::getUsage() const
{
    return "Usage: CHAMFER — inside a sketch: select two straight lines "
           "directly (reuses the D1/D2 distances from the last CHAMFER run, "
           "0,0 the first time); type D at the first-line prompt to set new "
           "D1/D2 before selecting (0,0 = trim to intersection). "
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
    // 帶入上一次執行 CHAMFER 設定的 D1/D2（第一次執行、或 App 剛啟動時皆為
    // 0.0），使用者不需要每次都重新輸入，可以直接選線；想改變距離的話，
    // 在「選第一條線」的提示下輸入 D 即可（見 onOptionSelected2D()）。
    m_dist1 = s_lastDist1;
    m_dist2 = s_lastDist2;
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
    // COMMAND_CANCELLED 整個 2D 流程只需要訂閱一次（不像 NUMBER_INPUT／
    // GEOM_PICKED／OPTION_SELECTED 會隨著「選線 ⇄ 改距離」的往返而重複
    // 訂閱/取消訂閱），故固定在真正的流程入口（本函式）訂閱一次，統一交由
    // cleanup2D() 取消。
    subscribeCancelled2D();
    // 直接進入選線階段（不再強制先問距離），見檔頭說明的新互動流程。
    beginFirstObjectStage();
    return CommandResult::Success("Waiting for first line selection...");
}

void ChamferCommand::beginDist1Stage()
{
    m_state2D = State2D::WaitDist1;

    subscribeNumberInput2D();
    // COMMAND_CANCELLED 已在 begin2D() 訂閱一次，這裡（可能因使用者在選線
    // 階段輸入 D 而重新進入）不重複訂閱。

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt(
            QString("[CHAMFER] Specify first chamfer distance (0 = trim to intersection) <%1>:")
                .arg(m_dist1, 0, 'f', 3));
        cmdMgr->waitForInput(core::InputType::Number);
    }
}

void ChamferCommand::beginDist2Stage()
{
    m_state2D = State2D::WaitDist2;

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt(
            QString("[CHAMFER] Specify second chamfer distance <%1>:")
                .arg(m_dist2, 0, 'f', 3));
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

    const QString text = payload.toString().trimmed();

    // 空字串＝直接按 Enter：沿用目前的預設值（提示列 <...> 內顯示的那個
    // 數字，即 m_dist1/m_dist2 當時的值），不強制每次都要重新輸入。
    double v = 0.0;
    if (text.isEmpty()) {
        v = (m_state2D == State2D::WaitDist1) ? m_dist1 : m_dist2;
    } else {
        bool ok = false;
        v = text.toDouble(&ok);
        auto* cmdMgr = core::CommandLineManager::instance();
        if (!ok || v < 0.0) {
            if (cmdMgr) {
                cmdMgr->printError("Invalid distance. Enter a non-negative number:");
                cmdMgr->waitForInput(core::InputType::Number);
            }
            return;  // 停留在同一階段
        }
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

    // 記住這次設定，供下一次執行 CHAMFER 時帶入。
    s_lastDist1 = m_dist1;
    s_lastDist2 = m_dist2;

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
    subscribeOptionSelected2D();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt(
            QString("[CHAMFER] Select first line or [Distance(D)] (D1=%1, D2=%2):")
                .arg(m_dist1, 0, 'f', 3).arg(m_dist2, 0, 'f', 3));
        // 讓命令列進入「等待選項輸入」狀態：使用者打 D（按 Enter 或 Space
        // 皆可送出，見 CommandInputEdit::keyPressEvent 對 InputType::Option
        // 的處理）會被 CommandLineManager::processInput() 攔截、比對到剛才
        // showPrompt() 從 "[Distance(D)]" 解析出的選項，發布 OPTION_SELECTED，
        // 而不會被誤判成一個全新的頂層指令名稱去查找。滑鼠點兩條線走的是
        // CadView 的 GEOM_PICKED，是另一條獨立通道，兩者互不影響、可以並存。
        cmdMgr->waitForInput(core::InputType::Option);
    }
}

void ChamferCommand::subscribeOptionSelected2D()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::OPTION_SELECTED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onOptionSelected2D(v); },
                                      Qt::QueuedConnection);
        });
}

void ChamferCommand::onOptionSelected2D(const QVariant& payload)
{
    if (m_is3DMode) return;
    // [距離(D)] 選項只在「選第一條線」提示下提供（比照 AutoCAD CHAMFER：
    // Distance 只在選第一條線之前可用）。
    if (m_state2D != State2D::WaitFirstObject) return;

    const QString opt = payload.toString().trimmed();
    // CommandLineManager::processInput() 依 InputParser::parsePrompt() 對
    // "[距離(D)]" 的解析結果，送出的可能是 shortcut "D" 也可能是 label
    // "距離"——兩種都接受，避免上游解析細節之後調整而漏接。
    if (opt.compare(QStringLiteral("D"),  Qt::CaseInsensitive) != 0 &&
        opt.compare(QStringLiteral("距離"), Qt::CaseInsensitive) != 0) {
        return;
    }

    // 使用者要求重新設定倒角距離：先關閉目前「選第一條線」子狀態的訂閱
    // （GEOM_PICKED／OPTION_SELECTED）與 GetGeom 檢視模式，改走
    // dist1 → dist2 兩階段數字輸入；輸入完成後 onNumberInput2D() 會自動
    // 呼叫 beginFirstObjectStage() 帶著新距離重新開始選線（見該函式）。
    auto* bus = core::Application::instance()->eventBus();
    if (bus) {
        bus->unsubscribe(core::Events::GEOM_PICKED,     this);
        bus->unsubscribe(core::Events::OPTION_SELECTED, this);
    }

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    beginDist1Stage();
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
        // 選第二條線這個階段不再提供 [距離(D)] 選項（比照 AutoCAD CHAMFER：
        // Distance 只在選第一條線之前可用），把 beginFirstObjectStage() 為了
        // 接收 D 而掛上的「等待 Option 輸入」狀態解除掉——這是滑鼠點選
        // （GEOM_PICKED）觸發的狀態轉換，不會經過
        // CommandLineManager::processInput()，該狀態不會自動被清掉，需要
        // 手動呼叫 resetInputWait()，否則之後打字會一直被誤判成選項輸入。
        if (cmdMgr) cmdMgr->resetInputWait();
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
    bus->unsubscribe(core::Events::OPTION_SELECTED,   this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED, this);
}

void ChamferCommand::cleanup2D()
{
    unsubscribeAll2D();

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->clearPrompt();
        // 防禦性重置：beginFirstObjectStage() 掛的「等待 Option 輸入」狀態
        // 若因滑鼠點選（不經過 processInput()）而未被清掉，這裡確保指令
        // 結束後 CommandLineManager 一定回到乾淨狀態，不會卡住下一個指令
        // 的文字輸入（見 onGeomPicked2D() 對同一問題的說明）。
        cmdMgr->resetInputWait();
    }

    m_state2D = State2D::Idle;
    // ⚠️ 不重設 m_dist1/m_dist2 為 0——它們已經在完成當下同步進
    // s_lastDist1/s_lastDist2（見 onNumberInput2D()），這裡保留原值單純是
    // 避免誤導；下一次 execute()/begin2D() 一律會重新從 s_lastDist1/
    // s_lastDist2 取值，不依賴這裡的殘留狀態。
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
