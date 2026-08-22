/**
 * @file RailwayAlignment.h
 * @brief Railway alignment container classes for AICAD.
 *
 * Depends on RailwayAlignmentElement.h for the element class hierarchy.
 *
 * Class overview
 * ──────────────
 *   AlignmentPoint          – raw keypoint record (from .ALD binary files)
 *   VerticalAlignmentPoint  – raw vertical record  (from .VALD binary files)
 *   HorizontalAlignment     – ordered list of AlignmentElement objects;
 *                             forward (p,w→XY) and inverse (XY→p,w)
 *   VerticalAlignment       – elevation and gradient queries
 *   TrackCenterLine         – combines H+V; primary AICAD entry point
 *
 * Coordinate conventions
 * ──────────────────────
 *   Chainage  p   [m]   – distance along centreline
 *   Offset    w   [m]   – lateral; POSITIVE = LEFT of direction of travel
 *   Easting   x   [m]   – grid X
 *   Northing  y   [m]   – grid Y
 *   Azimuth   a   [rad] – clockwise from North
 *   Radius    R   [m]   – signed; POSITIVE = right-hand curve
 *
 * @author AICAD Team
 * @date   2025-01
 */

#pragma once

#include "RailwayAlignmentElement.h"

#include <QString>
#include <QVector>
#include <QList>
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointF>
#include <QVector3D>
#include <memory>
#include <vector>
#include <limits>
#include <cmath>

namespace aicad {
namespace railway {

/**
 * @brief Standard track gauge [mm] used as the default lever-arm for
 *        cant→rotation-angle conversion (@c cantToAngle).
 *
 * No project-level gauge setting exists yet in this codebase (checked); this
 * constant is the interim source until a project setting / ALD-sourced value
 * replaces it. Track gauge is the distance between the two rail running
 * faces; 1435 mm is UIC standard gauge.
 */
constexpr double kStandardGaugeMM = 1435.0;

/**
 * @brief Convert a superelevation (cant) value to a rotation angle about the
 *        track's tangent direction.
 *
 * @param cantValueMM  Superelevation [mm], as returned by @c getCant(p).
 * @param gaugeWidthMM Track gauge / lever arm [mm]; defaults to
 *                     @c kStandardGaugeMM when not supplied by the caller.
 * @return Rotation angle [rad]. Small-angle geometry: cant is the vertical
 *         rise between the two rails separated by @p gaugeWidthMM, so the
 *         tilt angle is atan2(cant, gauge).
 */
inline double cantToAngle(double cantValueMM, double gaugeWidthMM = kStandardGaugeMM)
{
    return std::atan2(cantValueMM, gaugeWidthMM);
}

// ============================================================================
//  Raw data records
// ============================================================================

/**
 * @brief One keypoint record from a horizontal alignment (.ALD) file.
 *
 * TSC two-character code:
 *   First  char = element type arriving  at this point  (T / C / S)
 *   Second char = element type departing from this point (T / C / S)
 *
 * The element spanning point[i] → point[i+1] is described by the SECOND
 * character of point[i].tsc, plus neighbour context for spiral sub-types.
 */
struct AlignmentPoint
{
    QString plat;
    QString upDown;
    QString tsc;                  ///< e.g. "TC", "CS", "CC"
    double  easting      = 0.0;
    double  northing     = 0.0;
    double  chainage     = 0.0;
    double  contChainage = 0.0;   ///< Continuous/running chainage [m]
    double  azimuth      = 0.0;   ///< Tangent azimuth [rad, CW from N]
    double  length       = 0.0;   ///< Element length [m]
    double  radius       = 0.0;   ///< Signed circular radius (+ right) [m]
    QString curveType;             ///< "SPIRAL"|"HALFSINE"|"PARABOLA"|"CUBICJPN"|"CUBICECI"|"ARC"
                                    ///< |"SINUSOIDAL"|"COSINE"|"BLOSS"|"LEMNISCATE"|"WIENERBOGEN"
                                    ///< |"RADIOID"|"LOGARITHMIC"|"HYPERBOLIC"|"POLYNOMIAL"
                                    ///< |"QUINTIC"|"BIQUADRATIC"|"SPLINE"|"BLOSSEULERHYBRID"
                                    ///< (see AlignmentDocument.h SpiralType; ALD on-disk storage
                                    ///< abbreviates names over 8 chars — see AldFileIO.cpp)
    QString circularCurveNo;
    double  cant         = 0.0;   ///< Superelevation [mm]
    double  gaugeWidening= 0.0;   ///< Gauge widening [mm]
    double  speedLimit   = 0.0;   ///< Design speed [km/h]
    QString text1;
    QString text2;
    double  real1        = 0.0;
    double  real2        = 0.0;

