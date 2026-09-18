/**
 * @file AlignmentDrawChainCommand.cpp
 * @brief Implementation of ALIGNMENTDRAWCHAIN — see the header for design.
 */
#include "command/alignment/AlignmentDrawChainCommand.h"

#include "core/Application.h"
#include "core/DocumentManager.h"
#include "core/CommandLineManager.h"
#include "core/EventBus.h"
#include "core/geometry/ProjectOrigin.h"
#include "cad/Document.h"
#include "railway/AlignmentQuickCalc.h"
#include "railway/RailwayAlignment.h"
#include "ui/AlignmentDrawElementDialog.h"
#include "ui/UIManager.h"
#include "view/CadView.h"   // CadView : public QWidget — 供 static_cast(context.cadView) 使用

#include <QMetaObject>
#include <cmath>
#include <limits>

using namespace aicad::core;

namespace aicad {
namespace command {

using railway::TrackCenterLine;

namespace {

void convertLocalToTM2(QVector<railway::AlignmentPoint>& pts)
{
    core::geometry::ProjectOrigin::ensureDefault();
    auto& origin = core::geometry::ProjectOrigin::instance();
    for (railway::AlignmentPoint& pt : pts) {
        const QPointF global = origin.toGlobal(pt.easting, pt.northing);
        pt.easting  = global.x();
        pt.northing = global.y();
    }
}

void convertTM2ToLocal(QVector<railway::AlignmentPoint>& pts)
{
    core::geometry::ProjectOrigin::ensureDefault();
    auto& origin = core::geometry::ProjectOrigin::instance();
    for (railway::AlignmentPoint& pt : pts) {
        const QPointF local = origin.toLocal(pt.easting, pt.northing);
        pt.easting  = local.x();
        pt.northing = local.y();
    }
}

/**
 * @brief 取得既有關鍵點序列（Local 座標）。
 *
 * 刻意直接讀 tcl->horizontal()->rawPoints()，不透過
 * ensureTclAlignmentDocument()+solve() 改讀「已求解的 EditableElement 鏈
 * 結果」——那條路徑本身是一套複雜的模式比對／重建邏輯，對某些點序列可能
 * 會合併／省略中間關鍵點，一旦當成 ADC 的「既有資料」寫回，會把簡化過的
 * 版本永久取代掉原始資料（實測會看到緩和曲線消失）。寧可犧牲「元素只存在
 * 尚未同步的 EditableElement 鏈」這種邊角情況，也要優先保證不遺失資料。
 */
QVector<railway::AlignmentPoint> existingRawPoints(TrackCenterLine* tcl)
{
    return (tcl && tcl->horizontal()) ? tcl->horizontal()->rawPoints()
                                       : QVector<railway::AlignmentPoint>();
}

} // namespace

// ============================================================================
//  ctor / usage
// ============================================================================

AlignmentDrawChainCommand::AlignmentDrawChainCommand(QObject* parent)
    : Command("alignmentdrawchain", "Draw Alignment Chain", parent)
{}

QString AlignmentDrawChainCommand::getUsage() const
{
    return "Usage: alignmentdrawchain — 點選既有線形的起點或終點，接續繪製"
           "一段或二段新線元";
}

// ============================================================================
//  execute
// ============================================================================

CommandResult AlignmentDrawChainCommand::execute(const CommandContext& context)
{
    Application* app = Application::instance();
    cad::Document* doc = app->documentManager()->currentDocument();
    if (!doc)
        return CommandResult::Failure("No active document");

    if (doc->trackCenterLines().isEmpty()) {
        return CommandResult::Failure(
            "文件內尚無任何軌道中心線可供接續；請先用 ALIGNMENTQUICKTABLE (AQT) 建立一條。");
    }

    m_parentWidget = static_cast<QWidget*>(context.cadView);
    m_isFinishing  = false;

    EventBus* bus = app->eventBus();

    if (auto* uiMgr = app->uiManager()) {
        if (auto* cadView = uiMgr->cadView())
            cadView->setMode(view::InteractionMode::Sketching);
    }

    bus->subscribe(Events::POINT_ACQUIRED, this,
                   [this](const QVariant& data) {
                       const QVariantMap map = data.toMap();
                       const QPointF pt = map["point"].value<QPointF>();
                       QMetaObject::invokeMethod(this, [this, pt]() {
                           handlePointAcquired(pt);
                       }, Qt::QueuedConnection);
                   });

    bus->subscribe(Events::POINT_CANCELLED, this,
                   [this](const QVariant&) {
                       QMetaObject::invokeMethod(this, [this]() {
                           handleCancelled();
                       }, Qt::QueuedConnection);
                   });

    setState(CommandState::Running);
    bus->publish(Events::COMMAND_PROMPT,
                 tr("ADC — 點選要接續的既有線形（點在較靠近起點或終點的位置，"
                    "決定要從哪一端接續）："));
    outputMessage("ADC — 點選一條既有線形靠近起點或終點的位置，決定接續方向後，"
                  "於對話框內設定一或二段新線元。");
    return CommandResult::Success("Waiting for input");
}

// ============================================================================
//  handlePointAcquired / handleCancelled
// ============================================================================

void AlignmentDrawChainCommand::handlePointAcquired(const QPointF& point)
{
    QString tclId;
    bool isStart = false;
    if (!findNearestEndpoint(point, tclId, isStart)) {
        outputMessage(tr("找不到任何含關鍵點的線形，請重新點選，或按 Esc 取消。"));
        return;
    }
    proceedWithEndpoint(tclId, isStart);
}

void AlignmentDrawChainCommand::handleCancelled()
{
    cleanup();
    Q_EMIT finished(CommandResult::Failure("Cancelled"));
}

// ============================================================================
//  findNearestEndpoint
// ============================================================================

bool AlignmentDrawChainCommand::findNearestEndpoint(const QPointF& worldPos, QString& outTclId,
                                                     bool& outIsStart) const
{
    cad::Document* doc = Application::instance()->documentManager()->currentDocument();
    if (!doc)
        return false;

    double bestDist2 = std::numeric_limits<double>::max();
    QString bestTclId;
    bool bestIsStart = false;
    bool found = false;

    for (TrackCenterLine* tcl : doc->trackCenterLines()) {
        if (!tcl || !tcl->horizontal())
            continue;
        const QVector<railway::AlignmentPoint>& pts = tcl->horizontal()->rawPoints();
        if (pts.isEmpty())
            continue;

        const railway::AlignmentPoint& startPt = pts.first();
        const railway::AlignmentPoint& endPt   = pts.last();

        const double dxs = startPt.easting - worldPos.x();
        const double dys = startPt.northing - worldPos.y();
        const double d2s = dxs * dxs + dys * dys;
        if (d2s < bestDist2) {
            bestDist2 = d2s;
            bestTclId = tcl->id();
            bestIsStart = true;
            found = true;
        }

        const double dxe = endPt.easting - worldPos.x();
        const double dye = endPt.northing - worldPos.y();
        const double d2e = dxe * dxe + dye * dye;
        if (d2e < bestDist2) {
            bestDist2 = d2e;
            bestTclId = tcl->id();
            bestIsStart = false;
            found = true;
        }
    }

    if (!found)
        return false;
    outTclId = bestTclId;
    outIsStart = bestIsStart;
    return true;
}

// ============================================================================
//  proceedWithEndpoint
// ============================================================================

void AlignmentDrawChainCommand::proceedWithEndpoint(const QString& tclId, bool extendFromStart)
{
    Application* app = Application::instance();
    cad::Document* doc = app->documentManager()->currentDocument();
    TrackCenterLine* tcl = doc ? doc->findTrackCenterLine(tclId) : nullptr;
    if (!tcl) {
        cleanup();
        Q_EMIT finished(CommandResult::Failure("找不到指定的軌道中心線"));
        return;
    }

    QVector<railway::AlignmentPoint> existing = existingRawPoints(tcl);
    if (existing.isEmpty()) {
        cleanup();
        Q_EMIT finished(CommandResult::Failure("這條線路沒有任何關鍵點資料"));
        return;
    }

    // 需求 20：若判定為從「起點」端接續，先整條反轉方向，讓原本的起點變成
    // 反轉後陣列的「終點」，即可直接沿用既有「從終點接續」的計算與對話框
    // 邏輯，不需要另外實作一套反向接續路徑——見
    // railway::reverseAlignmentPoints() 的說明。
    QVector<railway::AlignmentPoint> workingLocal =
        extendFromStart ? railway::reverseAlignmentPoints(existing) : existing;

    QVector<railway::AlignmentPoint> workingTM2 = workingLocal;
    convertLocalToTM2(workingTM2);

    Application::instance()->eventBus()->publish(
        Events::COMMAND_PROMPT, tr("ADC — 於對話框中設定新線元..."));

    auto* dlg = new ui::AlignmentDrawElementDialog(tcl->name(), workingTM2, m_parentWidget);
    const int result = dlg->exec();

    if (result != QDialog::Accepted) {
        dlg->deleteLater();
        cleanup();
        Q_EMIT finished(CommandResult::Failure("使用者取消"));
        return;
    }

    QVector<railway::AlignmentPoint> resultTM2 = dlg->resultPoints();
    dlg->deleteLater();

    if (resultTM2.size() < 2) {
        cleanup();
        Q_EMIT finished(CommandResult::Failure("結果關鍵點不足"));
        return;
    }

    QVector<railway::AlignmentPoint> resultLocal = resultTM2;
    convertTM2ToLocal(resultLocal);

    // 若前面反轉過，這裡要轉回真正的方向再寫回。
    QVector<railway::AlignmentPoint> finalLocal =
        extendFromStart ? railway::reverseAlignmentPoints(resultLocal) : resultLocal;

    tcl->setAldHorizontalImport(finalLocal);
    tcl->loadHorizontal(finalLocal);
    doc->setModified(true);

    // 單一資料存取：raw points 已更新，讓快取的 AlignmentDocument（供
    // 「線形資料表」／grip 編輯使用）失效，避免與此處剛寫入的資料分岔。
    if (Application::instance()->uiManager())
        Application::instance()->uiManager()->invalidateTclAlignmentDocument(tclId);

    // 注意：不在此自動切換／開啟「Railway 3D Alignment」資料夾顯示。

    const QString summary = QStringLiteral("接續繪製完成: %1（%2端接續，共 %3 個關鍵點）")
                                 .arg(tcl->name())
                                 .arg(extendFromStart ? tr("起點") : tr("終點"))
                                 .arg(finalLocal.size());
    outputMessage(QStringLiteral("[接續繪製線形] %1").arg(summary));

    m_isFinishing = true;
    cleanup();
    Q_EMIT finished(CommandResult::Success(summary));
}

// ============================================================================
//  cleanup
// ============================================================================

void AlignmentDrawChainCommand::cleanup()
{
    EventBus* bus = Application::instance()->eventBus();
    bus->unsubscribeAll(this);

    if (auto* uiMgr = Application::instance()->uiManager()) {
        if (auto* cadView = uiMgr->cadView())
            cadView->setMode(view::InteractionMode::Sketching);
    }

    m_parentWidget = nullptr;
    m_isFinishing  = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Static registration — 指令 "alignmentdrawchain" 觸發 AlignmentDrawChainCommand
// ─────────────────────────────────────────────────────────────────────────────
REGISTER_COMMAND("alignmentdrawchain", AlignmentDrawChainCommand);

} // namespace command
} // namespace aicad
