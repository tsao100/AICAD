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
// (helpers removed — azimuthOf and footOfPerp are no longer needed here;
//  the dead-code pass that called them has been removed.  AlignmentSolver
//  has its own copies of these utilities.)
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

    // ── 委由 AlignmentSolver 完成所有解算 ───────────────────────────────────
    //
    //  NOTE: The previous design had a Pass 1 here that recomputed arc
    //  cut-points and built an intermediate AlignmentPoint sequence.  That
    //  code contained a stale assumption — it read `e.startPI` as the arc
    //  centre, whereas `addFixedCurve` now stores the arc centre in
    //  `e.arcCenter` and the PC in `e.startPI`.  The intermediate pts array
    //  was also discarded immediately after (AlignmentSolver::solve rebuilt
    //  it from scratch), so the whole pass was dead code.  It has been
    //  removed; AlignmentSolver::solve(m_elems) is the sole authoritative
    //  solver for both Fixed and Floating elements.
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