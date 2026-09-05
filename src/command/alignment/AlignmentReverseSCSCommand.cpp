/**
 * @file AlignmentReverseSCSCommand.cpp
 * @brief ALIGNMENTREVSCS（alias: RSCS）— 實作。
 *
 * 螺旋類型輸入語法與 AlignmentSCSCommand 完全相同（T1=/T2=/TM= 前綴，或
 * 無前綴的純名稱；空 Enter 保留目前值）。
 */

#include "command/alignment/AlignmentReverseSCSCommand.h"
#include "command/InputParser.h"
#include "core/Application.h"
#include "core/CommandLineManager.h"
#include "core/EventBus.h"
#include "railway/AlignmentDocument.h"
#include "ui/AlignmentReverseSCSCalcDialog.h"
#include "view/CadView.h"   // CadView : public QWidget — 供 static_cast(context.cadView) 使用

#include <QDebug>
#include <QtMath>
#include <cmath>

using namespace aicad::core;
using aicad::railway::SpiralType;

namespace aicad {
namespace command {

// ────────────────────────────────────────────────────────────────────────────
//  Constructor
// ────────────────────────────────────────────────────────────────────────────

AlignmentReverseSCSCommand::AlignmentReverseSCSCommand(QObject* parent)
    : AlignmentCommandBase("alignmentrevscs",
                           "Add reverse SCS+SCS curve (left-then-right or "
                           "right-then-left) between two tangents",
                           parent)
{}

// ────────────────────────────────────────────────────────────────────────────
//  parseSpiralType / spiralTypeName  (static — 邏輯與 AlignmentSCSCommand 相同)
// ────────────────────────────────────────────────────────────────────────────

bool AlignmentReverseSCSCommand::parseSpiralType(const QString& text, SpiralType& out)
{
    if (text.isEmpty()) return false;

    QString key = text.toUpper();
    if (key.startsWith(QLatin1String("T1=")) || key.startsWith(QLatin1String("T2="))
        || key.startsWith(QLatin1String("TM=")))
        key = key.mid(3);

    if (key == QLatin1String("CLOTHOID") || key == QLatin1String("C")) { out = SpiralType::Clothoid; return true; }
    if (key == QLatin1String("HALFSINE") || key == QLatin1String("HS")) { out = SpiralType::HalfSine; return true; }
    if (key == QLatin1String("PARABOLA") || key == QLatin1String("P")) { out = SpiralType::Parabola; return true; }
    if (key == QLatin1String("CUBICJPN") || key == QLatin1String("JPN")) { out = SpiralType::CubicJPN; return true; }
    if (key == QLatin1String("CUBICECI") || key == QLatin1String("ECI")) { out = SpiralType::CubicECI; return true; }
    if (key == QLatin1String("SINUSOIDAL") || key == QLatin1String("SIN")) { out = SpiralType::Sinusoidal; return true; }
    if (key == QLatin1String("COSINE") || key == QLatin1String("COS")) { out = SpiralType::Cosine; return true; }
    if (key == QLatin1String("BLOSS") || key == QLatin1String("BL")) { out = SpiralType::Bloss; return true; }
    if (key == QLatin1String("LEMNISCATE") || key == QLatin1String("LEM")) { out = SpiralType::Lemniscate; return true; }
    if (key == QLatin1String("WIENERBOGEN") || key == QLatin1String("WB")) { out = SpiralType::WienerBogen; return true; }
    if (key == QLatin1String("RADIOID") || key == QLatin1String("RAD")) { out = SpiralType::Radioid; return true; }
    if (key == QLatin1String("ELASRADIOID") || key == QLatin1String("ERAD")) { out = SpiralType::ElasticRadioid; return true; }
    if (key == QLatin1String("NORWICHSTURM") || key == QLatin1String("NWS")) { out = SpiralType::NorwichSturm; return true; }
    if (key == QLatin1String("PSEUELLRADIOID") || key == QLatin1String("PER")) { out = SpiralType::PseudoEllipticRadioid; return true; }
    if (key == QLatin1String("LOGARITHMIC") || key == QLatin1String("LOG")) { out = SpiralType::Logarithmic; return true; }
    if (key == QLatin1String("HYPERBOLIC") || key == QLatin1String("HYP")) { out = SpiralType::Hyperbolic; return true; }
    if (key == QLatin1String("POLYNOMIAL") || key == QLatin1String("POLY")) { out = SpiralType::Polynomial; return true; }
    if (key == QLatin1String("QUINTIC") || key == QLatin1String("QNT")) { out = SpiralType::Quintic; return true; }
    if (key == QLatin1String("PHQUINTIC") || key == QLatin1String("PHQ")) { out = SpiralType::PHQuintic; return true; }
    if (key == QLatin1String("BIQUADRATIC") || key == QLatin1String("BIQ")) { out = SpiralType::Biquadratic; return true; }
    if (key == QLatin1String("SPLINE") || key == QLatin1String("SPL")) { out = SpiralType::Spline; return true; }
    if (key == QLatin1String("BLOSSEULERHYBRID") || key == QLatin1String("BEH")) { out = SpiralType::BlossEulerHybrid; return true; }
    return false;
}

QString AlignmentReverseSCSCommand::spiralTypeName(SpiralType t)
{
    switch (t) {
    case SpiralType::HalfSine:         return QStringLiteral("HalfSine");
    case SpiralType::Parabola:         return QStringLiteral("Parabola");
    case SpiralType::CubicJPN:         return QStringLiteral("CubicJPN");
    case SpiralType::CubicECI:         return QStringLiteral("CubicECI");
    case SpiralType::Sinusoidal:       return QStringLiteral("Sinusoidal");
    case SpiralType::Cosine:           return QStringLiteral("Cosine");
    case SpiralType::Bloss:            return QStringLiteral("Bloss");
    case SpiralType::Lemniscate:       return QStringLiteral("Lemniscate");
    case SpiralType::WienerBogen:      return QStringLiteral("WienerBogen");
    case SpiralType::Radioid:          return QStringLiteral("Radioid");
    case SpiralType::ElasticRadioid:   return QStringLiteral("ElasticRadioid");
    case SpiralType::NorwichSturm:     return QStringLiteral("NorwichSturm");
    case SpiralType::PseudoEllipticRadioid: return QStringLiteral("PseudoEllipticRadioid");
    case SpiralType::Logarithmic:      return QStringLiteral("Logarithmic");
    case SpiralType::Hyperbolic:       return QStringLiteral("Hyperbolic");
    case SpiralType::Polynomial:       return QStringLiteral("Polynomial");
    case SpiralType::Quintic:          return QStringLiteral("Quintic");
    case SpiralType::PHQuintic:        return QStringLiteral("PHQuintic");
    case SpiralType::Biquadratic:      return QStringLiteral("Biquadratic");
    case SpiralType::Spline:           return QStringLiteral("Spline");
    case SpiralType::BlossEulerHybrid: return QStringLiteral("BlossEulerHybrid");
    case SpiralType::Clothoid:
    default:                           return QStringLiteral("Clothoid");
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  execute
// ────────────────────────────────────────────────────────────────────────────

CommandResult AlignmentReverseSCSCommand::execute(const CommandContext& context)
{
    m_alignDoc = context.alignmentDoc;
    if (!m_alignDoc) {
        return CommandResult::Failure(
            "No AlignmentDocument — open or create an alignment first.");
    }
    m_parentWidget = static_cast<QWidget*>(context.cadView);   // CadView : public QWidget

    const auto& elems = m_alignDoc->horizontal()->elements();
    int tangentCount = 0;
    for (const auto& e : elems) {
        if (e.type == railway::EditableElementType::Tangent)
            ++tangentCount;
    }
    if (tangentCount < 2) {
        return CommandResult::Failure(
            "At least two Fixed Tangents are required before adding a "
            "reverse SCS+SCS curve. Use FT to add tangents first.");
    }

    m_step        = Step::PickEntryTangent;
    m_isFinishing = false;
    m_idx1        = -1;
    m_idx2        = -1;
    m_radius1     = 0.0;
    m_L1          = 0.0;
    m_radius2     = 0.0;
    m_L2          = 0.0;
    m_type1       = SpiralType::Clothoid;
    m_type2       = SpiralType::Clothoid;
    m_typeM       = SpiralType::Clothoid;

    EventBus* bus = Application::instance()->eventBus();

    // 無即時橡皮筋預覽（見標頭檔說明）。
    QVariantMap viewSetup;
    viewSetup["mode"]           = "sketching";
    viewSetup["rubberBandMode"] = "none";
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
                 tr("RSCS — Select ENTRY tangent (click near a tangent line):"));
    outputMessage("RSCS — Select ENTRY tangent:");
    return CommandResult::Success("Waiting for input");
}

// ────────────────────────────────────────────────────────────────────────────
//  handlePointAcquired
// ────────────────────────────────────────────────────────────────────────────

void AlignmentReverseSCSCommand::handlePointAcquired(const QPointF& point)
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
        outputMessage(QString("Exit tangent #%1 selected.  Opening RSCS trial-calculation dialog...").arg(idx));
        openCalcDialog();
        break;
    }

