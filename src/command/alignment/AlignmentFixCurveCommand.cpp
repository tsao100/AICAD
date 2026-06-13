/**
 * @file AlignmentFixCurveCommand.cpp
 * @brief ALIGNMENTFIXCURVE (alias: FC) — 實作。
 *
 * 互動流程（state machine）：
 *   WaitingForStart  →  WaitingForMid  →  WaitingForEnd  → 建立圓弧
 *
 * 完全依照 ArcCommand / AlignmentFixTangentCommand 的 EventBus 訂閱模式：
 *   - 一次訂閱，持續整個命令生命週期
 *   - unsubscribeAll 只在 cleanup() 呼叫
 *   - handler 透過 QMetaObject::invokeMethod queued dispatch
 */

#include "command/alignment/AlignmentFixCurveCommand.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include <QDebug>
#include <cmath>

using namespace aicad::core;

namespace aicad {
namespace command {

// ────────────────────────────────────────────────────────────────────────────
//  Constructor
// ────────────────────────────────────────────────────────────────────────────

AlignmentFixCurveCommand::AlignmentFixCurveCommand(QObject* parent)
    : AlignmentCommandBase("alignmentfixcurve", "Add Fixed Curve (3-point arc)", parent)
{}

// ────────────────────────────────────────────────────────────────────────────
//  execute
// ────────────────────────────────────────────────────────────────────────────

CommandResult AlignmentFixCurveCommand::execute(const CommandContext& context)
{
    m_alignDoc = context.alignmentDoc;
    if (!m_alignDoc) {
        return CommandResult::Failure(
            "No AlignmentDocument — open or create an alignment first.");
    }

    m_pickState   = PickState::WaitingForStart;
    m_isFinishing = false;

    EventBus* bus = Application::instance()->eventBus();

    // 要求 CadView 切換到弧線 rubber-band 模式
    QVariantMap viewSetup;
    viewSetup["mode"]           = "sketching";
    viewSetup["rubberBandMode"] = "arc";
    bus->publish("command.request-view-setup", viewSetup);

    // ── 訂閱一次，整個命令生命週期有效 ───────────────────────────────────────
    bus->subscribe(Events::POINT_ACQUIRED, this,
        [this](const QVariant& data) {
            QVariantMap map = data.toMap();
            QPointF pt = map["point"].value<QPointF>(); // Alignment 模式發佈 QPointF（double，含 TM2 偏移）
            QMetaObject::invokeMethod(this, [this, pt]() {
                handlePointAcquired(pt);
            }, Qt::QueuedConnection);
        });

    bus->subscribe(Events::POINT_CANCELLED, this,
        [this](const QVariant&) {
            QMetaObject::invokeMethod(this, [this]() {
                handleCancelled();
            }, Qt::QueuedConnection);
        });

    setState(CommandState::Running);

    bus->publish(Events::COMMAND_PROMPT,
                 tr("Fixed Curve — Specify arc START point:"));
    outputMessage("Fixed Curve — Specify arc START point:");
    return CommandResult::Success("Waiting for input");
}

// ────────────────────────────────────────────────────────────────────────────
//  handlePointAcquired
// ────────────────────────────────────────────────────────────────────────────

void AlignmentFixCurveCommand::handlePointAcquired(const QPointF& point)
{
    if (m_isFinishing) return;

    EventBus* bus = Application::instance()->eventBus();

    switch (m_pickState) {

    // ── Step 1：起點 ─────────────────────────────────────────────────────────
    case PickState::WaitingForStart: {
        m_startPoint = point;
        m_pickState  = PickState::WaitingForMid;

        QVariantMap rb;
        rb["action"] = "clearAndAdd";
        rb["point"]  = QVariant::fromValue(QVector2D((float)point.x(), (float)point.y()));
        bus->publish("command.update-rubber-band", rb);

        bus->publish(Events::COMMAND_PROMPT,
                     tr("Specify a point ON the arc:"));
        outputMessage(QString("Start (%1, %2) — Specify a point on the arc:")
                          .arg(point.x(), 0, 'f', 3)
                          .arg(point.y(), 0, 'f', 3));
        break;
    }

    // ── Step 2：弧上點 ───────────────────────────────────────────────────────
    case PickState::WaitingForMid: {
        m_midPoint  = point;
        m_pickState = PickState::WaitingForEnd;

        QVariantMap rb;
        rb["action"] = "addPoint";
        rb["point"]  = QVariant::fromValue(QVector2D((float)point.x(), (float)point.y()));
        bus->publish("command.update-rubber-band", rb);

        bus->publish(Events::COMMAND_PROMPT,
                     tr("Specify arc END point:"));
        outputMessage(QString("Mid (%1, %2) — Specify arc END point:")
                          .arg(point.x(), 0, 'f', 3)
                          .arg(point.y(), 0, 'f', 3));
        break;
    }

    // ── Step 3：終點 → 求外接圓 → addFixedCurve + solve ─────────────────────
    case PickState::WaitingForEnd: {
        QPointF center;
        double  radius = 0.0;

        if (!circumcircle(m_startPoint, m_midPoint, point, center, radius)) {
            outputMessage("Error: The three points are collinear — "
                          "please specify a different end point.");
            bus->publish(Events::COMMAND_PROMPT,
                         tr("Points are collinear — specify arc END point again:"));
            // 留在 WaitingForEnd 狀態，等待重新輸入
            return;
        }

        // ── 建立 Fixed CircularArc（全部 double 精度，無截斷）─────────────────
        int idx = m_alignDoc->horizontal()->addFixedCurve(
                      m_startPoint, point, center, radius);
        m_alignDoc->horizontal()->solve();   // emit changed() → AlignmentRenderer::refresh()

        outputMessage(
            QString("Fixed Curve #%1  start(%2, %3) → end(%4, %5)  R=%6 m")
                .arg(idx)
                .arg(m_startPoint.x(), 0, 'f', 3)
                .arg(m_startPoint.y(), 0, 'f', 3)
                .arg(point.x(),        0, 'f', 3)
                .arg(point.y(),        0, 'f', 3)
                .arg(radius,           0, 'f', 3));

        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Success("AlignmentFixCurve completed"));
        break;
    }

    } // switch
}

// ────────────────────────────────────────────────────────────────────────────
//  handleCancelled
// ────────────────────────────────────────────────────────────────────────────

void AlignmentFixCurveCommand::handleCancelled()
{
    qDebug() << "[FC] Cancelled";
    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("AlignmentFixCurve cancelled"));
}

