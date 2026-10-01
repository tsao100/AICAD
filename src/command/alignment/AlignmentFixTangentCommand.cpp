/**
 * @file AlignmentFixTangentCommand.cpp
 *
 * Follows LineCommand's continuous pattern exactly:
 *  - One POINT_ACQUIRED subscription for the whole command lifetime
 *  - Right-click (POINT_CANCELLED) ends the command
 *  - Rubber band driven via "command.update-rubber-band" EventBus event
 *  - unsubscribeAll only in cleanup(), never inside a callback
 */

#include "command/alignment/AlignmentFixTangentCommand.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "core/DocumentManager.h"
#include "cad/Document.h"
#include "ui/UIManager.h"
#include <QDebug>

using namespace aicad::core;

namespace aicad {
namespace command {

namespace {
/**
 * @brief 需求 10：把 @p alignDoc 目前的求解結果，同步寫回它所屬的那條
 *        tcl->horizontal()（權威資料），讓 FT/FC 加入的元素立即成為
 *        「唯一一套 Alignment data」的一部分，而不是只留在暫存、其他
 *        命令（例如 ADC）需要靠 fallback 才讀得到的 AlignmentDocument
 *        裡（見 AlignmentDrawChainCommand::resolveExistingPoints() 對同一
 *        問題的說明）。比照 AlignmentQuickTableCommand／ImportAldCommand
 *        的既定慣例，同時呼叫 setAldHorizontalImport() 把這批資料標記為
 *        權威來源（供 getXYZForCalc() 等「優先讀 ALD import」的查詢使用）。
 *        找不到對應的 tcl（理論上不會發生，因為 AlignmentDocument 一律
 *        由某條既有 tcl 建立）時安靜略過，不影響指令繼續執行。
 */
void syncAlignmentDocToOwningTcl(railway::AlignmentDocument* alignDoc)
{
    if (!alignDoc) return;
    auto* uiMgr = core::Application::instance()->uiManager();
    if (!uiMgr) return;
    const QString tclId = uiMgr->tclAlignmentDocs().key(alignDoc, QString());
    if (tclId.isEmpty()) return;
    auto* doc = core::Application::instance()->documentManager()->currentDocument();
    if (!doc) return;
    auto* tcl = doc->findTrackCenterLine(tclId);
    if (!tcl) return;
    const railway::HorizontalAlignment* ha = alignDoc->horizontal()->result();
    if (!ha || ha->isEmpty()) return;
    tcl->setAldHorizontalImport(ha->rawPoints());
    tcl->loadHorizontal(ha->rawPoints());
    doc->setModified(true);
    // 需求 15／16：同 AlignmentDrawChainCommand，主動確保這條線立刻畫出來。
    uiMgr->ensureTclDisplayed(tclId);
}
} // namespace

AlignmentFixTangentCommand::AlignmentFixTangentCommand(QObject* parent)
    : Command("alignmentfixtangent", "Add Fixed Tangent", parent)
{}

CommandResult AlignmentFixTangentCommand::execute(const CommandContext& context)
{
    m_alignDoc = context.alignmentDoc;
    if (!m_alignDoc) {
        return CommandResult::Failure(
            "No AlignmentDocument — open or create an alignment first.");
    }

    m_hasStartPoint = false;
    m_isFinishing   = false;

    EventBus* bus = Application::instance()->eventBus();

    // Ask UIManager to activate Line rubber-band mode (same as LineCommand)
    QVariantMap viewSetup;
    viewSetup["mode"]          = "sketching";
    viewSetup["rubberBandMode"] = "line";
    bus->publish("command.request-view-setup", viewSetup);

    // ── Subscribe once; stays active until cleanup() ──────────────────
    bus->subscribe(Events::POINT_ACQUIRED, this,
        [this](const QVariant& data) {
            QVariantMap map = data.toMap();
            QPointF pt = map["point"].value<QPointF>();
            QMetaObject::invokeMethod(this, [this, pt]() {
                handlePointAcquired(pt);
            }, Qt::QueuedConnection);
        });

    // Right-click → POINT_CANCELLED → finish
    bus->subscribe(Events::POINT_CANCELLED, this,
        [this](const QVariant&) {
            QMetaObject::invokeMethod(this, [this]() {
                handleCancelled();
            }, Qt::QueuedConnection);
        });

    setState(CommandState::Running);   // keeps command alive between clicks

    bus->publish(Events::COMMAND_PROMPT, tr("Fixed Tangent — Specify start point:"));
    outputMessage("Fixed Tangent — Specify start point:");
    return CommandResult::Success("Waiting for input");
}

void AlignmentFixTangentCommand::handlePointAcquired(const QPointF& point)
{
    if (m_isFinishing) return;

    EventBus* bus = Application::instance()->eventBus();

    if (!m_hasStartPoint) {
        // ── First click: anchor rubber band ───────────────────────────
        m_startPoint    = point;
        m_hasStartPoint = true;

        QVariantMap rb;
        rb["action"] = "clearAndAdd";
        rb["point"]  = QVariant::fromValue(point);
        bus->publish("command.update-rubber-band", rb);

        bus->publish(Events::COMMAND_PROMPT,
                     tr("Specify end point [Right-click to finish]:"));
        outputMessage(QString("Start (%1, %2) — Specify end point:")
                          .arg(point.x(), 0, 'f', 3)
                          .arg(point.y(), 0, 'f', 3));
        return;
    }

    // ── Second click: commit segment ──────────────────────────────────
    int idx = m_alignDoc->horizontal()->addFixedTangent(m_startPoint, point);
    m_alignDoc->horizontal()->solve();   // changed() → AlignmentRenderer::refresh()
    syncAlignmentDocToOwningTcl(m_alignDoc);   // 需求 10：立即同步回權威 tcl

    outputMessage(QString("Fixed Tangent #%1  (%2,%3) → (%4,%5)")
                      .arg(idx)
                      .arg(m_startPoint.x(), 0, 'f', 2).arg(m_startPoint.y(), 0, 'f', 2)
                      .arg(point.x(), 0, 'f', 2).arg(point.y(), 0, 'f', 2));

    // Chain: current end becomes new start (continuous mode)
    m_startPoint = point;

    QVariantMap rb;
    rb["action"] = "clearAndAdd";
    rb["point"]  = QVariant::fromValue(point);
    bus->publish("command.update-rubber-band", rb);

    bus->publish(Events::COMMAND_PROMPT,
                 tr("Specify next end point [Right-click to finish]:"));
}

void AlignmentFixTangentCommand::handleCancelled()
{
    qDebug() << "[FT] Right-click — finishing";
    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("Fixed Tangent command completed"));
}

void AlignmentFixTangentCommand::cleanup()
{
    EventBus* bus = Application::instance()->eventBus();

    // Unsubscribe first, then request rubber-band clear (same order as LineCommand)
    bus->unsubscribeAll(this);

    QVariantMap rb;
    rb["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", rb);

    m_hasStartPoint = false;
    m_isFinishing   = false;
    m_alignDoc      = nullptr;
}

QString AlignmentFixTangentCommand::getUsage() const
{
    return "Usage: FT\n"
           "  Click start, then click each end point to chain tangents.\n"
           "  Right-click to finish.";
}

} // namespace command
} // namespace aicad
