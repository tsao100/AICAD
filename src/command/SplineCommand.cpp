#include "command/SplineCommand.h"
#include "command/Command.h"
#include "command/CommandTypes.h"
#include "core/Application.h"
#include "core/EventBus.h"

using namespace aicad::core;

namespace aicad {
namespace command {

SplineCommand::SplineCommand(QObject* parent)
    : Command("spline", "Draw Spline", parent)
    , m_isFinishing(false)
{
}

SplineCommand::~SplineCommand() {
}

CommandResult SplineCommand::execute(const CommandContext& context) {
    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // Non-interactive mode (with coordinates: x1 y1 x2 y2 x3 y3 ...)
    if (context.args.size() >= 6) {  // At least 3 points (6 coordinates)
        // ✅ Use EventBus to request sketch state
        QVariantMap request;
        request["commandId"] = "spline";
        request["args"] = QVariant::fromValue(context.args);
        bus->publish("command.request-sketch-spline", request);
        return CommandResult::Success("Spline creation requested");
    }

    // Interactive mode
    m_controlPoints.clear();
    m_isFinishing = false;

    // ✅ Request view setup via EventBus
    QVariantMap viewSetup;
    viewSetup["mode"] = "sketching";
    viewSetup["rubberBandMode"] = "spline";
    bus->publish("command.request-view-setup", viewSetup);

    bus->publish(Events::COMMAND_PROMPT, "Specify first point:");

    // ✅ Subscribe with Qt::QueuedConnection for safety
    bus->subscribe(Events::POINT_ACQUIRED, this,
                   [this](const QVariant& data) {
                       QVariantMap map = data.toMap();
                       QPointF _ptF = map["point"].value<QPointF>();
                       QVector2D point(static_cast<float>(_ptF.x()), static_cast<float>(_ptF.y()));

                       // ✅ Use QMetaObject::invokeMethod for thread safety
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

    setState(CommandState::Running);  // 關鍵一行

    outputMessage("Specify first point:");
    return CommandResult::Success("Waiting for input");
}

// ✅ Renamed from onPointAcquired
void SplineCommand::handlePointAcquired(QVector2D point)
{
    if (m_isFinishing) return;

    qDebug() << "[SplineCommand] Point acquired:"
             << point.x() << "," << point.y()
             << "Total points:" << (m_controlPoints.size() + 1);

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // Add the new control point
    m_controlPoints.append(point);

    // ✅ Update rubber band to show current spline preview
    QVariantMap rubberUpdate;
    rubberUpdate["action"] = "addPoint";
    rubberUpdate["point"] = QVariant::fromValue(QPointF(point.x(), point.y()));
    bus->publish("command.update-rubber-band", rubberUpdate);

    // Provide feedback based on point count
    if (m_controlPoints.size() == 1) {
        outputMessage(QString("First point: (%1, %2). Specify next point:")
                          .arg(point.x()).arg(point.y()));
        bus->publish(Events::COMMAND_PROMPT, "Specify next point:");
    }
    else if (m_controlPoints.size() == 2) {
        outputMessage(QString("Second point: (%1, %2). Specify third point:")
                          .arg(point.x()).arg(point.y()));
        bus->publish(Events::COMMAND_PROMPT, "Specify third point:");
    }
    else {
        // We have enough points to create a spline (3+)
        outputMessage(QString("Point %1: (%2, %3). Specify next point or press ESC to finish:")
                          .arg(m_controlPoints.size())
                          .arg(point.x()).arg(point.y()));
        bus->publish(Events::COMMAND_PROMPT, "Specify next point or press ESC to finish");
    }
}

// ✅ Renamed from onCancelled
void SplineCommand::handleCancelled() {
    qDebug() << "[SplineCommand] Cancelled via EventBus, points:" << m_controlPoints.size();

    m_isFinishing = true;  // ✅ Set flag to prevent further point handling

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // Check if we have enough points to create a spline
    if (m_controlPoints.size() < MIN_POINTS) {
        outputMessage(QString("Spline cancelled. Need at least %1 points, but only have %2")
                          .arg(MIN_POINTS)
                          .arg(m_controlPoints.size()));
        Q_EMIT finished(CommandResult::Failure("Not enough points for spline"));
        return;
    }

    // ✅ Request spline creation via EventBus
    QVariantMap splineData;
    splineData["controlPoints"] = QVariant::fromValue(m_controlPoints);
    bus->publish("command.create-sketch-spline", splineData);

    outputMessage(QString("Spline created with %1 control points")
                      .arg(m_controlPoints.size()));

    // ✅ Just emit finished with success
    Q_EMIT finished(CommandResult::Success("Spline command completed"));
}

void SplineCommand::cleanup() {
    qDebug() << "[SplineCommand] Cleanup started";

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // ✅ Unsubscribe FIRST, before changing view mode
    bus->unsubscribeAll(this);
    qDebug() << "[SplineCommand] Unsubscribed from EventBus";

    // ✅ Request cleanup via EventBus
    QVariantMap cleanupRequest;
    cleanupRequest["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", cleanupRequest);

    m_controlPoints.clear();
    m_isFinishing = false;
    qDebug() << "[SplineCommand] Cleanup completed";
}

QString SplineCommand::getUsage() const {
    return "Usage: spline [x1 y1 x2 y2 x3 y3 ...]\n"
           "Interactive mode: Click to add control points (min 3), press ESC to finish";
}

} // namespace command
} // namespace aicad
