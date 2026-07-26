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
#include "core/Application.h"
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
        return CommandResult::Failure("Internal error: no UIManager in context.");
    }

    view::Railway3DAlignmentRenderer* renderer = context.uiManager->railway3DRenderer();
    if (!renderer || !renderer->isVisible()) {
        return CommandResult::Failure(
            "3D Alignment 尚未顯示 — 請先開啟 Railway 資料夾的 3D Alignment 眼睛圖示。");
    }

    m_targets = renderer->selectedTcls();
    if (m_targets.isEmpty()) {
        return CommandResult::Failure(
            "尚未選取任何線路 — 請先在 3D Alignment 顯示中點選（可 Shift 多選）"
            "欲加入垂直線形的折線，再執行本命令。");
    }

    m_step        = Step::PickStartRef;
    m_isFinishing = false;
    m_startElev   = 0.0;
    m_endElev     = 0.0;

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
                 tr("點擊某條 3D Alignment 折線以取得【起點】高程："));
    outputMessage("點擊某條 3D Alignment 折線以取得【起點】高程：");

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
        outputMessage("該處未命中任何 3D Alignment 折線 — 請直接點擊在折線上。");
        bus->publish(Events::COMMAND_PROMPT,
                     tr("請直接點擊在某條 3D Alignment 折線上："));
        return;
    }

    switch (m_step) {
    case Step::PickStartRef:
        m_startElev = elevation;
        outputMessage(QString("起點高程 = %1 m（里程 %2）")
                          .arg(elevation, 0, 'f', 3)
                          .arg(chainage, 0, 'f', 3));
        bus->publish(Events::COMMAND_PROMPT,
                     tr("點擊某條 3D Alignment 折線以取得【終點】高程："));
        m_step = Step::PickEndRef;
        break;

    case Step::PickEndRef:
        m_endElev = elevation;
        outputMessage(QString("終點高程 = %1 m（里程 %2）")
                          .arg(elevation, 0, 'f', 3)
                          .arg(chainage, 0, 'f', 3));
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
           "  3. 點擊某條 3D Alignment 折線 → 取得【起點】高程。\n"
           "  4. 點擊某條 3D Alignment 折線 → 取得【終點】高程。\n"
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
