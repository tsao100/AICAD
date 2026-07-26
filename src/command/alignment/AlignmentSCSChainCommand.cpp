/**
 * @file AlignmentSCSChainCommand.cpp
 * @brief SCSCHAIN — 實作。見標頭檔狀態機說明。
 */

#include "command/alignment/AlignmentSCSChainCommand.h"
#include "command/InputParser.h"
#include "core/Application.h"
#include "core/CommandLineManager.h"
#include "core/EventBus.h"
#include "railway/AlignmentDocument.h"

#include <QDebug>
#include <QtMath>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

using namespace aicad::core;
using aicad::railway::SpiralType;
using aicad::railway::HorizontalAlignmentEdit;

namespace aicad {
namespace command {

// ────────────────────────────────────────────────────────────────────────────
//  Constructor
// ────────────────────────────────────────────────────────────────────────────

AlignmentSCSChainCommand::AlignmentSCSChainCommand(QObject* parent)
    : AlignmentCommandBase("scschain",
                           "Add S0-C0-S1-C1-...-Sn compound chain (N>=2 arcs) between two tangents",
                           parent)
{}

// ────────────────────────────────────────────────────────────────────────────
//  execute
// ────────────────────────────────────────────────────────────────────────────

CommandResult AlignmentSCSChainCommand::execute(const CommandContext& context)
{
    m_alignDoc = context.alignmentDoc;
    if (!m_alignDoc) {
        return CommandResult::Failure(
            "No AlignmentDocument — open or create an alignment first.");
    }

    const auto& elems = m_alignDoc->horizontal()->elements();
    int tangentCount = 0;
    for (const auto& e : elems) {
        if (e.type == railway::EditableElementType::Tangent)
            ++tangentCount;
    }
    if (tangentCount < 2) {
        return CommandResult::Failure(
            "At least two Fixed Tangents are required before adding a compound chain. "
            "Use FT to add tangents first.");
    }

    m_step        = Step::PickEntryTangent;
    m_isFinishing = false;
    m_idx1        = -1;
    m_idx2        = -1;
    m_arcCount    = 0;
    m_lens.clear();
    m_radii.clear();
    m_fillIdx     = 0;
    m_fillField   = ChainField::SpiralLength;
    m_hasUnknown      = false;
    m_unknownIsSpiral = false;
    m_unknownIndex    = -1;
    m_arcAnglesDeg.clear();
    m_angleFillIdx    = 0;

    EventBus* bus = Application::instance()->eventBus();

    QVariantMap viewSetup;
    viewSetup["mode"]           = "sketching";
    viewSetup["rubberBandMode"] = "none";   // 見標頭檔「簡化說明」：本指令不驅動即時預覽
    bus->publish("command.request-view-setup", viewSetup);

    bus->subscribe(Events::POINT_ACQUIRED, this,
                   [this](const QVariant& data) {
                       QVariantMap map = data.toMap();
                       QPointF pt = map["point"].value<QPointF>();
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
                 tr("SCSCHAIN — Select ENTRY tangent (click near a tangent line):"));
    outputMessage("SCSCHAIN — Select ENTRY tangent:");
    return CommandResult::Success("Waiting for input");
}

// ────────────────────────────────────────────────────────────────────────────
//  handlePointAcquired
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSChainCommand::handlePointAcquired(const QPointF& point)
{
    if (m_isFinishing) return;

    EventBus* bus = Application::instance()->eventBus();

    switch (m_step) {

    case Step::PickEntryTangent: {
        int idx = AlignmentFloatCurveCommand::nearestTangentIndex(
            point, m_alignDoc->horizontal());
        if (idx < 0) {
            outputMessage("No tangent found — click closer to a tangent line.");
            bus->publish(Events::COMMAND_PROMPT, tr("Select ENTRY tangent (click closer):"));
            return;
        }
        m_idx1 = idx;
        highlightTangent(m_idx1);
        outputMessage(QString("Entry tangent #%1 selected.  Select EXIT tangent:").arg(idx));
        bus->publish(Events::COMMAND_PROMPT, tr("Select EXIT tangent:"));
        m_step = Step::PickExitTangent;
        break;
    }

    case Step::PickExitTangent: {
        int idx = AlignmentFloatCurveCommand::nearestTangentIndex(
            point, m_alignDoc->horizontal());
        if (idx < 0) {
            outputMessage("No tangent found — click closer to a tangent line.");
            bus->publish(Events::COMMAND_PROMPT, tr("Select EXIT tangent (click closer):"));
            return;
        }
        if (idx == m_idx1) {
            outputMessage("Please select a DIFFERENT tangent for the exit.");
            bus->publish(Events::COMMAND_PROMPT, tr("Select a DIFFERENT tangent for the exit:"));
            return;
        }
        m_idx2 = idx;
        highlightTangent(m_idx2);
        outputMessage(QString("Exit tangent #%1 selected.  Enter arc count N (>=2):").arg(idx));
        bus->publish(Events::COMMAND_PROMPT, tr("N=<arc count, e.g. N=3>:"));
        m_step = Step::WaitingForArcCount;
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        break;
    }

    case Step::WaitingForConfirm: {
        commitChain();
        break;
    }

    default:
        break;
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  handleNumberInput
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSChainCommand::handleNumberInput(const QString& text)
{
    if (m_isFinishing) return;

    EventBus* bus = Application::instance()->eventBus();
    const QString trimmed = text.trimmed();

    // ════════════════════════════════════════════════════════════════════
    //  WaitingForConfirm：允許 Rk=/Lk= 重新輸入，或空 Enter 確認
    // ════════════════════════════════════════════════════════════════════
    if (m_step == Step::WaitingForConfirm) {
        if (trimmed.isEmpty()) { commitChain(); return; }
        if (tryParseReentry(trimmed)) return;

        outputMessage("Unrecognised input. Press Enter to confirm, or Rk=.../Lk=... to re-enter.");
        bus->publish(Events::COMMAND_PROMPT,
                     tr("Enter to confirm, or Rk=<radius> / Lk=<length> to re-enter:"));
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        return;
    }

    // ════════════════════════════════════════════════════════════════════
    //  WaitingForArcCount
    // ════════════════════════════════════════════════════════════════════
    if (m_step == Step::WaitingForArcCount) {
        double val = 0.0;
        if (!InputParser::tryParseKeyedOrPlainDouble(trimmed, "N", val)) {
            outputMessage("Invalid arc count. Please enter an integer >= 2.");
            bus->publish(Events::COMMAND_PROMPT, tr("Re-enter N=<arc count>:"));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        const int n = static_cast<int>(std::lround(val));
        if (n < 2) {
            outputMessage("Arc count must be >= 2 (use SCS for a single arc).");
            bus->publish(Events::COMMAND_PROMPT, tr("Re-enter N=<arc count, >=2>:"));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }

        m_arcCount = n;
        m_lens  = QVector<double>(n + 1, 0.0);
        m_radii = QVector<double>(n, 0.0);
        m_arcAnglesDeg = QVector<double>(n, 0.0);
        m_fillIdx   = 0;
        m_fillField = ChainField::SpiralLength;

        outputMessage(QString("N=%1 arcs.  Now enter L0, R1, L1, R2, ..., R%1, L%1"
                              " (spiral lengths: 0 = omit that spiral):").arg(n));
        m_step = Step::WaitingForChainInput;
        promptNextChainField();
        return;
    }

    // ════════════════════════════════════════════════════════════════════
    //  WaitingForChainInput
    // ════════════════════════════════════════════════════════════════════
    if (m_step == Step::WaitingForChainInput) {
        const bool isSpiral = (m_fillField == ChainField::SpiralLength);
        const QString key   = isSpiral ? "L" : "R";

        // Phase 4：輸入 "?" 標記本欄位為交給 solver 反解的未知數（最多 1 個）。
        if (trimmed == QStringLiteral("?")) {
            if (m_hasUnknown) {
                outputMessage(QString("Only one unknown is allowed (already marked %1%2=?)."
                                      "  Enter a numeric value here instead.")
                                  .arg(m_unknownIsSpiral ? "L" : "R")
                                  .arg(m_unknownIsSpiral ? m_unknownIndex : m_unknownIndex + 1));
                bus->publish(Events::COMMAND_PROMPT,
                             tr("%1%2 = <numeric value> (unknown slot already used):")
                                 .arg(key).arg(isSpiral ? m_fillIdx : m_fillIdx + 1));
                CommandLineManager::instance()->waitForInput(core::InputType::Number);
                return;
            }
            m_hasUnknown      = true;
            m_unknownIsSpiral = isSpiral;
            m_unknownIndex    = m_fillIdx;
            if (isSpiral) m_lens[m_fillIdx]  = 0.0;   // placeholder, overwritten by solver
            else          m_radii[m_fillIdx] = 1.0;   // placeholder (must pass >0 downstream checks), overwritten by solver
            outputMessage(QString("%1%2 marked as UNKNOWN — will be solved by the solver."
                                  "  You will be asked for every arc's central angle next.")
                              .arg(key).arg(isSpiral ? m_fillIdx : m_fillIdx + 1));
            if (isSpiral) {
                if (m_fillIdx < m_arcCount) { m_fillField = ChainField::ArcRadius; }
                else { startArcAnglesOrConfirm(); return; }
            } else {
                m_fillField = ChainField::SpiralLength;
                m_fillIdx  += 1;
            }
            promptNextChainField();
            return;
        }

        double val = 0.0;
        if (!InputParser::tryParseKeyedOrPlainDouble(trimmed, key, val)) {
            outputMessage(QString("Invalid value for %1%2. (Enter a number, or \"?\" to mark"
                                  " this as the unknown.)").arg(key).arg(m_fillIdx));
            bus->publish(Events::COMMAND_PROMPT,
                         tr("Re-enter %1=%2:").arg(key).arg(isSpiral ? "<length, 0=omit, or ?>" : "<radius, >0, or ?>"));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }

        if (isSpiral) {
            if (val < 0.0) {
                outputMessage("Spiral length must be >= 0 (0 = omit).");
                bus->publish(Events::COMMAND_PROMPT, tr("Re-enter L%1=<length, 0=omit>:").arg(m_fillIdx));
                CommandLineManager::instance()->waitForInput(core::InputType::Number);
                return;
            }
            m_lens[m_fillIdx] = val;
        } else {
            if (val <= 0.0) {
                outputMessage("Radius must be > 0.");
                bus->publish(Events::COMMAND_PROMPT, tr("Re-enter R%1=<radius>:").arg(m_fillIdx + 1));
                CommandLineManager::instance()->waitForInput(core::InputType::Number);
                return;
            }
            m_radii[m_fillIdx] = val;
        }

        // ── 前進到下一個欄位：L0 R1 L1 R2 L2 ... RN LN ──────────────────────
        if (isSpiral) {
            if (m_fillIdx < m_arcCount) {
                m_fillField = ChainField::ArcRadius;   // 下一個是 R(fillIdx+1)
            } else {
                startArcAnglesOrConfirm();   // 剛填完 LN（最後一個）
                return;
            }
        } else {
            m_fillField = ChainField::SpiralLength;
            m_fillIdx  += 1;   // 下一個是 L(fillIdx)
        }
        promptNextChainField();
        return;
    }

    // ════════════════════════════════════════════════════════════════════
    //  WaitingForArcAngles（僅在標記了未知數時進入）
    // ════════════════════════════════════════════════════════════════════
    if (m_step == Step::WaitingForArcAngles) {
        double val = 0.0;
        if (!InputParser::tryParseKeyedOrPlainDouble(trimmed, "A", val) || val <= 0.0) {
            outputMessage(QString("Invalid value for A%1 (central angle, degrees, must be > 0)."
                                  ).arg(m_angleFillIdx + 1));
            bus->publish(Events::COMMAND_PROMPT, tr("Re-enter A%1=<degrees, >0>:").arg(m_angleFillIdx + 1));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        m_arcAnglesDeg[m_angleFillIdx] = val;
        ++m_angleFillIdx;
        if (m_angleFillIdx < m_arcCount) {
            bus->publish(Events::COMMAND_PROMPT,
                         tr("A%1 (arc %1 central angle, degrees) = <degrees, >0>:").arg(m_angleFillIdx + 1));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
        } else {
            goToConfirm();
        }
        return;
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  promptNextChainField
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSChainCommand::promptNextChainField()
{
    EventBus* bus = Application::instance()->eventBus();
    if (m_fillField == ChainField::SpiralLength) {
        const QString label = (m_fillIdx == 0) ? "L0 (entry spiral)"
            : (m_fillIdx == m_arcCount) ? QString("L%1 (exit spiral)").arg(m_fillIdx)
            : QString("L%1 (interior spiral)").arg(m_fillIdx);
        bus->publish(Events::COMMAND_PROMPT,
                     tr("%1 = <length, 0=omit, or ? for unknown>:").arg(label));
    } else {
        bus->publish(Events::COMMAND_PROMPT,
                     tr("R%1 (arc %1 radius) = <radius, or ? for unknown>:").arg(m_fillIdx + 1));
    }
    CommandLineManager::instance()->waitForInput(core::InputType::Number);
}

// ────────────────────────────────────────────────────────────────────────────
//  tryParseReentry
// ────────────────────────────────────────────────────────────────────────────

bool AlignmentSCSChainCommand::tryParseReentry(const QString& text)
{
    // Rk=<radius>  (1<=k<=N)
    // Lk=<length>  (0<=k<=N)
    // Ak=<degrees> (1<=k<=N, only meaningful when m_hasUnknown)
    static const QRegularExpression reR(QStringLiteral("^[Rr](\\d+)\\s*=\\s*(.+)$"));
    static const QRegularExpression reL(QStringLiteral("^[Ll](\\d+)\\s*=\\s*(.+)$"));
    static const QRegularExpression reA(QStringLiteral("^[Aa](\\d+)\\s*=\\s*(.+)$"));

    QRegularExpressionMatch m = reR.match(text);
    if (m.hasMatch()) {
        const int k = m.captured(1).toInt();
        if (m_hasUnknown && !m_unknownIsSpiral && m_unknownIndex == k - 1) {
            outputMessage(QString("R%1 is the marked unknown — it is solved automatically,"
                                  " not re-entered.").arg(k));
            return true;
        }
        bool ok = false;
        const double val = m.captured(2).toDouble(&ok);
        if (!ok || val <= 0.0 || k < 1 || k > m_arcCount) {
            outputMessage(QString("Invalid R%1 value.").arg(k));
            return true;
        }
        m_radii[k - 1] = val;
        outputMessage(QString("R%1 updated to %2 m.").arg(k).arg(val, 0, 'f', 3));
        goToConfirm();
        return true;
    }

    m = reL.match(text);
    if (m.hasMatch()) {
        const int k = m.captured(1).toInt();
        if (m_hasUnknown && m_unknownIsSpiral && m_unknownIndex == k) {
            outputMessage(QString("L%1 is the marked unknown — it is solved automatically,"
                                  " not re-entered.").arg(k));
            return true;
        }
        bool ok = false;
        const double val = m.captured(2).toDouble(&ok);
        if (!ok || val < 0.0 || k < 0 || k > m_arcCount) {
            outputMessage(QString("Invalid L%1 value.").arg(k));
            return true;
        }
        m_lens[k] = val;
        outputMessage(QString("L%1 updated to %2 m.").arg(k).arg(val, 0, 'f', 3));
        goToConfirm();
        return true;
    }

    m = reA.match(text);
    if (m.hasMatch()) {
        const int k = m.captured(1).toInt();
        if (!m_hasUnknown) {
            outputMessage("Ak= is only meaningful once an unknown (?) has been marked.");
            return true;
        }
        bool ok = false;
        const double val = m.captured(2).toDouble(&ok);
        if (!ok || val <= 0.0 || k < 1 || k > m_arcCount) {
            outputMessage(QString("Invalid A%1 value.").arg(k));
            return true;
        }
        m_arcAnglesDeg[k - 1] = val;
        outputMessage(QString("A%1 updated to %2 deg.").arg(k).arg(val, 0, 'f', 4));
        goToConfirm();
        return true;
    }

    return false;
}

// ────────────────────────────────────────────────────────────────────────────
//  startArcAnglesOrConfirm
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSChainCommand::startArcAnglesOrConfirm()
{
    if (!m_hasUnknown) {
        goToConfirm();
        return;
    }

    EventBus* bus = Application::instance()->eventBus();
    m_angleFillIdx = 0;
    outputMessage(
        QString("An unknown was marked (%1%2=?).  Since only 1 closure equation is available,"
                " every arc's central angle must now be pinned (no auto-split) -- enter A1..A%3:")
            .arg(m_unknownIsSpiral ? "L" : "R")
            .arg(m_unknownIsSpiral ? m_unknownIndex : m_unknownIndex + 1)
            .arg(m_arcCount));
    bus->publish(Events::COMMAND_PROMPT,
                 tr("A1 (arc 1 central angle, degrees) = <degrees, >0>:"));
    m_step = Step::WaitingForArcAngles;
    CommandLineManager::instance()->waitForInput(core::InputType::Number);
}

// ────────────────────────────────────────────────────────────────────────────
//  goToConfirm
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSChainCommand::goToConfirm()
{
    EventBus* bus = Application::instance()->eventBus();

    QString summary = QString("Compound chain: %1 tangents(%2\xE2\x86\x92%3), N=%4 arcs\n")
                          .arg("SCSCHAIN").arg(m_idx1).arg(m_idx2).arg(m_arcCount);
    auto isUnknownSpiral = [this](int idx) { return m_hasUnknown && m_unknownIsSpiral && m_unknownIndex == idx; };
    auto isUnknownRadius = [this](int idx) { return m_hasUnknown && !m_unknownIsSpiral && m_unknownIndex == idx; };

    summary += QString("  L0=%1\n").arg(isUnknownSpiral(0) ? QStringLiteral("? (unknown, solved)")
                                                            : QString::number(m_lens[0], 'f', 3));
    for (int k = 0; k < m_arcCount; ++k) {
        summary += QString("  R%1=%2   L%3=%4\n")
                       .arg(k + 1)
                       .arg(isUnknownRadius(k) ? QStringLiteral("? (unknown, solved)")
                                               : QString::number(m_radii[k], 'f', 3))
                       .arg(k + 1)
                       .arg(isUnknownSpiral(k + 1) ? QStringLiteral("? (unknown, solved)")
                                                   : QString::number(m_lens[k + 1], 'f', 3));
    }
    if (m_hasUnknown) {
        summary += "  Pinned arc central angles:\n";
        for (int k = 0; k < m_arcCount; ++k)
            summary += QString("    A%1=%2 deg\n").arg(k + 1).arg(m_arcAnglesDeg[k], 0, 'f', 4);
    }
    summary += m_hasUnknown
        ? "Press Enter or click to confirm, or re-enter Rk=.../Lk=.../Ak=... (unknown slot excluded):"
        : "Press Enter or click to confirm, or re-enter Rk=.../Lk=...:";
    outputMessage(summary);

    bus->publish(Events::COMMAND_PROMPT,
                 tr("N=%1 arcs ready — Enter to confirm, or Rk=/Lk=/Ak= to re-enter:").arg(m_arcCount));

    m_step = Step::WaitingForConfirm;
    CommandLineManager::instance()->waitForInput(core::InputType::Number);
}

// ────────────────────────────────────────────────────────────────────────────
//  commitChain
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSChainCommand::commitChain()
{
    if (m_isFinishing) return;

    if (m_idx1 < 0 || m_idx2 < 0 || m_arcCount < 2
        || m_lens.size() != m_arcCount + 1 || m_radii.size() != m_arcCount) {
        outputMessage("Internal error: incomplete parameters for compound chain.");
        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Failure("Incomplete compound chain parameters"));
        return;
    }

    // ── Auto-correct reversed tangent selection ───────────────────────────
    if (m_idx1 > m_idx2) {
        std::swap(m_idx1, m_idx2);
        std::reverse(m_lens.begin(), m_lens.end());
        std::reverse(m_radii.begin(), m_radii.end());
        if (m_hasUnknown) {
            m_unknownIndex = m_unknownIsSpiral ? (m_arcCount - m_unknownIndex)
                                                : (m_arcCount - 1 - m_unknownIndex);
            std::reverse(m_arcAnglesDeg.begin(), m_arcAnglesDeg.end());
        }
        outputMessage(
            QString("Tangent selection order reversed — using tangent #%1 as entry"
                    " and #%2 as exit (arc/spiral order also reversed).")
                .arg(m_idx1).arg(m_idx2));
    }

    railway::HorizontalAlignmentEdit::CompoundChainSpec spec;
    spec.arcs.resize(m_arcCount);
    spec.spirals.resize(m_arcCount + 1);
    for (int k = 0; k < m_arcCount; ++k) {
        spec.arcs[k].radius = m_radii[k];
        if (m_hasUnknown) {
            // 指定未知數時，每段圓弧心角都必須釘死（見 AlignmentSolver.h
            // CompoundChainUnknown）；本指令的 WaitingForArcAngles 階段已
            // 強制收集 A1..AN，這裡直接轉成弧度寫入。
            spec.arcs[k].centralAngle = qDegreesToRadians(m_arcAnglesDeg[k]);
            if (!m_unknownIsSpiral && m_unknownIndex == k) spec.arcs[k].radiusIsUnknown = true;
        }
        // m_hasUnknown==false 時 centralAngle 留 0 = 由 solveCompoundChain()
        // 自動平分剩餘轉角（見 AlignmentDocument.h CompoundChainSpec 註解）。
    }
    for (int k = 0; k <= m_arcCount; ++k) {
        spec.spirals[k].length = m_lens[k];
        spec.spirals[k].type   = SpiralType::Clothoid;
        if (m_hasUnknown && m_unknownIsSpiral && m_unknownIndex == k)
            spec.spirals[k].lengthIsUnknown = true;
    }

    const int idx = m_alignDoc->horizontal()->addCompoundChain(m_idx1, m_idx2, spec);

    if (idx < 0) {
        outputMessage("Failed to add compound chain — check tangent indices and parameters.");
        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Failure("addCompoundChain returned -1"));
        return;
    }

    m_alignDoc->horizontal()->solve();

    outputMessage(
        QString("Compound Chain #%1  tangents(%2\xE2\x86\x92%3)  N=%4 arcs%5")
            .arg(idx).arg(m_idx1).arg(m_idx2).arg(m_arcCount)
            .arg(m_hasUnknown ? "  (1 unknown solved)" : ""));

    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("AlignmentSCSChain completed"));
}

// ────────────────────────────────────────────────────────────────────────────
//  handleCancelled
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSChainCommand::handleCancelled()
{
    qDebug() << "[SCSCHAIN] Cancelled";
    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("AlignmentSCSChain cancelled"));
}

// ────────────────────────────────────────────────────────────────────────────
//  cleanup
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSChainCommand::cleanup()
{
    EventBus* bus = Application::instance()->eventBus();
    bus->unsubscribeAll(this);

    highlightTangent(-1);

    QVariantMap rb;
    rb["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", rb);

    m_step        = Step::PickEntryTangent;
    m_isFinishing = false;
    m_idx1        = -1;
    m_idx2        = -1;
    m_arcCount    = 0;
    m_lens.clear();
    m_radii.clear();
    m_fillIdx     = 0;
    m_fillField   = ChainField::SpiralLength;
    m_hasUnknown      = false;
    m_unknownIsSpiral = false;
    m_unknownIndex    = -1;
    m_arcAnglesDeg.clear();
    m_angleFillIdx    = 0;
    m_alignDoc    = nullptr;
}

// ────────────────────────────────────────────────────────────────────────────
//  highlightTangent
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSChainCommand::highlightTangent(int elemIdx)
{
    QVariantMap msg;
    msg["elementIndex"] = elemIdx;
    Application::instance()->eventBus()
        ->publish("alignment.highlight-element", msg);
}

// ────────────────────────────────────────────────────────────────────────────
//  getUsage
// ────────────────────────────────────────────────────────────────────────────

QString AlignmentSCSChainCommand::getUsage() const
{
    return
        "Usage: SCSCHAIN\n"
        "  1. Click near the ENTRY tangent line.\n"
        "  2. Click near the EXIT tangent line.\n"
        "  3. Enter arc count:        N=3  (or just 3; must be >= 2)\n"
        "  4. Enter, in order:        L0, R1, L1, R2, L2, ..., RN, LN\n"
        "       Lk = spiral length (0 = omit that spiral)\n"
        "       Rk = arc k radius (must be > 0)\n"
        "       Type \"?\" for ANY ONE of these to mark it as an unknown for\n"
        "       the solver to find (at most one -- only 1 closure equation\n"
        "       is available, see AlignmentSolver.h CompoundChainUnknown).\n"
        "  5. If you marked an unknown: enter A1..AN (each arc's central\n"
        "     angle, in degrees). This is mandatory once an unknown is set,\n"
        "     because auto-split angles would absorb the closure equation\n"
        "     and leave nothing to solve for.\n"
        "  6. Press Enter or click to confirm.\n"
        "     At confirm, re-enter any (non-unknown) value with\n"
        "     Rk=.../Lk=.../Ak=...\n"
        "  Right-click / ESC to cancel.\n"
        "\n"
        "  Without a marked unknown, all arcs' central angles are\n"
        "  automatically split from the remaining turning angle (equal\n"
        "  share) -- see AlignmentDocument.h CompoundChainSpec for the\n"
        "  rationale. Spiral type is fixed at Clothoid; use the data\n"
        "  table's TS/CS double-click menu to change individual spiral\n"
        "  types after insertion.\n"
        "\n"
        "  For a single arc (N=1), use SCS instead.";
}

// ── 命令註冊 ──────────────────────────────────────────────────────────────
// 原本誤放在 AlignmentSCSChainCommand.h 內（namespace 作用域下的檔頭巨集），
// 違反本專案「REGISTER_COMMAND 必須放在 .cpp」的慣例（見其他 alignment
// 指令，如 ImportAlignmentCommand.cpp 底部）——若該標頭日後被其他 .cpp
// include，會在每個引用它的翻譯單元各自產生一份靜態初始化器，屬於未定義
// 行為的溫床、也可能造成難以排查的「靜默註冊失敗」。移到這裡，確保只有
// 這個 .cpp 對應的單一翻譯單元會執行註冊。
REGISTER_COMMAND("scschain", AlignmentSCSChainCommand);

} // namespace command
} // namespace aicad
