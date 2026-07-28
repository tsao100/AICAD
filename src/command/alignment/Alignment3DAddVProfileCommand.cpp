/**
 * @file Alignment3DAddVProfileCommand.cpp
 * @brief ALIGNMENT3DADDVPROFILE (alias: V3D) — 實作。
 *
 * REGISTER_COMMAND 刻意放在本 .cpp（而非標頭檔）——見專案慣例：巨集展開為
 * file-scope 的 static 初始化，若放在標頭檔，任何 #include 這個標頭的
 * 編譯單元都會各自產生一份同名 static 變數與註冊呼叫，有 ODR 風險且可能
 * 造成靜默的重複/遺漏註冊。
 */

#include "command/alignment/Alignment3DAddVProfileCommand.h"
#include "command/CommandTypes.h"
#include "command/InputParser.h"
#include "core/Application.h"
#include "core/CommandLineManager.h"
#include "core/DocumentManager.h"
#include "core/EventBus.h"
#include "cad/Document.h"
#include "ui/UIManager.h"
#include "view/CadView.h"
#include "view/Railway3DAlignmentRenderer.h"
#include "railway/AlignmentDocument.h"
#include "railway/RailwayAlignment.h"

#include <QDebug>

using namespace aicad::core;

namespace aicad {
namespace command {

Alignment3DAddVProfileCommand::Alignment3DAddVProfileCommand(QObject* parent)
    : Command("alignment3daddvprofile",
              "Add simple start/end vertical alignment to selected 3D Alignment lines",
              parent)
{}

// ────────────────────────────────────────────────────────────────────────────
//  execute
// ────────────────────────────────────────────────────────────────────────────

CommandResult Alignment3DAddVProfileCommand::execute(const CommandContext& context)
{
    m_ctx = context;

    if (!context.uiManager) {
        const QString msg = "Internal error: no UIManager in context.";
        outputMessage(msg);
        return CommandResult::Failure(msg);
    }

    view::Railway3DAlignmentRenderer* renderer = context.uiManager->railway3DRenderer();
    if (!renderer || !renderer->isVisible()) {
        const QString msg =
            "3D Alignment 尚未顯示 — 請先開啟 Railway 資料夾的 3D Alignment 眼睛圖示。";
        outputMessage(msg);
        return CommandResult::Failure(msg);
    }

    // ── 目標線路：從 context.args 取得，而非即時查詢 renderer->selectedTcls() ──
    //
    // CommandManager::executeCommand() 會先發佈 COMMAND_STARTED，UIManager 對
    // COMMAND_STARTED 的全域處理（切換至「繪圖模式」）會無條件呼叫
    // CadView::clearSketchGeomSelection()，清空 AIS_InteractiveContext 目前
    // 的選取集合——這發生在 cmd->execute() 之前。若這裡才去問
    // renderer->selectedTcls()（其內部即時檢查 ctx->IsSelected()），選取已經
    // 被清空，永遠回傳空清單，導致 V3D 每次都以「尚未選取任何線路」失敗，
    // 且使用者在點選畫面上的折線之後執行 V3D 看起來「毫無反應」。
    //
    // UIManager 的 COMMAND_EXECUTE_REQUEST 處理常式在呼叫
    // CommandManager::executeCommand()（因而在 COMMAND_STARTED 發佈、選取被
    // 清空）之前，已經把當時的 CadView::selectedGeomUuids() 存進
    // ctx.args ——這些 uuid 對 3D Alignment 折線而言即是
    // "railway3d:<tclId>"（見 Railway3DAlignmentRenderer::rebuildOverlays()
    // 對 registerSketchGeomAIS() 的呼叫）。改用這份「命令啟動前」的快照即可
    // 取得使用者真正選取的線路，不受 clearSketchGeomSelection() 影響。
    static const QString kPrefix = QStringLiteral("railway3d:");

    auto* docMgr = Application::instance()->documentManager();
    cad::Document* doc = docMgr ? docMgr->currentDocument() : nullptr;

    qDebug() << "[V3D] context.args =" << context.args
             << "doc =" << (doc ? "OK" : "nullptr");

    m_targets.clear();
    if (doc) {
        for (const QString& uuid : context.args) {
            if (!uuid.startsWith(kPrefix)) continue;
            const QString tclId = uuid.mid(kPrefix.size());
            railway::TrackCenterLine* tcl = doc->findTrackCenterLine(tclId);
            qDebug() << "[V3D]   arg uuid =" << uuid
                     << "-> tclId =" << tclId
                     << "-> tcl =" << (tcl ? tcl->name() : "NOT FOUND");
            if (tcl && !m_targets.contains(tcl))
                m_targets.append(tcl);
        }
    }
    qDebug() << "[V3D] m_targets.size() =" << m_targets.size();

    if (m_targets.isEmpty()) {
        const QString msg =
            "尚未選取任何線路 — 請先在 3D Alignment 顯示中點選（可 Shift 多選）"
            "欲加入垂直線形的折線，再執行本命令。";
        outputMessage(msg);
        return CommandResult::Failure(msg);
    }

    m_step        = Step::PickStartRef;
    m_isFinishing = false;
    m_startElev   = 0.0;
    m_endElev     = 0.0;

    // ── 切到 GetPoint 模式，讓滑鼠點擊真正被視為「取點」 ──────────────────
    //
    // CadView::mousePressEvent() 只有在 d->mode 為 Sketching / GetPoint /
    // GetGeom 時，才會走 handlePointInput() 進而發佈 POINT_ACQUIRED（見該
    // 函式內對 d->mode 的判斷）。使用者選取 3D Alignment 折線時，CadView
    // 停留在瀏覽用的 Selecting 模式——若命令不主動切換，PickStartRef/
    // PickEndRef 階段點擊畫面上的折線只會被當成一般物件選取（更新
    // AIS_InteractiveContext 的選取集合），完全不會觸發 POINT_ACQUIRED，
    // 因此命令看起來「無法輸入數據或選線形」。
    if (context.cadView) {
        m_prevMode    = context.cadView->mode();
        m_modeChanged = true;
        context.cadView->setMode(view::InteractionMode::GetPoint);
    }

    EventBus* bus = Application::instance()->eventBus();

    bus->subscribe(Events::POINT_ACQUIRED, this,
        [this](const QVariant& data) {
            QVariantMap map = data.toMap();
            QPointF pt        = map["point"].value<QPointF>();
            QString geomUuid  = map["geomUuid"].toString();
            QMetaObject::invokeMethod(this, [this, pt, geomUuid]() {
                handlePointAcquired(pt, geomUuid);
            }, Qt::QueuedConnection);
        });

    bus->subscribe(Events::NUMBER_INPUT, this,
        [this](const QVariant& data) {
            QString text = data.toString();
            QMetaObject::invokeMethod(this, [this, text]() {
                handleNumberInput(text);
            }, Qt::QueuedConnection);
        });

    bus->subscribe(Events::POINT_CANCELLED, this,
        [this](const QVariant&) {
            QMetaObject::invokeMethod(this, [this]() {
                handleCancelled();
            }, Qt::QueuedConnection);
        });

    setState(CommandState::Running);

    QStringList names;
    for (const auto* tcl : std::as_const(m_targets)) {
        if (tcl) names << tcl->name();
    }
    outputMessage(QString("V3D — 已選取 %1 條線路：%2")
                      .arg(m_targets.size())
                      .arg(names.join(", ")));
    bus->publish(Events::COMMAND_PROMPT,
                 tr("點擊某條 3D Alignment 折線，或直接輸入【起點】高程（EL=<高程> 或純數字）："));
    outputMessage("點擊某條 3D Alignment 折線，或直接輸入【起點】高程（EL=<高程> 或純數字）：");
    // 讓命令列同時接受純文字輸入：CommandLineManager 在 isWaitingForInput()
    // 為 true 時，才會把使用者送出的文字視為「資料」而發佈 NUMBER_INPUT，
    // 否則會被當成一個全新的指令名稱去解析（進而取消本命令）。滑鼠點擊走的
    // 是 CadView::mode()（見上方切到 GetPoint 的說明），與此無關，兩種輸入
    // 方式因此可以在同一步驟並存。
    core::CommandLineManager::instance()->waitForInput(core::InputType::Number);

    return CommandResult::Success("Waiting for start-elevation pick");
}

// ────────────────────────────────────────────────────────────────────────────
//  sampleElevation
// ────────────────────────────────────────────────────────────────────────────

bool Alignment3DAddVProfileCommand::sampleElevation(
    const QString& geomUuid, const QPointF& xy,
    double& outChainage, double& outElevation) const
{
    // Railway3DAlignmentRenderer 以 "railway3d:<tclId>" 的形式，透過
    // CadView::registerSketchGeomAIS() 登錄每條 3D Alignment 折線的反查資料
    // （見該類別 rebuildOverlays()）。
    static const QString kPrefix = QStringLiteral("railway3d:");
    if (!geomUuid.startsWith(kPrefix))
        return false;

    const QString tclId = geomUuid.mid(kPrefix.size());

    auto* docMgr = Application::instance()->documentManager();
    auto* doc    = docMgr ? docMgr->currentDocument() : nullptr;
    if (!doc) return false;

    railway::TrackCenterLine* tcl = doc->findTrackCenterLine(tclId);
    if (!tcl || !tcl->horizontal() || tcl->horizontal()->isEmpty())
        return false;

    const double p = tcl->getPW(xy.x(), xy.y()).x();   // .x() = chainage
    outChainage  = p;
    outElevation = tcl->getZ(p);
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
//  handlePointAcquired
// ────────────────────────────────────────────────────────────────────────────

void Alignment3DAddVProfileCommand::handlePointAcquired(const QPointF& point,
                                                        const QString& geomUuid)
{
    if (m_isFinishing) return;

    EventBus* bus = Application::instance()->eventBus();

    double chainage = 0.0, elevation = 0.0;
    if (!sampleElevation(geomUuid, point, chainage, elevation)) {
        outputMessage("該處未命中任何 3D Alignment 折線 — 請直接點擊在折線上，"
                      "或直接輸入高程數值。");
        bus->publish(Events::COMMAND_PROMPT,
                     tr("請直接點擊在某條 3D Alignment 折線上，或輸入高程數值："));
        // 點擊落空後，命令列的「等待輸入」狀態已因前一次 processInput() 而
        // 重設，這裡重新啟用，讓使用者仍可改用輸入高程數值的方式繼續。
        core::CommandLineManager::instance()->waitForInput(core::InputType::Number);
        return;
    }

    applyElevation(elevation, QString("里程 %1").arg(chainage, 0, 'f', 3));
}

// ────────────────────────────────────────────────────────────────────────────
//  handleNumberInput
// ────────────────────────────────────────────────────────────────────────────

void Alignment3DAddVProfileCommand::handleNumberInput(const QString& text)
{
    if (m_isFinishing) return;

    EventBus* bus = Application::instance()->eventBus();
    const QString trimmed = text.trimmed();

    double elevation = 0.0;
    // 接受 "EL=12.345" 或純數字 "12.345"（tryParseKeyedOrPlainDouble 兩者皆可）。
    if (!command::InputParser::tryParseKeyedOrPlainDouble(trimmed, "EL", elevation)) {
        const QString stepPrompt = (m_step == Step::PickStartRef)
            ? tr("【起點】高程")
            : tr("【終點】高程");
        outputMessage(QString("無法辨識的高程輸入 — 請輸入 EL=<高程> 或純數字，"
                              "或直接點擊某條 3D Alignment 折線。"));
        bus->publish(Events::COMMAND_PROMPT,
                     tr("請重新輸入%1，或點擊某條 3D Alignment 折線：").arg(stepPrompt));
        core::CommandLineManager::instance()->waitForInput(core::InputType::Number);
        return;
    }

    applyElevation(elevation, QString());
}

// ────────────────────────────────────────────────────────────────────────────
//  applyElevation
// ────────────────────────────────────────────────────────────────────────────

void Alignment3DAddVProfileCommand::applyElevation(double elevation,
                                                    const QString& originDesc)
{
    EventBus* bus = Application::instance()->eventBus();
    const QString suffix = originDesc.isEmpty() ? QString() : QString("（%1）").arg(originDesc);

    switch (m_step) {
    case Step::PickStartRef:
        m_startElev = elevation;
        outputMessage(QString("起點高程 = %1 m%2")
                          .arg(elevation, 0, 'f', 3)
                          .arg(suffix));
        bus->publish(Events::COMMAND_PROMPT,
                     tr("點擊某條 3D Alignment 折線，或直接輸入【終點】高程（EL=<高程> 或純數字）："));
        m_step = Step::PickEndRef;
        // 進入下一步仍要重新開放文字輸入通道（每次 processInput() 後會自動關閉）。
        core::CommandLineManager::instance()->waitForInput(core::InputType::Number);
        break;

    case Step::PickEndRef:
        m_endElev = elevation;
        outputMessage(QString("終點高程 = %1 m%2")
                          .arg(elevation, 0, 'f', 3)
                          .arg(suffix));
        commitAll();
        break;
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  commitAll
// ────────────────────────────────────────────────────────────────────────────

void Alignment3DAddVProfileCommand::commitAll()
{
    if (m_isFinishing) return;

    auto* docMgr = Application::instance()->documentManager();
    auto* doc    = docMgr ? docMgr->currentDocument() : nullptr;
    if (!doc || !m_ctx.uiManager) {
        outputMessage("Internal error: no current document.");
        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Failure("No current document"));
        return;
    }

    int addedCount = 0, skippedCount = 0;

    for (railway::TrackCenterLine* tcl : std::as_const(m_targets)) {
        if (!tcl) continue;

        if (!tcl->horizontal() || tcl->horizontal()->isEmpty()
            || tcl->horizontal()->rawPoints().size() < 2) {
            outputMessage(QString("略過 %1：尚無有效的平面線形。").arg(tcl->name()));
            ++skippedCount;
            continue;
        }

        if (tcl->vertical() && !tcl->vertical()->isEmpty()) {
            outputMessage(
                QString("略過 %1：已存在縱斷面資料，避免覆蓋（如需重建請先清除既有 VIP）。")
                    .arg(tcl->name()));
            ++skippedCount;
            continue;
        }

        railway::AlignmentDocument* aDoc =
            m_ctx.uiManager->ensureTclAlignmentDocument(tcl->id());
        if (!aDoc) {
            outputMessage(QString("略過 %1：無法建立 AlignmentDocument。").arg(tcl->name()));
            ++skippedCount;
            continue;
        }

        if (aDoc->vertical()->vipCount() > 0) {
            outputMessage(
                QString("略過 %1：編輯階段已存在 VIP 資料，避免覆蓋（如需重建，"
                        "請先於縱斷面編輯器中清除既有 VIP）。")
                    .arg(tcl->name()));
            ++skippedCount;
            continue;
        }

        const auto& rawPts   = tcl->horizontal()->rawPoints();
        const double startCh = rawPts.first().chainage;
        const double endCh   = rawPts.last().chainage;

        aDoc->vertical()->addVip(startCh, m_startElev, 0.0);
        aDoc->vertical()->addVip(endCh,   m_endElev,   0.0);
        aDoc->vertical()->solve();

        const railway::VerticalAlignment* va = aDoc->vertical()->result();
        if (va && !va->isEmpty()) {
            tcl->loadVertical(va->points());
            ++addedCount;
            outputMessage(
                QString("%1：新增簡易垂直線形  CH %2→%3  EL %4→%5 m")
                    .arg(tcl->name())
                    .arg(startCh, 0, 'f', 3)
                    .arg(endCh,   0, 'f', 3)
                    .arg(m_startElev, 0, 'f', 3)
                    .arg(m_endElev,   0, 'f', 3));
        } else {
            outputMessage(QString("%1：solve() 未產生有效結果，略過。").arg(tcl->name()));
            ++skippedCount;
        }
    }

    doc->setModified(true);

    outputMessage(QString("V3D 完成：新增 %1 條，略過 %2 條。")
                      .arg(addedCount).arg(skippedCount));

    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("Alignment3DAddVProfile completed"));
}

// ────────────────────────────────────────────────────────────────────────────
//  handleCancelled / cleanup
// ────────────────────────────────────────────────────────────────────────────

void Alignment3DAddVProfileCommand::handleCancelled()
{
    if (m_isFinishing) return;
    qDebug() << "[V3D] Cancelled";
    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("Alignment3DAddVProfile cancelled"));
}

void Alignment3DAddVProfileCommand::cleanup()
{
    EventBus* bus = Application::instance()->eventBus();
    bus->unsubscribeAll(this);

    // 換回進入命令前的 CadView 互動模式（見 execute() 內切到 GetPoint 的說明）。
    // m_ctx 保有 execute() 當時的 context 副本，其中的 cadView 指標在命令
    // 生命週期內維持有效。
    if (m_modeChanged && m_ctx.cadView) {
        m_ctx.cadView->setMode(m_prevMode);
    }
    m_modeChanged = false;

    m_step        = Step::PickStartRef;
    m_isFinishing = false;
    m_targets.clear();
    m_startElev = 0.0;
    m_endElev   = 0.0;
}

// ────────────────────────────────────────────────────────────────────────────
//  getUsage
// ────────────────────────────────────────────────────────────────────────────

QString Alignment3DAddVProfileCommand::getUsage() const
{
    return "Usage: V3D\n"
           "  1. 先在 3D Alignment 顯示中選取（可多選）欲加入垂直線形的線路。\n"
           "  2. 執行 V3D。\n"
           "  3. 點擊某條 3D Alignment 折線，或直接輸入 EL=<高程>（或純數字）\n"
           "     → 取得【起點】高程。\n"
           "  4. 同上 → 取得【終點】高程。\n"
           "  各目標線路的起訖里程沿用其自身平面線形頭尾里程。\n"
           "  Right-click / ESC 可隨時取消。";
}

// ─────────────────────────────────────────────────────────────────────────────
// Static registration — 指令 "alignment3daddvprofile" 觸發
// Alignment3DAddVProfileCommand。刻意放在 .cpp 檔案作用域內（namespace 內、
// 未加 aicad::command:: 完整限定名）——REGISTER_COMMAND 巨集以 token-pasting
// 組出 static 變數名稱（_reg_##CLASS），CLASS 前面不能帶 "::"，否則無法通過
// 前置處理器展開。
REGISTER_COMMAND("alignment3daddvprofile", Alignment3DAddVProfileCommand);

} // namespace command
} // namespace aicad
