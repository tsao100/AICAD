#pragma once

/**
 * @file AlignmentSolver.h
 * @brief Fixed + Floating Curve geometry solver for HorizontalAlignmentEdit.
 *
 * AlignmentSolver converts a QVector<EditableElement> into a fully solved
 * HorizontalAlignment, handling both Fixed and Floating constraint modes.
 *
 * Pass 1  — Lock all Fixed elements (Tangents already carry their startPI /
 *            endPI; Fixed CircularArcs compute T1/T2 via foot-of-perpendicular).
 *
 * Pass 2  — For every Floating CircularArc, call solveFloatingCurve(), which
 *            applies the standard simple-curve formula (Appendix A):
 *
 *              Δ  = α₂ − α₁
 *              T  = R × tan(|Δ|/2)
 *              PC = PI₁ − T × unit(α₁)      ← back along incoming tangent
 *              PT = PI₂ + T × unit(α₂)      ← forward along outgoing tangent
 *              L  = R × |Δ|
 *
 *            where PI is the intersection of the two tangent lines.
 *
 * Pass 3  — Build AlignmentPoint sequence and call HorizontalAlignment::load().
 *
 * Coordinate conventions (matches the rest of AICAD):
 *   QPointF::x()  = Easting   [m]
 *   QPointF::y()  = Northing  [m]
 *   Azimuth       = CW from North [rad]   (atan2(ΔEast, ΔNorth))
 *
 * @author AICAD Team
 * @date   2026-05
 */

#include "AlignmentDocument.h"
#include "RailwayAlignment.h"
#include "RailwayAlignmentElement.h"

#include <QPointF>
#include <QVector>
#include <memory>

namespace aicad {
namespace railway {

// ============================================================================
//  SolvedCurve  — internal result of solveFloatingCurve()
// ============================================================================

struct SolvedCurve
{
    bool    valid   = false;
    QPointF pc;             ///< Point of Curvature  (arc start = T1 cut point)
    QPointF pt;             ///< Point of Tangency   (arc end   = T2 cut point)
    double  azPC    = 0.0;  ///< Tangent azimuth at PC [rad]
    double  arcLen  = 0.0;  ///< Arc length L = R·|Δ| [m]
    double  delta   = 0.0;  ///< Signed turning angle Δ = α₂−α₁ [rad]
};

// ============================================================================
//  SolvedSCS  — internal result of solveSCS()
// ============================================================================

/**
 * @brief Geometry solution for one SCS group (SpiralIn + Arc + SpiralOut).
 *
 * Coordinate/azimuth conventions follow SolvedCurve above.
 * All three elements share the same PI₁ and PI₂; the arc is bounded by SC/CS
 * cut-points; each spiral runs from a tangent cut-point to an arc cut-point.
 */
struct SolvedSCS
{
    bool    valid    = false;

    // SpiralIn (TS → SC)
    QPointF tsPoint;     ///< Start of entry spiral (on incoming tangent)
    QPointF scPoint;     ///< End of entry spiral = start of circular arc
    double  azTS    = 0; ///< Azimuth at TS (= incoming tangent azimuth)
    double  azSC    = 0; ///< Azimuth at SC

    // Circular arc (SC → CS)
    QPointF csPoint;     ///< End of circular arc = start of exit spiral
    double  azCS    = 0; ///< Azimuth at CS
    double  arcLen  = 0; ///< Arc length between SC and CS [m]

    // SpiralOut (CS → ST)
    QPointF stPoint;     ///< End of exit spiral (on outgoing tangent)
    double  azST    = 0; ///< Azimuth at ST (= outgoing tangent azimuth)

