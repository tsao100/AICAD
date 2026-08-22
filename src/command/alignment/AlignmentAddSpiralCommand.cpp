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
#include "core/Application.h"
#include "core/CommandLineManager.h"
#include "core/EventBus.h"
#include "railway/AlignmentDocument.h"
#include "railway/AlignmentSolver.h"
#include "ui/UIManager.h"
#include "view/CadView.h"

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
//  arcAzimuthAtPC / arcAzimuthAtPT  (file-local helpers)
//
//  參考使用者提供的最新 VBA 螺線反算工具修正說明（螺線反算工具.bas 的
//  C-C 分支第二次修正）：該版明確指出「固定用某個慣例猜正負號（不論是
//  選取順序、或半徑大小）只對某一種轉向正確，鏡射成另一種轉向就會出
//  錯」——因為左彎/右彎是圓心+半徑之外、獨立的第三個自由度，任何「只憑
//  一個點就猜方向」的公式都無法穩定分辨。
//
//  這裡先前 LC/CA/ACA 三處預覽程式碼，各自用 `atan2(-r.y(), r.x())`
//  （只用「圓心→單一端點」這一個半徑向量）反推該點的切線方位角，完全
//  沒有辦法判斷這段圓弧究竟是順時針(右彎)還是逆時針(左彎)——等同於
//  VBA 第一次修正犯的同一種錯誤（隱含假設固定一種轉向），只是這裡連
//  「猜」都沒有明講，直接寫死一種轉向，對另一種轉向的圓弧會算出偏離
//  真實值（typically 相差接近 180°）的方位角，進而讓 solveLC/solveCA/
//  solveACA 內部用這個錯誤方位角判斷出的轉向正負號（signR）也跟著錯。
//
//  正確作法（比照 AlignmentSolver::solve() Pass 1 的 ArcData.azPC 算法，
//  也是 VBA 這次修正的精神——不猜，直接用實際量測到的資料驗證/決定）：
//  必須同時用圓弧的「起點」與「終點」兩個真實已知點，兩者對圓心的半徑
//  向量之外積（cross）才能唯一、無歧異地決定這段圓弧實際是順時針還是
//  逆時針，不管圓弧本身轉向為何都恆成立。
// ────────────────────────────────────────────────────────────────────────────

/// 圓弧起點(PC)的切線方位角；outSign（可選）回傳這段圓弧的實際轉向
/// （+1 = 順時針/右彎，-1 = 逆時針/左彎），供呼叫端需要沿same轉向
/// 繼續推算終點方位角時使用（見 arcAzimuthAtPT()）。
static double arcAzimuthAtPC(QPointF pc, QPointF pt, QPointF center, double* outSign = nullptr)
{
    const QPointF r1 = pc - center;
    const QPointF r2 = pt - center;
    const double  crossVal = r1.x() * r2.y() - r1.y() * r2.x();
    const double  sign     = (crossVal >= 0.0) ? 1.0 : -1.0;
    if (outSign) *outSign = sign;
    return std::atan2(sign * (-r1.y()), sign * r1.x());
}

