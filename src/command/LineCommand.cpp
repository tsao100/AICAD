#include "command/LineCommand.h"
#include "command/Command.h"
#include "command/CommandTypes.h"
#include "command/CommandFactory.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include <QFileDialog>

using namespace aicad::core;

namespace aicad {
namespace command {

LineCommand::LineCommand(QObject* parent)
    : Command("line", "Draw Line", parent)
    , m_hasStartPoint(false)
    , m_isFinishing(false)
{
}

LineCommand::~LineCommand() {
}

CommandResult LineCommand::execute(const CommandContext& context) {
    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // Non-interactive mode (with coordinates)
    if (context.args.size() >= 4) {
        // ✅ Use EventBus to request sketch state
        QVariantMap request;
        request["commandId"] = "line";
        request["args"] = QVariant::fromValue(context.args);
        bus->publish("command.request-sketch-line", request);
        return CommandResult::Success("Line creation requested");
    }

    // Interactive mode
    m_hasStartPoint = false;
    m_isFinishing = false;

    // ✅ Request view setup via EventBus
    QVariantMap viewSetup;
    viewSetup["mode"] = "sketching";
    viewSetup["rubberBandMode"] = "line";
    bus->publish("command.request-view-setup", viewSetup);

    bus->publish(Events::COMMAND_PROMPT, "Specify first point:");
    bus->publish(Events::COMMAND_LOG, "Specify first point:");


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

    setState(CommandState::Running);  // 關鍵一行

    outputMessage("Specify first point:");
    return CommandResult::Success("Waiting for input");
}

// ✅ Renamed from onPointAcquired
void LineCommand::handlePointAcquired(QVector2D point)
{
    if (m_isFinishing) return;

    qDebug() << "[LineCommand] Point acquired:"
             << point.x() << "," << point.y();

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    if (!m_hasStartPoint) {
        // === First point ===
        m_startPoint = point;
        m_hasStartPoint = true;

        // ✅ Request rubber band update via EventBus
        QVariantMap rubberUpdate;
        rubberUpdate["action"] = "clearAndAdd";
        rubberUpdate["point"] = QVariant::fromValue(point);
        bus->publish("command.update-rubber-band", rubberUpdate);

        outputMessage(QString("First point: (%1, %2). Specify next point:")
                          .arg(point.x()).arg(point.y()));

        bus->publish(Events::COMMAND_PROMPT, "Specify next point or press ESC to finish");
        return;
    }

    // ✅ Request line creation via EventBus
    QVariantMap lineData;
    lineData["startPoint"] = QVariant::fromValue(m_startPoint);
    lineData["endPoint"] = QVariant::fromValue(point);
    bus->publish("command.create-sketch-line", lineData);

    outputMessage(QString("Line created from (%1,%2) to (%3,%4)")
                      .arg(m_startPoint.x()).arg(m_startPoint.y())
                      .arg(point.x()).arg(point.y()));

    // Update rubber band
    QVariantMap rubberUpdate;
    rubberUpdate["action"] = "clearAndAdd";
    rubberUpdate["point"] = QVariant::fromValue(point);
    bus->publish("command.update-rubber-band", rubberUpdate);

    m_startPoint = point;
    bus->publish(Events::COMMAND_PROMPT, "Specify next point or press ESC to finish");

}

// ✅ Renamed from onCancelled
void LineCommand::handleCancelled() {
    qDebug() << "[LineCommand] Cancelled via EventBus";

    m_isFinishing = true;  // ✅ Set flag to prevent further point handling

    // ✅ Just emit finished with current state
    Q_EMIT finished(CommandResult::Success("Line command completed"));
}

void LineCommand::cleanup() {
    qDebug() << "[LineCommand] Cleanup started";

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // ✅ Unsubscribe FIRST, before changing view mode
    bus->unsubscribeAll(this);
    qDebug() << "[LineCommand] Unsubscribed from EventBus";

    // ✅ Request cleanup via EventBus
    QVariantMap cleanupRequest;
    cleanupRequest["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", cleanupRequest);

    m_hasStartPoint = false;
    m_isFinishing = false;
    qDebug() << "[LineCommand] Cleanup completed";
}

QString LineCommand::getUsage() const {
    return "Usage: line [x1 y1 x2 y2]";
}

} // namespace command
} // namespace aicad
