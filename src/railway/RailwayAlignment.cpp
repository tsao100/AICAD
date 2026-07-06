/**
 * @file RailwayAlignment.cpp
 * @brief Container class implementations for AICAD railway alignment.
 * @author AICAD Team
 * @date   2025-01
 */

#include "RailwayAlignment.h"

#include <QDebug>
#include <QJsonArray>
#include <QUuid>
#include <cmath>
#include <algorithm>
#include <limits>

namespace aicad {
namespace railway {

static constexpr double kTol = 1e-9;
static constexpr double kInf = std::numeric_limits<double>::infinity();

// 樁號邊界比對容差（用途不同於上面的 kTol，見 AlignmentElement::kChainageTol
// 的說明）。舊系統 ALD 資料的樁號與長度各自獨立以 ASCII 文字四捨五入寫入，
// 相鄰關鍵點或水平/垂直兩檔案各自的終點樁號之間可能有次公釐級落差，遠大於
// kTol (1e-9)；用於 rawIndexAt()/indexAt() 判斷「是否已到達最後一筆記錄」
// 以及可容忍的越界查詢範圍，避免退化為 0 座標/高程。
static constexpr double kChainageTol = 1e-3;

// ============================================================================
//  AlignmentPoint  serialisation
// ============================================================================

QJsonObject AlignmentPoint::toJson() const
{
    QJsonObject o;
    o["plat"]          = plat;
    o["upDown"]        = upDown;
    o["tsc"]           = tsc;
    o["easting"]       = easting;
    o["northing"]      = northing;
    o["chainage"]      = chainage;
    o["contChainage"]  = contChainage;
    o["azimuth"]       = azimuth;
    o["length"]        = length;
    o["radius"]        = radius;
    o["curveType"]     = curveType;
    o["curveNo"]       = circularCurveNo;
    o["cant"]          = cant;
    o["gaugeWidening"] = gaugeWidening;
    o["speedLimit"]    = speedLimit;
    o["text1"]         = text1;
    o["text2"]         = text2;
    o["real1"]         = real1;
    o["real2"]         = real2;
    return o;
}

bool AlignmentPoint::fromJson(const QJsonObject& o)
{
    plat             = o["plat"].toString();
    upDown           = o["upDown"].toString();
    tsc              = o["tsc"].toString();
    easting          = o["easting"].toDouble();
    northing         = o["northing"].toDouble();
    chainage         = o["chainage"].toDouble();
    contChainage     = o["contChainage"].toDouble();
    azimuth          = o["azimuth"].toDouble();
    length           = o["length"].toDouble();
    radius           = o["radius"].toDouble();
    curveType        = o["curveType"].toString();
    circularCurveNo  = o["curveNo"].toString();
    cant             = o["cant"].toDouble();
    gaugeWidening    = o["gaugeWidening"].toDouble();
    speedLimit       = o["speedLimit"].toDouble();
    text1            = o["text1"].toString();
    text2            = o["text2"].toString();
    real1            = o["real1"].toDouble();
    real2            = o["real2"].toDouble();
    return true;
}

// ============================================================================
//  VerticalAlignmentPoint  serialisation
// ============================================================================

QJsonObject VerticalAlignmentPoint::toJson() const
{
    QJsonObject o;
    o["chainage"]     = chainage;
    o["elevation"]    = elevation;
    o["grade"]        = grade;
    o["kValue"]       = kValue;
    o["pviElevation"] = pviElevation;
    o["lvc"]          = lvc;
    o["mo"]           = mo;
    return o;
}

bool VerticalAlignmentPoint::fromJson(const QJsonObject& o)
{
    chainage      = o["chainage"].toDouble();
    elevation     = o["elevation"].toDouble();
    grade         = o["grade"].toDouble();
    kValue        = o["kValue"].toDouble();
    pviElevation  = o["pviElevation"].toDouble();
    lvc           = o["lvc"].toDouble();
    mo            = o["mo"].toDouble();
    return true;
}

// ============================================================================
//  HorizontalAlignment
// ============================================================================

HorizontalAlignment::HorizontalAlignment(QObject* parent)
    : QObject(parent)
{}

// ── Data loading ──────────────────────────────────────────────────────────────

void HorizontalAlignment::load(const QVector<AlignmentPoint>& points)
{
    m_pts = points;
    m_elements.clear();

    if (m_pts.size() < 2) {
        qWarning() << "[HorizontalAlignment] Need at least 2 keypoints";
        Q_EMIT dataChanged();
        return;
    }

    const int n = m_pts.size();

    // ── Step 1: assign signed radii to circular elements ──────────────────
    // Strategy: for each C element, test whether the next keypoint lies to the
    // right of the current tangent → positive radius; left → negative.
    //   (Replicates C# ReadALDData post-processing.)
    for (int i = 0; i < n - 1; ++i) {
        if (m_pts[i].tsc.size() >= 2 && m_pts[i].tsc[1] == 'C') {
            // Project next keypoint onto tangent at current keypoint
            const double az = m_pts[i].azimuth;
            const double dx = m_pts[i+1].easting  - m_pts[i].easting;
            const double dy = m_pts[i+1].northing - m_pts[i].northing;
            // Cross-track (right-positive): dx*cos(az) − dy*sin(az)
            const double crossTrack = dx * std::cos(az) - dy * std::sin(az);
            m_pts[i].radius = std::abs(m_pts[i].radius)
                              * (crossTrack >= 0.0 ? 1.0 : -1.0);
        }
    }

    // ── Step 2: build elements ─────────────────────────────────────────────
    // Provide dummy neighbours for the first and last keypoints.
    AlignmentPoint dummyPrev, dummyNext;
    dummyPrev.tsc = "TT";
    dummyNext.tsc = "TT";

    for (int i = 0; i < n - 1; ++i) {
        const AlignmentPoint& prev = (i > 0)     ? m_pts[i-1] : dummyPrev;
        const AlignmentPoint& cur  =               m_pts[i];
        const AlignmentPoint& next = (i < n-2)   ? m_pts[i+1] : dummyNext;

        // Skip zero-length elements (degenerate keypoints)
        if (cur.length < kTol) continue;

        auto elem = AlignmentElementFactory::create(prev, cur, next);
        if (elem) {
            m_elements.push_back(std::move(elem));
        } else {
            qWarning() << "[HorizontalAlignment] Failed to create element at index" << i
                       << "tsc=" << cur.tsc;
        }
    }

    qDebug() << "[HorizontalAlignment] Loaded" << m_elements.size()
             << "elements from" << m_pts.size() << "keypoints";
    Q_EMIT dataChanged();
}

void HorizontalAlignment::clear()
{
    m_pts.clear();
    m_elements.clear();
    Q_EMIT dataChanged();
}

const std::vector<const AlignmentElement*> HorizontalAlignment::elements() const
{
    std::vector<const AlignmentElement*> result;
    result.reserve(m_elements.size());
    for (const auto& e : m_elements)
        result.push_back(e.get());
    return result;
}

// ── Serialisation ─────────────────────────────────────────────────────────────

QJsonObject HorizontalAlignment::toJson() const
{
    QJsonArray arr;
    for (const AlignmentPoint& pt : m_pts)
        arr.append(pt.toJson());
    QJsonObject o;
    o["rawPoints"] = arr;
    return o;
}

bool HorizontalAlignment::fromJson(const QJsonObject& j)
{
    QVector<AlignmentPoint> pts;
    for (const QJsonValue& v : j["rawPoints"].toArray()) {
        AlignmentPoint pt;
        pt.fromJson(v.toObject());
        pts.append(pt);
    }
    load(pts);
    return true;
}

// ── Internal lookup ───────────────────────────────────────────────────────────

const AlignmentElement* HorizontalAlignment::elementAt(double p) const
{
    for (const auto& e : m_elements)
        if (e->contains(p)) return e.get();

    // 找不到「嚴格包含」p 的元素。多半發生在舊系統 ALD 資料裡樁號與長度
    // 各自獨立四捨五入所造成的次公釐級銜接落差（見 contains() 的
    // kChainageTol 說明），而非真正意義上的越界查詢。
    // 退回距離 p 最近的邊界元素，避免呼叫端（getXY/getAzimuth/getRadius…）
    // 取得退化座標 (0,0)；只有整條線形完全沒有元素時才回傳 nullptr。
    if (m_elements.empty())
        return nullptr;

    const AlignmentElement* nearest = m_elements.begin()->get();
    double bestDist = std::abs(p - nearest->startChainage());
    for (const auto& e : m_elements) {
        const double d = (p < e->startChainage()) ? (e->startChainage() - p)
                        : (p > e->endChainage())   ? (p - e->endChainage())
                                                    : 0.0;
        if (d < bestDist) {
            bestDist = d;
            nearest  = e.get();
        }
    }
    return nearest;
}

int HorizontalAlignment::rawIndexAt(double p) const
{
    // Binary search on raw keypoints
    if (m_pts.isEmpty()) return -1;
    const int n = m_pts.size();

    // 容差內視為抵達終點（含終點稍微越界的查詢，例如取樣迴圈以最後一筆
    // 記錄之樁號當作終止值時，兩份獨立四捨五入的樁號可能有微小落差）。
    if (p >= m_pts[n-1].chainage - kChainageTol) return n - 2;
    if (p <= m_pts[0].chainage + kChainageTol)   return 0;

    int left = 0, right = n - 2;
    while (left <= right) {
        int mid = (left + right) / 2;
        if (m_pts[mid].chainage <= p && p < m_pts[mid+1].chainage)
            return mid;
        (m_pts[mid].chainage < p) ? (left = mid+1) : (right = mid-1);
    }
    return -1;
}

QList<const AlignmentElement*>
HorizontalAlignment::candidateElements(double /*x*/, double /*y*/) const
{
    // For hairpin detection: return all elements whose bounding chainage range
    // could plausibly contain the nearest foot-of-perpendicular.
    // Simple strategy: test ALL elements (adequate for typical alignment lengths).
    QList<const AlignmentElement*> result;
    for (const auto& e : m_elements)
        result.append(e.get());
    return result;
}

// ── Forward ───────────────────────────────────────────────────────────────────

QPointF HorizontalAlignment::getXY(double p, double w) const
{
    const AlignmentElement* e = elementAt(p);
    if (!e) {
        qWarning() << "[HorizontalAlignment] getXY: p=" << p << "out of range";
        return {};
    }
    return e->worldXY(p, w);
}

double HorizontalAlignment::getAzimuth(double p) const
{
    const AlignmentElement* e = elementAt(p);
    return e ? e->worldAzimuth(p) : 0.0;
}

// ── Inverse ───────────────────────────────────────────────────────────────────

QPointF HorizontalAlignment::getPW(double x, double y) const
{
    const auto all = getAllPW(x, y);
    return all.isEmpty() ? QPointF{} : all.first();
}

QList<QPointF> HorizontalAlignment::getAllPW(double x, double y) const
{
    if (m_elements.empty()) return {};

    QList<QPointF> results;
    double         minAbsW = kInf;

    for (const auto& e : m_elements) {
        QPointF pw = e->inversePW(x, y);
        // Accept only solutions inside the element's chainage range
        if (!e->contains(pw.x())) continue;
        // Guard against pathologically large offsets
        if (std::abs(pw.y()) > m_offsetLimit) continue;

        results.append(pw);
        if (std::abs(pw.y()) < minAbsW) {
            minAbsW = std::abs(pw.y());
            // Keep minimum-|w| solution at index 0
            results.swapItemsAt(0, results.size() - 1);
        }
    }

    return results;
}

// ── Auxiliary ─────────────────────────────────────────────────────────────────

int HorizontalAlignment::leftRightSign(int rawIdx) const
{
    // Sign of cross-track component from azimuth change at rawIdx
    if (rawIdx + 1 >= m_pts.size()) return 1;
    const double az1 = m_pts[rawIdx].azimuth;
    const double az2 = m_pts[rawIdx+1].azimuth;
    // Cross product of unit tangent vectors: sin(az2-az1)
    const double crossZ = std::sin(AlignmentElement::normalise(az2 - az1));
    return (crossZ >= 0.0) ? 1 : -1;
}

double HorizontalAlignment::computeRadius(int i, double p, bool signed_) const
{
    if (i < 0 || i >= m_pts.size()) return signed_ ? kInf : kInf;

    const AlignmentPoint& pt = m_pts[i];
    if (pt.tsc.size() < 2) return signed_ ? kInf : kInf;

    const QChar elem = pt.tsc[1];

    double R = kInf;

    if (elem == 'C') {
        R = pt.radius;

    } else if (elem == 'S') {
        // Determine spiral neighbour types.
        // 'S' 鄰居視同 'T'：兩段緩和曲線在 SS 交會點直接相接，等同於在該
        // 點達到零曲率（直線）狀態，行為與真正的切線鄰接相同（見
        // AlignmentElementFactory::createSpiral() 的對應處理與說明）。
        auto asPatternChar = [](QChar c) -> QChar {
            return (c == 'S') ? QChar('T') : c;
        };
        const QChar prev = asPatternChar((i > 0) ? m_pts[i-1].tsc[1] : QChar('T'));
        const QChar next = asPatternChar((i+1 < m_pts.size()) ? m_pts[i+1].tsc[1] : QChar('T'));
        const QString nc = QString(prev) + QString(next);
        const QString& ct = pt.curveType;

        auto spiralR = [&](double exitR, double len, double distFromStart) -> double {
            if (ct == "HALFSINE")
                return 1.0 / ((1.0 / (2.0 * exitR))
                              * (1.0 - std::cos(distFromStart / len * M_PI)));
            // SPIRAL / PARABOLA / default: linear curvature growth
            return len * std::abs(exitR) / distFromStart;
        };

        if (nc == "TC") {
            const double dist = p - pt.chainage;
            R = (dist < kTol) ? kInf : spiralR(m_pts[i+1].radius, pt.length, dist);

        } else if (nc == "CT") {
            const double dist = (i+1 < m_pts.size() ? m_pts[i+1].chainage : pt.chainage + pt.length) - p;
            R = (dist < kTol) ? kInf : spiralR(m_pts[i-1].radius, pt.length, dist);

        } else if (nc == "CC") {
            // Egg: interpolate between R1 and R2 by arc length
            const double R1   = (i > 0)             ? m_pts[i-1].radius : kInf;
            const double R2   = (i+1 < m_pts.size())? m_pts[i+1].radius : kInf;
            const double t    = (p - pt.chainage) / pt.length;
            if (std::abs(R1) > kTol && std::abs(R2) > kTol)
                R = 1.0 / (t / std::abs(R2) + (1.0 - t) / std::abs(R1));
            else
                R = kInf;
        }
    }

    return signed_ ? R : std::abs(R);
}

double HorizontalAlignment::getRadius(double p, bool signed_) const
{
    int i = rawIndexAt(p);
    return computeRadius(i, p, signed_);
}

double HorizontalAlignment::interpolateCant(int i, double p) const
{
    if (i < 0 || i >= m_pts.size()) return 0.0;
    const AlignmentPoint& pt = m_pts[i];
    if (pt.tsc.size() < 2) return 0.0;

    const QChar elem = pt.tsc[1];
    double cant = 0.0;

    if (elem == 'C') {
        cant = pt.cant;

    } else if (elem == 'S') {
        const QString& ct = pt.curveType;
        double cantVal = 0.0;
        double L       = 0.0;

        if (pt.tsc == "CS") {
            cantVal = (i > 0) ? m_pts[i-1].cant : 0.0;
            L       = (i+1 < m_pts.size() ? m_pts[i+1].chainage : pt.chainage + pt.length) - p;
        } else {
            cantVal = (i+1 < m_pts.size()) ? m_pts[i+1].cant : 0.0;
            L       = p - pt.chainage;
        }

        if (ct == "HALFSINE")
            cant = cantVal / 2.0 * (1.0 - std::cos(L / pt.length * M_PI));
        else
            cant = cantVal / pt.length * L;
    }

    return cant;
}

double HorizontalAlignment::getCant(double p, bool signed_) const
{
    int i = rawIndexAt(p);
    double cant = interpolateCant(i, p);
    return signed_ ? cant * leftRightSign(i) : cant;
}

double HorizontalAlignment::interpolateGaugeWidening(int i, double p) const
{
    if (i < 0 || i >= m_pts.size()) return 0.0;
    const AlignmentPoint& pt = m_pts[i];
    if (pt.tsc.size() < 2) return 0.0;

    const QChar elem = pt.tsc[1];
    double gw = 0.0;

    if (elem == 'C') {
        gw = pt.gaugeWidening;

    } else if (elem == 'S') {
        const QString& ct = pt.curveType;
        double gwVal = 0.0;
        double L     = 0.0;

        if (pt.tsc == "CS") {
            gwVal = (i > 0) ? m_pts[i-1].gaugeWidening : 0.0;
            L     = (i+1 < m_pts.size() ? m_pts[i+1].chainage : pt.chainage + pt.length) - p;
        } else {
            gwVal = (i+1 < m_pts.size()) ? m_pts[i+1].gaugeWidening : 0.0;
            L     = p - pt.chainage;
        }

        if (ct == "HALFSINE")
            gw = gwVal / 2.0 * (1.0 - std::cos(L / pt.length * M_PI));
        else
            gw = gwVal / pt.length * L;
    }

    return gw;
}

double HorizontalAlignment::getGaugeWidening(double p, bool signed_) const
{
    int i = rawIndexAt(p);
    double gw = interpolateGaugeWidening(i, p);
    return signed_ ? gw * leftRightSign(i) : gw;
}

QString HorizontalAlignment::getTSC(double p) const
{
    int i = rawIndexAt(p);
    return (i >= 0 && i < m_pts.size()) ? m_pts[i].tsc : QString("??");
}

double HorizontalAlignment::getContinuousChainage(double p) const
{
    if (m_pts.isEmpty()) return p;
    // Increasing cont-chainage direction:
    if (m_pts.size() > 1 && m_pts[0].contChainage < m_pts[1].contChainage)
        return p - m_pts[0].chainage + m_pts[0].contChainage;
    return m_pts[0].contChainage - (p - m_pts[0].chainage);
}

double HorizontalAlignment::getBulge(double p, double arcLen) const
{
    const AlignmentElement* e = elementAt(p);
    if (const auto* arc = dynamic_cast<const CircularArcElement*>(e))
        return arc->bulge(arcLen);
    return 0.0;
}

// ============================================================================
//  VerticalAlignment
// ============================================================================

VerticalAlignment::VerticalAlignment(QObject* parent)
    : QObject(parent)
{}

void VerticalAlignment::load(const QVector<VerticalAlignmentPoint>& pts)
{
    m_pts  = pts;
    m_idx  = 0;
    Q_EMIT dataChanged();
    qDebug() << "[VerticalAlignment] Loaded" << m_pts.size() << "points";
}

void VerticalAlignment::initFlat(double startCh, double endCh)
{
    m_pts.clear();
    VerticalAlignmentPoint p0, p1;
    p0.chainage = startCh; p0.elevation = 0.0; p0.grade = 0.0;
    p1.chainage = endCh;   p1.elevation = 0.0; p1.grade = 0.0;
    m_pts << p0 << p1;
    Q_EMIT dataChanged();
}

void VerticalAlignment::clear()
{
    m_pts.clear();
    m_idx = 0;
    Q_EMIT dataChanged();
}

QJsonObject VerticalAlignment::toJson() const
{
    QJsonArray arr;
    for (const VerticalAlignmentPoint& pt : m_pts)
        arr.append(pt.toJson());
    QJsonObject o;
    o["points"] = arr;
    return o;
}

bool VerticalAlignment::fromJson(const QJsonObject& j)
{
    QVector<VerticalAlignmentPoint> pts;
    for (const QJsonValue& v : j["points"].toArray()) {
        VerticalAlignmentPoint pt;
        pt.fromJson(v.toObject());
        pts.append(pt);
    }
    load(pts);
    return true;
}

// ── Internal helpers ──────────────────────────────────────────────────────────

int VerticalAlignment::indexAt(double p) const
{
    if (m_pts.isEmpty()) return -1;
    const int n = m_pts.size();

    // 容差內視為抵達終點；亦涵蓋查詢樁號略微超出本檔案最後一筆記錄的情況
    // （水平/垂直線形分屬不同檔案，各自獨立四捨五入，端點樁號可能有次
    // 公釐級落差，不應因此讓高程退化為 0）。
    if (p >= m_pts[n-1].chainage - kChainageTol) {
        m_idx = n - 2; return m_idx;
    }
    if (p <= m_pts[0].chainage + kChainageTol) {
        m_idx = 0; return m_idx;
    }

    int left = 0, right = n - 2;
    while (left <= right) {
        int mid = (left + right) / 2;
        if (m_pts[mid].chainage <= p && p < m_pts[mid+1].chainage) {
            // Step back to the entry-tangent record if inside a VC triplet
            if (insideVC(mid) && (mid % 3) == 2) --mid;
            m_idx = mid;
            return mid;
        }
        (m_pts[mid].chainage < p) ? (left = mid+1) : (right = mid-1);
    }
    return -1;
}

bool VerticalAlignment::insideVC(int idx) const
{
    // A VC triplet has grade[idx] != grade[idx+1]
    return (idx + 1 < m_pts.size()) &&
           (m_pts[idx].grade != m_pts[idx+1].grade);
}

double VerticalAlignment::parabolaElev(int idx, double p) const
{
    // Symmetric parabola (C# SYVL)
    //   z = pviElevation − lvc·s1/2 + s1·x − (s1−s2)·x²/(2·lvc)
    // where x = p − entry_chainage, s1 = entry grade, s2 = exit grade [%→fraction]
    const double x  = p - m_pts[idx].chainage;
    const double s1 = m_pts[idx].grade / 100.0;
    const double s2 = (idx+2 < m_pts.size()) ? m_pts[idx+2].grade / 100.0 : s1;
    const double lvc = m_pts[idx+1].lvc;
    return m_pts[idx+1].pviElevation
           - lvc * s1 / 2.0
           + s1 * x
           - (s1 - s2) * x * x / (2.0 * lvc);
}

double VerticalAlignment::parabolaSlope(int idx, double p) const
{
    const double x   = p - m_pts[idx].chainage;
    const double s1  = m_pts[idx].grade;
    const double s2  = (idx+2 < m_pts.size()) ? m_pts[idx+2].grade : s1;
    const double lvc = m_pts[idx+1].lvc;
    return s1 + (s2 - s1) / lvc * x;
}

double VerticalAlignment::tangentElev(int idx, double p) const
{
    return (p - m_pts[idx].chainage) * m_pts[idx].grade / 100.0 + m_pts[idx].elevation;
}

double VerticalAlignment::tangentSlope(int idx) const
{
    return m_pts[idx].grade;
}

double VerticalAlignment::getElevation(double p) const
{
    int idx = indexAt(p);
    if (idx < 0) return 0.0;
    return insideVC(idx) ? parabolaElev(idx, p) : tangentElev(idx, p);
}

double VerticalAlignment::getSlope(double p) const
{
    int idx = indexAt(p);
    if (idx < 0) return 0.0;
    return insideVC(idx) ? parabolaSlope(idx, p) : tangentSlope(idx);
}

double VerticalAlignment::getRadius(double p) const
{
    int idx = indexAt(p);
    if (idx < 0 || !insideVC(idx))
        return std::numeric_limits<double>::infinity();

    const double s1  = m_pts[idx].grade;                                  // [%]
    const double s2  = (idx + 2 < m_pts.size()) ? m_pts[idx + 2].grade : s1;
    const double lvc = m_pts[idx + 1].lvc;
    const double dA  = s2 - s1;                                           // [%]

    if (std::abs(dA) < 1e-9 || lvc < 1e-9)
        return std::numeric_limits<double>::infinity();

    return std::abs(lvc / dA) * 100.0;   // R ≈ 100·K，K = Lvc / |Δgrade(%)|
}

// ============================================================================
//  TrackCenterLine
// ============================================================================

TrackCenterLine::TrackCenterLine(QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_name(QStringLiteral("Track"))
    , m_h(new HorizontalAlignment(this))
    , m_v(new VerticalAlignment(this))
{
    connect(m_h, &HorizontalAlignment::dataChanged, this, &TrackCenterLine::dataChanged);
    connect(m_v, &VerticalAlignment::dataChanged,   this, &TrackCenterLine::dataChanged);
}

void TrackCenterLine::setName(const QString& n)
{
    if (m_name != n) { m_name = n; Q_EMIT nameChanged(m_name); }
}

void TrackCenterLine::loadHorizontal(const QVector<AlignmentPoint>& pts)
{
    m_h->load(pts);
}

void TrackCenterLine::loadVertical(const QVector<VerticalAlignmentPoint>& pts)
{
    m_v->load(pts);
}

QJsonObject TrackCenterLine::toJson() const
{
    QJsonObject o;
    o["id"]             = m_id;
    o["name"]           = m_name;
    o["hAlignVisible"]  = m_hAlignVisible;
    o["vAlignVisible"]  = m_vAlignVisible;
    o["horizontal"] = m_h->toJson();
    o["vertical"]   = m_v->toJson();
    return o;
}

bool TrackCenterLine::fromJson(const QJsonObject& j)
{
    m_id = j["id"].toString(m_id);
    setName(j["name"].toString(m_name));
    m_hAlignVisible = j["hAlignVisible"].toBool(false);
    m_vAlignVisible = j["vAlignVisible"].toBool(false);

    if (!m_h->fromJson(j["horizontal"].toObject()))
        return false;

    m_v->fromJson(j["vertical"].toObject());
    return true;
}

QVector3D TrackCenterLine::getXYZ(double p, double w) const
{
    QPointF xy = m_h->getXY(p, w);
    return { static_cast<float>(xy.x()),
            static_cast<float>(xy.y()),
            static_cast<float>(m_v->getElevation(p)) };
}

QPointF TrackCenterLine::getXY(double p, double w) const
{
    return m_h->getXY(p, w);
}

double TrackCenterLine::getZ(double p) const
{
    return m_v->getElevation(p);
}

QPointF TrackCenterLine::getPW(double x, double y) const
{
    return m_h->getPW(x, y);
}

QList<QPointF> TrackCenterLine::getAllPW(double x, double y) const
{
    return m_h->getAllPW(x, y);
}

double TrackCenterLine::getAzimuth(double p) const { return m_h->getAzimuth(p); }
double TrackCenterLine::getSlope  (double p) const { return m_v->getSlope(p);   }
double TrackCenterLine::getRadius (double p, bool signed_) const { return m_h->getRadius(p, signed_); }
double TrackCenterLine::getVerticalRadius(double p) const { return m_v->getRadius(p); }
double TrackCenterLine::getCant         (double p, bool s) const { return m_h->getCant(p, s);          }
double TrackCenterLine::getGaugeWidening(double p, bool s) const { return m_h->getGaugeWidening(p, s); }

// ── Offset polyline ───────────────────────────────────────────────────────────

QList<QVector3D> TrackCenterLine::getOffsetPolyline(double p1, double p2,
                                                    double w) const
{
    if (m_h->isEmpty()) return {};
    QList<QVector3D> result;

    // Walk element by element across [p1, p2]
    double pCur = p1;
    while (pCur < p2 - kTol) {
        const AlignmentElement* e = m_h->elementAt(pCur);
        if (!e) break;

        const double pEnd = std::min(e->endChainage(), p2);

        if (const auto* arc = dynamic_cast<const CircularArcElement*>(e)) {
            // One vertex at pCur with the bulge for the segment [pCur, pEnd]
            QPointF xy  = arc->worldXY(pCur, w);
            double bulge = arc->bulge(pEnd - pCur);
            result.append({ static_cast<float>(xy.x()),
                           static_cast<float>(xy.y()),
                           static_cast<float>(bulge) });

        } else if (dynamic_cast<const TangentElement*>(e)) {
            // Two vertices (bulge = 0)
            QPointF xy = e->worldXY(pCur, w);
            result.append({ static_cast<float>(xy.x()),
                           static_cast<float>(xy.y()), 0.f });

        } else {
            // Spiral: densify (one vertex per metre, minimum 2)
            const int N  = std::max(2, static_cast<int>(pEnd - pCur));
            const double step = (pEnd - pCur) / N;
            for (int k = 0; k < N; ++k) {
                QPointF xy = e->worldXY(pCur + k * step, w);
                result.append({ static_cast<float>(xy.x()),
                               static_cast<float>(xy.y()), 0.f });
            }
        }

        pCur = pEnd;
    }

    // Always add the final point
    if (!m_h->isEmpty()) {
        QPointF xy = m_h->getXY(p2, w);
        result.append({ static_cast<float>(xy.x()),
                       static_cast<float>(xy.y()), 0.f });
    }

    return result;
}

// ── Fastener spacing ──────────────────────────────────────────────────────────

void TrackCenterLine::loadSpacingRules(const QVector<SpacingRule>& rules,
                                       double preDistance)
{
    m_spacingRules = rules;
    m_preDistance  = preDistance;
}

double TrackCenterLine::getSpacing(double p) const
{
    if (m_spacingRules.isEmpty()) return 0.6;

    double R = std::abs(getRadius(p, false));
    if (std::isinf(R)) R = 1e19;

    // Look-ahead / look-back near TC and CT transitions:
    // If within preDistance of an upcoming TC boundary, use the circular
    // radius so that fastener spacing begins ramping up in advance.
    const auto& pts = m_h->rawPoints();
    int i = m_h->rawIndexAt(p);
    if (i >= 0 && i + 1 < pts.size()) {
        const QString& tsc = pts[i].tsc;
        // Approaching a TC (tangent ending, curve starting)?
        if (tsc == "TT" || tsc == "CT" || tsc == "ST") {
            if (i + 1 < pts.size() && pts[i+1].tsc == "TC") {
                double dist = pts[i+1].chainage - p;
                if (dist > 0.0 && dist <= m_preDistance && i+2 < pts.size())
                    R = std::abs(pts[i+2].radius);
            }
        }
        // Leaving a CT (curve ending, tangent starting)?
        if (tsc == "CT") {
            double dist = p - pts[i].chainage;
            if (dist > 0.0 && dist <= m_preDistance && i > 0)
                R = std::abs(pts[i-1].radius);
        }
    }

    for (const SpacingRule& rule : m_spacingRules) {
        if (R >= rule.radiusMin && R < rule.radiusMax)
            return rule.spacing;
    }

    return 0.6; // fallback
}

} // namespace railway
} // namespace aicad