    // Shared
    double  Ls1     = 0; ///< Entry spiral length L1 [m]
    double  Ls2     = 0; ///< Exit  spiral length L2 [m]
    double  R       = 0; ///< Circular radius [m] (absolute)
    double  delta   = 0; ///< Total turning angle Δ [rad]
    double  thetaS1 = 0; ///< Entry spiral angle Θ1 = L1/(2R) [rad]
    double  thetaS2 = 0; ///< Exit  spiral angle Θ2 = L2/(2R) [rad]
};

// ============================================================================
//  SolvedLC  — unknown-length Clothoid between Fixed Tangent → Fixed Arc
// ============================================================================

/**
 * @brief Result of solveLC(): Line → Clothoid → Arc.
 *
 * The spiral's entry tangent lies on the incoming Fixed Tangent (TS slides
 * along the tangent).  The spiral's exit point SC lies on the Fixed Arc and
 * its tangent is tangent to the arc.  The arc is trimmed: its new PC' = scPoint;
 * its original end (PT) is unchanged.
 *
 * Coordinate / azimuth conventions identical to SolvedCurve.
 */
struct SolvedLC
{
    bool    valid    = false;

    // Spiral  TS → SC
    double  Ls       = 0.0;   ///< Solved Clothoid length [m]
    double  R        = 0.0;   ///< Arc radius (absolute) [m]
    double  thetaS   = 0.0;   ///< Spiral angle at SC = Ls/(2R) for Clothoid [rad]
    QPointF tsPoint;           ///< Start of spiral (on incoming tangent)
    QPointF scPoint;           ///< End of spiral = trimmed arc new-PC
    double  azTS     = 0.0;   ///< Azimuth at TS = incoming tangent azimuth [rad]
    double  azSC     = 0.0;   ///< Azimuth at SC = azTS + thetaS [rad]

    // Trimmed arc  SC → original arc-end (PT)
    QPointF arcEndPoint;       ///< Original arc end (PT), unchanged
    double  arcLen   = 0.0;   ///< Trimmed arc length  SC → PT [m]
    double  azArcEnd = 0.0;   ///< Forward azimuth at PT [rad]
};

// ============================================================================
//  SolvedCA  — unknown-length Clothoid between Fixed Arc → Fixed Tangent
// ============================================================================

/**
 * @brief Result of solveCA(): Arc → Clothoid → Line.
 *
 * Mirror image of SolvedLC: the arc is trimmed at its exit end; the spiral
 * runs from the new CS point (on the arc) to ST (on the outgoing Fixed Tangent).
 */
struct SolvedCA
{
    bool    valid    = false;

    // Trimmed arc  original arc-start (PC) → CS
    QPointF arcStartPoint;     ///< Original arc start (PC), unchanged
    QPointF csPoint;           ///< Trimmed arc new-PT = start of spiral
    double  arcLen   = 0.0;   ///< Trimmed arc length  PC → CS [m]
    double  azCS     = 0.0;   ///< Forward azimuth at CS [rad]

    // Spiral  CS → ST
    double  Ls       = 0.0;   ///< Solved Clothoid length [m]
    double  R        = 0.0;   ///< Arc radius (absolute) [m]
    double  thetaS   = 0.0;   ///< Spiral angle at CS [rad]
    QPointF stPoint;           ///< End of spiral (on outgoing tangent)
    double  azST     = 0.0;   ///< Azimuth at ST = outgoing tangent azimuth [rad]
};

// ============================================================================
//  SolvedACA  — unknown-length Clothoid between Fixed Arc → Fixed Arc
// ============================================================================

/**
 * @brief Result of solveACA(): Arc₁ → Clothoid → Arc₂.
 *
 * The Clothoid has curvature κ₁ = 1/R₁ at its entry (SC₁, tangent to Arc₁)
 * and curvature κ₂ = 1/R₂ at its exit (SC₂, tangent to Arc₂).  Because
 * curvature varies linearly with arc length, this is equivalent to a segment
 * of a longer clothoid whose parameter A satisfies  A² = Ls · Req  where the
 * equivalent radius is  Req = |R₁·R₂| / |R₁ − R₂|  (i.e. an EggTransition).
 *
 * Both arcs are trimmed: Arc₁ new-PT = sc1Point; Arc₂ new-PC = sc2Point.
 * The arc lengths stored here are the trimmed lengths.
 *
 * Coordinate / azimuth conventions identical to SolvedLC / SolvedCA.
 */
struct SolvedACA
{
    bool    valid    = false;