    case Step::WaitingForConfirm:
        commitRSCS();
        break;

    default:
        break;
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  confirmPrompt
// ────────────────────────────────────────────────────────────────────────────

QString AlignmentReverseSCSCommand::confirmPrompt() const
{
    return tr("R1=%1  L1=%2[%3]  R2=%4  L2=%5[%6]  Mid=%7 — Enter to confirm, "
              "or re-enter R1=/L1=/T1=/R2=/L2=/T2=/TM=:")
        .arg(m_radius1, 0, 'f', 3)
        .arg(m_L1, 0, 'f', 3).arg(spiralTypeName(m_type1))
        .arg(m_radius2, 0, 'f', 3)
        .arg(m_L2, 0, 'f', 3).arg(spiralTypeName(m_type2))
        .arg(spiralTypeName(m_typeM));
}

// ────────────────────────────────────────────────────────────────────────────
//  handleNumberInput
// ────────────────────────────────────────────────────────────────────────────

void AlignmentReverseSCSCommand::handleNumberInput(const QString& text)
{
    if (m_isFinishing) return;

    EventBus* bus = Application::instance()->eventBus();
    const QString trimmed = text.trimmed();
    double val = 0.0;

    // ════════════════════════════════════════════════════════════════════════
    //  WaitingForConfirm：允許返回重設各參數，或空 Enter 確認
    // ════════════════════════════════════════════════════════════════════════
    if (m_step == Step::WaitingForConfirm) {
        if (trimmed.isEmpty()) { commitRSCS(); return; }

        if (InputParser::tryParseKeyValueDouble(trimmed, "R1", val)) {
            if (val <= 0.0) {
                outputMessage("R1 must be > 0.");
            } else {
                m_radius1 = val;
            }
            bus->publish(Events::COMMAND_PROMPT, confirmPrompt());
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        if (InputParser::tryParseKeyValueDouble(trimmed, "R2", val)) {
            if (val <= 0.0) {
                outputMessage("R2 must be > 0.");
            } else {
                m_radius2 = val;
            }
            bus->publish(Events::COMMAND_PROMPT, confirmPrompt());
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        if (InputParser::tryParseKeyValueDouble(trimmed, "L1", val)) {
            m_L1 = (val < 0.0) ? 0.0 : val;
            bus->publish(Events::COMMAND_PROMPT, confirmPrompt());
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        if (InputParser::tryParseKeyValueDouble(trimmed, "L2", val)) {
            m_L2 = (val < 0.0) ? 0.0 : val;
            bus->publish(Events::COMMAND_PROMPT, confirmPrompt());
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        if (trimmed.toUpper().startsWith(QLatin1String("T1="))) {
            SpiralType t = m_type1;
            if (parseSpiralType(trimmed, t)) m_type1 = t;
            else outputMessage(QString("Unknown type '%1'.").arg(trimmed));
            bus->publish(Events::COMMAND_PROMPT, confirmPrompt());
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        if (trimmed.toUpper().startsWith(QLatin1String("T2="))) {
            SpiralType t = m_type2;
            if (parseSpiralType(trimmed, t)) m_type2 = t;
            else outputMessage(QString("Unknown type '%1'.").arg(trimmed));
            bus->publish(Events::COMMAND_PROMPT, confirmPrompt());
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        if (trimmed.toUpper().startsWith(QLatin1String("TM="))) {
            SpiralType t = m_typeM;
            if (parseSpiralType(trimmed, t)) m_typeM = t;
            else outputMessage(QString("Unknown type '%1'.").arg(trimmed));
            bus->publish(Events::COMMAND_PROMPT, confirmPrompt());
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }

        outputMessage(QString("Unrecognised input '%1' — Enter to confirm, "
                              "or R1=/L1=/T1=/R2=/L2=/T2=/TM=.").arg(trimmed));
        bus->publish(Events::COMMAND_PROMPT, confirmPrompt());
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        return;
    }

    // ════════════════════════════════════════════════════════════════════════
    //  一般線性流程
    // ════════════════════════════════════════════════════════════════════════
    switch (m_step) {

    case Step::WaitingForRadius1: {
        bool ok = InputParser::tryParseKeyValueDouble(trimmed, "R1", val)
                  || InputParser::tryParseKeyedOrPlainDouble(trimmed, "R1", val);
        if (!ok || val <= 0.0) {
            outputMessage("Invalid radius — enter R1=<number> (> 0) or just the number.");
            bus->publish(Events::COMMAND_PROMPT, tr("R1=<radius> (e.g. R1=600):"));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        m_radius1 = val;
        outputMessage(QString("R1=%1.  Enter L1=<length> (0 = no entry spiral):").arg(m_radius1, 0, 'f', 3));
        bus->publish(Events::COMMAND_PROMPT, tr("L1=<length> (e.g. L1=150, or 0):"));
        m_step = Step::WaitingForL1;
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        break;
    }

    case Step::WaitingForL1: {
        bool ok = InputParser::tryParseKeyValueDouble(trimmed, "L1", val)
                  || InputParser::tryParseKeyValueDouble(trimmed, "L", val)
                  || InputParser::tryParseKeyedOrPlainDouble(trimmed, "L1", val);
        if (!ok || val < 0.0) {
            outputMessage("Invalid length — enter L1=<number> (>= 0) or just the number.");
            bus->publish(Events::COMMAND_PROMPT, tr("L1=<length> (e.g. L1=150, or 0):"));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        m_L1 = val;
        if (m_L1 > 1e-9) {
            outputMessage(QString("L1=%1.  Enter entry spiral type T1= (Enter = Clothoid):").arg(m_L1, 0, 'f', 3));
            bus->publish(Events::COMMAND_PROMPT, tr("T1=CLOTHOID (default, Enter to skip):"));
            m_step = Step::WaitingForType1;
        } else {
            outputMessage("L1=0 (no entry spiral).  Enter R2=<radius>:");
            bus->publish(Events::COMMAND_PROMPT, tr("R2=<radius> (e.g. R2=600):"));
            m_step = Step::WaitingForRadius2;
        }
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        break;
    }

    case Step::WaitingForType1: {
        SpiralType t = m_type1;
        if (!trimmed.isEmpty()) {
            if (parseSpiralType(trimmed, t)) m_type1 = t;
            else outputMessage(QString("Unknown type '%1' — keeping %2.").arg(trimmed, spiralTypeName(m_type1)));
        }
        outputMessage(QString("Entry spiral type: %1.  Enter R2=<radius>:").arg(spiralTypeName(m_type1)));
        bus->publish(Events::COMMAND_PROMPT, tr("R2=<radius> (e.g. R2=600):"));
        m_step = Step::WaitingForRadius2;
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        break;
    }

    case Step::WaitingForRadius2: {
        bool ok = InputParser::tryParseKeyValueDouble(trimmed, "R2", val)
                  || InputParser::tryParseKeyedOrPlainDouble(trimmed, "R2", val);
        if (!ok || val <= 0.0) {
            outputMessage("Invalid radius — enter R2=<number> (> 0) or just the number.");
            bus->publish(Events::COMMAND_PROMPT, tr("R2=<radius> (e.g. R2=600):"));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        m_radius2 = val;
        outputMessage(QString("R2=%1.  Enter L2=<length> (0 = no exit spiral):").arg(m_radius2, 0, 'f', 3));
        bus->publish(Events::COMMAND_PROMPT, tr("L2=<length> (e.g. L2=150, or 0):"));
        m_step = Step::WaitingForL2;
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        break;
    }

    case Step::WaitingForL2: {
        bool ok = InputParser::tryParseKeyValueDouble(trimmed, "L2", val)
                  || InputParser::tryParseKeyValueDouble(trimmed, "L", val)
                  || InputParser::tryParseKeyedOrPlainDouble(trimmed, "L2", val);
        if (!ok || val < 0.0) {
            outputMessage("Invalid length — enter L2=<number> (>= 0) or just the number.");
            bus->publish(Events::COMMAND_PROMPT, tr("L2=<length> (e.g. L2=150, or 0):"));
            CommandLineManager::instance()->waitForInput(core::InputType::Number);
            return;
        }
        m_L2 = val;
        if (m_L2 > 1e-9) {
            outputMessage(QString("L2=%1.  Enter exit spiral type T2= (Enter = Clothoid):").arg(m_L2, 0, 'f', 3));
            bus->publish(Events::COMMAND_PROMPT, tr("T2=CLOTHOID (default, Enter to skip):"));
            m_step = Step::WaitingForType2;
        } else {
            outputMessage("L2=0 (no exit spiral).  Enter reverse-pair spiral type TM= (Enter = Clothoid):");
            bus->publish(Events::COMMAND_PROMPT, tr("TM=CLOTHOID (default, Enter to skip):"));
            m_step = Step::WaitingForMidType;
        }
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        break;
    }

    case Step::WaitingForType2: {
        SpiralType t = m_type2;
        if (!trimmed.isEmpty()) {
            if (parseSpiralType(trimmed, t)) m_type2 = t;
            else outputMessage(QString("Unknown type '%1' — keeping %2.").arg(trimmed, spiralTypeName(m_type2)));
        }
        outputMessage(QString("Exit spiral type: %1.  Enter reverse-pair spiral type TM= (Enter = Clothoid):")
                          .arg(spiralTypeName(m_type2)));
        bus->publish(Events::COMMAND_PROMPT, tr("TM=CLOTHOID (default, Enter to skip):"));
        m_step = Step::WaitingForMidType;
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        break;
    }

    case Step::WaitingForMidType: {
        SpiralType t = m_typeM;
        if (!trimmed.isEmpty()) {
            if (parseSpiralType(trimmed, t)) m_typeM = t;
            else outputMessage(QString("Unknown type '%1' — keeping %2.").arg(trimmed, spiralTypeName(m_typeM)));
        }
        m_step = Step::WaitingForConfirm;
        outputMessage(confirmPrompt());
        bus->publish(Events::COMMAND_PROMPT, confirmPrompt());
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        break;
    }

    default:
        break;
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  openCalcDialog
// ────────────────────────────────────────────────────────────────────────────

void AlignmentReverseSCSCommand::openCalcDialog()
{
    EventBus* bus = Application::instance()->eventBus();

    auto* dlg = new ui::AlignmentReverseSCSCalcDialog(m_alignDoc, m_idx1, m_idx2, m_parentWidget);
    const int result = dlg->exec();   // Modal — 阻塞直到使用者按「套用」或取消/關閉
    delete dlg;

    if (m_isFinishing) return;   // 對話框開啟期間指令被外部取消（極少見，保險檢查）

    if (result == QDialog::Accepted) {
        // 對話框「套用」已完成 addReverseSCS() + solve()，這裡只需結束指令。
        outputMessage(
            QString("Reverse SCS+SCS added via dialog (tangents %1\xE2\x86\x92%2).")
                .arg(m_idx1).arg(m_idx2));
        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Success("AlignmentReverseSCS completed via dialog"));
        return;
    }

    // 取消/關閉對話框 → 退回文字循序輸入模式（見標頭檔流程說明）。
    outputMessage("Dialog cancelled.  Falling back to text input — enter R1=<radius>"
                  " (or ESC / right-click to cancel the command entirely):");
    bus->publish(Events::COMMAND_PROMPT, tr("R1=<radius> (e.g. R1=600):"));
    m_step = Step::WaitingForRadius1;
    CommandLineManager::instance()->waitForInput(core::InputType::Number);
}

// ────────────────────────────────────────────────────────────────────────────
//  commitRSCS
// ────────────────────────────────────────────────────────────────────────────

void AlignmentReverseSCSCommand::commitRSCS()
{
    railway::ReverseSCSSpec spec;
    spec.tangentIdxBefore = m_idx1;
    spec.tangentIdxAfter  = m_idx2;
    spec.radius1 = m_radius1;
    spec.length1 = m_L1;
    spec.radius2 = m_radius2;
    spec.length2 = m_L2;
    spec.type1  = m_type1;
    spec.type2  = m_type2;
    spec.typeM1 = m_typeM;
    spec.typeM2 = m_typeM;

    int idx = m_alignDoc->horizontal()->addReverseSCS(spec);

    if (idx < 0) {
        outputMessage("Failed to add reverse SCS+SCS curve — check tangent indices and parameters.");
        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Failure("addReverseSCS returned -1"));
        return;
    }

    m_alignDoc->horizontal()->solve();   // emit changed() → AlignmentRenderer::refresh()

    outputMessage(
        QString("Reverse SCS+SCS #%1  tangents(%2→%3)  R1=%4 m  L1=%5 m[%6]  "
                "R2=%7 m  L2=%8 m[%9]  Mid=%10")
            .arg(idx)
            .arg(m_idx1)
            .arg(m_idx2)
            .arg(m_radius1, 0, 'f', 3)
            .arg(m_L1, 0, 'f', 3).arg(spiralTypeName(m_type1))
            .arg(m_radius2, 0, 'f', 3)
            .arg(m_L2, 0, 'f', 3).arg(spiralTypeName(m_type2))
            .arg(spiralTypeName(m_typeM)));

    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("AlignmentReverseSCS completed"));
}

// ────────────────────────────────────────────────────────────────────────────
//  handleCancelled
// ────────────────────────────────────────────────────────────────────────────

void AlignmentReverseSCSCommand::handleCancelled()
{
    qDebug() << "[RSCS] Cancelled";
    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("AlignmentReverseSCS cancelled"));
}

// ────────────────────────────────────────────────────────────────────────────
//  cleanup
// ────────────────────────────────────────────────────────────────────────────

void AlignmentReverseSCSCommand::cleanup()
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
    m_radius1     = 0.0;
    m_L1          = 0.0;
    m_radius2     = 0.0;
    m_L2          = 0.0;
    m_type1       = SpiralType::Clothoid;
    m_type2       = SpiralType::Clothoid;
    m_typeM       = SpiralType::Clothoid;
    m_alignDoc    = nullptr;
}

// ────────────────────────────────────────────────────────────────────────────
//  highlightTangent
// ────────────────────────────────────────────────────────────────────────────

void AlignmentReverseSCSCommand::highlightTangent(int elemIdx)
{
    QVariantMap msg;
    msg["elementIndex"] = elemIdx;
    Application::instance()->eventBus()
        ->publish("alignment.highlight-element", msg);
}

// ────────────────────────────────────────────────────────────────────────────
//  getUsage
// ────────────────────────────────────────────────────────────────────────────

QString AlignmentReverseSCSCommand::getUsage() const
{
    return
        "Usage: RSCS\n"
        "  Insert a reverse SCS+SCS curve (left-then-right or right-then-left,\n"
        "  auto-detected from tangent geometry) between two tangents:\n"
        "    Tangent -> Spiral(L1) -> Arc(R1) -> [reverse pair, auto-solved]\n"
        "            -> Arc(R2) -> Spiral(L2) -> Tangent\n"
        "\n"
        "  1. Click near the ENTRY tangent line.\n"
        "  2. Click near the EXIT tangent line.\n"
        "  3. A trial-calculation dialog opens: enter R1/L1/T1, R2/L2/T2, TM,\n"
        "     click Calculate to preview the node sequence, then Apply to commit.\n"
        "     Cancelling the dialog falls back to text prompts:\n"
        "  4. Enter R1=<radius>\n"
        "  5. Enter L1=<length>  (0 = no entry spiral)\n"
        "  6. Enter T1=<type>    (default Clothoid, Enter to skip; only if L1>0)\n"
        "  7. Enter R2=<radius>\n"
        "  8. Enter L2=<length>  (0 = no exit spiral)\n"
        "  9. Enter T2=<type>    (default Clothoid, Enter to skip; only if L2>0)\n"
        " 10. Enter TM=<type>    (reverse-pair spiral type, default Clothoid)\n"
        " 11. Press Enter or click to confirm.\n"
        "     At confirm, re-enter any param with R1=/L1=/T1=/R2=/L2=/T2=/TM=\n"
        "  Right-click / ESC to cancel.\n"
        "\n"
        "  Note: R1/L1/R2/L2 are all direct inputs (like SCS's R=/L1=/L2=).\n"
        "  Only the reverse pair's spiral lengths are auto-solved (EqualLength).\n"
        "\n"
        "  Spiral types (T1=/T2=/TM=): same set as SCS —\n"
        "    CLOTHOID(C) HALFSINE(HS) PARABOLA(P) CUBICJPN(JPN) CUBICECI(ECI) …";
}

} // namespace command
} // namespace aicad
