/**
 * @file AlignmentAddSpiralCommand.cpp
 * @brief ALIGNMENTADDSPIRAL (alias: AS) — 實作。
 *
 * 狀態機轉換摘要
 * ──────────────
 *  execute()
 *    → PickFirst
 *        POINT_ACQUIRED → nearestTangentOrArc()
 *          type==Tangent    → m_mode=LC, m_tangentIdx=idx → PickSecond (選弧)
 *          type==CircularArc→ m_mode=CA, m_arcIdx=idx     → PickSecond (選切線)
 *          找不到元素 → 繼續等待
 *        POINT_CANCELLED → cancel
 *    → PickSecond
 *        LC: POINT_ACQUIRED → nearestFixedArcIndex() → m_arcIdx  → WaitingForType
 *        CA: POINT_ACQUIRED → nearestTangentIndex()  → m_tangentIdx → WaitingForType
 *        POINT_CANCELLED → cancel
 *    → WaitingForType
 *        NUMBER_INPUT "CLOTHOID"/"C"/"HS"/… → m_spiralType → WaitingForConfirm
 *        NUMBER_INPUT ""（Enter）           → m_spiralType 保持 Clothoid → WaitingForConfirm
 *        POINT_CANCELLED → cancel
 *    → WaitingForConfirm
 *        NUMBER_INPUT ""（Enter）→ commitSpiral()
 *        POINT_ACQUIRED          → commitSpiral()
 *        NUMBER_INPUT "T=…"      → 重設類型 → goToConfirm()
 *        POINT_CANCELLED → cancel
 */

#include "command/alignment/AlignmentAddSpiralCommand.h"
#include "command/InputParser.h"
#include "core/Application.h"
#include "core/CommandLineManager.h"
#include "core/EventBus.h"
#include "railway/AlignmentDocument.h"
#include "railway/AlignmentSolver.h"

#include <QDebug>
#include <QLineF>
#include <QtMath>
#include <cmath>
#include <limits>

using namespace aicad::core;
using aicad::railway::SpiralType;
using aicad::railway::EditableElementType;
using aicad::railway::ConstraintMode;