// ────────────────────────────────────────────────────────────────────────────
//  cleanup
// ────────────────────────────────────────────────────────────────────────────

void AlignmentFixCurveCommand::cleanup()
{
    EventBus* bus = Application::instance()->eventBus();

    bus->unsubscribeAll(this);

    QVariantMap rb;
    rb["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", rb);

    m_pickState   = PickState::WaitingForStart;
    m_isFinishing = false;
    m_alignDoc    = nullptr;
}

// ────────────────────────────────────────────────────────────────────────────
//  circumcircle  (static helper)
//
//  外接圓方程式（垂直平分線聯立）：
//    (x2-x1)·cx + (y2-y1)·cy = [(x2²-x1²)+(y2²-y1²)] / 2
//    (x3-x1)·cx + (y3-y1)·cy = [(x3²-x1²)+(y3²-y1²)] / 2
//
//  det = (x2-x1)·(y3-y1) − (y2-y1)·(x3-x1)
//  三點共線時 det ≈ 0。
// ────────────────────────────────────────────────────────────────────────────

bool AlignmentFixCurveCommand::circumcircle(const QPointF& p1,
                                            const QPointF& p2,
                                            const QPointF& p3,
                                            QPointF&         outCenter,
                                            double&          outRadius)
{
    const double ax = p2.x() - p1.x();
    const double ay = p2.y() - p1.y();
    const double bx = p3.x() - p1.x();
    const double by = p3.y() - p1.y();

    const double det = ax * by - ay * bx;

    constexpr double kEps = 1e-9;
    if (std::abs(det) < kEps)
        return false;

    // Cramer's rule
    const double aa = ax * ax + ay * ay;   // |p1p2|²
    const double bb = bx * bx + by * by;   // |p1p3|²

    const double ux = (by * aa - ay * bb) / (2.0 * det);
    const double uy = (ax * bb - bx * aa) / (2.0 * det);

    // Full double precision — p1 is already QPointF(double)
    const double cx = p1.x() + ux;
    const double cy = p1.y() + uy;

    outCenter = QPointF(cx, cy);
    outRadius = std::hypot(ux, uy);
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
//  getUsage
// ────────────────────────────────────────────────────────────────────────────

QString AlignmentFixCurveCommand::getUsage() const
{
    return "Usage: FC\n"
           "  Click arc START → click a point ON the arc → click arc END.\n"
           "  A fixed circular arc is fitted to the three points.";
}

} // namespace command
} // namespace aicad