    // Trimmed Arc₁  (original arc1-start → SC₁)
    QPointF arc1StartPoint;    ///< Original arc1 start (PC₁), unchanged
    QPointF sc1Point;          ///< Trimmed arc1 new-PT = start of spiral
    double  arc1Len  = 0.0;   ///< Trimmed arc1 length  PC₁ → SC₁ [m]
    double  azSC1    = 0.0;   ///< Forward azimuth at SC₁ [rad]

    // Spiral  SC₁ → SC₂
    double  Ls       = 0.0;   ///< Solved Clothoid length [m]
    double  R1       = 0.0;   ///< Arc₁ radius (absolute) [m]
    double  R2       = 0.0;   ///< Arc₂ radius (absolute) [m]
    double  thetaS   = 0.0;   ///< Total spiral angle = Ls·(1/R₂ − 1/R₁) / 2  [rad]
                               ///<   (accumulated tangent rotation from SC₁ to SC₂)

    // Trimmed Arc₂  (SC₂ → original arc2-end)
    QPointF sc2Point;          ///< Trimmed arc2 new-PC = end of spiral
    QPointF arc2EndPoint;      ///< Original arc2 end (PT₂), unchanged
    double  arc2Len  = 0.0;   ///< Trimmed arc2 length  SC₂ → PT₂ [m]
    double  azSC2    = 0.0;   ///< Forward azimuth at SC₂ [rad]
};

// ============================================================================
//  CompoundChainUnknown  — Phase 4：讓「恰好一個」Rk 或 Lk 交給 solver 反解
//
//  數學上的限制（見與使用者的討論，直接寫在這裡避免日後遺忘）：整個
//  複合鏈結群組的閉合方程式只有 3 條（ΔX/ΔY/Δθ），其中 ΔX/ΔY 這兩條
//  永遠可以透過 TS 沿入切線自由滑動（d5 修正）精確滿足，不消耗任何未
//  知數、也不因為 N 變大而多出可用方程式。真正「有資訊量」的方程式只
//  剩 Δθ 這一條——但前提是所有圓弧心角都是使用者釘死的固定值（見
//  CompoundChainSpec::ArcSeg::centralAngle）；只要有任何一段圓弧心角
//  留給「自動平分剩餘轉角」，Δθ 方程式會自動被那段平分吸收掉、變成
//  恒成立，等於沒有方程式可用。因此：
//    - 最多只能有 1 個 Rk/Lk 是未知數（1 條方程式只能解 1 個未知數）。
//    - 只要指定了未知數，所有圓弧心角都必須釘死（不可再用自動平分）。
//  這兩條規則由 solveCompoundChain() 與 addCompoundChain() 共同驗證。
// ============================================================================

/** 未知數種類：留白的是哪一種量。 */
enum class CompoundChainUnknownKind {
    None,          ///< 沒有未知數（Phase 1-3 的封閉解模式，維持零回歸）
    SpiralLength,  ///< 某一段緩和曲線長度未知（index 對應 spirals[]，0..N）
    ArcRadius      ///< 某一段圓弧半徑未知（index 對應 arcs[]，0..N-1）
};

struct CompoundChainUnknown
{
    CompoundChainUnknownKind kind = CompoundChainUnknownKind::None;
    int index = -1;   ///< SpiralLength: 0..N；ArcRadius: 0..N-1
};

// ============================================================================
//  SolvedCompoundChain  — S0 C0 S1 C1 ... Sn 複合鏈結（N≥2 個圓弧）
//
//  參見 AlignmentDocument.h 的 CompoundChainSpec 註解（求解規則、與計畫
//  文件的落差說明）。座標/角度慣例與 SolvedSCS 相同。
// ============================================================================

/**
 * @brief 複合鏈結中的一個關鍵點（TS / SC_k / CS_k / ... / ST）。
 */
struct CompoundChainNode
{
    QPointF pt;                 ///< 世界座標
    double  az        = 0.0;    ///< 該點切線方位角 [rad]
    bool    isArcStart = false; ///< true = 此點之後緊接圓弧段；false = 緊接緩和曲線段
    double  segLength  = 0.0;   ///< 此點之後那一段（弧或螺旋）的長度 [m]（末端點為 0）
    double  radius     = 0.0;   ///< 此點之後那一段的（絕對）半徑；螺旋段填寫其終端半徑供顯示
};

struct SolvedCompoundChain
{
    bool    valid = false;

