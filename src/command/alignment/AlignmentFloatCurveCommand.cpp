/**
 * @file AlignmentFloatCurveCommand.cpp
 * @brief ALIGNMENTFLOATCURVE (alias: AFC) — 實作。
 *
 * 狀態機
 * ──────
 *   PickFirstTangent   ← execute() 進入
 *       │  POINT_ACQUIRED → nearestTangentIndex() → idx1 ≥ 0
 *       ▼
 *   PickSecondTangent
 *       │  POINT_ACQUIRED → nearestTangentIndex() → idx2 ≥ 0，idx2 ≠ idx1
 *       ▼
 *   WaitingForRadius
 *       │  NUMBER_INPUT → m_radius > 0
 *       ▼
 *   WaitingForConfirm  ← 更新 RubberBand::Arc 預覽
 *       │  POINT_ACQUIRED（任何點）或 NUMBER_INPUT（重輸入）→ commitCurve()
 *       │  POINT_CANCELLED → cancel
 *
 * pickAlignmentElement 機制
 * ─────────────────────────
 *   nearestTangentIndex()：靜態工具函式，遍歷 HorizontalAlignmentEdit::elements()
 *   中 type == Tangent 的條目，計算 clickPt 到每段 (startPI → endPI) 的最短
 *   距離，回傳距離最近那條的 index。
 *   此函式設計為 static public，供 AlignmentSCSCommand 等後續命令直接呼叫。
 *
 * RubberBand 預覽
 * ───────────────
 *   取得 idx1、idx2 兩條切線的 endPI / startPI（交叉點 PI），以及 m_radius，
 *   透過 "command.update-rubber-band" 事件設定 Arc 模式並帶入三個點位。
 *
 * EventBus 訂閱規則（與 AlignmentFixTangentCommand 相同）
 * ──────────────────────────────────────────────────────
 *   - 訂閱一次，整個命令生命週期有效
 *   - unsubscribeAll 只在 cleanup() 呼叫
 *   - handler 用 QMetaObject::invokeMethod queued 分派
 */

#include "command/alignment/AlignmentFloatCurveCommand.h"
#include "core/Application.h"
#include "core/CommandLineManager.h"
#include "core/EventBus.h"
#include "railway/AlignmentDocument.h"

#include <QDebug>
#include <QLineF>
#include <QtMath>
#include <cmath>
#include <limits>

using namespace aicad::core;

