#include "command/EllipseCommand.h"
#include "command/Command.h"
#include "command/CommandTypes.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include <QtMath>

using namespace aicad::core;

namespace aicad {
namespace command {

EllipseCommand::EllipseCommand(QObject* parent)
    : Command("ellipse", "Draw Ellipse", parent)
    , m_inputState(InputState::WaitingForCenter)
    , m_isFinishing(false)
{
}

EllipseCommand::~EllipseCommand() {
}

CommandResult EllipseCommand::execute(const CommandContext& context) {
    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // Non-interactive mode (with coordinates: cx cy majorX majorY minorRadius)
    if (context.args.size() >= 5) {
        // ✅ Use EventBus to request sketch state
        QVariantMap request;
        request["commandId"] = "ellipse";
        request["args"] = QVariant::fromValue(context.args);
        bus->publish("command.request-sketch-ellipse", request);
        return CommandResult::Success("Ellipse creation requested");
    }

    // Interactive mode
    m_inputState = InputState::WaitingForCenter;
    m_isFinishing = false;

    // ✅ Request view setup via EventBus
    QVariantMap viewSetup;
    viewSetup["mode"] = "sketching";
    viewSetup["rubberBandMode"] = "ellipse";
    bus->publish("command.request-view-setup", viewSetup);

    bus->publish(Events::COMMAND_PROMPT, "Specify center point:");
    bus->publish(Events::COMMAND_LOG, "Specify center point:");

    // ✅ Subscribe with Qt::QueuedConnection for safety
    bus->subscribe(Events::POINT_ACQUIRED, this,
                   [this](const QVariant& data) {
                       QVariantMap map = data.toMap();
                       QVector2D point = map["point"].value<QVector2D>();

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

    setState(CommandState::Running);

    outputMessage("Specify center point:");
    return CommandResult::Success("Waiting for input");
}

// ✅ Handle point acquisition
void EllipseCommand::handlePointAcquired(QVector2D point)
{
    if (m_isFinishing) return;

    qDebug() << "[EllipseCommand] Point acquired:"
             << point.x() << "," << point.y()
             << "State:" << static_cast<int>(m_inputState);

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    switch (m_inputState) {
    case InputState::WaitingForCenter:
    {
        // === Center point ===
        m_center = point;
        m_inputState = InputState::WaitingForMajorAxis;

        // ✅ Request rubber band update via EventBus
        QVariantMap rubberUpdate;
        rubberUpdate["action"] = "clearAndAdd";
        rubberUpdate["point"] = QVariant::fromValue(point);
        bus->publish("command.update-rubber-band", rubberUpdate);

        outputMessage(QString("Center: (%1, %2). Specify endpoint of major axis:")
                          .arg(point.x()).arg(point.y()));

        bus->publish(Events::COMMAND_PROMPT, "Specify endpoint of major axis:");
        break;
    }

    case InputState::WaitingForMajorAxis:
    {
        // === Major axis endpoint ===
        m_majorAxisEnd = point;
        m_inputState = InputState::WaitingForMinorAxis;

        // Calculate major axis radius for display
        QVector2D majorVector = m_majorAxisEnd - m_center;
        float majorRadius = majorVector.length();

        // ✅ Update rubber band to show major axis
        QVariantMap rubberUpdate;
        rubberUpdate["action"] = "addPoint";
        rubberUpdate["point"] = QVariant::fromValue(point);
        bus->publish("command.update-rubber-band", rubberUpdate);

        outputMessage(QString("Major axis: %1. Specify minor axis distance:")
                          .arg(majorRadius));

        bus->publish(Events::COMMAND_PROMPT, "Specify minor axis distance:");
        break;
    }

    case InputState::WaitingForMinorAxis:
    {
        // === Minor axis distance ===
        // Calculate minor radius from the distance to the point
        // Project point onto the perpendicular to major axis
        QVector2D majorVector = m_majorAxisEnd - m_center;
        QVector2D toPoint = point - m_center;

        // Get perpendicular vector to major axis
        QVector2D majorNormalized = majorVector.normalized();
        QVector2D perpendicular(-majorNormalized.y(), majorNormalized.x());

        // Project the point onto the perpendicular direction to get minor radius
        float minorRadius = qAbs(QVector2D::dotProduct(toPoint, perpendicular));

        // If the point is too close to center, use distance to point as minor radius
        if (minorRadius < 0.001f) {
            minorRadius = toPoint.length();
        }

        float majorRadius = majorVector.length();

        // ✅ Request ellipse creation via EventBus
        QVariantMap ellipseData;
        ellipseData["center"] = QVariant::fromValue(m_center);
        ellipseData["majorAxisEnd"] = QVariant::fromValue(m_majorAxisEnd);
        ellipseData["minorRadius"] = minorRadius;
        ellipseData["majorRadius"] = majorRadius;
        bus->publish("command.create-sketch-ellipse", ellipseData);

        outputMessage(QString("Ellipse created: center (%1,%2), major radius %3, minor radius %4")
                          .arg(m_center.x()).arg(m_center.y())
                          .arg(majorRadius).arg(minorRadius));

        // ✅ Mark as finishing and emit finished
        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Success("Ellipse command completed"));
        break;
    }
    }
}

// ✅ Handle cancellation
void EllipseCommand::handleCancelled() {
    qDebug() << "[EllipseCommand] Cancelled via EventBus";

    m_isFinishing = true;  // ✅ Set flag to prevent further point handling

    // ✅ Emit finished with appropriate message based on state
    QString msg;
    switch (m_inputState) {
    case InputState::WaitingForCenter:
        msg = "Ellipse command cancelled (no points specified)";
        break;
    case InputState::WaitingForMajorAxis:
        msg = "Ellipse command cancelled (center specified)";
        break;
    case InputState::WaitingForMinorAxis:
        msg = "Ellipse command cancelled (center and major axis specified)";
        break;
    }

    Q_EMIT finished(CommandResult::Success(msg));
}

void EllipseCommand::cleanup() {
    qDebug() << "[EllipseCommand] Cleanup started";

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // ✅ Unsubscribe FIRST, before changing view mode
    bus->unsubscribeAll(this);
    qDebug() << "[EllipseCommand] Unsubscribed from EventBus";

    // ✅ Request cleanup via EventBus
    QVariantMap cleanupRequest;
    cleanupRequest["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", cleanupRequest);

    m_inputState = InputState::WaitingForCenter;
    m_isFinishing = false;
    qDebug() << "[EllipseCommand] Cleanup completed";
}

QString EllipseCommand::getUsage() const {
    return "Usage: ellipse [cx cy majorX majorY minorRadius]\n"
           "  Interactive mode:\n"
           "    1. Click to specify center point\n"
           "    2. Click to specify endpoint of major axis\n"
           "    3. Click to specify minor axis distance\n"
           "  Non-interactive: Provide center (cx,cy), major axis endpoint (majorX,majorY), and minor radius";
}

} // namespace command
} // namespace aicad
