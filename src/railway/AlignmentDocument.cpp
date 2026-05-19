#include "AlignmentDocument.h"
#include "AlignmentSolver.h"

#include <QJsonArray>
#include <QLineF>
#include <QtDebug>
#include <cmath>

namespace aicad {
namespace railway {

// ============================================================================
//  Static geometry helpers (file-local)
// ============================================================================

namespace {

/** 方位角 [rad, 順時針自北] = atan2(dEast, dNorth) */
static double azimuthOf(QPointF from, QPointF to)
{
    return std::atan2(to.x() - from.x(), to.y() - from.y());
}

/**
 * P 到直線 A→B 的垂足。
 * t 未必在 [0,1]，呼叫端可視需要 clamp。
 */
static QPointF footOfPerp(QPointF A, QPointF B, QPointF P)
{
    QPointF ab = B - A;
    double  ab2 = QPointF::dotProduct(ab, ab);
    if (ab2 < 1e-18) return A;
    double t = QPointF::dotProduct(P - A, ab) / ab2;
    return A + t * ab;
}

} // anonymous namespace

// ============================================================================
//  HorizontalAlignmentEdit
// ============================================================================

HorizontalAlignmentEdit::HorizontalAlignmentEdit(QObject* parent)
    : QObject(parent)
    , m_result(std::make_unique<HorizontalAlignment>())
{
}

// ── 元素新增 ──────────────────────────────────────────────────────────────────

int HorizontalAlignmentEdit::addFixedTangent(QPointF from, QPointF to)
{
    EditableElement e;
    e.type    = EditableElementType::Tangent;
    e.mode    = ConstraintMode::Fixed;
    e.startPI = from;
    e.endPI   = to;
    e.length  = QLineF(from, to).length();
    m_elems.append(e);
    return m_elems.size() - 1;
}

int HorizontalAlignmentEdit::addFixedCurve(QPointF arcStart, QPointF arcEnd,
                                           QPointF arcCenter, double radius)
{
    EditableElement e;
    e.type      = EditableElementType::CircularArc;
    e.mode      = ConstraintMode::Fixed;
    e.startPI   = arcStart;    // PC  — 弧起點（使用者第一個點擊點）
    e.endPI     = arcEnd;      // PT  — 弧終點（使用者第三個點擊點）
    e.arcCenter = arcCenter;   // 外接圓圓心（由命令層計算）
    e.radius    = std::abs(radius);
    m_elems.append(e);
    return m_elems.size() - 1;
}

int HorizontalAlignmentEdit::addFloatingCurve(int tangentIdxBefore,
                                              int tangentIdxAfter,
                                              double radius)
{
    if (tangentIdxBefore < 0 || tangentIdxBefore >= m_elems.size()) {
        qWarning() << "[HorizontalAlignmentEdit] addFloatingCurve: tangentIdxBefore out of range:" << tangentIdxBefore;
        return -1;
    }
    if (tangentIdxAfter < 0 || tangentIdxAfter >= m_elems.size()) {
        qWarning() << "[HorizontalAlignmentEdit] addFloatingCurve: tangentIdxAfter out of range:" << tangentIdxAfter;
        return -1;
    }
    if (m_elems[tangentIdxBefore].type != EditableElementType::Tangent) {
        qWarning() << "[HorizontalAlignmentEdit] addFloatingCurve: element at tangentIdxBefore is not a Tangent";
        return -1;
    }
    if (m_elems[tangentIdxAfter].type != EditableElementType::Tangent) {
        qWarning() << "[HorizontalAlignmentEdit] addFloatingCurve: element at tangentIdxAfter is not a Tangent";
        return -1;
    }

    EditableElement e;
    e.type             = EditableElementType::CircularArc;
    e.mode             = ConstraintMode::Floating;
    e.radius           = std::abs(radius);
    e.tangentIdxBefore = tangentIdxBefore;
    e.tangentIdxAfter  = tangentIdxAfter;
    // startPI / endPI left at default (0,0); AlignmentSolver fills them in.

    m_elems.append(e);
    return m_elems.size() - 1;
}


int HorizontalAlignmentEdit::addSCS(int    tangentIdxBefore,
                                    int    tangentIdxAfter,
                                    double radius,
                                    double spiralLength)
{
    // Validate tangent references
    if (tangentIdxBefore < 0 || tangentIdxBefore >= m_elems.size()) {
        qWarning() << "[HorizontalAlignmentEdit] addSCS: tangentIdxBefore out of range:" << tangentIdxBefore;
        return -1;
    }
    if (tangentIdxAfter < 0 || tangentIdxAfter >= m_elems.size()) {
        qWarning() << "[HorizontalAlignmentEdit] addSCS: tangentIdxAfter out of range:" << tangentIdxAfter;
        return -1;
    }
    if (m_elems[tangentIdxBefore].type != EditableElementType::Tangent) {
        qWarning() << "[HorizontalAlignmentEdit] addSCS: element at tangentIdxBefore is not a Tangent";
        return -1;
    }
    if (m_elems[tangentIdxAfter].type != EditableElementType::Tangent) {
        qWarning() << "[HorizontalAlignmentEdit] addSCS: element at tangentIdxAfter is not a Tangent";
        return -1;
    }
    if (std::abs(radius) < 1e-9) {
        qWarning() << "[HorizontalAlignmentEdit] addSCS: radius ≈ 0";
        return -1;
    }
    if (spiralLength < 1e-9) {
        qWarning() << "[HorizontalAlignmentEdit] addSCS: spiralLength ≈ 0";
        return -1;
    }

    const int spiralInIdx = m_elems.size();

    // ── 入螺旋 (SpiralIn) ──────────────────────────────────────────────────
    EditableElement spiralIn;
    spiralIn.type             = EditableElementType::SpiralIn;
    spiralIn.mode             = ConstraintMode::Floating;
    spiralIn.radius           = std::abs(radius);
    spiralIn.length           = std::abs(spiralLength);
    spiralIn.tangentIdxBefore = tangentIdxBefore;
    spiralIn.tangentIdxAfter  = tangentIdxAfter;
    m_elems.append(spiralIn);

    // ── 圓弧 (CircularArc) ────────────────────────────────────────────────
    EditableElement arc;
    arc.type             = EditableElementType::CircularArc;
    arc.mode             = ConstraintMode::Floating;
    arc.radius           = std::abs(radius);
    arc.tangentIdxBefore = tangentIdxBefore;
    arc.tangentIdxAfter  = tangentIdxAfter;
    m_elems.append(arc);

    // ── 出螺旋 (SpiralOut) ────────────────────────────────────────────────
    EditableElement spiralOut;
    spiralOut.type             = EditableElementType::SpiralOut;
    spiralOut.mode             = ConstraintMode::Floating;
    spiralOut.radius           = std::abs(radius);
    spiralOut.length           = std::abs(spiralLength);
    spiralOut.tangentIdxBefore = tangentIdxBefore;
    spiralOut.tangentIdxAfter  = tangentIdxAfter;
    m_elems.append(spiralOut);

    return spiralInIdx;  // 回傳入螺旋的 index
}

// ── 元素操作 ──────────────────────────────────────────────────────────────────

void HorizontalAlignmentEdit::movePI(int idx, QPointF newPos)
{
    if (idx < 0 || idx >= m_elems.size()) return;
    auto& e = m_elems[idx];

    switch (e.type) {
    case EditableElementType::Tangent:
        // 移動切線的遠端（結束端）
        e.endPI  = newPos;
        e.length = QLineF(e.startPI, newPos).length();
        break;

    case EditableElementType::CircularArc:
        // Fixed arc：移動圓心
        e.startPI = newPos;
        // endPI / length 由 solve() 重新計算
        e.solved  = false;
        break;

    default:
        e.startPI = newPos;
        break;
    }
}

void HorizontalAlignmentEdit::setRadius(int idx, double radius)
{
    // TODO Step 3: Floating 模式才需要 re-solve；Fixed 直接更新
    if (idx < 0 || idx >= m_elems.size()) return;
    m_elems[idx].radius = std::abs(radius);
    m_elems[idx].solved = false;
}

void HorizontalAlignmentEdit::setConstraintMode(int idx, ConstraintMode mode)
{
    // TODO Step 3
    if (idx < 0 || idx >= m_elems.size()) return;
    m_elems[idx].mode   = mode;
    m_elems[idx].solved = false;
}

void HorizontalAlignmentEdit::removeElement(int idx)
{
    if (idx < 0 || idx >= m_elems.size()) return;
    m_elems.removeAt(idx);
}

// ── solve() ──────────────────────────────────────────────────────────────────

/**
 * @brief Fixed-element solver。
 *
 * 演算流程
 * ─────────
 * Pass 1  為每個 Fixed CircularArc 計算切點（切線垂足），
 *         並修正相鄰切線的端點。
 * Pass 2  逐一建立 AlignmentPoint，依元素順序累積 chainage。
 *         最後補上終端哨兵點（length=0）。
 * Pass 3  呼叫 HorizontalAlignment::load()。
 *
 * AlignmentPoint 欄位對應
 * ───────────────────────
 *   tsc[1]=='T'  → TangentElement   (tsc="TT")
 *   tsc[1]=='C'  → CircularArcElement (tsc="CC")
 *   radius       傳入絕對值；load() 以 crossTrack 判斷符號。
 */
void HorizontalAlignmentEdit::solve()
{
    const int n = m_elems.size();
    if (n == 0) {
        m_result->clear();
        emit changed();
        return;
    }

    // ── 工作用副本（保留 startPI / endPI 原始意義）──────────────────────────
    // arcStart / arcEnd：解算後的弧端點（切點）
    // arcAz             ：弧起點的切線方位角
    // arcLen            ：弧長

    struct ArcData {
        bool    valid  = false;
        QPointF start;          ///< T1 切點 = 弧起點
        QPointF end;            ///< T2 切點 = 弧終點
        double  azStart = 0.0;  ///< 弧起點切線方位角 [rad]
        double  len     = 0.0;  ///< 弧長 [m]
    };

    // 切線段調整後的 startPI / endPI
    QVector<QPointF> tanStart(n), tanEnd(n);
    for (int i = 0; i < n; ++i) {
        tanStart[i] = m_elems[i].startPI;
        tanEnd[i]   = m_elems[i].endPI;
    }

    QVector<ArcData> arcs(n);

    // ── Pass 1：解算 Fixed CircularArc ───────────────────────────────────────
    for (int i = 0; i < n; ++i) {
        const auto& e = m_elems[i];
        if (e.type != EditableElementType::CircularArc) continue;
        if (e.mode != ConstraintMode::Fixed) continue;  // Floating → Step 3

        const QPointF center = e.startPI;   // Fixed arc 用 startPI 存圓心
        const double  R      = std::abs(e.radius);
        if (R < 1e-9) {
            qWarning() << "[HorizontalAlignmentEdit] Arc radius ~ 0 at index" << i;
            continue;
        }

        // 尋找前後切線元素
        int prevT = -1;
        for (int j = i - 1; j >= 0; --j)
            if (m_elems[j].type == EditableElementType::Tangent) { prevT = j; break; }

        int nextT = -1;
        for (int j = i + 1; j < n; ++j)
            if (m_elems[j].type == EditableElementType::Tangent) { nextT = j; break; }

        ArcData arc;

        // ── T1：圓心到前切線的垂足 ──────────────────────────────────────────
        if (prevT >= 0) {
            arc.start = footOfPerp(tanStart[prevT], tanEnd[prevT], center);
            tanEnd[prevT] = arc.start;              // 修正前切線終點
            arc.azStart   = azimuthOf(tanStart[prevT], arc.start);
        } else {
            // 無前切線：用 startPI 本身作為弧起點（standalone arc）
            arc.start   = center + QPointF(0, R);  // 正北方向作 fallback
            arc.azStart = 0.0;
        }

        // ── T2：圓心到後切線的垂足 ──────────────────────────────────────────
        if (nextT >= 0) {
            arc.end         = footOfPerp(tanStart[nextT], tanEnd[nextT], center);
            tanStart[nextT] = arc.end;              // 修正後切線起點
        } else {
            arc.end = center + QPointF(R, 0);       // 正東方向作 fallback
        }

        // ── 弧長 ─────────────────────────────────────────────────────────────
        // 以圓心角計算：Δθ = angle between (center→T1) and (center→T2)
        const QPointF v1 = arc.start - center;
        const QPointF v2 = arc.end   - center;
        const double  cross = v1.x() * v2.y() - v1.y() * v2.x();
        const double  dot   = v1.x() * v2.x() + v1.y() * v2.y();
        const double  delta = std::abs(std::atan2(cross, dot));  // 始終取正值
        arc.len = R * delta;

        if (arc.len < 1e-9) {
            qWarning() << "[HorizontalAlignmentEdit] Arc length ~ 0 at index" << i;
            continue;
        }

        arc.valid     = true;
        arcs[i]       = arc;
        m_elems[i].solved = true;
    }

    // ── Pass 2：建立 AlignmentPoint 序列 ─────────────────────────────────────
    QVector<AlignmentPoint> pts;
    pts.reserve(n + 1);
    double chainage = 0.0;

    for (int i = 0; i < n; ++i) {
        const auto& e = m_elems[i];
        AlignmentPoint pt;

        if (e.type == EditableElementType::Tangent) {
            const double len = QLineF(tanStart[i], tanEnd[i]).length();
            if (len < 1e-9) continue;   // 退化段略過

            pt.tsc      = QStringLiteral("TT");
            pt.easting  = tanStart[i].x();
            pt.northing = tanStart[i].y();
            pt.azimuth  = azimuthOf(tanStart[i], tanEnd[i]);
            pt.length   = len;
            pt.chainage = chainage;
            chainage   += len;
            pts.append(pt);

        } else if (e.type == EditableElementType::CircularArc) {
            if (!arcs[i].valid) continue;
            const ArcData& arc = arcs[i];

            pt.tsc      = QStringLiteral("CC");
            pt.easting  = arc.start.x();
            pt.northing = arc.start.y();
            pt.azimuth  = arc.azStart;
            pt.radius   = e.radius;    // 絕對值；load() 自動判斷符號
            pt.length   = arc.len;
            pt.chainage = chainage;
            chainage   += arc.len;
            pts.append(pt);
        }
        // SpiralIn / SpiralOut → Step 4
    }

    // ── 終端哨兵點（length = 0）───────────────────────────────────────────────
    if (!pts.isEmpty()) {
        AlignmentPoint sentinel;
        sentinel.tsc     = QStringLiteral("TT");
        sentinel.length  = 0.0;
        sentinel.chainage = chainage;

        // 最後一個元素的終點
        const int last = n - 1;
        if (m_elems[last].type == EditableElementType::Tangent) {
            sentinel.easting  = tanEnd[last].x();
            sentinel.northing = tanEnd[last].y();
            sentinel.azimuth  = azimuthOf(tanStart[last], tanEnd[last]);
        } else if (m_elems[last].type == EditableElementType::CircularArc
                   && arcs[last].valid) {
            sentinel.easting  = arcs[last].end.x();
            sentinel.northing = arcs[last].end.y();
            // 弧終點切線方位角 = 弧起點方位角 + 圓心角（帶符號由 load() 決定）
            sentinel.azimuth  = arcs[last].azStart;
        } else {
            // fallback：沿用最後一個 AlignmentPoint 的方位角
            sentinel.easting  = pts.last().easting;
            sentinel.northing = pts.last().northing;
            sentinel.azimuth  = pts.last().azimuth;
        }

        pts.append(sentinel);
    }

    // ── Pass 3：交給 HorizontalAlignment 建立幾何元素 ────────────────────────
    AlignmentSolver solver;
    m_result = solver.solve(m_elems);
    emit changed();
}

const HorizontalAlignment* HorizontalAlignmentEdit::result() const
{
    return m_result.get();
}

QJsonObject HorizontalAlignmentEdit::toJson() const
{
    // TODO Step 18: 序列化所有 EditableElement
    QJsonObject obj;
    QJsonArray arr;
    for (const auto& e : m_elems) {
        QJsonObject elem;
        elem["type"]   = static_cast<int>(e.type);
        elem["mode"]   = static_cast<int>(e.mode);
        elem["radius"] = e.radius;
        elem["length"] = e.length;
        elem["startX"] = e.startPI.x();
        elem["startY"] = e.startPI.y();
        elem["endX"]   = e.endPI.x();
        elem["endY"]   = e.endPI.y();
        elem["centerX"] = e.arcCenter.x();
        elem["centerY"] = e.arcCenter.y();
        elem["solved"] = e.solved;
        elem["tangentIdxBefore"]  = e.tangentIdxBefore;
        elem["tangentIdxAfter"]   = e.tangentIdxAfter;
        arr.append(elem);
    }
    obj["elements"] = arr;
    return obj;
}

bool HorizontalAlignmentEdit::fromJson(const QJsonObject& obj)
{
    // TODO Step 18: 還原所有 EditableElement
    m_elems.clear();
    const QJsonArray arr = obj["elements"].toArray();
    for (const auto& v : arr) {
        const QJsonObject e = v.toObject();
        EditableElement elem;
        elem.type   = static_cast<EditableElementType>(e["type"].toInt());
        elem.mode   = static_cast<ConstraintMode>(e["mode"].toInt());
        elem.radius = e["radius"].toDouble();
        elem.length = e["length"].toDouble();
        elem.startPI = QPointF(e["startX"].toDouble(), e["startY"].toDouble());
        elem.endPI   = QPointF(e["endX"].toDouble(),   e["endY"].toDouble());
        // BUG FIX: Always reset to false — solve() recomputes this flag.
        // Loading a stale 'true' would leave Floating elements appearing
        // solved before the solver has actually run.
        elem.solved  = false;
        elem.tangentIdxBefore = e["tangentIdxBefore"].toInt(-1);
        elem.tangentIdxAfter  = e["tangentIdxAfter"].toInt(-1);
        m_elems.append(elem);
    }
    return true;
}

// ============================================================================
//  VerticalAlignmentEdit
// ============================================================================

VerticalAlignmentEdit::VerticalAlignmentEdit(QObject* parent)
    : QObject(parent)
    , m_result(std::make_unique<VerticalAlignment>())
{
}

int VerticalAlignmentEdit::addVip(double /*chainage*/,
                                  double /*elevation*/,
                                  double /*lvc*/)
{
    // TODO Step 12
    return -1;
}

void VerticalAlignmentEdit::moveVip(int /*idx*/,
                                    double /*newChainage*/,
                                    double /*newElevation*/)
{
    // TODO Step 12
}

void VerticalAlignmentEdit::removeVip(int /*idx*/)
{
    // TODO Step 12
}

void VerticalAlignmentEdit::setKValue(int /*vipIdx*/, double /*K*/)
{
    // TODO Step 12: lvc = K × |Δg%|
}

void VerticalAlignmentEdit::solve()
{
    // TODO Step 12: 將 m_vips 轉成 VerticalAlignmentPoint → m_result->load() → emit changed()
    emit changed();
}

const VerticalAlignment* VerticalAlignmentEdit::result() const
{
    return m_result.get();
}

QJsonObject VerticalAlignmentEdit::toJson() const
{
    QJsonObject obj;
    QJsonArray arr;
    for (const auto& v : m_vips) {
        QJsonObject vip;
        vip["chainage"]  = v.chainage;
        vip["elevation"] = v.elevation;
        vip["lvc"]       = v.lvc;
        arr.append(vip);
    }
    obj["vips"] = arr;
    return obj;
}

bool VerticalAlignmentEdit::fromJson(const QJsonObject& obj)
{
    m_vips.clear();
    const QJsonArray arr = obj["vips"].toArray();
    for (const auto& v : arr) {
        const QJsonObject j = v.toObject();
        VipRecord rec;
        rec.chainage  = j["chainage"].toDouble();
        rec.elevation = j["elevation"].toDouble();
        rec.lvc       = j["lvc"].toDouble();
        m_vips.append(rec);
    }
    return true;
}

// ============================================================================
//  AlignmentDocument
// ============================================================================

AlignmentDocument::AlignmentDocument(QObject* parent)
    : QObject(parent)
    , m_horizontal(std::make_unique<HorizontalAlignmentEdit>(this))
    , m_vertical(std::make_unique<VerticalAlignmentEdit>(this))
{
}

QJsonObject AlignmentDocument::toJson() const
{
    QJsonObject obj;
    obj["horizontal"] = m_horizontal->toJson();
    obj["vertical"]   = m_vertical->toJson();
    return obj;
}

bool AlignmentDocument::fromJson(const QJsonObject& obj)
{
    bool ok = true;
    if (obj.contains("horizontal"))
        ok &= m_horizontal->fromJson(obj["horizontal"].toObject());
    if (obj.contains("vertical"))
        ok &= m_vertical->fromJson(obj["vertical"].toObject());

    // Re-solve so AlignmentRenderer gets refreshed after load
    if (ok) {
        m_horizontal->solve();
        m_vertical->solve();
    }
    return ok;
}

void AlignmentDocument::syncToOCAF(aicad::cad::Document* /*doc*/)
{
    // TODO Step 5: 呼叫 AlignmentRenderer 同步 AIS 物件至 OCAF Document
}

} // namespace railway
} // namespace aicad