    /** 節點序列：TS, SC0, CS0(=S1 起點), SC1, CS1, ..., CS(N-1), ST。共 2N+2 個節點。 */
    QVector<CompoundChainNode> nodes;

    QVector<double> spiralLengths; ///< 已求解的緩和曲線長度，size = arcs.size()+1（含已知與求解值；
                                    ///< 若 unknown.kind==SpiralLength，對應 index 已填入反解結果）
    QVector<SpiralType> spiralTypes; ///< 每段緩和曲線類型，size = arcs.size()+1（供 Pass 3 標示 curveType 用）
    QVector<double> arcRadii;      ///< 已求解的圓弧半徑絕對值，size = arcs.size()（若
                                    ///< unknown.kind==ArcRadius，對應 index 已填入反解結果）
    QVector<double> arcAngles;     ///< 每段圓弧的弧心角（有號，[rad]），size = arcs.size()
    QVector<double> arcLengths;    ///< 每段圓弧弧長 [m]，size = arcs.size()

    int    iterations   = 0;       ///< 保留供未來擴充（例如疊加 Fixed 圓弧需要求根）；目前恆為 0，
                                    ///< 因為 solveCompoundChain 是封閉解，不需要迭代（見下方 solveCompoundChain 說明）。
    double residualNorm = 0.0;     ///< 保留供未來擴充；目前恆 ≈ 0（僅浮點誤差量級）。
};

// ============================================================================
//  SolvedSSJunction  — internal result of AlignmentSolver::solveSSJunction()
// ============================================================================

/**
 * @brief SS 交會點（兩段緩和曲線直接相接，中間無圓弧）兩側 SCS 群組的
 *        共用虛擬切線方位角求解結果。
 *
 * @see AlignmentSolver::solveSSJunction() 完整問題描述與演算法。
 */
struct SolvedSSJunction
{
    bool       converged  = false; ///< 是否已收斂到 |gap| < tol
    double     azimuth    = 0.0;   ///< 收斂後的虛擬切線方位角 [rad]
    double     gap        = 0.0;   ///< 收斂後的殘留間距（沿切線方向，帶號）[m]
    int        iterations = 0;     ///< 實際疊代次數
    SolvedSCS  group1;             ///< 以收斂方位角重解的 group1（進入 SS 的 SCS）
    SolvedSCS  group2;             ///< 以收斂方位角重解的 group2（離開 SS 的 SCS）
};

// ============================================================================
//  AlignmentSolver
// ============================================================================

class AlignmentSolver
{
public:
    AlignmentSolver() = default;

    /**
     * @brief Solve the alignment and return a fully constructed
     *        HorizontalAlignment.
     *
     * @param elems  Element list from HorizontalAlignmentEdit (may contain
     *               Fixed and Floating entries in any order).
     * @return       Newly allocated HorizontalAlignment (never nullptr).
     */
    std::unique_ptr<HorizontalAlignment>
    solve(const QVector<EditableElement>& elems);

    // ── Public so unit tests can call it directly ────────────────────────────

    /**
     * @brief Compute PC / PT for a Floating CircularArc given its two bounding
     *        tangent elements.  Implements Appendix A formula.
     *
     * @param cur   The Floating CircularArc element (provides radius).
     * @param prev  Preceding Fixed Tangent (possibly with trimmed endpoints).
     * @param next  Following Fixed Tangent (possibly with trimmed endpoints).
     * @param tanStartPrev  Effective start of incoming tangent (may differ from
     *                      prev.startPI after earlier trim operations).
     * @param tanEndPrev    Effective end   of incoming tangent.
     * @param tanStartNext  Effective start of outgoing tangent.
     * @param tanEndNext    Effective end   of outgoing tangent.
     * @return SolvedCurve with valid==true on success.
     */
    static SolvedCurve solveFloatingCurve(
        const EditableElement& cur,
        const QPointF& tanStartPrev, const QPointF& tanEndPrev,
        const QPointF& tanStartNext, const QPointF& tanEndNext);

