#include "command/PolygonCommand.h"
#include "command/Command.h"
#include "command/CommandTypes.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include <QtMath>

using namespace aicad::core;

namespace aicad {
namespace command {

PolygonCommand::PolygonCommand(QObject* parent)
    : Command("polygon", "Draw Regular Polygon", parent)
    , m_hasCenterPoint(false)
    , m_sides(6)  // Default to hexagon
    , m_isFinishing(false)
{
}

PolygonCommand::~PolygonCommand() {
}

CommandResult PolygonCommand::execute(const CommandContext& context) {
    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // Non-interactive mode (with coordinates)
    // Format: polygon centerX centerY radius [sides]
    if (context.args.size() >= 3) {
        bool ok1, ok2, ok3, ok4 = true;
        double centerX = context.args[0].toDouble(&ok1);
        double centerY = context.args[1].toDouble(&ok2);
        double radius = context.args[2].toDouble(&ok3);
        int sides = 6;

        if (context.args.size() >= 4) {
            sides = context.args[3].toInt(&ok4);
        }

        if (ok1 && ok2 && ok3 && ok4 && sides >= 3 && radius > 0) {
            QVector2D center(centerX, centerY);
            createPolygon(center, radius, sides);
            return CommandResult::Success(QString("Polygon with %1 sides created").arg(sides));
        } else {
            return CommandResult::Failure("Invalid parameters. Sides must be >= 3, radius > 0");
        }
    }

    // Interactive mode
    m_hasCenterPoint = false;
    m_sides = 6;  // Default to hexagon
    m_isFinishing = false;

    // ✅ Request view setup via EventBus
    QVariantMap viewSetup;
    viewSetup["mode"] = "sketching";
    viewSetup["rubberBandMode"] = "polygon";
    bus->publish("command.request-view-setup", viewSetup);

    bus->publish(Events::COMMAND_PROMPT, "Specify center point:");

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

    setState(CommandState::Running);

    outputMessage("Specify center point:");
    return CommandResult::Success("Waiting for input");
}

// ✅ Handle point acquisition
void PolygonCommand::handlePointAcquired(QVector2D point)
{
    if (m_isFinishing) return;

    qDebug() << "[PolygonCommand] Point acquired:"
             << point.x() << "," << point.y();

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    if (!m_hasCenterPoint) {
        // === First point - center ===
        m_centerPoint = point;
        m_hasCenterPoint = true;

        // ✅ Update rubber band with center point
        QVariantMap rubberUpdate;
        rubberUpdate["action"] = "clearAndAdd";
        rubberUpdate["point"] = QVariant::fromValue(QPointF(point.x(), point.y()));
        rubberUpdate["polygonCenter"] = QVariant::fromValue(point);
        rubberUpdate["polygonSides"] = m_sides;
        bus->publish("command.update-rubber-band", rubberUpdate);

        outputMessage(QString("Center point: (%1, %2). Specify radius point:")
                          .arg(point.x()).arg(point.y()));

        bus->publish(Events::COMMAND_PROMPT, "Specify radius point or press ESC to cancel:");
        return;
    }

    // === Second point - determines radius ===
    double radius = QVector2D(point - m_centerPoint).length();

    if (radius < 0.001) {
        outputMessage("Radius too small, please specify a point further from center");
        return;
    }

    // ✅ Create the polygon
    createPolygon(m_centerPoint, radius, m_sides);

    outputMessage(QString("Polygon with %1 sides created (center: %2,%3, radius: %4)")
                      .arg(m_sides)
                      .arg(m_centerPoint.x()).arg(m_centerPoint.y())
                      .arg(radius, 0, 'f', 2));

    // Finish command
    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("Polygon command completed"));
}

// ✅ Handle cancellation
void PolygonCommand::handleCancelled() {
    qDebug() << "[PolygonCommand] Cancelled via EventBus";

    m_isFinishing = true;  // ✅ Set flag to prevent further point handling

    // ✅ Just emit finished with current state
    Q_EMIT finished(CommandResult::Success("Polygon command cancelled"));
}

// ✅ Create polygon geometry and publish to EventBus
void PolygonCommand::createPolygon(const QVector2D& center, double radius, int sides)
{
    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // Calculate polygon vertices
    QVector<QVector2D> vertices;
    double angleStep = 2.0 * M_PI / sides;
    double startAngle = -M_PI / 2.0;  // Start from top (12 o'clock position)

    for (int i = 0; i < sides; ++i) {
        double angle = startAngle + i * angleStep;
        double x = center.x() + radius * qCos(angle);
        double y = center.y() + radius * qSin(angle);
        vertices.append(QVector2D(x, y));
    }

    // ✅ Create polygon via EventBus
    QVariantMap polygonData;
    polygonData["center"] = QVariant::fromValue(center);
    polygonData["radius"] = radius;
    polygonData["sides"] = sides;

    // Convert vertices to QVariantList for event transmission
    QVariantList verticesList;
    for (const auto& vertex : vertices) {
        verticesList.append(QVariant::fromValue(vertex));
    }
    polygonData["vertices"] = verticesList;

    bus->publish("command.create-sketch-polygon", polygonData);
}

void PolygonCommand::cleanup() {
    qDebug() << "[PolygonCommand] Cleanup started";

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // ✅ Unsubscribe FIRST, before changing view mode
    bus->unsubscribeAll(this);
    qDebug() << "[PolygonCommand] Unsubscribed from EventBus";

    // ✅ Request cleanup via EventBus
    QVariantMap cleanupRequest;
    cleanupRequest["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", cleanupRequest);

    m_hasCenterPoint = false;
    m_isFinishing = false;
    qDebug() << "[PolygonCommand] Cleanup completed";
}

QString PolygonCommand::getUsage() const {
    return "Usage: polygon [centerX centerY radius sides]\n"
           "       Interactive: Specify center point, then radius point\n"
           "       Non-interactive: polygon 0 0 100 6 (creates hexagon)\n"
           "       sides must be >= 3 (default: 6)";
}

} // namespace command
} // namespace aicad