    QJsonObject toJson()              const;
    bool        fromJson(const QJsonObject&);
};

/**
 * @brief 將 @p oldPts 中「非幾何」輔助欄位合併進 @p newPts（依最近里程比對，
 *        容許誤差 @p toleranceM）。
 *
 * 背景：AlignmentSolver／HorizontalAlignmentEdit 內部的元素鏈
 * （TangentElement/CircularArcElement/ClothoidElement/…）只描述幾何，不
 * 認得軌道代碼／上下行別／圓曲線編號／超高／軌距加寬／速限／備註一／
 * 備註二／數值一／數值二這些欄位。每次 solve() 後重新展開的
 * rawPoints()，這些欄位一律是預設值（空字串／0）。凡是要把 solve() 結果
 * 整批寫回 TCL（tcl->loadHorizontal(newPts)）的地方，都必須先呼叫本函式
 * 把舊資料裡的輔助欄位補回 newPts，否則每次幾何編輯都會把它們清空。
 *
 * 純幾何欄位（easting/northing/chainage/contChainage/azimuth/length/radius/
 * curveType/tsc）不受影響，一律保留 @p newPts（即 solver 剛算出的最新值）。
 *
 * @param newPts     Solver 重新展開的關鍵點；就地修改。
 * @param oldPts     合併前 TCL 既有的關鍵點（例如覆寫前的
 *                   tcl->horizontal()->rawPoints()）。
 * @param toleranceM 里程比對容許誤差 [m]；超出容許誤差則該點的輔助欄位維持
 *                   newPts 原值（通常是預設值——例如全新插入的關鍵點，
 *                   舊資料裡本來就沒有對應項）。
 */
void mergeAuxiliaryFields(QVector<AlignmentPoint>& newPts,
                           const QVector<AlignmentPoint>& oldPts,
                           double toleranceM = 1.0);

/**
 * @brief One keypoint record from a vertical alignment (.VALD) file.
 *
 * Inside a parabolic VC the records come in triplets (entry, PVI, exit).
 * On tangent grades all records in the run share the same grade value.
 */
struct VerticalAlignmentPoint
{
    QString plat;
    QString upDown;
    double  chainage      = 0.0;
    double  elevation     = 0.0;
    double  grade         = 0.0;   ///< Tangent slope [%]
    double  kValue        = 0.0;
    double  pviElevation  = 0.0;   ///< PVI elevation [m]
    double  lvc           = 0.0;   ///< Vertical-curve length [m]
    double  mo            = 0.0;   ///< Mid-ordinate

    QJsonObject toJson()              const;
    bool        fromJson(const QJsonObject&);
};

// ============================================================================
//  HorizontalAlignment
// ============================================================================

/**
 * @brief Manages an ordered sequence of AlignmentElement objects.
 *
 * Raw AlignmentPoint records are converted to a typed element list by
 * @c AlignmentElementFactory.  All geometric queries are dispatched
 * polymorphically through the element hierarchy.
 *
 * Forward (p,w → XY)
 * ──────────────────
 *   getXY(p,w)     → QPointF(Easting, Northing)
 *   getAzimuth(p)  → tangent azimuth [rad]
 *
 * Inverse (XY → p,w)
 * ──────────────────
 *   getPW(x,y)     → nearest (p, w)           [single solution]
 *   getAllPW(x,y)  → all solutions, [0]=min|w| [hairpin-safe]
 *
 * Auxiliary
 * ─────────
 *   getRadius, getCant, getGaugeWidening, getTSC
 */
class HorizontalAlignment : public QObject
{
    Q_OBJECT

public:
    explicit HorizontalAlignment(QObject* parent = nullptr);
    ~HorizontalAlignment() override = default;

