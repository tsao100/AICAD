/**
 * @file AlignmentSCSCommand.cpp
 * @brief ALIGNMENTSCS（alias: SCS）— 實作。
 *
 * 狀態機轉換摘要
 * ──────────────
 *  execute()
 *    → PickEntryTangent
 *        POINT_ACQUIRED → nearestTangentIndex() → idx1  → PickExitTangent
 *        POINT_CANCELLED → cancel
 *    → PickExitTangent
 *        POINT_ACQUIRED → nearestTangentIndex() → idx2  → WaitingForRadius
 *        POINT_CANCELLED → cancel
 *    → WaitingForRadius
 *        NUMBER_INPUT "R=600" or "600" → m_radius  → WaitingForL1
 *        POINT_CANCELLED → cancel
 *    → WaitingForL1
 *        NUMBER_INPUT "L1=150"/"L=150"/"150"/"0" → m_L1 → WaitingForL2
 *        POINT_CANCELLED → cancel
 *    → WaitingForL2
 *        NUMBER_INPUT "L2=150"/"L=150"/"150"/"0" → m_L2 → WaitingForConfirm
 *        POINT_CANCELLED → cancel
 *    → WaitingForConfirm
 *        POINT_ACQUIRED  → commitSCS()
 *        NUMBER_INPUT ""（空 Enter）→ commitSCS()
 *        NUMBER_INPUT "R=…" → re-enter radius   → WaitingForRadius (re-enter)
 *        NUMBER_INPUT "L1=…" → re-enter L1       → WaitingForL1
 *        NUMBER_INPUT "L2=…" → re-enter L2       → WaitingForL2
 *        POINT_CANCELLED → cancel
 *
 * InputParser 擴充：
 *   R=<num>   → onRadiusInput
 *   L1=<num>  → onSpiralLength1Input
 *   L2=<num>  → onSpiralLength2Input
 *   L=<num>   → setSpiralLength1 AND setSpiralLength2 (symmetric)
 *   @x,y      → 相對座標（已由 InputParser::isRelativeCoordinate 處理）
 *   @dist<ang → 相對極座標（已由 InputParser::isPolarCoordinate 處理）
 */

#include "command/alignment/AlignmentSCSCommand.h"
#include "command/InputParser.h"
#include "core/Application.h"
#include "core/CommandLineManager.h"
#include "core/EventBus.h"
#include "railway/AlignmentDocument.h"
#include "view/RubberBand.h"

#include <QDebug>
#include <QtMath>
#include <cmath>
#include <limits>

using namespace aicad::core;