    /**
     * @brief Compute full SCS geometry using Appendix B formulae (asymmetric).
     *
     * Xm / Ym / thetaS for each spiral are derived by calling the appropriate
     * TransitionElement subclass's localFrame(Ls) rather than using the
     * Clothoid-only Fresnel approximation, so all five spiral families
     * (Clothoid, HalfSine, Parabola, CubicJPN, CubicECI) are handled exactly.
     *
     * @param radius         Circular radius R [m] (absolute).
     * @param spiralLength1  Entry spiral length L1 [m].
     * @param spiralLength2  Exit  spiral length L2 [m].
     * @param type1          Entry spiral family (default: Clothoid).
     * @param type2          Exit  spiral family (default: Clothoid).
     * @param tanStartPrev   Effective start of incoming tangent.
     * @param tanEndPrev     Effective end   of incoming tangent.
     * @param tanStartNext   Effective start of outgoing tangent.
     * @param tanEndNext     Effective end   of outgoing tangent.
     * @return SolvedSCS with valid==true on success.
     */
    static SolvedSCS solveSCS(
        double radius, double spiralLength1, double spiralLength2,
        SpiralType type1, SpiralType type2,
        const QPointF& tanStartPrev, const QPointF& tanEndPrev,
        const QPointF& tanStartNext, const QPointF& tanEndNext);

    /**
     * @brief Solve for an unknown-length Clothoid between a Fixed Tangent
     *        (Line) and a Fixed CircularArc  —  LC group.
     *
     * @par Problem statement
     * Given:
     *   - Fixed Tangent with direction az1 = azimuthOf(tanStart, tanEnd).
     *   - Fixed CircularArc with centre @p arcCenter, absolute radius @p arcRadius,
     *     and fixed far-end @p arcEnd (PT stays where the user placed it).
     *
     * Find: Clothoid length Ls such that
     *   (a) TS lies on the incoming tangent line,
     *   (b) SC lies exactly on the circular arc  (|SC − arcCenter| = R), and
     *   (c) the spiral's tangent at SC is tangent to the arc.
     *
     * @par Algorithm  (cross-track gap f(Ls) = 0)
     * For a given Ls, the spiral end-point SC relative to TS is:
     *   @code
     *   azSC  = az1 + thetaS(Ls)
     *   scDx  = Xm·sin(az1) + Ym·cos(az1)
     *   scDy  = Xm·cos(az1) − Ym·sin(az1)
     *   @endcode
     * Condition (c) pins SC to the specific arc point whose tangent is azSC:
     *   @code
     *   SC_on_arc = arcCenter + R·(−signR·cos(azSC),  signR·sin(azSC))
     *   @endcode
     * Condition (a) means TS slides freely along az1; its along-tangent
     * position cancels out in the cross-track gap:
     *   @code
     *   f(Ls) = (tanEnd + (scDx,scDy) − SC_on_arc) · n1   [n1 = right-perp of az1]
     *   @endcode
     * A bisection solver on f(Ls) finds Ls to 0.1 mm accuracy.
     *
     * After solving, TS = SC_on_arc − (scDx, scDy); the arc is trimmed
     * to SC → arcEnd.
     *
     * @param arcCenter    Centre of the Fixed CircularArc.
     * @param arcRadius    Absolute radius [m].
     * @param arcEnd       Unchanged far-end (PT) of the Fixed Arc.
     * @param arcAzEnd     Forward azimuth at PT [rad].
     * @param tanStart     Start of the Fixed Tangent (far end from the arc).
     * @param tanEnd       End of the Fixed Tangent (close end, near the arc).
     * @param spiralType   Clothoid family (default: Clothoid).
     * @return SolvedLC with valid==true on success.
     */
    static SolvedLC solveLC(
        QPointF arcCenter, double arcRadius,
        QPointF arcEnd,    double arcAzEnd,
        const QPointF& tanStart, const QPointF& tanEnd,
        SpiralType spiralType = SpiralType::Clothoid);