/// 圓弧終點(PT)的切線方位角：先用 arcAzimuthAtPC() 依實際轉向算出起點
/// 方位角，再加上（帶正確正負號的）實際掃過角度，恆對任一轉向成立。
static double arcAzimuthAtPT(QPointF pc, QPointF pt, QPointF center)
{
    double sign = 1.0;
    const double azPC = arcAzimuthAtPC(pc, pt, center, &sign);
    const QPointF r1 = pc - center;
    const QPointF r2 = pt - center;
    const double crossVal = r1.x() * r2.y() - r1.y() * r2.x();
    const double dotVal   = r1.x() * r2.x() + r1.y() * r2.y();
    const double sweep    = std::abs(std::atan2(crossVal, dotVal));
    return azPC + sign * sweep;
}

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
    if (key == QLatin1String("SINUSOIDAL")       || key == QLatin1String("SIN")) { out = SpiralType::Sinusoidal;       return true; }
    if (key == QLatin1String("COSINE")           || key == QLatin1String("COS")) { out = SpiralType::Cosine;           return true; }
    if (key == QLatin1String("BLOSS")            || key == QLatin1String("BL"))  { out = SpiralType::Bloss;            return true; }
    if (key == QLatin1String("LEMNISCATE")       || key == QLatin1String("LEM")) { out = SpiralType::Lemniscate;       return true; }
    if (key == QLatin1String("WIENERBOGEN")      || key == QLatin1String("WB"))  { out = SpiralType::WienerBogen;      return true; }
    if (key == QLatin1String("RADIOID")          || key == QLatin1String("RAD")) { out = SpiralType::Radioid;          return true; }
    if (key == QLatin1String("ELASRADIOID")      || key == QLatin1String("ERAD")) { out = SpiralType::ElasticRadioid;   return true; }
    if (key == QLatin1String("NORWICHSTURM")     || key == QLatin1String("NWS")) { out = SpiralType::NorwichSturm;     return true; }
    if (key == QLatin1String("PSEUELLRADIOID")   || key == QLatin1String("PER")) { out = SpiralType::PseudoEllipticRadioid; return true; }
    if (key == QLatin1String("LOGARITHMIC")      || key == QLatin1String("LOG")) { out = SpiralType::Logarithmic;      return true; }
    if (key == QLatin1String("HYPERBOLIC")       || key == QLatin1String("HYP")) { out = SpiralType::Hyperbolic;       return true; }
    if (key == QLatin1String("POLYNOMIAL")       || key == QLatin1String("POLY")){ out = SpiralType::Polynomial;       return true; }
    if (key == QLatin1String("QUINTIC")          || key == QLatin1String("QNT")) { out = SpiralType::Quintic;          return true; }
    if (key == QLatin1String("PHQUINTIC")        || key == QLatin1String("PHQ")) { out = SpiralType::PHQuintic;        return true; }
    if (key == QLatin1String("BIQUADRATIC")      || key == QLatin1String("BIQ")) { out = SpiralType::Biquadratic;      return true; }
    if (key == QLatin1String("SPLINE")           || key == QLatin1String("SPL")) { out = SpiralType::Spline;           return true; }
    if (key == QLatin1String("BLOSSEULERHYBRID") || key == QLatin1String("BEH")) { out = SpiralType::BlossEulerHybrid; return true; }
    return false;
}

// ────────────────────────────────────────────────────────────────────────────
//  spiralTypeName  (static)
// ────────────────────────────────────────────────────────────────────────────

QString AlignmentAddSpiralCommand::spiralTypeName(SpiralType t)
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
    default:                           return QStringLiteral("Clothoid");
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  describeAdjacentFloatingSpiral  (static)
//
//  檢查 elemIdx（一個 Fixed Tangent 或 Fixed CircularArc 的 index）是否已經
//  有一段 Floating SpiralIn/SpiralOut 依附在它旁邊（透過該浮動元素的
//  tangentIdxBefore／tangentIdxAfter 指回 elemIdx——這個欄位命名雖然叫
//  「tangentIdx」，但 LC/CA（切線-弧）與 ACA（弧-弧）都共用同一套機制，
//  指向的可能是 Tangent 也可能是 CircularArc 的 index）。
//
//  找到的話回傳一段人類可讀的說明文字（含目前是否已成功求解），供呼叫端
//  在使用者剛點選到這個元素時提示；找不到則回傳空字串。
// ────────────────────────────────────────────────────────────────────────────

QString AlignmentAddSpiralCommand::describeAdjacentFloatingSpiral(
    int                                      elemIdx,
    const railway::HorizontalAlignmentEdit*  edit)
{
    if (!edit || elemIdx < 0) return QString();
    const auto& elems = edit->elements();
    if (elemIdx >= elems.size()) return QString();

    for (int i = 0; i < elems.size(); ++i) {
        const auto& e = elems[i];
        if (e.mode != ConstraintMode::Floating) continue;
        if (e.type != EditableElementType::SpiralIn
            && e.type != EditableElementType::SpiralOut) continue;
        if (e.tangentIdxBefore != elemIdx && e.tangentIdxAfter != elemIdx) continue;

        const QString typeStr = (e.type == EditableElementType::SpiralIn)
                                     ? QStringLiteral("SpiralIn")
                                     : QStringLiteral("SpiralOut");
        return QString("⚠️  Element #%1 already has a Floating %2 (#%3, Ls=%4m) "
                       "attached %5 it, %6.")
            .arg(elemIdx)
            .arg(typeStr)
            .arg(i)
            .arg(e.length, 0, 'f', 3)
            .arg(e.tangentIdxBefore == elemIdx ? "after" : "before")
            .arg(e.solved
                     ? "already solved"
                     : "still UNSOLVED — adding another spiral here will very"
                       " likely conflict with it instead of fixing it; consider"
                       " deleting the existing one first (or checking why it"
                       " failed to solve) before adding a new one");
    }
    return QString();
}