namespace aicad {
namespace command {

// ────────────────────────────────────────────────────────────────────────────
//  Constructor
// ────────────────────────────────────────────────────────────────────────────

AlignmentFloatCurveCommand::AlignmentFloatCurveCommand(QObject* parent)
    : AlignmentCommandBase("alignmentfloatcurve",
                           "Add Floating Curve between two tangents",
                           parent)
{}

// ────────────────────────────────────────────────────────────────────────────
//  execute
// ────────────────────────────────────────────────────────────────────────────

CommandResult AlignmentFloatCurveCommand::execute(const CommandContext& context)
{
    m_alignDoc = context.alignmentDoc;
    if (!m_alignDoc) {
        return CommandResult::Failure(
            "No AlignmentDocument — open or create an alignment first.");
    }

    // 確認已有足夠的切線元素可供選取
    const auto& elems = m_alignDoc->horizontal()->elements();
    int tangentCount = 0;
    for (const auto& e : elems) {
        if (e.type == railway::EditableElementType::Tangent)
            ++tangentCount;
    }
    if (tangentCount < 2) {
        return CommandResult::Failure(
            "At least two Fixed Tangents are required before adding a Floating Curve. "
            "Use FT to add tangents first.");
    }

    // ── 初始化狀態 ─────────────────────────────────────────────────────────
    m_step        = Step::PickFirstTangent;
    m_isFinishing = false;
    m_idx1        = -1;
    m_idx2        = -1;
    m_radius      = 0.0;

    EventBus* bus = Application::instance()->eventBus();

    // 切換到 arc rubber-band 模式（初始無點，稍後更新）
    QVariantMap viewSetup;
    viewSetup["mode"]           = "sketching";
    viewSetup["rubberBandMode"] = "arc";
    bus->publish("command.request-view-setup", viewSetup);

    // ── 訂閱一次，整個命令生命週期 ───────────────────────────────────────────

    // 點輸入：用於「選切線」與「WaitingForConfirm」確認
    bus->subscribe(Events::POINT_ACQUIRED, this,
        [this](const QVariant& data) {
            QVariantMap map = data.toMap();
            QPointF pt = map["point"].value<QPointF>();
            QMetaObject::invokeMethod(this, [this, pt]() {
                handlePointAcquired(pt);
            }, Qt::QueuedConnection);
        });

    // 數值輸入：用於 Radius
    bus->subscribe(Events::NUMBER_INPUT, this,
        [this](const QVariant& data) {
            QString text = data.toString();
            QMetaObject::invokeMethod(this, [this, text]() {
                handleNumberInput(text);
            }, Qt::QueuedConnection);
        });

    // 取消（右鍵 / ESC）
    bus->subscribe(Events::POINT_CANCELLED, this,
        [this](const QVariant&) {
            QMetaObject::invokeMethod(this, [this]() {
                handleCancelled();
            }, Qt::QueuedConnection);
        });

    setState(CommandState::Running);

    bus->publish(Events::COMMAND_PROMPT,
                 tr("Floating Curve — Select FIRST tangent (click near a tangent line):"));
    outputMessage("Floating Curve — Select FIRST tangent:");
    return CommandResult::Success("Waiting for input");
}

// ────────────────────────────────────────────────────────────────────────────
//  handlePointAcquired
// ────────────────────────────────────────────────────────────────────────────

void AlignmentFloatCurveCommand::handlePointAcquired(const QPointF& point)
{
    if (m_isFinishing) return;

    EventBus* bus = Application::instance()->eventBus();

    switch (m_step) {

    // ── Step 1：選第一條切線 ─────────────────────────────────────────────────
    case Step::PickFirstTangent: {
        int idx = nearestTangentIndex(point, m_alignDoc->horizontal());
        if (idx < 0) {
            outputMessage("No tangent found near that point — please click closer to a tangent line.");
            bus->publish(Events::COMMAND_PROMPT,
                         tr("Select FIRST tangent (click closer to a tangent line):"));
            return;
        }
        m_idx1 = idx;
        highlightTangent(m_idx1);

        outputMessage(
            QString("First tangent #%1 selected.  Select SECOND tangent:").arg(idx));
        bus->publish(Events::COMMAND_PROMPT,
                     tr("Select SECOND tangent:"));
        m_step = Step::PickSecondTangent;
        break;
    }

    // ── Step 2：選第二條切線 ─────────────────────────────────────────────────
    case Step::PickSecondTangent: {
        int idx = nearestTangentIndex(point, m_alignDoc->horizontal());
        if (idx < 0) {
            outputMessage("No tangent found — please click closer to a tangent line.");
            bus->publish(Events::COMMAND_PROMPT,
                         tr("Select SECOND tangent (click closer to a tangent line):"));
            return;
        }
        if (idx == m_idx1) {
            outputMessage("Please select a DIFFERENT tangent for the second one.");
            bus->publish(Events::COMMAND_PROMPT,
                         tr("Select a DIFFERENT tangent for the second one:"));
            return;
        }
        m_idx2 = idx;
        highlightTangent(m_idx2);

        outputMessage(
            QString("Second tangent #%1 selected.  Enter Radius (m):").arg(idx));
        bus->publish(Events::COMMAND_PROMPT,
                     tr("Radius (m):"));
        m_step = Step::WaitingForRadius;
        // ★ 必須告知 CommandLineManager 現在期待數値輸入，
        //   否則 m_isWaitingForInput==false 時 executeCommand() 會投入 processCommand()
        //   把半徑当成新指令執行，NUMBER_INPUT 永遠不會發出。
        core::CommandLineManager::instance()->waitForInput(
            core::InputType::Number);
        break;
    }

    // ── WaitingForConfirm：使用者點任意位置即視為確認 ─────────────────────────
    case Step::WaitingForConfirm: {
        commitCurve();
        break;
    }

    // ── WaitingForRadius：此步驟只接受數值輸入，忽略點擊 ────────────────────
    case Step::WaitingForRadius:
    default:
        break;
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  handleNumberInput
// ────────────────────────────────────────────────────────────────────────────

void AlignmentFloatCurveCommand::handleNumberInput(const QString& text)
{
    if (m_isFinishing) return;

    EventBus* bus = Application::instance()->eventBus();

    // 在 WaitingForRadius 或 WaitingForConfirm（允許修改半徑）階段接受
    if (m_step != Step::WaitingForRadius && m_step != Step::WaitingForConfirm)
        return;

    bool   ok     = false;
    double radius = text.trimmed().toDouble(&ok);

    if (!ok || radius <= 0.0) {
        outputMessage(QString("Invalid radius '%1' — please enter a positive number.").arg(text));
        bus->publish(Events::COMMAND_PROMPT, tr("Radius (m) — enter a positive number:"));
        return;
    }

    m_radius = radius;

    // ── 更新 RubberBand::Arc 預覽 ─────────────────────────────────────────
    const auto& elems = m_alignDoc->horizontal()->elements();
    if (m_idx1 >= 0 && m_idx1 < elems.size() &&
        m_idx2 >= 0 && m_idx2 < elems.size())
    {
        // 以兩條切線的端點作為弧預覽的控制點：
        //   points[0] = tangent1 的 endPI（切線尾端，接近 PI 側）
        //   points[1] = 兩切線的「目視交叉點」估算（此處用中點近似）
        //   currentPoint = tangent2 的 startPI
        const QPointF& t1end   = elems[m_idx1].endPI;
        const QPointF& t2start = elems[m_idx2].startPI;
        QPointF        pi      = QPointF((t1end.x() + t2start.x()) * 0.5,
                                          (t1end.y() + t2start.y()) * 0.5);

        QVariantMap rb;
        rb["action"]      = "clearAndAdd";
        rb["point"]       = QVariant::fromValue(QPointF(t1end.x(), t1end.y()));
        rb["radius"]      = m_radius;
        rb["mode"]        = "arc";
        bus->publish("command.update-rubber-band", rb);

        QVariantMap rb2;
        rb2["action"] = "addPoint";
        rb2["point"]  = QVariant::fromValue(QPointF(pi.x(), pi.y()));
        bus->publish("command.update-rubber-band", rb2);
    }

    outputMessage(
        QString("Radius = %1 m — press Enter or click to confirm, or type new radius:")
            .arg(radius, 0, 'f', 3));
    bus->publish(Events::COMMAND_PROMPT,
                 tr("Confirm Radius = %1 m: press Enter / click, or type new radius.")
                     .arg(radius, 0, 'f', 3));

    m_step = Step::WaitingForConfirm;
    // ★ 繼續候數値輸入：使用者可不需再次點擊確認，
    //   直接重輸半徑修改（它會再次觸發 handleNumberInput）。
    core::CommandLineManager::instance()->waitForInput(
        core::InputType::Number);
}

// ────────────────────────────────────────────────────────────────────────────
//  commitCurve
// ────────────────────────────────────────────────────────────────────────────

void AlignmentFloatCurveCommand::commitCurve()
{
    if (m_isFinishing) return;
    if (m_idx1 < 0 || m_idx2 < 0 || m_radius <= 0.0) {
        outputMessage("Internal error: incomplete parameters for floating curve.");
        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Failure("Incomplete parameters"));
        return;
    }

    // ── Auto-correct reversed tangent selection ───────────────────────────
    // tangentIdxBefore (entry) must have a lower element index than
    // tangentIdxAfter (exit) because elements are appended in alignment order.
    // If the user picked them in reverse order, swap so the solver receives
    // the correct entry→exit pairing.
    if (m_idx1 > m_idx2) {
        std::swap(m_idx1, m_idx2);
        outputMessage(
            QString("Tangent selection order reversed — "
                    "using tangent #%1 as entry and #%2 as exit.")
                .arg(m_idx1).arg(m_idx2));
    }

    int idx = m_alignDoc->horizontal()->addFloatingCurve(m_idx1, m_idx2, m_radius);
    if (idx < 0) {
        outputMessage("Failed to add floating curve — check tangent indices.");
        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Failure("addFloatingCurve returned -1"));
        return;
    }

    m_alignDoc->horizontal()->solve();   // emit changed() → AlignmentRenderer::refresh()

    outputMessage(
        QString("Floating Curve #%1  tangents(%2→%3)  R=%4 m")
            .arg(idx)
            .arg(m_idx1)
            .arg(m_idx2)
            .arg(m_radius, 0, 'f', 3));

    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("AlignmentFloatCurve completed"));
}

// ────────────────────────────────────────────────────────────────────────────
//  handleCancelled
// ────────────────────────────────────────────────────────────────────────────

void AlignmentFloatCurveCommand::handleCancelled()
{
    qDebug() << "[AFC] Cancelled";
    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("AlignmentFloatCurve cancelled"));
}