    /**
     * @brief Solve for an unknown-length Clothoid between a Fixed CircularArc
     *        and a Fixed Tangent (Line)  —  CA group.
     *
     * Mirror image of solveLC: the spiral runs CT (curvature decreasing) from
     * CS (on the arc) to ST (on the outgoing Fixed Tangent).
     *
     * @par Gap function
     *   @code
     *   azCS = az2 − thetaS(Ls)
     *   CS_on_arc = arcCenter + R·(−signR·cos(azCS),  signR·sin(azCS))
     *   f(Ls) = (tanStart − (stDx,stDy) − CS_on_arc) · n2   [n2 = right-perp of az2]
     *   @endcode
     *
     * @param arcCenter    Centre of the Fixed CircularArc.
     * @param arcRadius    Absolute radius [m].
     * @param arcStart     Unchanged near-start (PC) of the Fixed Arc.
     * @param arcAzStart   Forward azimuth at PC [rad].
     * @param tanStart     Start of the Fixed Tangent (close end, near the arc).
     * @param tanEnd       End of the Fixed Tangent (far end from the arc).
     * @param spiralType   Clothoid family (default: Clothoid).
     * @return SolvedCA with valid==true on success.
     */
    static SolvedCA solveCA(
        QPointF arcCenter, double arcRadius,
        QPointF arcStart,  double arcAzStart,
        const QPointF& tanStart, const QPointF& tanEnd,
        SpiralType spiralType = SpiralType::Clothoid);

    /**
     * @brief Solve for an unknown-length Clothoid between two Fixed CircularArcs
     *        —  ACA group  (Arc₁ → Clothoid → Arc₂).
     *
     * @par Problem statement
     * Given:
     *   - Fixed Arc₁: centre @p arc1Center, absolute radius @p arc1Radius,
     *     fixed start @p arc1Start (PC₁, unchanged), start azimuth @p arc1AzStart.
     *   - Fixed Arc₂: centre @p arc2Center, absolute radius @p arc2Radius,
     *     fixed end   @p arc2End   (PT₂, unchanged), end azimuth @p arc2AzEnd.
     *
     * Find: Clothoid length Ls such that
     *   (a) SC₁ lies on Arc₁ and the spiral's entry tangent is tangent to Arc₁;
     *   (b) SC₂ lies on Arc₂ and the spiral's exit  tangent is tangent to Arc₂;
     *   (c) The spiral has curvature 1/R₁ at SC₁ and 1/R₂ at SC₂ (linear κ).
     *
     * @par Algorithm  (two-point cross-track gap  f(Ls) = 0)
     * The spiral is modelled as an EggTransition (bi-quadratic clothoid segment)
     * with equivalent full-spiral length  Ls_eq = Ls · max(R₁,R₂)/|R₁ − R₂|.
     *
     * For a given trial Ls, the EggTransitionElement provides SC₁ and SC₂
     * positions in local frame.  The gap function measures the cross-track
     * error of SC₂ relative to its required position on Arc₂ (tangency condition).
     * Bisection drives f(Ls) to zero.
     *
     * @param arc1Center   Centre of Fixed Arc₁.
     * @param arc1Radius   Absolute radius of Arc₁ [m].
     * @param arc1Start    Unchanged start (PC₁) of Fixed Arc₁.
     * @param arc1AzStart  Forward azimuth at PC₁ [rad].
     * @param arc2Center   Centre of Fixed Arc₂.
     * @param arc2Radius   Absolute radius of Arc₂ [m].
     * @param arc2End      Unchanged end (PT₂) of Fixed Arc₂.
     * @param arc2AzEnd    Forward azimuth at PT₂ [rad].
     * @param spiralType   Clothoid family (default: Clothoid).
     * @return SolvedACA with valid==true on success.
     */
    static SolvedACA solveACA(
        QPointF arc1Center, double arc1Radius,
        QPointF arc1Start,  double arc1AzStart,
        QPointF arc2Center, double arc2Radius,
        QPointF arc2End,    double arc2AzEnd,
        SpiralType spiralType = SpiralType::Clothoid);

