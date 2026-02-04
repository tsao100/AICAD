#include "command/LineCommand.h"
#include "command/Command.h"
#include "command/CommandTypes.h"
#include "cad/Document.h"
#include "cad/Plane.h"
#include "cad/Sketch.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "ui/UIManager.h"
#include "view/CadView.h"
#include "view/RubberBand.h"
#include <QFileDialog>

using namespace aicad::core;

namespace aicad {
namespace command {

LineCommand::LineCommand(QObject* parent)
    : Command("line", "Draw Line", parent)
    , m_hasStartPoint(false)
{
}

LineCommand::~LineCommand() {
}

CommandResult LineCommand::execute(const CommandContext& context) {
    Application* app = Application::instance();

    // Non-interactive mode (with coordinates)
    if (context.args.size() >= 4) {
        // ... existing coordinate parsing code ...
        cad::Sketch* sketch = app->activeSketch();
        if (!sketch) return CommandResult::Failure("No active sketch");

        bool ok;
        double x1 = context.args[0].toDouble(&ok);
        if (!ok) return CommandResult::Failure("Invalid x1");

        double y1 = context.args[1].toDouble(&ok);
        if (!ok) return CommandResult::Failure("Invalid y1");

        double x2 = context.args[2].toDouble(&ok);
        if (!ok) return CommandResult::Failure("Invalid x2");

        double y2 = context.args[3].toDouble(&ok);
        if (!ok) return CommandResult::Failure("Invalid y2");

        return createLine(x1, y1, x2, y2);
    }

    m_hasStartPoint=false;

    // Interactive mode
    ui::UIManager* uiMgr = app->uiManager();
    view::CadView* cadView = uiMgr->cadView();

    if (!cadView) {
        return CommandResult::Failure("No active view");
    }


    cad::Sketch* sketch = app->activeSketch();
    if (!sketch) {
        return CommandResult::Failure("No active sketch");
    }

    // Setup view mode
    cadView->setMode(view::InteractionMode::Sketching);

    // Setup rubber band
    view::RubberBand* rubber = cadView->rubberBand();
    rubber->setMode(view::RubberBandMode::Line);
    rubber->setPlane(convertPlane(sketch->plane())); // ✅ Use sketch's plane

    // ✅ Subscribe to EventBus instead of direct signal connection
    EventBus* bus = app->eventBus();

    bus->publish(Events::COMMAND_PROMPT,"Specify first point:");
    bus->publish(Events::COMMAND_LOG,"Specify first point:");


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
    qDebug() << "[LineCommand] Point acquired:"
             << point.x() << "," << point.y();

    Application* app = Application::instance();
    view::CadView* cadView = app->uiManager()->cadView();
    view::RubberBand* rubber = cadView->rubberBand();
    EventBus* bus = app->eventBus();

    if (!m_hasStartPoint) {
        // === First point ===
        m_startPoint = point;
        m_hasStartPoint = true;

        rubber->clear();
        rubber->addPoint(point);

        outputMessage(QString("First point: (%1, %2). Specify next point:")
                          .arg(point.x()).arg(point.y()));

        bus->publish(Events::COMMAND_PROMPT, "Specify next point or press ESC to finish");
        return;
    }

    // === Subsequent points ===
    cad::Sketch* sketch = app->activeSketch();
    if (!sketch)
        return;

    // Create line from previous point to current
    sketch->addLine(m_startPoint, point);
    sketch->rebuild();

    bus->publish(Events::FEATURE_UPDATED,
                 QVariant::fromValue(sketch->name()));

    outputMessage(QString("Line created from (%1,%2) to (%3,%4)")
                      .arg(m_startPoint.x()).arg(m_startPoint.y())
                      .arg(point.x()).arg(point.y()));

    // Update rubber band
    rubber->clearPoints();
    rubber->addPoint(point);

    // Chain: current point becomes next start point
    m_startPoint = point;

    // Stay in command, DO NOT finish
    bus->publish(Events::COMMAND_PROMPT, "Specify next point or press ESC to finish");
}

// ✅ Renamed from onCancelled
void LineCommand::handleCancelled() {
    qDebug() << "[LineCommand] Cancelled via EventBus";

    // ✅ DON'T call cleanup() here either
    // cleanup();  // ❌ Remove this line

    // ✅ Just emit cancelled
    Q_EMIT cancelled();
}

void LineCommand::cleanup() {
    qDebug() << "[LineCommand] Cleanup started";

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // ✅ Unsubscribe FIRST, before changing view mode
    bus->unsubscribeAll(this);
    qDebug() << "[LineCommand] Unsubscribed from EventBus";

    // ✅ Then cleanup view
    view::CadView* cadView = app->uiManager()->cadView();
    if (cadView) {
        view::RubberBand* rubber = cadView->rubberBand();
        if (rubber) {
            rubber->clearPoints();
            rubber->clear();
        }
        //cadView->setMode(view::InteractionMode::Idle);
        //qDebug() << "[LineCommand] View mode reset to Idle";
    }

    m_hasStartPoint = false;
    qDebug() << "[LineCommand] Cleanup completed";
}

// ✅ Helper method
CommandResult LineCommand::createLine(double x1, double y1, double x2, double y2) {
    Application* app = Application::instance();
    cad::Sketch* sketch = app->activeSketch();

    if (!sketch) {
        return CommandResult::Failure("No active sketch");
    }

    sketch->addLine(QVector2D(x1, y1), QVector2D(x2, y2));
    sketch->rebuild();

    EventBus* bus = app->eventBus();
    bus->publish(Events::FEATURE_UPDATED,
                 QVariant::fromValue(sketch->name()));

    return CommandResult::Success("Line created");
}


// ✅ Helper to convert plane
view::CustomPlane LineCommand::convertPlane(const cad::Plane& plane) {
    view::CustomPlane customPlane;
    customPlane.origin = plane.origin();
    customPlane.normal = plane.normal();
    customPlane.uAxis = plane.xAxis();
    customPlane.vAxis = plane.yAxis();
    return customPlane;
}

void LineCommand::cleanup(view::CadView* cadView, view::RubberBand* rubber) {
    rubber->clearPoints();
    rubber->clear();
    cadView->setMode(view::InteractionMode::Idle);
    disconnect(cadView, nullptr, this, nullptr);  // ✅ Disconnect all
}

QString LineCommand::getUsage() const {
    return "Usage: line [x1 y1 x2 y2]";
}

} // namespace command
} // namespace aicad