// ────────────────────────────────────────────────────────────────────────────
//  cleanup
// ────────────────────────────────────────────────────────────────────────────

void AlignmentFloatCurveCommand::cleanup()
{
    EventBus* bus = Application::instance()->eventBus();

    bus->unsubscribeAll(this);

    // 清除高亮
    highlightTangent(-1);

    QVariantMap rb;
    rb["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", rb);

    m_step        = Step::PickFirstTangent;
    m_isFinishing = false;
    m_idx1        = -1;
    m_idx2        = -1;
    m_radius      = 0.0;
    m_alignDoc    = nullptr;
}

// ────────────────────────────────────────────────────────────────────────────
//  highlightTangent  (private)
// ────────────────────────────────────────────────────────────────────────────

void AlignmentFloatCurveCommand::highlightTangent(int elemIdx)
{
    // 透過 EventBus 通知 AlignmentRenderer（或 GripManager）高亮指定元素。
    // idx == -1 代表清除所有高亮。
    QVariantMap msg;
    msg["elementIndex"] = elemIdx;
    Application::instance()->eventBus()
        ->publish("alignment.highlight-element", msg);
}

// ────────────────────────────────────────────────────────────────────────────
//  nearestTangentIndex  (public static)
//
//  遍歷 edit->elements()（即 m_elems），找出 type == Tangent 且
//  clickPt 到線段 (startPI→endPI) 距離最短的那一條，回傳其 index。
//
//  點 P 到有限線段 AB 的距離公式：
//    t     = dot(AP, AB) / dot(AB, AB)
//    t_clamped = clamp(t, 0, 1)
//    closest = A + t_clamped * AB
//    dist    = |P - closest|
// ────────────────────────────────────────────────────────────────────────────