    /**
     * @brief 求解 S0 C0 S1 C1 ... Sn 複合緩和曲線鏈結（N≥2 個圓弧）。
     *
     * 對應 SCS複合線形求解昇級計畫.md Phase 2～4。有兩種模式：
     *
     *  【封閉解模式】unknown.kind == None（預設，與最初交付版本相同）
     *   ── solveSCS 的直接推廣，見 AlignmentDocument.h CompoundChainSpec：
     *   所有緩和曲線長度都由呼叫端直接給定，圓弧心角依 givenArcAngles 決
     *   定（留空的用剩餘轉角平分）。不需要迭代。
     *
     *  【反解模式】unknown.kind != None（Phase 4）
     *   ── 恰好一個 Rk 或 Lk 交給 solver 用二分法反解，前提是
     *   givenArcAngles 必須「全部」給定（size==N 且每個都非 0）：此時
     *   角度閉合不再是恒成立的（見 CompoundChainUnknown 的說明），二分
     *   法驅動的殘差就是「Δ − Σ緩和曲線轉角 − Σ圓弧心角」。找到根之後，
     *   對應的 spiralLengths[unknown.index] 或 arcRadii[unknown.index]
     *   會被反解結果覆寫（原始輸入值僅作為佔位，不影響結果）。
     *
     * @param arcRadii      每段圓弧的半徑絕對值，size = N（N≥2）。若
     *                      unknown.kind==ArcRadius，對應 index 的值僅為
     *                      佔位，會被反解結果覆寫。
     * @param spiralLengths 每段緩和曲線長度，size = N+1；0 = 省略該段。若
     *                      unknown.kind==SpiralLength，對應 index 的值僅
     *                      為佔位，會被反解結果覆寫。
     * @param spiralTypes   每段緩和曲線類型，size = N+1。
     * @param tanStartPrev/tanEndPrev/tanStartNext/tanEndNext
     *                      同 solveSCS：入/出邊界切線的有效端點。
     * @param givenArcAngles 每段圓弧的固定弧心角絕對值 [rad]，size = N；
     *                      留空（預設）＝全部自動平分（此時 unknown 必須
     *                      是 None，見上）。非空時，0 的項目仍自動平分
     *                      「扣除所有固定角度與所有緩和曲線轉角後」的剩
     *                      餘角度；若 unknown.kind != None，則要求全部
     *                      非 0（不可再混用自動平分）。
     * @param unknown       Phase 4：指定恰好一個 Rk/Lk 交給 solver 反解；
     *                      預設 None（維持封閉解、零回歸）。
     * @return SolvedCompoundChain，valid==true 時 nodes 已含完整節點序列，
     *         arcRadii/spiralLengths 已含反解後的最終值。
     */
    static SolvedCompoundChain solveCompoundChain(
        const QVector<double>&     arcRadii,
        const QVector<double>&     spiralLengths,
        const QVector<SpiralType>& spiralTypes,
        const QPointF& tanStartPrev, const QPointF& tanEndPrev,
        const QPointF& tanStartNext, const QPointF& tanEndNext,
        const QVector<double>&     givenArcAngles = QVector<double>(),
        const CompoundChainUnknown& unknown = CompoundChainUnknown());
    /**
     * @brief 消去 SS 交會點兩側 SCS 群組間的殘留短切線（"short T"）。
     *
     * @par 問題背景
     * ALD 匯入還原元素鏈時，SS 交會點（兩段緩和曲線直接相接、中間無
     * 圓弧）會產生一條長度僅 kSSEpsilon（約 1 微米）的虛擬 Fixed
     * Tangent，純粹作為 azimuthOf() 的方向載體，供交會點前後兩個獨立
     * 的 SCS 群組（group1 進入 SS、group2 離開 SS）各自依附求解。因為
     * ALD 原始資料（半徑、緩和曲線長度、方位角）各自獨立四捨五入，
     * 兩群組獨立解出的端點通常不會恰好重合，殘留一小段實際不存在的
     * 短直線（"short T"，量級通常數公釐～數公分）。
     *
     * @par 演算法（使用者提出的方法：SS 點固定＋方位角微調求根）
     * 以 SS 點座標 @p ssPoint 為固定不動點，虛擬切線方位角 θ 視為待解
     * 未知數（初始值＝ @p initialAzimuth，通常取 ALD 記錄的方位角）。
     * 因為兩群組的端點依構造恆落在同一條方位角為 θ 的直線上（分別由
     * solveSCS() 的 sliding-fit 機制保證 cross-track = 0），殘餘間距只有
     * 「沿線」一個自由度，故只需一維求根，不需要二維牛頓法：
     *
     *   g(θ) = (ST_group1(θ) − TS_group2(θ)) · (sin θ, cos θ)
     *
     * 用正割法（secant method）疊代到 |g(θ)| < @p tol，或疊代次數超過
     * @p maxIter 而回報未收斂（呼叫端應保留疊代前的結果，不強行採用）。
     *
     * @param ssPoint        SS 交會點座標（固定不動，來自 ALD 原始資料）。
     * @param initialAzimuth 虛擬切線方位角初始猜測 [rad]（通常為 ALD 記錄的方位角）。
     * @param radius1,spiralLen1In,spiralLen1Out,type1In,type1Out
     *                       group1（進入 SS 的 SCS）的半徑／緩和曲線長度／類型。
     * @param tanStartPrev1,tanEndPrev1
     *                       group1 上游（已知、穩定）的 Fixed Tangent。
     * @param radius2,spiralLen2In,spiralLen2Out,type2In,type2Out
     *                       group2（離開 SS 的 SCS）的半徑／緩和曲線長度／類型。
     * @param tanStartNext2,tanEndNext2
     *                       group2 下游（已知、穩定）的 Fixed Tangent。
     * @param tol            收斂容許誤差 [m]（預設 1e-6）。
     * @param maxIter        最大疊代次數（預設 30）。
     * @return SolvedSSJunction，converged==true 時 azimuth/group1/group2 為可用結果。
     */
    static SolvedSSJunction solveSSJunction(
        const QPointF& ssPoint, double initialAzimuth,
        double radius1, double spiralLen1In, double spiralLen1Out,
        SpiralType type1In, SpiralType type1Out,
        const QPointF& tanStartPrev1, const QPointF& tanEndPrev1,
        double radius2, double spiralLen2In, double spiralLen2Out,
        SpiralType type2In, SpiralType type2Out,
        const QPointF& tanStartNext2, const QPointF& tanEndNext2,
        double tol = 1.0e-6, int maxIter = 30);

    // ── Internal helpers ─────────────────────────────────────────────────────

    /** Azimuth of the vector from→to.  CW from North [rad]. */
    static double azimuthOf(QPointF from, QPointF to);

    /**
     * @brief Intersection of two infinite lines in azimuth form.
     *
     * Line A: through pointA in direction azA.
     * Line B: through pointB in direction azB.
     *
     * Returns the midpoint of A and B as a fallback when lines are parallel
     * (|sin(azB − azA)| < 1e-9).
     */
    static QPointF lineIntersect(QPointF pointA, double azA,
                                 QPointF pointB, double azB);

    /**
     * @brief Foot of perpendicular from P onto the line through A→B.
     *        t is unconstrained (perpendicular may lie outside the segment).
     */
    static QPointF footOfPerp(QPointF A, QPointF B, QPointF P);

    /**
     * @brief Normalise angle to (−π, π].
     */
    static double normaliseAngle(double a);
};

} // namespace railway
} // namespace aicad