namespace aicad {
namespace command {

// ────────────────────────────────────────────────────────────────────────────
//  Constructor
// ────────────────────────────────────────────────────────────────────────────

AlignmentAddSpiralCommand::AlignmentAddSpiralCommand(QObject* parent)
    : AlignmentCommandBase("alignmentaddspiral",
                           "Insert unknown-length Clothoid between Fixed elements: LC, CA, or ACA",
                           parent)
{}

// ────────────────────────────────────────────────────────────────────────────
//  parseSpiralType  (static)
// ────────────────────────────────────────────────────────────────────────────

bool AlignmentAddSpiralCommand::parseSpiralType(const QString& text, SpiralType& out)
{
    if (text.isEmpty()) return false;

    QString key = text.toUpper();
    // Strip T= or T1= or T2= prefix
    for (const char* prefix : {"T1=", "T2=", "T="})
        if (key.startsWith(QLatin1String(prefix))) { key = key.mid(qstrlen(prefix)); break; }

    if (key == QLatin1String("CLOTHOID") || key == QLatin1String("C"))  { out = SpiralType::Clothoid; return true; }
    if (key == QLatin1String("HALFSINE") || key == QLatin1String("HS")) { out = SpiralType::HalfSine; return true; }
    if (key == QLatin1String("PARABOLA") || key == QLatin1String("P"))  { out = SpiralType::Parabola; return true; }
    if (key == QLatin1String("CUBICJPN") || key == QLatin1String("JPN")){ out = SpiralType::CubicJPN; return true; }
    if (key == QLatin1String("CUBICECI") || key == QLatin1String("ECI")){ out = SpiralType::CubicECI; return true; }
    return false;
}

// ────────────────────────────────────────────────────────────────────────────
//  spiralTypeName  (static)
// ────────────────────────────────────────────────────────────────────────────

QString AlignmentAddSpiralCommand::spiralTypeName(SpiralType t)
{
    switch (t) {
    case SpiralType::HalfSine: return QStringLiteral("HalfSine");
    case SpiralType::Parabola: return QStringLiteral("Parabola");
    case SpiralType::CubicJPN: return QStringLiteral("CubicJPN");
    case SpiralType::CubicECI: return QStringLiteral("CubicECI");
    default:                   return QStringLiteral("Clothoid");
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  nearestFixedArcIndex  (static)
//
//  找最近 Fixed CircularArc：以弦（PC→PT）中點的距離近似，足夠精確。
// ────────────────────────────────────────────────────────────────────────────

int AlignmentAddSpiralCommand::nearestFixedArcIndex(
    const QVector2D&                        clickPt,
    const railway::HorizontalAlignmentEdit* edit)
{
    if (!edit) return -1;
    const auto& elems = edit->elements();

    int    bestIdx  = -1;
    double bestDist = std::numeric_limits<double>::max();

    const double px = static_cast<double>(clickPt.x());
    const double py = static_cast<double>(clickPt.y());

    for (int i = 0; i < elems.size(); ++i) {
        const auto& e = elems[i];
        if (e.type != EditableElementType::CircularArc) continue;
        if (e.mode != ConstraintMode::Fixed)            continue;

        // Approximate distance: point to chord midpoint
        const double mx = (e.startPI.x() + e.endPI.x()) * 0.5;
        const double my = (e.startPI.y() + e.endPI.y()) * 0.5;

        // Also consider distance to chord segment itself
        const double ax = e.startPI.x(), ay = e.startPI.y();
        const double bx = e.endPI.x(),   by = e.endPI.y();
        const double abx = bx - ax, aby = by - ay;
        const double ab2 = abx * abx + aby * aby;

        double dist;
        if (ab2 < 1e-12) {
            dist = std::hypot(px - ax, py - ay);
        } else {
            double t = ((px - ax) * abx + (py - ay) * aby) / ab2;
            t = std::max(0.0, std::min(1.0, t));
            const double cx = ax + t * abx;
            const double cy = ay + t * aby;
            dist = std::hypot(px - cx, py - cy);
        }

        if (dist < bestDist) {
            bestDist = dist;
            bestIdx  = i;
        }
    }
    return bestIdx;
}

// ────────────────────────────────────────────────────────────────────────────
//  nearestTangentOrArc  (static)
//
//  綜合偵測：回傳最近的 Tangent 或 Fixed CircularArc，並輸出其類型。
// ────────────────────────────────────────────────────────────────────────────

int AlignmentAddSpiralCommand::nearestTangentOrArc(
    const QVector2D&                        clickPt,
    const railway::HorizontalAlignmentEdit* edit,
    EditableElementType&                    outType)
{
    if (!edit) return -1;
    const auto& elems = edit->elements();

    int    bestIdx  = -1;
    double bestDist = std::numeric_limits<double>::max();
    EditableElementType bestType = EditableElementType::Tangent;

    const double px = static_cast<double>(clickPt.x());
    const double py = static_cast<double>(clickPt.y());

    for (int i = 0; i < elems.size(); ++i) {
        const auto& e = elems[i];
        bool isTangent = (e.type == EditableElementType::Tangent);
        bool isFixedArc = (e.type == EditableElementType::CircularArc
                           && e.mode == ConstraintMode::Fixed);
        if (!isTangent && !isFixedArc) continue;

        const double ax = e.startPI.x(), ay = e.startPI.y();
        const double bx = e.endPI.x(),   by = e.endPI.y();
        const double abx = bx - ax, aby = by - ay;
        const double ab2 = abx * abx + aby * aby;

        double dist;
        if (ab2 < 1e-12) {
            dist = std::hypot(px - ax, py - ay);
        } else {
            double t = ((px - ax) * abx + (py - ay) * aby) / ab2;
            t = std::max(0.0, std::min(1.0, t));
            dist = std::hypot(px - (ax + t * abx), py - (ay + t * aby));
        }

        if (dist < bestDist) {
            bestDist = dist;
            bestIdx  = i;
            bestType = e.type;
        }
    }

    if (bestIdx >= 0) outType = bestType;
    return bestIdx;
}

// ────────────────────────────────────────────────────────────────────────────
//  highlightElement
// ────────────────────────────────────────────────────────────────────────────

void AlignmentAddSpiralCommand::highlightElement(int elemIdx)
{
    QVariantMap msg;
    msg["elementIndex"] = elemIdx;
    Application::instance()->eventBus()->publish("alignment.highlight-element", msg);
}

// ────────────────────────────────────────────────────────────────────────────
//  showSolverPreview
//
//  呼叫 AlignmentSolver::solveLC / solveCA 進行試算，並將結果輸出到命令列
//  讓使用者在確認前查看求解的 Ls。不修改 document。
// ────────────────────────────────────────────────────────────────────────────

void AlignmentAddSpiralCommand::showSolverPreview()
{
    if (!m_alignDoc) return;
    const auto& elems = m_alignDoc->horizontal()->elements();
    const int n = elems.size();

    if (m_mode == GroupMode::LC) {
        if (m_tangentIdx < 0 || m_arcIdx < 0
            || m_tangentIdx >= n || m_arcIdx >= n) return;

        const auto& tanElem = elems[m_tangentIdx];
        const auto& arcElem = elems[m_arcIdx];

        // Reconstruct arcAzEnd from arcCenter / PC / PT geometry
        const QPointF r1 = arcElem.startPI - arcElem.arcCenter;
        const QPointF r2 = arcElem.endPI   - arcElem.arcCenter;
        const double crossV = r1.x() * r2.y() - r1.y() * r2.x();
        const double dotV   = r1.x() * r2.x() + r1.y() * r2.y();
        const double arcAngle = std::abs(std::atan2(crossV, dotV));
        const double R = std::abs(arcElem.radius);
        // Rough azimuth at arc end (used only for display)
        const double azPC    = std::atan2(-r1.y(), r1.x()); // approximate
        const double azArcEnd = azPC + (crossV >= 0.0 ? arcAngle : -arcAngle);

        const railway::SolvedLC lc = railway::AlignmentSolver::solveLC(
            arcElem.arcCenter, R,
            arcElem.endPI, azArcEnd,
            tanElem.startPI, tanElem.endPI,
            m_spiralType);

        if (lc.valid) {
            outputMessage(
                QString("LC preview:  Ls = %1 m  [%2]\n"
                        "  TS  = (%3, %4)\n"
                        "  SC  = (%5, %6)  (new arc PC)\n"
                        "  Trimmed arc length = %7 m\n"
                        "Press Enter or click to confirm, or T=<type> to change spiral family:")
                    .arg(lc.Ls,           0, 'f', 3)
                    .arg(spiralTypeName(m_spiralType))
                    .arg(lc.tsPoint.x(),  0, 'f', 3)
                    .arg(lc.tsPoint.y(),  0, 'f', 3)
                    .arg(lc.scPoint.x(),  0, 'f', 3)
                    .arg(lc.scPoint.y(),  0, 'f', 3)
                    .arg(lc.arcLen,       0, 'f', 3));
        } else {
            outputMessage("LC solver: no solution found with current geometry. "
                          "Check that the tangent direction is compatible with the arc.");
        }

    } else if (m_mode == GroupMode::CA) {
        if (m_arcIdx < 0 || m_tangentIdx < 0
            || m_arcIdx >= n || m_tangentIdx >= n) return;

        const auto& arcElem = elems[m_arcIdx];
        const auto& tanElem = elems[m_tangentIdx];

        const QPointF r1 = arcElem.startPI - arcElem.arcCenter;
        const double crossV = r1.x() * (arcElem.endPI.y() - arcElem.arcCenter.y())
                              - r1.y() * (arcElem.endPI.x() - arcElem.arcCenter.x());
        const double azArcStart = std::atan2(-r1.y(), r1.x()); // approximate

        const railway::SolvedCA ca = railway::AlignmentSolver::solveCA(
            arcElem.arcCenter, std::abs(arcElem.radius),
            arcElem.startPI, azArcStart,
            tanElem.startPI, tanElem.endPI,
            m_spiralType);

        if (ca.valid) {
            outputMessage(
                QString("CA preview:  Ls = %1 m  [%2]\n"
                        "  CS  = (%3, %4)  (new arc PT)\n"
                        "  ST  = (%5, %6)\n"
                        "  Trimmed arc length = %7 m\n"
                        "Press Enter or click to confirm, or T=<type> to change spiral family:")
                    .arg(ca.Ls,           0, 'f', 3)
                    .arg(spiralTypeName(m_spiralType))
                    .arg(ca.csPoint.x(),  0, 'f', 3)
                    .arg(ca.csPoint.y(),  0, 'f', 3)
                    .arg(ca.stPoint.x(),  0, 'f', 3)
                    .arg(ca.stPoint.y(),  0, 'f', 3)
                    .arg(ca.arcLen,       0, 'f', 3));
        } else {
            outputMessage("CA solver: no solution found with current geometry. "
                          "Check that the tangent direction is compatible with the arc.");
        }

    } else if (m_mode == GroupMode::ACA) {
        if (m_arcIdx < 0 || m_arc2Idx < 0
            || m_arcIdx >= n || m_arc2Idx >= n) return;

        const auto& arc1Elem = elems[m_arcIdx];
        const auto& arc2Elem = elems[m_arc2Idx];
        const double R1 = std::abs(arc1Elem.radius);
        const double R2 = std::abs(arc2Elem.radius);

        // Approximate azimuths from geometry stored in EditableElement
        const QPointF r1s = arc1Elem.startPI - arc1Elem.arcCenter;
        const QPointF r1e = arc1Elem.endPI   - arc1Elem.arcCenter;
        const double cross1 = r1s.x() * r1e.y() - r1s.y() * r1e.x();
        const double phi1   = std::abs(std::atan2(std::abs(cross1),
                                                   r1s.x()*r1e.x()+r1s.y()*r1e.y()));
        const double azArc1Start = std::atan2(-r1s.y(), r1s.x());
        const double azArc1End   = azArc1Start + (cross1 >= 0.0 ? phi1 : -phi1);

        const QPointF r2s = arc2Elem.startPI - arc2Elem.arcCenter;
        const QPointF r2e = arc2Elem.endPI   - arc2Elem.arcCenter;
        const double cross2 = r2s.x() * r2e.y() - r2s.y() * r2e.x();
        const double phi2   = std::abs(std::atan2(std::abs(cross2),
                                                   r2s.x()*r2e.x()+r2s.y()*r2e.y()));
        const double azArc2End = std::atan2(-r2s.y(), r2s.x())
                               + (cross2 >= 0.0 ? phi2 : -phi2);

        const railway::SolvedACA aca = railway::AlignmentSolver::solveACA(
            arc1Elem.arcCenter, R1,
            arc1Elem.startPI,   azArc1Start,
            arc2Elem.arcCenter, R2,
            arc2Elem.endPI,     azArc2End,
            m_spiralType);

        if (aca.valid) {
            const double Req = (R1 * R2) / std::abs(R1 - R2);
            outputMessage(
                QString("ACA preview:  Ls = %1 m  [%2]  Req = %3 m\n"
                        "  Arc₁ trimmed length = %4 m\n"
                        "  SC₁ = (%5, %6)\n"
                        "  SC₂ = (%7, %8)\n"
                        "  Arc₂ trimmed length = %9 m\n"
                        "Press Enter or click to confirm, or T=<type> to change spiral family:")
                    .arg(aca.Ls,           0, 'f', 3)
                    .arg(spiralTypeName(m_spiralType))
                    .arg(Req,              0, 'f', 1)
                    .arg(aca.arc1Len,      0, 'f', 3)
                    .arg(aca.sc1Point.x(), 0, 'f', 3)
                    .arg(aca.sc1Point.y(), 0, 'f', 3)
                    .arg(aca.sc2Point.x(), 0, 'f', 3)
                    .arg(aca.sc2Point.y(), 0, 'f', 3)
                    .arg(aca.arc2Len,      0, 'f', 3));
        } else {
            outputMessage("ACA solver: no solution found with current geometry.\n"
                          "Check that R1 ≠ R2 and the two arcs are geometrically compatible.");
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  goToConfirm
// ────────────────────────────────────────────────────────────────────────────

void AlignmentAddSpiralCommand::goToConfirm()
{
    EventBus* bus = Application::instance()->eventBus();
    showSolverPreview();
    m_step = Step::WaitingForConfirm;

    QString modeLabel;
    switch (m_mode) {
    case GroupMode::LC:  modeLabel = QStringLiteral("LC");  break;
    case GroupMode::CA:  modeLabel = QStringLiteral("CA");  break;
    case GroupMode::ACA: modeLabel = QStringLiteral("ACA"); break;
    default:             modeLabel = QStringLiteral("?");   break;
    }

    bus->publish(Events::COMMAND_PROMPT,
                 tr("%1 spiral [%2] — Enter to confirm, T=<type> to change:")
                     .arg(modeLabel)
                     .arg(spiralTypeName(m_spiralType)));
    CommandLineManager::instance()->waitForInput(core::InputType::Number);
}

// ────────────────────────────────────────────────────────────────────────────
//  execute
// ────────────────────────────────────────────────────────────────────────────

CommandResult AlignmentAddSpiralCommand::execute(const CommandContext& context)
{
    m_alignDoc = context.alignmentDoc;
    if (!m_alignDoc) {
        return CommandResult::Failure(
            "No AlignmentDocument — open or create an alignment first.");
    }

    // Need at least one Fixed Tangent and one Fixed CircularArc  (LC/CA),
    // OR at least two Fixed CircularArcs with different radii (ACA).
    const auto& elems = m_alignDoc->horizontal()->elements();
    bool hasTangent = false, hasArc = false;
    int fixedArcCount = 0;
    for (const auto& e : elems) {
        if (e.type == EditableElementType::Tangent) hasTangent = true;
        if (e.type == EditableElementType::CircularArc
            && e.mode == ConstraintMode::Fixed) { hasArc = true; ++fixedArcCount; }
    }
    if (!hasTangent && fixedArcCount < 2) {
        return CommandResult::Failure(
            "AS requires either:\n"
            "  • a Fixed Tangent + Fixed Arc  (LC or CA), or\n"
            "  • two Fixed CircularArcs with different radii (ACA).\n"
            "Add these elements first (FT / FC).");
    }

    // ── Reset state ──────────────────────────────────────────────────────────
    m_step        = Step::PickFirst;
    m_mode        = GroupMode::Unknown;
    m_isFinishing = false;
    m_tangentIdx  = -1;
    m_arcIdx      = -1;
    m_arc2Idx     = -1;
    m_spiralType  = SpiralType::Clothoid;

    EventBus* bus = Application::instance()->eventBus();

    // ── Subscribe ────────────────────────────────────────────────────────────
    bus->subscribe(Events::POINT_ACQUIRED, this,
                   [this](const QVariant& data) {
                       QVariantMap map = data.toMap();
                       QVector2D pt   = map["point"].value<QVector2D>();
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
                 tr("AS — Click a Fixed Tangent (LC/CA) or Fixed Arc (CA/ACA):"));
    outputMessage(
        "AS — Insert Clothoid between Fixed elements.\n"
        "  Click a FIXED TANGENT → LC (Line→Clothoid→Arc), or\n"
        "  Click a FIXED ARC     → CA (Arc→Clothoid→Line)  — then click a tangent, or\n"
        "                          ACA (Arc→Clothoid→Arc)  — then click a second arc.");
    return CommandResult::Success("Waiting for input");
}

// ────────────────────────────────────────────────────────────────────────────
//  handlePointAcquired
// ────────────────────────────────────────────────────────────────────────────

void AlignmentAddSpiralCommand::handlePointAcquired(const QVector2D& point)
{
    if (m_isFinishing) return;
    EventBus* bus = Application::instance()->eventBus();

    switch (m_step) {

        // ── PickFirst：自動偵測切線或弧 ──────────────────────────────────────────
    case Step::PickFirst: {
        EditableElementType detectedType = EditableElementType::Tangent;
        int idx = nearestTangentOrArc(point, m_alignDoc->horizontal(), detectedType);
        if (idx < 0) {
            outputMessage("No Fixed Tangent or Fixed Arc found near that point — click closer.");
            bus->publish(Events::COMMAND_PROMPT,
                         tr("Click a Fixed Tangent (LC) or Fixed Arc (CA/ACA):"));
            return;
        }

        highlightElement(idx);

        if (detectedType == EditableElementType::Tangent) {
            // LC mode: first element is the tangent
            m_mode       = GroupMode::LC;
            m_tangentIdx = idx;
            outputMessage(QString("LC mode: Fixed Tangent #%1 selected.  "
                                  "Now click the Fixed Arc to connect to:").arg(idx));
            bus->publish(Events::COMMAND_PROMPT, tr("LC: Click the Fixed CircularArc:"));
        } else {
            // CA or ACA: first element is an arc — defer mode decision to PickSecond
            m_mode   = GroupMode::Unknown;   // resolved in PickSecond
            m_arcIdx = idx;
            outputMessage(QString("Fixed Arc #%1 selected.\n"
                                  "  Click a FIXED TANGENT → CA (Arc→Clothoid→Line)\n"
                                  "  Click a FIXED ARC     → ACA (Arc→Clothoid→Arc)").arg(idx));
            bus->publish(Events::COMMAND_PROMPT,
                         tr("CA/ACA: Click a Fixed Tangent or a second Fixed Arc:"));
        }
        m_step = Step::PickSecond;
        break;
    }

        // ── PickSecond ────────────────────────────────────────────────────────────
    case Step::PickSecond: {
        if (m_mode == GroupMode::LC) {
            // Expect a Fixed Arc
            int idx = nearestFixedArcIndex(point, m_alignDoc->horizontal());
            if (idx < 0) {
                outputMessage("No Fixed CircularArc found near that point — click closer to an arc.");
                bus->publish(Events::COMMAND_PROMPT, tr("LC: Click the Fixed CircularArc:"));
                return;
            }
            if (idx == m_arcIdx) {
                outputMessage("Please select a different arc.");
                return;
            }
            m_arcIdx = idx;
            highlightElement(idx);
            outputMessage(QString("Fixed Arc #%1 selected.").arg(idx));

        } else {
            // First element was an arc (m_arcIdx is set); resolve CA vs ACA now.
            // Try to detect what the user clicked: Tangent → CA, Arc → ACA.
            EditableElementType detectedType = EditableElementType::Tangent;
            int idx = nearestTangentOrArc(point, m_alignDoc->horizontal(), detectedType);

            if (idx < 0) {
                outputMessage("Nothing found near that point — click closer to a tangent or arc.");
                bus->publish(Events::COMMAND_PROMPT,
                             tr("CA/ACA: Click a Fixed Tangent or a second Fixed Arc:"));
                return;
            }
            if (idx == m_arcIdx) {
                outputMessage("Please select a different element.");
                return;
            }

            if (detectedType == EditableElementType::Tangent) {
                // CA mode
                m_mode       = GroupMode::CA;
                m_tangentIdx = idx;
                highlightElement(idx);
                outputMessage(QString("CA mode: Fixed Tangent #%1 selected.").arg(idx));
            } else {
                // ACA mode: second element is another Fixed Arc
                m_mode    = GroupMode::ACA;
                m_arc2Idx = idx;
                highlightElement(idx);
                outputMessage(QString("ACA mode: Fixed Arc₂ #%1 selected.  "
                                      "Arc₁=#%2  Arc₂=#%3").arg(idx).arg(m_arcIdx).arg(idx));
            }
        }

        // Proceed to spiral type selection
        outputMessage(
            QString("Spiral type T= [Clothoid(C) / HalfSine(HS) / Parabola(P) / "
                    "CubicJPN(JPN) / CubicECI(ECI)]  (Enter = Clothoid):"));
        bus->publish(Events::COMMAND_PROMPT,
                     tr("Spiral type T= (Enter = Clothoid):"));
        m_step = Step::WaitingForType;
        CommandLineManager::instance()->waitForInput(core::InputType::Number);
        break;
    }

        // ── WaitingForConfirm：點擊確認 ───────────────────────────────────────────
    case Step::WaitingForConfirm:
        commitSpiral();
        break;

    default:
        break;
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  handleNumberInput
// ────────────────────────────────────────────────────────────────────────────

void AlignmentAddSpiralCommand::handleNumberInput(const QString& text)
{
    if (m_isFinishing) return;
    EventBus* bus = Application::instance()->eventBus();
    const QString trimmed = text.trimmed();

    // ── WaitingForType ────────────────────────────────────────────────────────
    if (m_step == Step::WaitingForType) {
        if (!trimmed.isEmpty()) {
            SpiralType t = m_spiralType;
            if (parseSpiralType(trimmed, t)) {
                m_spiralType = t;
                outputMessage(QString("Spiral type set to %1.").arg(spiralTypeName(m_spiralType)));
            } else {
                outputMessage(
                    QString("Unknown type '%1'. Valid: CLOTHOID(C) HALFSINE(HS) "
                            "PARABOLA(P) CUBICJPN(JPN) CUBICECI(ECI)").arg(trimmed));
                bus->publish(Events::COMMAND_PROMPT,
                             tr("T= [C / HS / P / JPN / ECI]  (Enter = Clothoid):"));
                CommandLineManager::instance()->waitForInput(core::InputType::Number);
                return;
            }
        }
        // Empty Enter → keep Clothoid default; proceed to confirm
        goToConfirm();
        return;
    }

    // ── WaitingForConfirm ─────────────────────────────────────────────────────
    if (m_step == Step::WaitingForConfirm) {
        if (trimmed.isEmpty()) {
            commitSpiral();
            return;
        }

        // Re-enter spiral type
        SpiralType t = m_spiralType;
        if (parseSpiralType(trimmed, t)) {
            m_spiralType = t;
            outputMessage(QString("Spiral type updated to %1.").arg(spiralTypeName(m_spiralType)));
            goToConfirm();
            return;
        }

        // Any other non-empty input → treat as confirm
        commitSpiral();
        return;
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  commitSpiral
// ────────────────────────────────────────────────────────────────────────────

void AlignmentAddSpiralCommand::commitSpiral()
{
    if (m_isFinishing) return;

    // Validate indices per mode
    bool valid = false;
    switch (m_mode) {
    case GroupMode::LC:  valid = (m_tangentIdx >= 0 && m_arcIdx  >= 0); break;
    case GroupMode::CA:  valid = (m_arcIdx     >= 0 && m_tangentIdx >= 0); break;
    case GroupMode::ACA: valid = (m_arcIdx     >= 0 && m_arc2Idx >= 0); break;
    default: break;
    }
    if (!valid) {
        outputMessage("Internal error: elements not fully selected.");
        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Failure("Incomplete selection"));
        return;
    }

    int idx = -1;
    QString modeStr;

    if (m_mode == GroupMode::LC) {
        idx     = m_alignDoc->horizontal()->addLC(m_tangentIdx, m_arcIdx, m_spiralType);
        modeStr = QStringLiteral("LC");
    } else if (m_mode == GroupMode::CA) {
        idx     = m_alignDoc->horizontal()->addCA(m_arcIdx, m_tangentIdx, m_spiralType);
        modeStr = QStringLiteral("CA");
    } else {
        idx     = m_alignDoc->horizontal()->addACA(m_arcIdx, m_arc2Idx, m_spiralType);
        modeStr = QStringLiteral("ACA");
    }

    if (idx < 0) {
        outputMessage(
            QString("%1: add%1 failed — check that the geometry is compatible.\n"
                    "  ACA: R1 ≠ R2 required; arcs must be geometrically reachable.")
                .arg(modeStr));
        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Failure(modeStr + " returned -1"));
        return;
    }

    // Solve and refresh
    m_alignDoc->horizontal()->solve();

    // Report solved Ls from the newly created spiral element
    const auto& elems = m_alignDoc->horizontal()->elements();
    const double Ls = (idx >= 0 && idx < elems.size()) ? elems[idx].length : 0.0;

    if (m_mode == GroupMode::ACA) {
        outputMessage(
            QString("ACA spiral #%1 added  [%2]  Ls = %3 m\n"
                    "  Arc₁ #%4  →  Clothoid  →  Arc₂ #%5")
                .arg(idx)
                .arg(spiralTypeName(m_spiralType))
                .arg(Ls, 0, 'f', 3)
                .arg(m_arcIdx)
                .arg(m_arc2Idx));
    } else {
        outputMessage(
            QString("%1 spiral #%2 added  [%3]  Ls = %4 m  "
                    "(tangent #%5  arc #%6)")
                .arg(modeStr)
                .arg(idx)
                .arg(spiralTypeName(m_spiralType))
                .arg(Ls, 0, 'f', 3)
                .arg(m_tangentIdx)
                .arg(m_arcIdx));
    }

    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("AlignmentAddSpiral completed"));
}

// ────────────────────────────────────────────────────────────────────────────
//  handleCancelled
// ────────────────────────────────────────────────────────────────────────────

void AlignmentAddSpiralCommand::handleCancelled()
{
    qDebug() << "[AS] Cancelled";
    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("AlignmentAddSpiral cancelled"));
}

// ────────────────────────────────────────────────────────────────────────────
//  cleanup
// ────────────────────────────────────────────────────────────────────────────

void AlignmentAddSpiralCommand::cleanup()
{
    EventBus* bus = Application::instance()->eventBus();
    bus->unsubscribeAll(this);

    highlightElement(-1);   // clear highlight

    QVariantMap rb;
    rb["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", rb);

    m_step        = Step::PickFirst;
    m_mode        = GroupMode::Unknown;
    m_isFinishing = false;
    m_tangentIdx  = -1;
    m_arcIdx      = -1;
    m_arc2Idx     = -1;
    m_spiralType  = SpiralType::Clothoid;
    m_alignDoc    = nullptr;
}

// ────────────────────────────────────────────────────────────────────────────
//  getUsage
// ────────────────────────────────────────────────────────────────────────────

QString AlignmentAddSpiralCommand::getUsage() const
{
    return
        "Usage: AS  (Add Spiral — LC, CA, or ACA group)\n"
        "\n"
        "  LC mode (Line → Clothoid → Arc):\n"
        "    1. Click a Fixed Tangent (the incoming straight line).\n"
        "    2. Click a Fixed CircularArc (the arc the spiral must join).\n"
        "    3. Enter spiral type T= (or Enter for Clothoid).\n"
        "    4. Enter to confirm.\n"
        "    Result: Clothoid of solved length Ls inserted;\n"
        "            arc's PC trimmed to the SC point.\n"
        "\n"
        "  CA mode (Arc → Clothoid → Line):\n"
        "    1. Click a Fixed CircularArc (the arc the spiral exits).\n"
        "    2. Click a Fixed Tangent (the outgoing straight line).\n"
        "    3–4. Same as LC.\n"
        "    Result: Clothoid inserted; arc's PT trimmed to CS point.\n"
        "\n"
        "  ACA mode (Arc₁ → Clothoid → Arc₂):\n"
        "    1. Click a Fixed CircularArc (Arc₁, the arc before the spiral).\n"
        "    2. Click a second Fixed CircularArc (Arc₂, after the spiral).\n"
        "       Note: auto-detected when the second click hits an arc, not a tangent.\n"
        "    3–4. Same as LC.\n"
        "    Result: Egg-Transition Clothoid inserted between the two arcs;\n"
        "            Arc₁ trimmed at SC₁, Arc₂ trimmed at SC₂.\n"
        "    Constraint: |R₁| ≠ |R₂| required (degenerate otherwise).\n"
        "\n"
        "  Auto-detect flow:\n"
        "    1st click on Tangent → LC mode (arc pick next).\n"
        "    1st click on Arc     → CA/ACA pending.\n"
        "      2nd click on Tangent → CA mode.\n"
        "      2nd click on Arc     → ACA mode.\n"
        "\n"
        "  Spiral types (T=):\n"
        "    CLOTHOID (C)   — Euler-Cornu, linear curvature [default]\n"
        "    HALFSINE (HS)  — Half-sine curvature profile\n"
        "    PARABOLA (P)   — Cubic parabola\n"
        "    CUBICJPN (JPN) — Japanese cubic parabola (JIS E 1301)\n"
        "    CUBICECI (ECI) — CECI cubic parabola\n"
        "\n"
        "  At confirm step, enter T=<type> to change the spiral family.\n"
        "  Right-click / ESC to cancel.\n"
        "\n"
        "  Prerequisites:\n"
        "    FT  — Add Fixed Tangent\n"
        "    FC  — Add Fixed CircularArc (3-point arc)\n";
}

} // namespace command
} // namespace aicad