int AlignmentFloatCurveCommand::nearestTangentIndex(
    const QPointF&                          clickPt,
    const railway::HorizontalAlignmentEdit* edit)
{
    if (!edit) return -1;

    const auto& elems = edit->elements();

    int    bestIdx  = -1;
    double bestDist = std::numeric_limits<double>::max();

    for (int i = 0; i < elems.size(); ++i) {
        const railway::EditableElement& e = elems[i];
        if (e.type != railway::EditableElementType::Tangent)
            continue;

        // 線段兩端（以 double 精度計算）
        const double ax = e.startPI.x();
        const double ay = e.startPI.y();
        const double bx = e.endPI.x();
        const double by = e.endPI.y();

        const double px = clickPt.x();
        const double py = clickPt.y();

        // AB 向量
        const double abx = bx - ax;
        const double aby = by - ay;
        const double ab2 = abx * abx + aby * aby;   // |AB|²

        double dist;
        if (ab2 < 1e-12) {
            // 退化線段（零長），直接用端點距離
            dist = std::hypot(px - ax, py - ay);
        } else {
            // 投影參數 t，限縮到 [0,1]
            double t = ((px - ax) * abx + (py - ay) * aby) / ab2;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            const double cx = ax + t * abx;
            const double cy = ay + t * aby;
            dist = std::hypot(px - cx, py - cy);
        }

        if (dist < bestDist) {
            bestDist = dist;
            bestIdx  = i;
        }
    }

    // 僅當距離在可接受範圍內才回傳（500 m 保守上限，實際由螢幕解析度決定）
    constexpr double kMaxPickDist = 500.0;   // [m] world-space pick tolerance
    if (bestDist > kMaxPickDist)
        return -1;

    return bestIdx;
}

// ────────────────────────────────────────────────────────────────────────────
//  getUsage
// ────────────────────────────────────────────────────────────────────────────

QString AlignmentFloatCurveCommand::getUsage() const
{
    return "Usage: AFC\n"
           "  1. Click near the FIRST tangent line.\n"
           "  2. Click near the SECOND tangent line.\n"
           "  3. Enter the radius in metres (e.g. 600).\n"
           "  4. Press Enter or click to confirm.\n"
           "  Right-click / ESC to cancel.";
}

} // namespace command
} // namespace aicad
