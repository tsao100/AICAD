#include "command/ArcCommand.h"
#include "command/Command.h"
#include "command/CommandTypes.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include <QtMath>

using namespace aicad::core;

namespace aicad {
namespace command {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

ArcCommand::ArcCommand(QObject* parent)
    : Command("arc", "Draw Arc (3 Points)", parent)
    , m_pickState(PickState::WaitingForStart)
    , m_isFinishing(false)
{
}

ArcCommand::~ArcCommand() {
}

// ---------------------------------------------------------------------------
// execute
// ---------------------------------------------------------------------------

CommandResult ArcCommand::execute(const CommandContext& context) {
    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    // ── 非互動模式：直接帶入 6 個座標 (x1 y1 x2 y2 x3 y3) ──────────────────
    if (context.args.size() >= 6) {
        QVariantMap request;
        request["commandId"] = "arc";
        request["args"]      = QVariant::fromValue(context.args);
        bus->publish("command.request-sketch-arc", request);
        return CommandResult::Success("Arc creation requested");
    }

    // ── 互動模式初始化 ─────────────────────────────────────────────────────
    m_pickState   = PickState::WaitingForStart;
    m_isFinishing = false;

    // 要求 View 切換到 sketching / arc rubber-band 模式
    QVariantMap viewSetup;
    viewSetup["mode"]           = "sketching";
    viewSetup["rubberBandMode"] = "arc";
    bus->publish("command.request-view-setup", viewSetup);

    bus->publish(Events::COMMAND_PROMPT, "Specify arc start point:");

    // 訂閱點輸入事件
    bus->subscribe(Events::POINT_ACQUIRED, this,
                   [this](const QVariant& data) {
                       QVariantMap map   = data.toMap();
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

    outputMessage("Specify arc start point:");
    return CommandResult::Success("Waiting for input");
}

// ---------------------------------------------------------------------------
// handlePointAcquired
// ---------------------------------------------------------------------------

void ArcCommand::handlePointAcquired(QVector2D point)
{
    if (m_isFinishing) return;

    qDebug() << "[ArcCommand] Point acquired:"
             << point.x() << "," << point.y()
             << "  state:" << static_cast<int>(m_pickState);

    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    switch (m_pickState) {

        // ── 第 1 點：起點 ────────────────────────────────────────────────────
    case PickState::WaitingForStart: {
        m_startPoint = point;
        m_pickState  = PickState::WaitingForMid;

        // 更新 rubber band 錨點
        QVariantMap rubberUpdate;
        rubberUpdate["action"] = "clearAndAdd";
        rubberUpdate["point"] = QVariant::fromValue(QPointF(point.x(), point.y()));
        bus->publish("command.update-rubber-band", rubberUpdate);

        outputMessage(QString("Start point: (%1, %2). Specify arc mid point:")
                          .arg(point.x()).arg(point.y()));
        bus->publish(Events::COMMAND_PROMPT,
                     "Specify arc mid point (a point on the arc):");
        break;
    }

        // ── 第 2 點：弧上中間點 ──────────────────────────────────────────────
    case PickState::WaitingForMid: {
        m_midPoint  = point;
        m_pickState = PickState::WaitingForEnd;

        // 更新 rubber band：提供起點 + 中點，讓 View 可預覽弧形
        QVariantMap rubberUpdate;
        rubberUpdate["action"]     = "addPoint";
        rubberUpdate["point"] = QVariant::fromValue(QPointF(point.x(), point.y()));
        bus->publish("command.update-rubber-band", rubberUpdate);

        outputMessage(QString("Mid point: (%1, %2). Specify arc end point:")
                          .arg(point.x()).arg(point.y()));
        bus->publish(Events::COMMAND_PROMPT,
                     "Specify arc end point or press ESC to cancel:");
        break;
    }

        // ── 第 3 點：終點 → 建立弧線 ─────────────────────────────────────────
    case PickState::WaitingForEnd: {
        QVector2D endPoint = point;

        // 計算圓心與半徑（用於傳給 Sketch）
        QVector2D center;
        float     radius = 0.f;
        bool      ok     = calcCircleFrom3Points(m_startPoint, m_midPoint,
                                        endPoint, center, radius);

        if (!ok) {
            // 三點共線，無法構成弧：提示重新輸入終點
            outputMessage("Error: The three points are collinear. "
                          "Please specify a different end point.");
            bus->publish(Events::COMMAND_PROMPT,
                         "Points are collinear – specify arc end point again:");
            // 保持 WaitingForEnd 狀態
            return;
        }

        // 計算起、終角度（以圓心為基準）
        float startAngle = qRadiansToDegrees(
            qAtan2(m_startPoint.y() - center.y(),
                   m_startPoint.x() - center.x()));
        float endAngle   = qRadiansToDegrees(
            qAtan2(endPoint.y() - center.y(),
                   endPoint.x() - center.x()));

        // 發佈建立弧線請求
        QVariantMap arcData;
        arcData["startPoint"] = QVariant::fromValue(m_startPoint);
        arcData["midPoint"]   = QVariant::fromValue(m_midPoint);
        arcData["endPoint"]   = QVariant::fromValue(endPoint);
        arcData["center"]     = QVariant::fromValue(center);
        arcData["radius"]     = radius;
        arcData["startAngle"] = startAngle;
        arcData["endAngle"]   = endAngle;
        bus->publish("command.create-sketch-arc", arcData);

        outputMessage(
            QString("Arc created: start(%1,%2) mid(%3,%4) end(%5,%6) "
                    "| center(%7,%8) radius=%9")
                .arg(m_startPoint.x()).arg(m_startPoint.y())
                .arg(m_midPoint.x()).arg(m_midPoint.y())
                .arg(endPoint.x()).arg(endPoint.y())
                .arg(center.x()).arg(center.y())
                .arg(radius));

        // 結束命令
        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Success("Arc command completed"));
        break;
    }

    } // switch
}

// ---------------------------------------------------------------------------
// handleCancelled
// ---------------------------------------------------------------------------

void ArcCommand::handleCancelled()
{
    qDebug() << "[ArcCommand] Cancelled via EventBus";

    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("Arc command cancelled"));
}

// ---------------------------------------------------------------------------
// cleanup
// ---------------------------------------------------------------------------

void ArcCommand::cleanup()
{
    qDebug() << "[ArcCommand] Cleanup started";

    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    bus->unsubscribeAll(this);
    qDebug() << "[ArcCommand] Unsubscribed from EventBus";

    QVariantMap cleanupRequest;
    cleanupRequest["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", cleanupRequest);

    m_pickState   = PickState::WaitingForStart;
    m_isFinishing = false;

    qDebug() << "[ArcCommand] Cleanup completed";
}

// ---------------------------------------------------------------------------
// getUsage
// ---------------------------------------------------------------------------

QString ArcCommand::getUsage() const {
    return "Usage: arc [x1 y1 x2 y2 x3 y3]";
}

// ---------------------------------------------------------------------------
// calcCircleFrom3Points  (靜態輔助)
//
//  利用垂直平分線聯立方程式求圓心：
//
//  ┌  (x2-x1)*cx + (y2-y1)*cy = [(x2²-x1²) + (y2²-y1²)] / 2
//  └  (x3-x1)*cx + (y3-y1)*cy = [(x3²-x1²) + (y3²-y1²)] / 2
//
// ---------------------------------------------------------------------------

bool ArcCommand::calcCircleFrom3Points(const QVector2D& p1,
                                       const QVector2D& p2,
                                       const QVector2D& p3,
                                       QVector2D& center,
                                       float& radius)
{
    const float ax = p2.x() - p1.x();
    const float ay = p2.y() - p1.y();
    const float bx = p3.x() - p1.x();
    const float by = p3.y() - p1.y();

    // 行列式（= 2 × 三角形面積的有號值）
    const float det = ax * by - ay * bx;

    // 行列式接近 0 → 三點共線
    constexpr float kEpsilon = 1e-6f;
    if (qAbs(det) < kEpsilon)
        return false;

    const float ux = (by * (ax * ax + ay * ay) - ay * (bx * bx + by * by))
                     / (2.f * det);
    const float uy = (ax * (bx * bx + by * by) - bx * (ax * ax + ay * ay))
                     / (2.f * det);

    center = QVector2D(p1.x() + ux, p1.y() + uy);
    radius = QVector2D(center - p1).length();
    return true;
}

} // namespace command
} // namespace aicad
