#include "command/CircleCommand.h"
#include "command/Command.h"
#include "command/CommandTypes.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include <cmath>

using namespace aicad::core;

namespace aicad {
namespace command {

CircleCommand::CircleCommand(QObject* parent)
    : Command("circle", "Draw Circle", parent)
    , m_hasCenterPoint(false)
    , m_isFinishing(false)
{
}

CircleCommand::~CircleCommand() {
}

CommandResult CircleCommand::execute(const CommandContext& context) {
    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // Non-interactive mode (center x, center y, radius)
    if (context.args.size() >= 3) {
        QVariantMap request;
        request["commandId"] = "circle";
        request["args"] = QVariant::fromValue(context.args);
        bus->publish("command.request-sketch-circle", request);
        return CommandResult::Success("Circle creation requested");
    }

    // Interactive mode
    m_hasCenterPoint = false;
    m_isFinishing = false;

    // Request view setup via EventBus
    QVariantMap viewSetup;
    viewSetup["mode"] = "sketching";
    viewSetup["rubberBandMode"] = "circle";
    bus->publish("command.request-view-setup", viewSetup);

    bus->publish(Events::COMMAND_PROMPT, "Specify center point:");
    bus->publish(Events::COMMAND_LOG, "Specify center point:");

    // Subscribe to point and cancel events
    bus->subscribe(Events::POINT_ACQUIRED, this,
                   [this](const QVariant& data) {
                       QVariantMap map = data.toMap();
                       QVector2D point = map["point"].value<QVector2D>();

                       QMetaObject::invokeMethod(this, [this, point]() {
                           this->handlePointAcquired(point);
                       }, Qt::QueuedConnection);
                   });

    bus->subscribe(Events::POINT_CANCELLED, this,
                   [this](const QVariant&) {
                       QMetaObject::invokeMethod(this, [this]() {
                           this->handleCancelled();
                       }, Qt::QueuedConnection);
                   });

    setState(CommandState::Running);

    outputMessage("Specify center point:");
    return CommandResult::Success("Waiting for input");
}

void CircleCommand::handlePointAcquired(QVector2D point)
{
    if (m_isFinishing) return;

    qDebug() << "[CircleCommand] Point acquired:"
             << point.x() << "," << point.y();

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    if (!m_hasCenterPoint) {
        // === First point: center ===
        m_centerPoint = point;
        m_hasCenterPoint = true;

        // Request rubber band update to show circle preview
        QVariantMap rubberUpdate;
        rubberUpdate["action"] = "clearAndAdd";
        rubberUpdate["point"] = QVariant::fromValue(point);
        bus->publish("command.update-rubber-band", rubberUpdate);

        outputMessage(QString("Center point: (%1, %2). Specify radius point:")
                          .arg(point.x()).arg(point.y()));

        bus->publish(Events::COMMAND_PROMPT, "Specify radius point or press ESC to cancel:");
        return;
    }

    // === Second point: radius ===
    float dx = point.x() - m_centerPoint.x();
    float dy = point.y() - m_centerPoint.y();
    float radius = sqrt(dx * dx + dy * dy);

    // Request circle creation via EventBus
    QVariantMap circleData;
    circleData["centerPoint"] = QVariant::fromValue(m_centerPoint);
    circleData["radius"] = radius;
    bus->publish("command.create-sketch-circle", circleData);

    outputMessage(QString("Circle created at (%1, %2) with radius %3")
                      .arg(m_centerPoint.x()).arg(m_centerPoint.y())
                      .arg(radius));

    // Circle is fully defined — finish the command
    m_hasCenterPoint = false;
    m_isFinishing = true;
    // 清除 rubber band，等待下一次輸入
    QVariantMap rubberClear;
    rubberClear["action"] = "clear";
    bus->publish("command.update-rubber-band", rubberClear);
    Q_EMIT finished(CommandResult::Success("Circle command completed"));
}

void CircleCommand::handleCancelled() {
    qDebug() << "[CircleCommand] Cancelled via EventBus";

    m_isFinishing = true;

    Q_EMIT finished(CommandResult::Success("Circle command cancelled"));
}

void CircleCommand::cleanup() {
    qDebug() << "[CircleCommand] Cleanup started";

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // Unsubscribe FIRST, before changing view mode
    bus->unsubscribeAll(this);
    qDebug() << "[CircleCommand] Unsubscribed from EventBus";

    // Request cleanup via EventBus
    QVariantMap cleanupRequest;
    cleanupRequest["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", cleanupRequest);

    m_hasCenterPoint = false;
    m_isFinishing = false;
    qDebug() << "[CircleCommand] Cleanup completed";
}

QString CircleCommand::getUsage() const {
    return "Usage: circle [cx cy radius]";
}

} // namespace command
} // namespace aicad