    // ── Data loading ──────────────────────────────────────────────────────────

    /**
     * @brief Build the element list from raw keypoints.
     *
     * Automatically assigns signed radii to circular elements using the
     * next-keypoint cross-product sign test (mirrors C# ReadALDData).
     */
    void load(const QVector<AlignmentPoint>& points);
    void clear();

    bool isEmpty() const { return m_elements.empty(); }
    int  count()   const { return static_cast<int>(m_elements.size()); }

    const QVector<AlignmentPoint>&       rawPoints() const { return m_pts; }
    const std::vector<const AlignmentElement*> elements()  const;

    /** Index into raw keypoints for chainage p (binary search). */
    int rawIndexAt(double p) const;

    // ── Serialisation ─────────────────────────────────────────────────────────

    QJsonObject toJson()               const;
    bool        fromJson(const QJsonObject&);

    // ── Forward: (p, w) → world ───────────────────────────────────────────────

    QPointF  getXY     (double p, double w = 0.0) const;
    double   getAzimuth(double p)                 const;

    // ── Inverse: world → (p, w) ───────────────────────────────────────────────

    /** Nearest (p, w). w > 0 = left of direction of travel. */
    QPointF        getPW   (double x, double y) const;

    /**
     * All solutions; element [0] = minimum |w|.
     * Use near hairpin bends where multiple perpendiculars exist.
     */
    QList<QPointF> getAllPW(double x, double y) const;

    // ── Auxiliary ─────────────────────────────────────────────────────────────

    /** Signed radius [m]; ±∞ on tangent sections. */
    double   getRadius         (double p, bool signed_ = true) const;

    /** Superelevation [mm]. */
    double   getCant           (double p, bool signed_ = true) const;

    /** Gauge widening [mm]. */
    double   getGaugeWidening  (double p, bool signed_ = true) const;

    /** Horizontal (lateral) distance from centreline for tunnel/structure offset H. */
    double   getAppliedH       (double p) const;

    /** Two-character TSC code of the element at chainage @p p. */
    QString  getTSC            (double p) const;

    /** Continuous chainage at local chainage @p p. */
    double   getContinuousChainage(double p) const;

    /** LWPOLYLINE bulge factor on the circular element at @p p. */
    double   getBulge          (double p, double arcLen) const;

    const AlignmentElement*               elementAt        (double p)            const;

Q_SIGNALS:
    void dataChanged();

private:
    QList<const AlignmentElement*>        candidateElements(double x, double y)  const;

    double  computeRadius          (int rawIdx, double p, bool signed_) const;
    double  interpolateCant        (int rawIdx, double p) const;
    double  interpolateGaugeWidening(int rawIdx, double p) const;
    double  interpolateAppliedH    (int rawIdx, double p) const;
    int     leftRightSign          (int rawIdx) const;

    QVector<AlignmentPoint>                   m_pts;
    std::vector<std::unique_ptr<AlignmentElement>>    m_elements;
    double                                    m_offsetLimit = 500.0;
};

// ============================================================================
//  VerticalAlignment
// ============================================================================

/**
 * @brief Manages vertical alignment keypoints; provides elevation and slope.
 *
 * Inside a parabolic vertical curve the three-record triplet structure is
 * detected by comparing consecutive grade values.  Elevation is then
 * computed from the symmetric-parabola formula (C# SYVL).
 */
class VerticalAlignment : public QObject
{
    Q_OBJECT

public:
    explicit VerticalAlignment(QObject* parent = nullptr);
    ~VerticalAlignment() override = default;

    void load(const QVector<VerticalAlignmentPoint>& pts);
    void initFlat(double startChainage, double endChainage);
    void clear();

    bool isEmpty() const { return m_pts.isEmpty(); }
    int  count()   const { return m_pts.size(); }
    const QVector<VerticalAlignmentPoint>& points() const { return m_pts; }

