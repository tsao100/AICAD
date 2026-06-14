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
#include <QDebug>

using namespace aicad::core;

namespace aicad {
namespace command {

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
    viewSetup["mode"] = "navigation"; // Alignment 命令用 Navigation 模式，POINT_ACQUIRED 發佈 QPointF（含 TM2 偏移）
    viewSetup["rubberBandMode"] = "line";
    bus->publish("command.request-view-setup", viewSetup);

    // ── Subscribe once; stays active until cleanup() ──────────────────
    bus->subscribe(Events::POINT_ACQUIRED, this,
        [this](const QVariant& data) {
            QVariantMap map = data.toMap();
            // Alignment 模式下 CadView 發佈 QPointF（double，含 TM2 偏移）
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
        rb["point"]  = QVariant::fromValue(point);   // QPointF — TM2 精度
        bus->publish("command.update-rubber-band", rb);

        bus->publish(Events::COMMAND_PROMPT,
                     tr("Specify end point [Right-click to finish]:"));
        outputMessage(QString("Start (%1, %2) — Specify end point:")
                          .arg(point.x(), 0, 'f', 3)
                          .arg(point.y(), 0, 'f', 3));
        return;
    }

    // ── Second click: commit segment ──────────────────────────────────
    QPointF p1 = m_startPoint;
    QPointF p2 = point;

    int idx = m_alignDoc->horizontal()->addFixedTangent(p1, p2);
    m_alignDoc->horizontal()->solve();   // changed() → AlignmentRenderer::refresh()

    outputMessage(QString("Fixed Tangent #%1  (%2,%3) → (%4,%5)")
                      .arg(idx)
                      .arg(p1.x(), 0, 'f', 3).arg(p1.y(), 0, 'f', 3)
                      .arg(p2.x(), 0, 'f', 3).arg(p2.y(), 0, 'f', 3));

    // Chain: current end becomes new start (continuous mode)
    m_startPoint = point;

    QVariantMap rb;
    rb["action"] = "clearAndAdd";
    rb["point"]  = QVariant::fromValue(point);   // QPointF — TM2 精度
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
