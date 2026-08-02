#include "command/RectCommand.h"
#include "command/Command.h"
#include "command/CommandTypes.h"
#include "core/Application.h"
#include "core/EventBus.h"

using namespace aicad::core;

namespace aicad {
namespace command {

RectCommand::RectCommand(QObject* parent)
    : Command("rect", "Draw Rectangle", parent)
    , m_hasFirstCorner(false)
    , m_isFinishing(false)
{
}

RectCommand::~RectCommand() {
}

CommandResult RectCommand::execute(const CommandContext& context) {
    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // Non-interactive mode: rect x1 y1 x2 y2
    if (context.args.size() >= 4) {
        QVariantMap request;
        request["commandId"] = "rect";
        request["args"] = QVariant::fromValue(context.args);
        bus->publish("command.request-sketch-rect", request);
        return CommandResult::Success("Rectangle creation requested");
    }

    // Interactive mode
    m_hasFirstCorner = false;
    m_isFinishing = false;

    // 設定 view 為 sketching 模式，rubber band 顯示矩形預覽
    QVariantMap viewSetup;
    viewSetup["mode"] = "sketching";
    viewSetup["rubberBandMode"] = "rect";
    bus->publish("command.request-view-setup", viewSetup);

    bus->publish(Events::COMMAND_PROMPT, "Specify first corner:");

    // 訂閱點輸入事件
    bus->subscribe(Events::POINT_ACQUIRED, this,
                   [this](const QVariant& data) {
                       QVariantMap map = data.toMap();
                       QPointF _ptF = map["point"].value<QPointF>();
                       QVector2D point(static_cast<float>(_ptF.x()), static_cast<float>(_ptF.y()));

                       QMetaObject::invokeMethod(this, [this, point]() {
                           this->handlePointAcquired(point);
                       }, Qt::QueuedConnection);
                   });

    // 訂閱取消事件
    bus->subscribe(Events::POINT_CANCELLED, this,
                   [this](const QVariant&) {
                       QMetaObject::invokeMethod(this, [this]() {
                           this->handleCancelled();
                       }, Qt::QueuedConnection);
                   });

    setState(CommandState::Running);

    outputMessage("Specify first corner:");
    return CommandResult::Success("Waiting for input");
}

void RectCommand::handlePointAcquired(QVector2D point)
{
    if (m_isFinishing) return;

    qDebug() << "[RectCommand] Point acquired:"
             << point.x() << "," << point.y();

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    if (!m_hasFirstCorner) {
        // === 第一個角點 ===
        m_firstCorner = point;
        m_hasFirstCorner = true;

        // 通知 view 更新 rubber band 起始點
        QVariantMap rubberUpdate;
        rubberUpdate["action"] = "clearAndAdd";
        rubberUpdate["point"] = QVariant::fromValue(QPointF(point.x(), point.y()));
        bus->publish("command.update-rubber-band", rubberUpdate);

        outputMessage(QString("First corner: (%1, %2). Specify opposite corner:")
                          .arg(point.x()).arg(point.y()));

        bus->publish(Events::COMMAND_PROMPT, "Specify opposite corner or press ESC to cancel:");
        return;
    }

    // === 第二個角點：建立矩形 ===
    QVariantMap rectData;
    rectData["Corner1"]    = QVariant::fromValue(m_firstCorner);
    rectData["Corner2"] = QVariant::fromValue(point);
    bus->publish("command.create-sketch-rect", rectData);

    outputMessage(QString("Rectangle created: (%1,%2) to (%3,%4)")
                      .arg(m_firstCorner.x()).arg(m_firstCorner.y())
                      .arg(point.x()).arg(point.y()));

    // 矩形完成後，重置狀態以便連續繪製（如需單次繪製可改為直接 finish）
    m_hasFirstCorner = false;

    // 清除 rubber band，等待下一次輸入
    QVariantMap rubberClear;
    rubberClear["action"] = "clear";
    bus->publish("command.update-rubber-band", rubberClear);

    bus->publish(Events::COMMAND_PROMPT, "Specify first corner or press ESC to finish:");
    outputMessage("Specify first corner or press ESC to finish:");
}

void RectCommand::handleCancelled() {
    qDebug() << "[RectCommand] Cancelled via EventBus";

    m_isFinishing = true;

    Q_EMIT finished(CommandResult::Success("Rectangle command completed"));
}

void RectCommand::cleanup() {
    qDebug() << "[RectCommand] Cleanup started";

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // 先取消訂閱，再清理 view
    bus->unsubscribeAll(this);
    qDebug() << "[RectCommand] Unsubscribed from EventBus";

    QVariantMap cleanupRequest;
    cleanupRequest["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", cleanupRequest);

    m_hasFirstCorner = false;
    m_isFinishing = false;
    qDebug() << "[RectCommand] Cleanup completed";
}

QString RectCommand::getUsage() const {
    return "Usage: rect [x1 y1 x2 y2]";
}

} // namespace command
} // namespace aicad