    QJsonObject toJson()               const;
    bool        fromJson(const QJsonObject&);

    double getElevation(double p) const;  ///< [m]
    double getSlope    (double p) const;  ///< [%]

    /**
     * @brief 豎曲線（拋物線）近似半徑 [m]；直線坡度（無豎曲線）回傳 +∞。
     *
     * 以 K 值定義近似之：R ≈ 100 · K = 100 · Lvc / |Δgrade(%)|。
     * 供 3D Alignment 取樣時，依豎曲線曲率決定適當取樣間距使用。
     */
    double getRadius   (double p) const;

Q_SIGNALS:
    void dataChanged();

private:
    int    indexAt       (double p) const;
    bool   insideVC      (int idx)  const;
    double parabolaElev  (int idx, double p) const;
    double parabolaSlope (int idx, double p) const;
    double tangentElev   (int idx, double p) const;
    double tangentSlope  (int idx)           const;

    QVector<VerticalAlignmentPoint> m_pts;
    mutable int                     m_idx = 0;
};

// ============================================================================
//  TrackCenterLine
// ============================================================================

/**
 * @brief Top-level centreline class: combines H+V, exposes a 3-D API.
 *
 * Quick-start
 * ───────────
 * @code
 *   TrackCenterLine tcl;
 *   tcl.loadHorizontal(hPoints);
 *   tcl.loadVertical  (vPoints);
 *
 *   QVector3D pos = tcl.getXYZ(1234.5, 0.0);
 *   QPointF   pw  = tcl.getPW (452300, 2765000);
 *   double    az  = tcl.getAzimuth(1234.5);   // [rad]
 *   double    sl  = tcl.getSlope  (1234.5);   // [%]
 * @endcode
 *
 * OCCT integration
 * ────────────────
 *   Call @c getOffsetPolyline(p1, p2, w) to obtain a list of
 *   QVector3D(x, y, bulge) vertices.  Straight segments have bulge=0;
 *   circular arc segments carry the non-zero LWPOLYLINE bulge.
 *   Feed these into @c BRepBuilderAPI_MakeEdge / @c MakeWire.
 */
class TrackCenterLine : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString id   READ id                 CONSTANT)

public:
    explicit TrackCenterLine(QObject* parent = nullptr);
    ~TrackCenterLine() override = default;

    QString id()   const { return m_id;   }
    QString name() const { return m_name; }
    void    setName(const QString& n);

    // ── Visibility state (persisted) ──────────────────────────────────────────
    bool hAlignVisible() const { return m_hAlignVisible; }
    void setHAlignVisible(bool v) { m_hAlignVisible = v; Q_EMIT dataChanged(); }

    bool vAlignVisible() const { return m_vAlignVisible; }
    void setVAlignVisible(bool v) { m_vAlignVisible = v; Q_EMIT dataChanged(); }

    HorizontalAlignment* horizontal() const { return m_h; }
    VerticalAlignment*   vertical()   const { return m_v; }

    void loadHorizontal(const QVector<AlignmentPoint>&         pts);
    void loadVertical  (const QVector<VerticalAlignmentPoint>& pts);

    // ── ALD-import preservation ────────────────────────────────────────────
    //
    // horizontal()/vertical() reflect whatever was loaded *most recently* —
    // which, once the user opens the interactive alignment editor (even just
    // to look, via EDITALIGNMENT / the alignment data table), gets
    // overwritten by AlignmentDocument's seedFromRawPoints()+solve()
    // reconstruction of an editable element chain (see UIManager.cpp). That
    // reconstruction is a heuristic reverse-engineering pass and is not
    // guaranteed to reproduce the original ALD file bit-for-bit.
    //
    // setAldHorizontalImport()/setAldVerticalImport() let the ALD import
    // pipeline record the pristine imported keypoints separately; nothing
    // else in the codebase should call them. Consumers that need the most
    // authoritative geometry available (e.g. AlignedProfileArray) should
    // query the "…ForCalc" methods below instead of the plain getters —
    // those prefer the preserved ALD import when present and fall back to
    // horizontal()/vertical() (element-chain-derived) only when this TCL was
    // never ALD-imported.

