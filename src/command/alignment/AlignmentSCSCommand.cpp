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
 *        NUMBER_INPUT "L1=150"/"L=150"/"150"/"0" → m_L1
 *          L1 > 0 → WaitingForType1
 *          L1 = 0 → WaitingForL2（跳過類型選擇）
 *        POINT_CANCELLED → cancel
 *    → WaitingForType1
 *        NUMBER_INPUT "T1=CLOTHOID"/"T1=HS"/…/Enter（保留預設） → m_type1
 *          → WaitingForL2
 *        POINT_CANCELLED → cancel
 *    → WaitingForL2
 *        NUMBER_INPUT "L2=150"/"L=150"/"150"/"0"/"" → m_L2
 *          L2 > 0 → WaitingForType2
 *          L2 = 0 → WaitingForConfirm（跳過類型選擇）
 *        POINT_CANCELLED → cancel
 *    → WaitingForType2
 *        NUMBER_INPUT "T2=CLOTHOID"/…/Enter → m_type2 → WaitingForConfirm
 *        POINT_CANCELLED → cancel
 *    → WaitingForConfirm
 *        POINT_ACQUIRED  → commitSCS()
 *        NUMBER_INPUT ""（空 Enter）→ commitSCS()
 *        NUMBER_INPUT "R=…"  → re-enter radius   → WaitingForRadius
 *        NUMBER_INPUT "L1=…" → re-enter L1        → WaitingForL1
 *        NUMBER_INPUT "L2=…" → re-enter L2        → WaitingForL2
 *        NUMBER_INPUT "T1=…" → re-enter type1     → WaitingForType1
 *        NUMBER_INPUT "T2=…" → re-enter type2     → WaitingForType2
 *        POINT_CANCELLED → cancel
 *
 * 螺旋類型輸入語法（T1= / T2= 前綴，或無前綴純名稱）：
 *   CLOTHOID / C          → SpiralType::Clothoid（預設）
 *   HALFSINE / HS         → SpiralType::HalfSine
 *   PARABOLA / P          → SpiralType::Parabola
 *   CUBICJPN / JPN        → SpiralType::CubicJPN
 *   CUBICECI / ECI        → SpiralType::CubicECI
 *   Enter（空輸入）        → 保持目前值（初始為 Clothoid）
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
using aicad::railway::SpiralType;

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
//  parseSpiralType  (static)
// ────────────────────────────────────────────────────────────────────────────

bool AlignmentSCSCommand::parseSpiralType(const QString& text, SpiralType& out)
{
    if (text.isEmpty()) return false;   // 空輸入 → 保持呼叫方預設值

    // 去除 T1= / T2= 前綴（大小寫不敏感）
    QString key = text.toUpper();
    if (key.startsWith(QLatin1String("T1=")) || key.startsWith(QLatin1String("T2=")))
        key = key.mid(3);

    if (key == QLatin1String("CLOTHOID") || key == QLatin1String("C")) {
        out = SpiralType::Clothoid; return true;
    }
    if (key == QLatin1String("HALFSINE") || key == QLatin1String("HS")) {
        out = SpiralType::HalfSine; return true;
    }
    if (key == QLatin1String("PARABOLA") || key == QLatin1String("P")) {
        out = SpiralType::Parabola; return true;
    }
    if (key == QLatin1String("CUBICJPN") || key == QLatin1String("JPN")) {
        out = SpiralType::CubicJPN; return true;
    }
    if (key == QLatin1String("CUBICECI") || key == QLatin1String("ECI")) {
        out = SpiralType::CubicECI; return true;
    }
    return false;   // 無法識別
}

// ────────────────────────────────────────────────────────────────────────────
//  spiralTypeName  (static)
// ────────────────────────────────────────────────────────────────────────────

QString AlignmentSCSCommand::spiralTypeName(SpiralType t)
{
    switch (t) {
    case SpiralType::HalfSine: return QStringLiteral("HalfSine");
    case SpiralType::Parabola: return QStringLiteral("Parabola");
    case SpiralType::CubicJPN: return QStringLiteral("CubicJPN");
    case SpiralType::CubicECI: return QStringLiteral("CubicECI");
    case SpiralType::Clothoid:
    default:                   return QStringLiteral("Clothoid");
    }
}

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
    m_type1       = SpiralType::Clothoid;
    m_type2       = SpiralType::Clothoid;

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
                 tr("SCS — Select ENTRY tangent (click near a tangent line):"));
    outputMessage("SCS — Select ENTRY tangent:");
    return CommandResult::Success("Waiting for input");
}