// ────────────────────────────────────────────────────────────────────────────
//  nearestFixedArcIndex  (static)
//
//  找最近 Fixed CircularArc：以弦（PC→PT）中點的距離近似，足夠精確。
// ────────────────────────────────────────────────────────────────────────────

int AlignmentAddSpiralCommand::nearestFixedArcIndex(
    const QPointF&                        clickPt,
    const railway::HorizontalAlignmentEdit* edit)
{
    if (!edit) return -1;
    const auto& elems = edit->elements();

    int    bestIdx  = -1;
    double bestDist = std::numeric_limits<double>::max();

    const double px = clickPt.x();
    const double py = clickPt.y();

    for (int i = 0; i < elems.size(); ++i) {
        const auto& e = elems[i];
        if (e.type != EditableElementType::CircularArc) continue;
        if (e.mode != ConstraintMode::Fixed)            continue;

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
    const QPointF&                        clickPt,
    const railway::HorizontalAlignmentEdit* edit,
    EditableElementType&                    outType)
{
    if (!edit) return -1;
    const auto& elems = edit->elements();

    int    bestIdx  = -1;
    double bestDist = std::numeric_limits<double>::max();
    EditableElementType bestType = EditableElementType::Tangent;

    const double px = clickPt.x();
    const double py = clickPt.y();

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

        const double R = std::abs(arcElem.radius);
        const double azArcEnd = arcAzimuthAtPT(arcElem.startPI, arcElem.endPI, arcElem.arcCenter);

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
            // ── 參考 VBA 螺線反算工具（螺線反算函數.bas）的診斷方式：不是
            // 只回報「找不到解」，而是先算出「圓心到切線的垂距 D」跟「圓
            // 半徑 R」，明確告訴使用者是哪一個幾何條件不成立（D < R 時，
            // 這個切線／圓弧組合在幾何上本來就不存在合理的漸變曲線解，
            // 換句話說就是 VBA 版 SolveSpiral_TC 裡 D < Rmag 的那個檢查）。
            const double tanAz = std::atan2(tanElem.endPI.x() - tanElem.startPI.x(),
                                             tanElem.endPI.y() - tanElem.startPI.y());
            const double sA = std::sin(tanAz), cA = std::cos(tanAz);
            const double D = std::abs((arcElem.arcCenter.x() - tanElem.startPI.x()) * cA
                                     - (arcElem.arcCenter.y() - tanElem.startPI.y()) * sA);
            if (D < R - 1e-6) {
                outputMessage(
                    QString("LC solver: no solution — the arc centre's perpendicular"
                            " distance to the tangent (D = %1 m) is less than the arc"
                            " radius (R = %2 m). This tangent/arc pair cannot be joined"
                            " by any transition curve; pick a different tangent or arc.")
                        .arg(D, 0, 'f', 3)
                        .arg(R, 0, 'f', 3));
            } else {
                outputMessage("LC solver: no solution found with current geometry. "
                              "Check that the tangent direction is compatible with the arc.");
            }
        }

    } else if (m_mode == GroupMode::CA) {
        if (m_arcIdx < 0 || m_tangentIdx < 0
            || m_arcIdx >= n || m_tangentIdx >= n) return;

        const auto& arcElem = elems[m_arcIdx];
        const auto& tanElem = elems[m_tangentIdx];

        const double azArcStart = arcAzimuthAtPC(arcElem.startPI, arcElem.endPI, arcElem.arcCenter);

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
            // 同上（LC 分支）參考 VBA SolveSpiral_TC 的 D < Rmag 診斷方式。
            const double R    = std::abs(arcElem.radius);
            const double tanAz = std::atan2(tanElem.endPI.x() - tanElem.startPI.x(),
                                             tanElem.endPI.y() - tanElem.startPI.y());
            const double sA = std::sin(tanAz), cA = std::cos(tanAz);
            const double D = std::abs((arcElem.arcCenter.x() - tanElem.startPI.x()) * cA
                                     - (arcElem.arcCenter.y() - tanElem.startPI.y()) * sA);
            if (D < R - 1e-6) {
                outputMessage(
                    QString("CA solver: no solution — the arc centre's perpendicular"
                            " distance to the tangent (D = %1 m) is less than the arc"
                            " radius (R = %2 m). This arc/tangent pair cannot be joined"
                            " by any transition curve; pick a different arc or tangent.")
                        .arg(D, 0, 'f', 3)
                        .arg(R, 0, 'f', 3));
            } else {
                outputMessage("CA solver: no solution found with current geometry. "
                              "Check that the tangent direction is compatible with the arc.");
            }
        }

    } else if (m_mode == GroupMode::ACA) {
        if (m_arcIdx < 0 || m_arc2Idx < 0
            || m_arcIdx >= n || m_arc2Idx >= n) return;

        const auto& arc1Elem = elems[m_arcIdx];
        const auto& arc2Elem = elems[m_arc2Idx];
        const double R1 = std::abs(arc1Elem.radius);
        const double R2 = std::abs(arc2Elem.radius);

        const double azArc1Start = arcAzimuthAtPC(arc1Elem.startPI, arc1Elem.endPI, arc1Elem.arcCenter);
        const double azArc2End   = arcAzimuthAtPT(arc2Elem.startPI, arc2Elem.endPI, arc2Elem.arcCenter);

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
            // ── 參考 VBA 螺線反算工具（螺線反算函數.bas 的 C-C／蛋形線分支）
            // 的診斷精神：蛋形線兩端圓心之間的距離只跟 LE、R1、R2 有關，
            // 這裡先算出兩圓心的實際距離，跟「半徑差的絕對值」比較，明確
            // 指出兩弧是否有機會被一段蛋形線銜接，而不是只回報籠統的
            // 「找不到解」。
            const double centreDist = std::hypot(arc2Elem.arcCenter.x() - arc1Elem.arcCenter.x(),
                                                  arc2Elem.arcCenter.y() - arc1Elem.arcCenter.y());
            const double minReach = std::abs(R1 - R2);
            if (centreDist < minReach - 1e-6) {
                outputMessage(
                    QString("ACA solver: no solution — the distance between the two arc"
                            " centres (%1 m) is less than |R₁ − R₂| (%2 m). An"
                            " Egg-Transition curve cannot bridge these two arcs at all;"
                            " pick a different pair of arcs.")
                        .arg(centreDist, 0, 'f', 3)
                        .arg(minReach,   0, 'f', 3));
            } else {
                outputMessage("ACA solver: no solution found with current geometry.\n"
                              "Check that R1 ≠ R2 and the two arcs are geometrically compatible.");
            }
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
    bool hasTangent = false;
    int fixedArcCount = 0;
    for (const auto& e : elems) {
        if (e.type == EditableElementType::Tangent) hasTangent = true;
        if (e.type == EditableElementType::CircularArc
            && e.mode == ConstraintMode::Fixed) { ++fixedArcCount; }
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

    // ── 切換 CadView 進入可取點模式 ──────────────────────────────────────────
    // 比照 EraseCommand 等既有互動式命令的既定作法：CadView 的滑鼠事件處理
    // 有一段「不論目前是什麼模式都會執行」的通用 AIS 選取邏輯（見
    // CadView::mousePressEvent 最後那個獨立的 if 區塊），只有當 mode 落在
    // Sketching / GetPoint / GetGeom 時，滑鼠左鍵才會改成呼叫
    // handlePointInput() 並發布 POINT_ACQUIRED 交給目前作用中的命令。
    //
    // AS 命令先前完全沒有呼叫 setMode()，若使用者是「剛載入檔案、還沒有
    // 進入任何草圖／線形編輯」就直接下 AS 指令，CadView 當時的 mode 仍是
    // 預設的 Idle——結果點擊圓弧/切線時，落入的是那段通用選取邏輯（只會
    // 觸發 geometry.selected／alignment.elementSelected，附加 grips），
    // 完全不會發布 POINT_ACQUIRED，AS 因此永遠收不到使用者點的第一個
    // 點。此修正讓 AS 一開始就明確切到 Sketching 模式（cleanup() 會還原），
    // 不再依賴「使用者剛好因為別的操作而讓 mode 處於正確狀態」這種偶然。
    if (auto* uiMgr = Application::instance()->uiManager()) {
        if (auto* cadView = uiMgr->cadView())
            cadView->setMode(view::InteractionMode::Sketching);
    }

    // ── Subscribe ────────────────────────────────────────────────────────────
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

void AlignmentAddSpiralCommand::handlePointAcquired(const QPointF& point)
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

        const QString existingWarn =
            describeAdjacentFloatingSpiral(idx, m_alignDoc->horizontal());

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
        if (!existingWarn.isEmpty()) outputMessage(existingWarn);
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
            {
                const QString warn = describeAdjacentFloatingSpiral(idx, m_alignDoc->horizontal());
                if (!warn.isEmpty()) outputMessage(warn);
            }

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
            {
                const QString warn = describeAdjacentFloatingSpiral(idx, m_alignDoc->horizontal());
                if (!warn.isEmpty()) outputMessage(warn);
            }
        }

        // Proceed to spiral type selection
        outputMessage(
            QString("Spiral type T= [Clothoid(C) / HalfSine(HS) / Parabola(P) / "
                    "CubicJPN(JPN) / CubicECI(ECI) / Sinusoidal(SIN) / Cosine(COS) / "
                    "Bloss(BL) / Lemniscate(LEM) / WienerBogen(WB) / Radioid(RAD) / "
                    "Logarithmic(LOG) / Hyperbolic(HYP) / Polynomial(POLY) / "
                    "Quintic(QNT) / Biquadratic(BIQ) / Spline(SPL) / "
                    "BlossEulerHybrid(BEH)]  (Enter = Clothoid):"));
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
                            "PARABOLA(P) CUBICJPN(JPN) CUBICECI(ECI) SINUSOIDAL(SIN) "
                            "COSINE(COS) BLOSS(BL) LEMNISCATE(LEM) WIENERBOGEN(WB) "
                            "RADIOID(RAD) ELASRADIOID(ERAD) NORWICHSTURM(NWS) "
                            "PSEUELLRADIOID(PER) LOGARITHMIC(LOG) HYPERBOLIC(HYP) "
                            "POLYNOMIAL(POLY) QUINTIC(QNT) PHQUINTIC(PHQ) "
                            "BIQUADRATIC(BIQ) SPLINE(SPL) BLOSSEULERHYBRID(BEH)").arg(trimmed));
                bus->publish(Events::COMMAND_PROMPT,
                             tr("T= [C/HS/P/JPN/ECI/SIN/COS/BL/LEM/WB/RAD/ERAD/NWS/PER/"
                                "LOG/HYP/POLY/QNT/PHQ/BIQ/SPL/BEH]  (Enter = Clothoid):"));
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

    // 還原 CadView 模式（比照 EraseCommand::cleanup() 的既定作法）。
    if (auto* uiMgr = Application::instance()->uiManager()) {
        if (auto* cadView = uiMgr->cadView())
            cadView->setMode(view::InteractionMode::Sketching);
    }

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
        "    CLOTHOID (C)          — Euler-Cornu, linear curvature [default]\n"
        "    HALFSINE (HS)         — Half-sine curvature profile\n"
        "    PARABOLA (P)          — Cubic parabola\n"
        "    CUBICJPN (JPN)        — Japanese cubic parabola (JIS E 1301)\n"
        "    CUBICECI (ECI)        — CECI cubic parabola\n"
        "    SINUSOIDAL (SIN)      — Sine-ramp curvature profile\n"
        "    COSINE (COS)          — Raised-cosine curvature ramp\n"
        "    BLOSS (BL)            — Bloss cubic (smoothstep) curvature ramp\n"
        "    LEMNISCATE (LEM)      — Lemniscate-style convex curvature ramp\n"
        "    WIENERBOGEN (WB)      — Wiener Bogen (Vienna curve), septic ramp\n"
        "    RADIOID (RAD)         — Radioid concave curvature ramp\n"
        "    ELASRADIOID (ERAD)    — Elastic curve (elastica), kappa(x)=2x/a^2\n"
        "    NORWICHSTURM (NWS)    — Norwich/Sturm spiral, kappa=1/r\n"
        "    PSEUELLRADIOID (PER)  — Pseudo-elliptic radioid, y=a*gd^-1(x/a)\n"
        "    LOGARITHMIC (LOG)     — Logarithmic curvature ramp\n"
        "    HYPERBOLIC (HYP)      — Hyperbolic-tangent curvature ramp\n"
        "    POLYNOMIAL (POLY)     — Plain cubic-power curvature ramp\n"
        "    QUINTIC (QNT)         — Quintic (5th-order) smoothstep ramp\n"
        "    PHQUINTIC (PHQ)       — Pythagorean-Hodograph quintic spiral (Walton-Meek)\n"
        "    BIQUADRATIC (BIQ)     — Quartic-power curvature ramp\n"
        "    SPLINE (SPL)          — Piecewise cubic-Hermite curvature ramp\n"
        "    BLOSSEULERHYBRID (BEH)— 50/50 Bloss / linear (Euler) blend\n"
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