    bool hasAldHorizontalImport() const;
    bool hasAldVerticalImport()   const;

    void setAldHorizontalImport(const QVector<AlignmentPoint>&         pts);
    void setAldVerticalImport  (const QVector<VerticalAlignmentPoint>& pts);

    QJsonObject toJson()               const;
    bool        fromJson(const QJsonObject&);

    // ── 3-D forward ──────────────────────────────────────────────────────────

    QVector3D getXYZ(double p, double w = 0.0) const;
    QPointF   getXY (double p, double w = 0.0) const;
    double    getZ  (double p)                 const;

    // ── Inverse ───────────────────────────────────────────────────────────────

    QPointF        getPW   (double x, double y) const;
    QList<QPointF> getAllPW(double x, double y) const;

    // ── Differential geometry ─────────────────────────────────────────────────

    double getAzimuth(double p)                      const;
    double getSlope  (double p)                      const;
    double getRadius (double p, bool signed_ = true)  const;

    /** 豎曲線近似半徑 [m]（+∞ = 直線坡度）；供 3D Alignment 取樣使用。 */
    double getVerticalRadius(double p)               const;

    // ── Track attributes ──────────────────────────────────────────────────────

    double getCant         (double p, bool signed_ = true) const;
    double getGaugeWidening(double p, bool signed_ = true) const;

    /** Horizontal (lateral) distance from centreline for tunnel/structure offset H. */
    double getAppliedH     (double p) const;

    // ── "For calculation" queries — prefer ALD import ─────────────────────────
    //
    // Same semantics as the plain getters above, but sourced from the
    // preserved ALD import (see setAldHorizontalImport()/
    // setAldVerticalImport()) when one exists, falling back to
    // horizontal()/vertical() otherwise. Use these for calculations that
    // should stay pinned to the authoritative imported geometry regardless
    // of what the interactive alignment editor currently holds (e.g.
    // AlignedProfileArray).

    QVector3D getXYZForCalc     (double p, double w = 0.0)        const;
    double    getAzimuthForCalc (double p)                        const;
    double    getSlopeForCalc   (double p)                        const;
    double    getCantForCalc    (double p, bool signed_ = true)   const;
    double    getAppliedHForCalc(double p)                        const;

    // ── Offset polyline (for OCCT / DXF output) ───────────────────────────────

    /**
     * @brief 3-D offset polyline encoded as QVector3D(x, y, bulge).
     * @param p1  Start chainage [m]
     * @param p2  End   chainage [m]
     * @param w   Lateral offset [m]; POSITIVE = LEFT
     */
    QList<QVector3D> getOffsetPolyline(double p1, double p2, double w) const;

    // ── Fastener spacing ──────────────────────────────────────────────────────

    struct SpacingRule {
        double radiusMin;  ///< Inclusive lower bound [m]
        double radiusMax;  ///< Exclusive upper bound [m] (use ∞ for tangents)
        double spacing;    ///< Fastener spacing [m]
    };

    void   loadSpacingRules(const QVector<SpacingRule>& rules,
                          double preDistance = 25.0);
    double getSpacing(double p) const;

Q_SIGNALS:
    void nameChanged(const QString&);
    void dataChanged();

private:
    QString              m_id;
    QString              m_name;
    HorizontalAlignment* m_h = nullptr;
    VerticalAlignment*   m_v = nullptr;

    /** Pristine ALD-imported geometry, preserved separately from m_h/m_v
     *  (see setAldHorizontalImport()/setAldVerticalImport()). Empty
     *  (isEmpty()==true) when this TCL was never ALD-imported. */
    HorizontalAlignment* m_hAld = nullptr;
    VerticalAlignment*   m_vAld = nullptr;

    bool                 m_hAlignVisible = false;  ///< 3D view visibility
    bool                 m_vAlignVisible = false;  ///< profile dock visibility

    QVector<SpacingRule> m_spacingRules;
    double               m_preDistance = 25.0;
};

} // namespace railway
} // namespace aicad