// ────────────────────────────────────────────────────────────────────────────
//  handlePointAcquired
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSCommand::handlePointAcquired(const QPointF& point)
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
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSCommand::handleNumberInput(const QString& text)
{
    if (m_isFinishing) return;

    EventBus* bus = Application::instance()->eventBus();
    const QString trimmed = text.trimmed();

    // ════════════════════════════════════════════════════════════════════════
    //  WaitingForConfirm：允許返回重設各參數，或空 Enter 確認
    // ════════════════════════════════════════════════════════════════════════
    if (m_step == Step::WaitingForConfirm) {
        if (trimmed.isEmpty()) { commitSCS(); return; }

        double val = 0.0;

        // R= 重新輸入半徑
        if (InputParser::tryParseKeyValueDouble(trimmed, "R", val)) {
            if (val <= 0.0) {
                outputMessage("Radius must be > 0.");
                bus->publish(Events::COMMAND_PROMPT, tr("Re-enter R=<radius>:"));
                CommandLineManager::instance()->waitForInput(core::InputType::Number);
                return;
            }
            m_radius = val;
            updateRubberBandPreview();
            bus->publish(Events::COMMAND_PROMPT,
                         tr("R=%1  L1=%2[%3]  L2=%4[%5] — Enter to confirm, "
                            "or re-enter R=/L1=/T1=/L2=/T2=:")
                             .arg(m_radius, 0, 'f', 3)
                             .arg(m_L1, 0, 'f', 3).arg(spiralTypeName(m_type1))
                             .arg(m_L2, 0, 'f', 3).arg(spiralTypeName(m_type2)));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        // L1= 重新輸入
        if (InputParser::tryParseKeyValueDouble(trimmed, "L1", val)) {
            m_L1 = (val < 0.0) ? 0.0 : val;
            updateRubberBandPreview();
            bus->publish(Events::COMMAND_PROMPT,
                         tr("L1 updated to %1 m — Enter to confirm:").arg(m_L1, 0, 'f', 3));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        // L2= 重新輸入
        if (InputParser::tryParseKeyValueDouble(trimmed, "L2", val)) {
            m_L2 = (val < 0.0) ? 0.0 : val;
            updateRubberBandPreview();
            bus->publish(Events::COMMAND_PROMPT,
                         tr("L2 updated to %1 m — Enter to confirm:").arg(m_L2, 0, 'f', 3));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        // L= 對稱設定
        if (InputParser::tryParseKeyValueDouble(trimmed, "L", val)) {
            m_L1 = m_L2 = (val < 0.0) ? 0.0 : val;
            updateRubberBandPreview();
            bus->publish(Events::COMMAND_PROMPT,
                         tr("L1=L2=%1 m — Enter to confirm:").arg(m_L1, 0, 'f', 3));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        // T1= 重新輸入入螺旋類型
        if (trimmed.toUpper().startsWith(QLatin1String("T1="))) {
            SpiralType t = m_type1;
            if (parseSpiralType(trimmed, t)) {
                m_type1 = t;
                outputMessage(QString("Entry spiral type updated to %1.").arg(spiralTypeName(m_type1)));
            } else {
                outputMessage(QString("Unknown type '%1' — valid: CLOTHOID(C), HALFSINE(HS), "
                                      "PARABOLA(P), CUBICJPN(JPN), CUBICECI(ECI).")
                                  .arg(trimmed));
            }
            bus->publish(Events::COMMAND_PROMPT,
                         tr("R=%1  L1=%2[%3]  L2=%4[%5] — Enter to confirm:")
                             .arg(m_radius, 0, 'f', 3)
                             .arg(m_L1, 0, 'f', 3).arg(spiralTypeName(m_type1))
                             .arg(m_L2, 0, 'f', 3).arg(spiralTypeName(m_type2)));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        // T2= 重新輸入出螺旋類型
        if (trimmed.toUpper().startsWith(QLatin1String("T2="))) {
            SpiralType t = m_type2;
            if (parseSpiralType(trimmed, t)) {
                m_type2 = t;
                outputMessage(QString("Exit spiral type updated to %1.").arg(spiralTypeName(m_type2)));
            } else {
                outputMessage(QString("Unknown type '%1' — valid: CLOTHOID(C), HALFSINE(HS), "
                                      "PARABOLA(P), CUBICJPN(JPN), CUBICECI(ECI).")
                                  .arg(trimmed));
            }
            bus->publish(Events::COMMAND_PROMPT,
                         tr("R=%1  L1=%2[%3]  L2=%4[%5] — Enter to confirm:")
                             .arg(m_radius, 0, 'f', 3)
                             .arg(m_L1, 0, 'f', 3).arg(spiralTypeName(m_type1))
                             .arg(m_L2, 0, 'f', 3).arg(spiralTypeName(m_type2)));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        // 其餘純數字或無法辨識輸入 → 確認
        commitSCS();
        return;
    }

    // ════════════════════════════════════════════════════════════════════════
    //  WaitingForRadius
    // ════════════════════════════════════════════════════════════════════════
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
        outputMessage(QString("Radius R = %1 m.  Enter L1=<entry spiral length> (0 = no entry spiral):")
                          .arg(m_radius, 0, 'f', 3));
        bus->publish(Events::COMMAND_PROMPT,
                     tr("L1=<entry spiral length> (0 = none, e.g. L1=150):"));
        m_step = Step::WaitingForL1;
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        return;
    }

    // ════════════════════════════════════════════════════════════════════════
    //  WaitingForL1
    // ════════════════════════════════════════════════════════════════════════
    if (m_step == Step::WaitingForL1) {
        double val = 0.0;
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

        // "L=150" → 對稱，同時設定 L2，跳到 WaitingForType1（或 WaitingForL2）
        double lSymm = 0.0;
        if (InputParser::tryParseKeyValueDouble(trimmed, "L", lSymm) && lSymm >= 0.0) {
            m_L2 = lSymm;
            updateRubberBandPreview();
            outputMessage(QString("L1=L2=%1 m (symmetric).").arg(m_L1, 0, 'f', 3));
            // 進入 WaitingForType1（若 L1 > 0）
            if (m_L1 > 1e-9) {
                bus->publish(Events::COMMAND_PROMPT,
                             tr("Entry spiral type T1= [Clothoid(C) / HalfSine(HS) / "
                                "Parabola(P) / CubicJPN(JPN) / CubicECI(ECI)] "
                                "(Enter = Clothoid):"));
                m_step = Step::WaitingForType1;
            } else {
                // L1=0：跳過 Type1，進入 WaitingForL2
                bus->publish(Events::COMMAND_PROMPT,
                             tr("L2=<exit spiral length> (or Enter to confirm L1=L2=%1):")
                                 .arg(m_L1, 0, 'f', 3));
                m_step = Step::WaitingForL2;
            }
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }

        updateRubberBandPreview();

        if (m_L1 > 1e-9) {
            // L1 > 0：詢問入螺旋類型
            outputMessage(QString("L1 = %1 m.  Choose entry spiral type (T1=):")
                              .arg(m_L1, 0, 'f', 3));
            bus->publish(Events::COMMAND_PROMPT,
                         tr("T1= Clothoid(C) / HalfSine(HS) / Parabola(P) / "
                            "CubicJPN(JPN) / CubicECI(ECI)  [Enter = Clothoid]:"));
            m_step = Step::WaitingForType1;
        } else {
            // L1 = 0：跳過類型，直接詢問 L2
            outputMessage("L1 = 0 (no entry spiral).  Enter L2=<exit spiral length>:");
            bus->publish(Events::COMMAND_PROMPT,
                         tr("L2=<exit spiral length> (0 = none, e.g. L2=150):"));
            m_step = Step::WaitingForL2;
        }
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        return;
    }

    // ════════════════════════════════════════════════════════════════════════
    //  WaitingForType1  — 入螺旋類型
    // ════════════════════════════════════════════════════════════════════════
    if (m_step == Step::WaitingForType1) {
        if (!trimmed.isEmpty()) {
            SpiralType t = m_type1;
            if (parseSpiralType(trimmed, t)) {
                m_type1 = t;
            } else {
                outputMessage(
                    QString("Unknown spiral type '%1'.\n"
                            "Valid: CLOTHOID(C)  HALFSINE(HS)  PARABOLA(P)  "
                            "CUBICJPN(JPN)  CUBICECI(ECI)")
                        .arg(trimmed));
                bus->publish(Events::COMMAND_PROMPT,
                             tr("T1= [C / HS / P / JPN / ECI]  (Enter = Clothoid):"));
                CommandLineManager::instance()->waitForInput(core::InputType::Number);
                return;
            }
        }
        // 空 Enter 或成功解析 → 繼續詢問 L2
        outputMessage(QString("Entry spiral type: %1.  Enter L2=<exit spiral length>:")
                          .arg(spiralTypeName(m_type1)));
        bus->publish(Events::COMMAND_PROMPT,
                     tr("L2=<exit spiral length> (0 = none, e.g. L2=150):"));
        m_step = Step::WaitingForL2;
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        return;
    }

    // ════════════════════════════════════════════════════════════════════════
    //  WaitingForL2
    // ════════════════════════════════════════════════════════════════════════
    if (m_step == Step::WaitingForL2) {
        double val = 0.0;
        if (trimmed.isEmpty()) {
            // 空 Enter：沿用 m_L1 作對稱預設
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

        if (m_L2 > 1e-9) {
            // L2 > 0：詢問出螺旋類型
            outputMessage(QString("L2 = %1 m.  Choose exit spiral type (T2=):")
                              .arg(m_L2, 0, 'f', 3));
            bus->publish(Events::COMMAND_PROMPT,
                         tr("T2= Clothoid(C) / HalfSine(HS) / Parabola(P) / "
                            "CubicJPN(JPN) / CubicECI(ECI)  [Enter = Clothoid]:"));
            m_step = Step::WaitingForType2;
        } else {
            // L2 = 0：跳過類型，進入確認
            goToConfirm();
        }
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        return;
    }

    // ════════════════════════════════════════════════════════════════════════
    //  WaitingForType2  — 出螺旋類型
    // ════════════════════════════════════════════════════════════════════════
    if (m_step == Step::WaitingForType2) {
        if (!trimmed.isEmpty()) {
            SpiralType t = m_type2;
            if (parseSpiralType(trimmed, t)) {
                m_type2 = t;
            } else {
                outputMessage(
                    QString("Unknown spiral type '%1'.\n"
                            "Valid: CLOTHOID(C)  HALFSINE(HS)  PARABOLA(P)  "
                            "CUBICJPN(JPN)  CUBICECI(ECI)")
                        .arg(trimmed));
                bus->publish(Events::COMMAND_PROMPT,
                             tr("T2= [C / HS / P / JPN / ECI]  (Enter = Clothoid):"));
                CommandLineManager::instance()->waitForInput(core::InputType::Number);
                return;
            }
        }
        outputMessage(QString("Exit spiral type: %1.").arg(spiralTypeName(m_type2)));
        goToConfirm();
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        return;
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  goToConfirm  (private helper)
// ────────────────────────────────────────────────────────────────────────────

void AlignmentSCSCommand::goToConfirm()
{
    EventBus* bus = Application::instance()->eventBus();

    const char* curveTag =
        (m_L1 < 1e-9 && m_L2 < 1e-9) ? "AFC" :
            (m_L1 < 1e-9)                  ? "CS"  :
            (m_L2 < 1e-9)                  ? "SC"  : "SCS";

    outputMessage(
        QString("%1 parameters:\n"
                "  R  = %2 m\n"
                "  L1 = %3 m  [%4]\n"
                "  L2 = %5 m  [%6]\n"
                "Press Enter or click to confirm, or re-enter R= / L1= / T1= / L2= / T2=:")
            .arg(curveTag)
            .arg(m_radius, 0, 'f', 3)
            .arg(m_L1, 0, 'f', 3).arg(spiralTypeName(m_type1))
            .arg(m_L2, 0, 'f', 3).arg(spiralTypeName(m_type2)));

    bus->publish(Events::COMMAND_PROMPT,
                 tr("R=%1  L1=%2[%3]  L2=%4[%5] — Enter to confirm:")
                     .arg(m_radius, 0, 'f', 3)
                     .arg(m_L1, 0, 'f', 3).arg(spiralTypeName(m_type1))
                     .arg(m_L2, 0, 'f', 3).arg(spiralTypeName(m_type2)));

    m_step = Step::WaitingForConfirm;
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
    if (m_idx1 > m_idx2) {
        std::swap(m_idx1, m_idx2);
        std::swap(m_L1,   m_L2);
        std::swap(m_type1, m_type2);
        outputMessage(
            QString("Tangent selection order reversed — "
                    "using tangent #%1 as entry (L1=%2 m [%3]) "
                    "and #%4 as exit (L2=%5 m [%6]).")
                .arg(m_idx1)
                .arg(m_L1, 0, 'f', 3).arg(spiralTypeName(m_type1))
                .arg(m_idx2)
                .arg(m_L2, 0, 'f', 3).arg(spiralTypeName(m_type2)));
    }

    // 呼叫完整版 addSCS（L1=L2=0 → 退化為 AFC）
    int idx = m_alignDoc->horizontal()->addSCS(
        m_idx1, m_idx2, m_radius, m_L1, m_L2, m_type1, m_type2);

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
        QString("%1 #%2  tangents(%3→%4)  R=%5 m  L1=%6 m[%7]  L2=%8 m[%9]")
            .arg(curveType)
            .arg(idx)
            .arg(m_idx1)
            .arg(m_idx2)
            .arg(m_radius, 0, 'f', 3)
            .arg(m_L1, 0, 'f', 3).arg(spiralTypeName(m_type1))
            .arg(m_L2, 0, 'f', 3).arg(spiralTypeName(m_type2)));

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
    m_type1       = SpiralType::Clothoid;
    m_type2       = SpiralType::Clothoid;
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

    QVariantMap rbProps;
    rbProps["action"]        = "setParams";
    rbProps["mode"]          = "scs";
    rbProps["radius"]        = m_radius;
    rbProps["spiralLength1"] = m_L1;
    rbProps["spiralLength2"] = m_L2;
    rbProps["spiralType1"]   = static_cast<int>(m_type1);
    rbProps["spiralType2"]   = static_cast<int>(m_type2);
    bus->publish("command.update-rubber-band", rbProps);

    QVariantMap rb1;
    rb1["action"] = "clearAndAdd";
    rb1["mode"]   = "scs";
    rb1["point"]  = QVariant::fromValue(QPointF(t1end.x(), t1end.y()));
    bus->publish("command.update-rubber-band", rb1);

    QVariantMap rb2;
    rb2["action"] = "addPoint";
    rb2["point"]  = QVariant::fromValue(QPointF(pi.x(), pi.y()));
    bus->publish("command.update-rubber-band", rb2);

    QVariantMap rb3;
    rb3["action"] = "setCurrentPoint";
    rb3["point"]  = QVariant::fromValue(QPointF(t2start.x(), t2start.y()));
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
        "  3. Enter radius:            R=600  (or just 600)\n"
        "  4. Enter entry spiral len:  L1=150 (or 0 for no entry spiral)\n"
        "  5. Enter entry spiral type: T1=CLOTHOID (default, Enter to skip)\n"
        "       Options: CLOTHOID(C)  HALFSINE(HS)  PARABOLA(P)\n"
        "                CUBICJPN(JPN)  CUBICECI(ECI)\n"
        "  6. Enter exit spiral len:   L2=150 (Enter = same as L1)\n"
        "  7. Enter exit spiral type:  T2=CLOTHOID (default, Enter to skip)\n"
        "       Options: same as T1\n"
        "  8. Press Enter or click to confirm.\n"
        "     At confirm, re-enter any param with R= / L1= / T1= / L2= / T2=\n"
        "  Right-click / ESC to cancel.\n"
        "\n"
        "  Shortcuts:\n"
        "    'L=150'   → sets both L1 and L2 to 150 (symmetric SCS)\n"
        "\n"
        "  Degenerate forms:\n"
        "    L1=L2=0   → Floating Arc (same as AFC)\n"
        "    L1>0,L2=0 → SC curve\n"
        "    L1=0,L2>0 → CS curve\n"
        "    L1=L2>0   → Symmetric SCS\n"
        "\n"
        "  Spiral types (T1= / T2=):\n"
        "    CLOTHOID (C)   — Euler-Cornu, linear curvature [default]\n"
        "    HALFSINE (HS)  — Half-sine (raised cosine curvature)\n"
        "    PARABOLA (P)   — Cubic parabola\n"
        "    CUBICJPN (JPN) — Japanese cubic parabola (JIS E 1301)\n"
        "    CUBICECI (ECI) — CECI cubic parabola";
}

} // namespace command
} // namespace aicad