namespace aicad {
namespace command {

// ────────────────────────────────────────────────────────────────────────────
//  Constructor
// ────────────────────────────────────────────────────────────────────────────

AlignmentSCSCommand::AlignmentSCSCommand(QObject* parent)
    : AlignmentCommandBase("alignmentscs",
                           "Add SCS (Spiral-Circular-Spiral) curve between two tangents",
                           parent)
{}

// ────────────────────────────────────────────────────────────────────────────
//  execute
// ────────────────────────────────────────────────────────────────────────────

CommandResult AlignmentSCSCommand::execute(const CommandContext& context)
{
    m_alignDoc = context.alignmentDoc;
    if (!m_alignDoc) {
        return CommandResult::Failure(
            "No AlignmentDocument — open or create an alignment first.");
    }

    // 至少需要兩條切線
    const auto& elems = m_alignDoc->horizontal()->elements();
    int tangentCount = 0;
    for (const auto& e : elems) {
        if (e.type == railway::EditableElementType::Tangent)
            ++tangentCount;
    }
    if (tangentCount < 2) {
        return CommandResult::Failure(
            "At least two Fixed Tangents are required before adding an SCS curve. "
            "Use FT to add tangents first.");
    }

    // ── 初始化 ─────────────────────────────────────────────────────────────
    m_step        = Step::PickEntryTangent;
    m_isFinishing = false;
    m_idx1        = -1;
    m_idx2        = -1;
    m_radius      = 0.0;
    m_L1          = 0.0;
    m_L2          = 0.0;

    EventBus* bus = Application::instance()->eventBus();

    // SCS 預覽模式
    QVariantMap viewSetup;
    viewSetup["mode"]           = "sketching";
    viewSetup["rubberBandMode"] = "scs";
    bus->publish("command.request-view-setup", viewSetup);

    // ── 訂閱 EventBus（整個命令生命週期） ────────────────────────────────────

    bus->subscribe(Events::POINT_ACQUIRED, this,
                   [this](const QVariant& data) {
                       QVariantMap map = data.toMap();
                       QVector2D pt    = map["point"].value<QVector2D>();
                       QMetaObject::invokeMethod(this, [this, pt]() {
                           handlePointAcquired(pt);
                       }, Qt::QueuedConnection);
                   });

    bus->subscribe(Events::NUMBER_INPUT, this,
                   [this](const QVariant& data) {
                       QString text = data.toString();
                       QMetaObject::invokeMethod(this, [this, text]() {
                           handleNumberInput(text);
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
                 tr("SCS — Select ENTRY tangent (click near a tangent line):"));
    outputMessage("SCS — Select ENTRY tangent:");
    return CommandResult::Success("Waiting for input");
}

// ────────────────────────────────────────────────────────────────────────────
//  handlePointAcquired
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSCommand::handlePointAcquired(const QVector2D& point)
{
    if (m_isFinishing) return;

    EventBus* bus = Application::instance()->eventBus();

    switch (m_step) {

        // ── Step 1：選入切線 ──────────────────────────────────────────────────────
    case Step::PickEntryTangent: {
        int idx = AlignmentFloatCurveCommand::nearestTangentIndex(
            point, m_alignDoc->horizontal());
        if (idx < 0) {
            outputMessage("No tangent found — click closer to a tangent line.");
            bus->publish(Events::COMMAND_PROMPT,
                         tr("Select ENTRY tangent (click closer):"));
            return;
        }
        m_idx1 = idx;
        highlightTangent(m_idx1);
        outputMessage(QString("Entry tangent #%1 selected.  Select EXIT tangent:").arg(idx));
        bus->publish(Events::COMMAND_PROMPT, tr("Select EXIT tangent:"));
        m_step = Step::PickExitTangent;
        break;
    }

        // ── Step 2：選出切線 ──────────────────────────────────────────────────────
    case Step::PickExitTangent: {
        int idx = AlignmentFloatCurveCommand::nearestTangentIndex(
            point, m_alignDoc->horizontal());
        if (idx < 0) {
            outputMessage("No tangent found — click closer to a tangent line.");
            bus->publish(Events::COMMAND_PROMPT,
                         tr("Select EXIT tangent (click closer):"));
            return;
        }
        if (idx == m_idx1) {
            outputMessage("Please select a DIFFERENT tangent for the exit.");
            bus->publish(Events::COMMAND_PROMPT,
                         tr("Select a DIFFERENT tangent for the exit:"));
            return;
        }
        m_idx2 = idx;
        highlightTangent(m_idx2);
        outputMessage(QString("Exit tangent #%1 selected.  Enter R=<radius>:").arg(idx));
        bus->publish(Events::COMMAND_PROMPT, tr("R=<radius> (e.g. R=600):"));
        m_step = Step::WaitingForRadius;
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        break;
    }

        // ── WaitingForConfirm：點擊確認 ───────────────────────────────────────────
    case Step::WaitingForConfirm: {
        commitSCS();
        break;
    }

        // 其餘步驟忽略點擊
    default:
        break;
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  handleNumberInput
//
//  接收原始字串（可能是 "R=600"、"L1=150"、"L=150"、"150"、"" 等）。
//  依當前 m_step 決定如何解析。
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSCommand::handleNumberInput(const QString& text)
{
    if (m_isFinishing) return;

    EventBus* bus = Application::instance()->eventBus();
    const QString trimmed = text.trimmed();

    // ── WaitingForConfirm：允許返回重設各參數，或空 Enter 確認 ────────────────
    if (m_step == Step::WaitingForConfirm) {
        // 空輸入 → 確認
        if (trimmed.isEmpty()) {
            commitSCS();
            return;
        }
        // R= 重新輸入半徑
        double val = 0.0;
        if (InputParser::tryParseKeyValueDouble(trimmed, "R", val)) {
            if (val <= 0.0) {
                outputMessage("Radius must be > 0.");
                bus->publish(Events::COMMAND_PROMPT,
                             tr("Re-enter R=<radius>:"));
                CommandLineManager::instance()->waitForInput(core::InputType::Number);
                return;
            }
            m_radius = val;
            updateRubberBandPreview();
            outputMessage(QString("Radius updated to %1 m.").arg(m_radius, 0, 'f', 3));
            bus->publish(Events::COMMAND_PROMPT,
                         tr("R=%1  L1=%2  L2=%3 — Enter to confirm, or re-enter R= / L1= / L2=:")
                             .arg(m_radius, 0, 'f', 3)
                             .arg(m_L1, 0, 'f', 3)
                             .arg(m_L2, 0, 'f', 3));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        // L1= 重新輸入
        if (InputParser::tryParseKeyValueDouble(trimmed, "L1", val)) {
            m_L1 = (val < 0.0) ? 0.0 : val;
            updateRubberBandPreview();
            outputMessage(QString("L1 updated to %1 m.").arg(m_L1, 0, 'f', 3));
            bus->publish(Events::COMMAND_PROMPT,
                         tr("R=%1  L1=%2  L2=%3 — Enter to confirm:")
                             .arg(m_radius, 0, 'f', 3)
                             .arg(m_L1, 0, 'f', 3)
                             .arg(m_L2, 0, 'f', 3));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        // L2= 重新輸入
        if (InputParser::tryParseKeyValueDouble(trimmed, "L2", val)) {
            m_L2 = (val < 0.0) ? 0.0 : val;
            updateRubberBandPreview();
            outputMessage(QString("L2 updated to %1 m.").arg(m_L2, 0, 'f', 3));
            bus->publish(Events::COMMAND_PROMPT,
                         tr("R=%1  L1=%2  L2=%3 — Enter to confirm:")
                             .arg(m_radius, 0, 'f', 3)
                             .arg(m_L1, 0, 'f', 3)
                             .arg(m_L2, 0, 'f', 3));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        // L= 對稱設定
        if (InputParser::tryParseKeyValueDouble(trimmed, "L", val)) {
            m_L1 = m_L2 = (val < 0.0) ? 0.0 : val;
            updateRubberBandPreview();
            outputMessage(QString("L1=L2 updated to %1 m.").arg(m_L1, 0, 'f', 3));
            bus->publish(Events::COMMAND_PROMPT,
                         tr("R=%1  L1=%2  L2=%3 — Enter to confirm:")
                             .arg(m_radius, 0, 'f', 3)
                             .arg(m_L1, 0, 'f', 3)
                             .arg(m_L2, 0, 'f', 3));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        // 其餘純數字 → 也視為確認（相容輸入習慣）
        commitSCS();
        return;
    }

    // ── WaitingForRadius ───────────────────────────────────────────────────────
    if (m_step == Step::WaitingForRadius) {
        double val = 0.0;
        bool ok = InputParser::tryParseKeyValueDouble(trimmed, "R", val)
                  || InputParser::tryParseKeyedOrPlainDouble(trimmed, "R", val);
        if (!ok || val <= 0.0) {
            outputMessage(
                QString("Invalid radius '%1' — enter R=<positive number> or just the number.")
                    .arg(trimmed));
            bus->publish(Events::COMMAND_PROMPT,
                         tr("R=<radius> — enter a positive number:"));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        m_radius = val;
        outputMessage(QString("Radius R = %1 m.  Enter L1=<spiral length> (0 = no entry spiral):")
                          .arg(m_radius, 0, 'f', 3));
        bus->publish(Events::COMMAND_PROMPT,
                     tr("L1=<entry spiral length> (0 = none, e.g. L1=150):"));
        m_step = Step::WaitingForL1;
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        return;
    }

    // ── WaitingForL1 ──────────────────────────────────────────────────────────
    if (m_step == Step::WaitingForL1) {
        double val = 0.0;
        // 允許：L1=150 / L=150 / 150 / 0
        bool ok = InputParser::tryParseKeyValueDouble(trimmed, "L1", val)
                  || InputParser::tryParseKeyValueDouble(trimmed, "L",  val)
                  || InputParser::tryParseKeyedOrPlainDouble(trimmed, "L1", val);
        if (!ok || val < 0.0) {
            outputMessage(
                QString("Invalid L1 '%1' — enter L1=<length> or 0 for no entry spiral.")
                    .arg(trimmed));
            bus->publish(Events::COMMAND_PROMPT,
                         tr("L1=<entry spiral length> (0 = none):"));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        m_L1 = val;
        // If the user wrote "L=150" → set both L1 and L2 symmetrically and jump to Confirm
        double lSymm = 0.0;
        if (InputParser::tryParseKeyValueDouble(trimmed, "L", lSymm) && lSymm >= 0.0) {
            m_L2 = lSymm;
            updateRubberBandPreview();
            outputMessage(
                QString("L1=L2=%1 m (symmetric).  R=%2 m — Enter to confirm "
                        "or re-enter L2=<length>:")
                    .arg(m_L1, 0, 'f', 3)
                    .arg(m_radius, 0, 'f', 3));
            bus->publish(Events::COMMAND_PROMPT,
                         tr("L2=<exit spiral length> (or Enter to confirm L1=L2=%1):")
                             .arg(m_L1, 0, 'f', 3));
            m_step = Step::WaitingForL2;
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        updateRubberBandPreview();
        outputMessage(
            QString("L1 = %1 m.  Enter L2=<exit spiral length> (0 = none):")
                .arg(m_L1, 0, 'f', 3));
        bus->publish(Events::COMMAND_PROMPT,
                     tr("L2=<exit spiral length> (0 = none, e.g. L2=150):"));
        m_step = Step::WaitingForL2;
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        return;
    }

    // ── WaitingForL2 ──────────────────────────────────────────────────────────
    if (m_step == Step::WaitingForL2) {
        double val = 0.0;
        // 空 Enter → 保持 0（或沿用 m_L1 作對稱預設）
        if (trimmed.isEmpty()) {
            // 空 Enter 在此步驟：若 m_L2 尚未被設定，沿用 m_L1 作對稱預設
            m_L2 = m_L1;
        } else {
            bool ok = InputParser::tryParseKeyValueDouble(trimmed, "L2", val)
                      || InputParser::tryParseKeyValueDouble(trimmed, "L",  val)
                      || InputParser::tryParseKeyedOrPlainDouble(trimmed, "L2", val);
            if (!ok || val < 0.0) {
                outputMessage(
                    QString("Invalid L2 '%1' — enter L2=<length> or 0 for no exit spiral.")
                        .arg(trimmed));
                bus->publish(Events::COMMAND_PROMPT,
                             tr("L2=<exit spiral length> (0 = none):"));
                CommandLineManager::instance()->waitForInput(core::InputType::Number);
                return;
            }
            m_L2 = val;
        }
        updateRubberBandPreview();
        outputMessage(
            QString("SCS parameters:  R=%1 m  L1=%2 m  L2=%3 m\n"
                    "Press Enter or click to confirm, or re-enter R= / L1= / L2=:")
                .arg(m_radius, 0, 'f', 3)
                .arg(m_L1,     0, 'f', 3)
                .arg(m_L2,     0, 'f', 3));
        bus->publish(Events::COMMAND_PROMPT,
                     tr("R=%1  L1=%2  L2=%3 — Enter to confirm:")
                         .arg(m_radius, 0, 'f', 3)
                         .arg(m_L1,     0, 'f', 3)
                         .arg(m_L2,     0, 'f', 3));
        m_step = Step::WaitingForConfirm;
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        return;
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  commitSCS
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSCommand::commitSCS()
{
    if (m_isFinishing) return;

    if (m_idx1 < 0 || m_idx2 < 0 || m_radius <= 0.0) {
        outputMessage("Internal error: incomplete parameters for SCS curve.");
        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Failure("Incomplete SCS parameters"));
        return;
    }

    // ── Auto-correct reversed tangent selection ───────────────────────────
    // tangentIdxBefore (entry) must have a lower element index than
    // tangentIdxAfter (exit) because elements are appended in alignment order.
    // If the user selected exit THEN entry, swap both the indices and the
    // corresponding spiral lengths so L1 always belongs to the entry spiral
    // and L2 to the exit spiral.
    if (m_idx1 > m_idx2) {
        std::swap(m_idx1, m_idx2);
        std::swap(m_L1, m_L2);
        outputMessage(
            QString("Tangent selection order reversed — "
                    "using tangent #%1 as entry (L1=%2 m) "
                    "and #%3 as exit (L2=%4 m).")
                .arg(m_idx1)
                .arg(m_L1, 0, 'f', 3)
                .arg(m_idx2)
                .arg(m_L2, 0, 'f', 3));
    }

    // 呼叫非對稱版 addSCS（L1=L2=0 → 退化為 AFC）
    int idx = m_alignDoc->horizontal()->addSCS(
        m_idx1, m_idx2, m_radius, m_L1, m_L2);

    if (idx < 0) {
        outputMessage("Failed to add SCS curve — check tangent indices and parameters.");
        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Failure("addSCS returned -1"));
        return;
    }

    m_alignDoc->horizontal()->solve();   // emit changed() → AlignmentRenderer::refresh()

    const char* curveType =
        (m_L1 < 1e-9 && m_L2 < 1e-9) ? "Floating Arc (AFC)" :
            (m_L1 < 1e-9)                  ? "CS (no entry spiral)" :
            (m_L2 < 1e-9)                  ? "SC (no exit spiral)"  :
            "SCS";

    outputMessage(
        QString("%1 #%2  tangents(%3→%4)  R=%5 m  L1=%6 m  L2=%7 m")
            .arg(curveType)
            .arg(idx)
            .arg(m_idx1)
            .arg(m_idx2)
            .arg(m_radius, 0, 'f', 3)
            .arg(m_L1,     0, 'f', 3)
            .arg(m_L2,     0, 'f', 3));

    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("AlignmentSCS completed"));
}

// ────────────────────────────────────────────────────────────────────────────
//  handleCancelled
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSCommand::handleCancelled()
{
    qDebug() << "[SCS] Cancelled";
    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("AlignmentSCS cancelled"));
}

// ────────────────────────────────────────────────────────────────────────────
//  cleanup
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSCommand::cleanup()
{
    EventBus* bus = Application::instance()->eventBus();
    bus->unsubscribeAll(this);

    highlightTangent(-1);   // 清除高亮

    QVariantMap rb;
    rb["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", rb);

    m_step        = Step::PickEntryTangent;
    m_isFinishing = false;
    m_idx1        = -1;
    m_idx2        = -1;
    m_radius      = 0.0;
    m_L1          = 0.0;
    m_L2          = 0.0;
    m_alignDoc    = nullptr;
}

// ────────────────────────────────────────────────────────────────────────────
//  highlightTangent  (private)
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSCommand::highlightTangent(int elemIdx)
{
    QVariantMap msg;
    msg["elementIndex"] = elemIdx;
    Application::instance()->eventBus()
        ->publish("alignment.highlight-element", msg);
}

// ────────────────────────────────────────────────────────────────────────────
//  updateRubberBandPreview  (private)
//
//  在兩條切線都已知、半徑已設定之後，更新 RubberBand::SCS 預覽。
//  點位配置與 AlignmentFloatCurveCommand 相同：
//    points[0] = entry tangent endPI（切線尾端，靠近 PI）
//    points[1] = PI 估算中點
//    currentPoint = exit tangent startPI（出切線頭端）
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSCommand::updateRubberBandPreview()
{
    if (m_idx1 < 0 || m_idx2 < 0 || m_radius <= 0.0) return;

    const auto& elems = m_alignDoc->horizontal()->elements();
    if (m_idx1 >= elems.size() || m_idx2 >= elems.size()) return;

    const QPointF& t1end   = elems[m_idx1].endPI;
    const QPointF& t2start = elems[m_idx2].startPI;
    const QPointF  pi(
        (t1end.x() + t2start.x()) * 0.5,
        (t1end.y() + t2start.y()) * 0.5);

    EventBus* bus = Application::instance()->eventBus();

    // 設定半徑與螺旋長度（發事件讓 CadView 側更新 RubberBand 屬性）
    QVariantMap rbProps;
    rbProps["action"]        = "setParams";
    rbProps["mode"]          = "scs";
    rbProps["radius"]        = m_radius;
    rbProps["spiralLength1"] = m_L1;
    rbProps["spiralLength2"] = m_L2;
    bus->publish("command.update-rubber-band", rbProps);

    // 設定控制點：clearAndAdd 設起點 → addPoint 設 PI → setCurrentPoint 設出切線端
    QVariantMap rb1;
    rb1["action"] = "clearAndAdd";
    rb1["mode"]   = "scs";
    rb1["point"]  = QVariant::fromValue(
        QVector2D(static_cast<float>(t1end.x()),
                  static_cast<float>(t1end.y())));
    bus->publish("command.update-rubber-band", rb1);

    QVariantMap rb2;
    rb2["action"] = "addPoint";
    rb2["point"]  = QVariant::fromValue(
        QVector2D(static_cast<float>(pi.x()),
                  static_cast<float>(pi.y())));
    bus->publish("command.update-rubber-band", rb2);

    QVariantMap rb3;
    rb3["action"] = "setCurrentPoint";
    rb3["point"]  = QVariant::fromValue(
        QVector2D(static_cast<float>(t2start.x()),
                  static_cast<float>(t2start.y())));
    bus->publish("command.update-rubber-band", rb3);
}

// ────────────────────────────────────────────────────────────────────────────
//  getUsage
// ────────────────────────────────────────────────────────────────────────────

QString AlignmentSCSCommand::getUsage() const
{
    return
        "Usage: SCS\n"
        "  1. Click near the ENTRY tangent line.\n"
        "  2. Click near the EXIT tangent line.\n"
        "  3. Enter radius:        R=600  (or just 600)\n"
        "  4. Enter entry spiral:  L1=150 (or 0 for no entry spiral)\n"
        "  5. Enter exit spiral:   L2=150 (or 0 for no exit spiral)\n"
        "     Tip: 'L=150' sets both L1 and L2 to 150 (symmetric SCS).\n"
        "  6. Press Enter or click to confirm.\n"
        "  Right-click / ESC to cancel.\n"
        "\n"
        "  L1=L2=0   → Floating Arc (same as AFC)\n"
        "  L1>0,L2=0 → SC curve\n"
        "  L1=0,L2>0 → CS curve\n"
        "  L1=L2>0   → Symmetric SCS";
}

} // namespace command
} // namespace aicad
