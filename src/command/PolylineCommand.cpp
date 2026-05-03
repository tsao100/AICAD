#include "command/PolylineCommand.h"
#include "command/Command.h"
#include "command/CommandTypes.h"
#include "core/Application.h"
#include "core/EventBus.h"

using namespace aicad::core;

namespace aicad {
namespace command {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

PolylineCommand::PolylineCommand(QObject* parent)
    : Command("polyline", "Draw Polyline", parent)
    , m_isFinishing(false)
{
}

PolylineCommand::~PolylineCommand() {
}

// ---------------------------------------------------------------------------
// execute()
// ---------------------------------------------------------------------------

CommandResult PolylineCommand::execute(const CommandContext& context) {
    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    // ------------------------------------------------------------------
    // Non-interactive mode: args must contain an even number of values
    // with at least 4 (i.e. 2 points).
    //   polyline x1 y1  x2 y2  [x3 y3 ...]
    // ------------------------------------------------------------------
    if (context.args.size() >= 4 && context.args.size() % 2 == 0) {
        QVariantMap request;
        request["commandId"] = "polyline";
        request["args"]      = QVariant::fromValue(context.args);
        bus->publish("command.request-sketch-polyline", request);
        return CommandResult::Success("Polyline creation requested");
    }

    // ------------------------------------------------------------------
    // Interactive mode
    // ------------------------------------------------------------------
    m_points.clear();
    m_isFinishing = false;

    // Request view setup
    QVariantMap viewSetup;
    viewSetup["mode"]           = "sketching";
    viewSetup["rubberBandMode"] = "polyline";
    bus->publish("command.request-view-setup", viewSetup);

    bus->publish(Events::COMMAND_PROMPT, "Specify first point:");

    // Subscribe to point / cancel events
    bus->subscribe(Events::POINT_ACQUIRED, this,
                   [this](const QVariant& data) {
                       QVariantMap  map   = data.toMap();
                       QVector2D    point = map["point"].value<QVector2D>();
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

    outputMessage("Specify first point:");
    return CommandResult::Success("Waiting for input");
}

// ---------------------------------------------------------------------------
// handlePointAcquired()
// ---------------------------------------------------------------------------

void PolylineCommand::handlePointAcquired(QVector2D point) {
    if (m_isFinishing) return;

    qDebug() << "[PolylineCommand] Point acquired:" << point.x() << "," << point.y();

    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    const bool isFirstPoint = m_points.isEmpty();
    m_points.append(point);

    // Update rubber-band anchor to the latest point
    QVariantMap rubberUpdate;
    rubberUpdate["action"] = "addPoint";
    rubberUpdate["point"]  = QVariant::fromValue(point);
    bus->publish("command.update-rubber-band", rubberUpdate);

    if (isFirstPoint) {
        outputMessage(QString("First point: (%1, %2). Specify next point:")
                          .arg(point.x()).arg(point.y()));
        bus->publish(Events::COMMAND_PROMPT,
                     "Specify next point or press ESC to finish:");
        return;
    }

    // We now have at least 2 points – draw the latest segment for live preview
    QVariantMap segData;
    segData["startPoint"] = QVariant::fromValue(m_points[m_points.size() - 2]);
    segData["endPoint"]   = QVariant::fromValue(point);
    bus->publish("command.preview-sketch-segment", segData);

    outputMessage(QString("Point %1: (%2, %3). Specify next point or press ESC to finish:")
                      .arg(m_points.size())
                      .arg(point.x()).arg(point.y()));

    bus->publish(Events::COMMAND_PROMPT,
                 QString("Specify next point or press ESC to finish [%1 pts]:")
                     .arg(m_points.size()));
}

// ---------------------------------------------------------------------------
// handleCancelled()  – ESC pressed
// ---------------------------------------------------------------------------

void PolylineCommand::handleCancelled() {
    qDebug() << "[PolylineCommand] Cancelled via EventBus";

    if (m_points.size() >= 2) {
        // Enough points collected – commit the polyline then finish
        finalise();
    } else {
        // Not enough points; abort without creating geometry
        qDebug() << "[PolylineCommand] Not enough points – aborting";
        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Failure("Polyline cancelled – need at least 2 points"));
    }
}

// ---------------------------------------------------------------------------
// finalise()  – publish the completed polyline and signal done
// ---------------------------------------------------------------------------

void PolylineCommand::finalise() {
    m_isFinishing = true;

    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    // Pack all vertices into a QVariantList
    // QVariantList vertices;
    // for (const QVector2D& pt : m_points)
    //     vertices.append(QVariant::fromValue(pt));

    QVariantMap polylineData;
    polylineData["points"]     = QVariant::fromValue<QVector<QVector2D>>(m_points);
    polylineData["pointCount"] = m_points.size();
    bus->publish("command.create-sketch-polyline", polylineData);

    outputMessage(QString("Polyline created with %1 points.").arg(m_points.size()));

    Q_EMIT finished(CommandResult::Success("Polyline command completed"));
}

// ---------------------------------------------------------------------------
// cleanup()
// ---------------------------------------------------------------------------

void PolylineCommand::cleanup() {
    qDebug() << "[PolylineCommand] Cleanup started";

    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    bus->unsubscribeAll(this);
    qDebug() << "[PolylineCommand] Unsubscribed from EventBus";

    QVariantMap cleanupRequest;
    cleanupRequest["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", cleanupRequest);

    m_points.clear();
    m_isFinishing = false;
    qDebug() << "[PolylineCommand] Cleanup completed";
}

// ---------------------------------------------------------------------------
// getUsage()
// ---------------------------------------------------------------------------

QString PolylineCommand::getUsage() const {
    return "Usage: polyline [x1 y1 x2 y2 [x3 y3 ...]]";
}

} // namespace command
} // namespace aicad
