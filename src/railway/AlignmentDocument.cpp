#include "AlignmentDocument.h"
#include "AlignmentSolver.h"
#include "command/alignment/AlignmentEditCommand.h"
#include "core/Application.h"
#include "ui/UIManager.h"

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
    const QJsonObject before = parentDocument() ? parentDocument()->toJson() : QJsonObject();

    EditableElement e;
    e.type    = EditableElementType::Tangent;
    e.mode    = ConstraintMode::Fixed;
    e.startPI = from;
    e.endPI   = to;
    e.length  = QLineF(from, to).length();
    m_elems.append(e);

    if (parentDocument()) {
        const QJsonObject after = parentDocument()->toJson();
        command::AlignmentEditCommand::push(parentDocument(), before, after,
                                            "Add Fixed Tangent");
    }
    return m_elems.size() - 1;
}

int HorizontalAlignmentEdit::addFixedCurve(QPointF arcStart, QPointF arcEnd,
                                           QPointF arcCenter, double radius)
{
    const QJsonObject before = parentDocument() ? parentDocument()->toJson() : QJsonObject();

    EditableElement e;
    e.type      = EditableElementType::CircularArc;
    e.mode      = ConstraintMode::Fixed;
    e.startPI   = arcStart;
    e.endPI     = arcEnd;
    e.arcCenter = arcCenter;
    e.radius    = std::abs(radius);
    m_elems.append(e);

    if (parentDocument()) {
        command::AlignmentEditCommand::push(parentDocument(),
                                            before, parentDocument()->toJson(),
                                            "Add Fixed Curve");
    }
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

    const QJsonObject before = parentDocument() ? parentDocument()->toJson() : QJsonObject();

    EditableElement e;
    e.type             = EditableElementType::CircularArc;
    e.mode             = ConstraintMode::Floating;
    e.radius           = std::abs(radius);
    e.tangentIdxBefore = tangentIdxBefore;
    e.tangentIdxAfter  = tangentIdxAfter;
    // startPI / endPI left at default (0,0); AlignmentSolver fills them in.

    m_elems.append(e);

    if (parentDocument()) {
        command::AlignmentEditCommand::push(parentDocument(),
                                            before, parentDocument()->toJson(),
                                            "Add Floating Curve");
    }
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
    const QJsonObject before = parentDocument() ? parentDocument()->toJson() : QJsonObject();

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

    if (parentDocument()) {
        command::AlignmentEditCommand::push(parentDocument(),
                                            before, parentDocument()->toJson(),
                                            "Add SCS");
    }
    return spiralInIdx;  // 回傳入螺旋的 index
}

// ── addSCS (非對稱版：L1、L2 可獨立設定) ─────────────────────────────────────
//
//  L1=L2=0 → 退化為單一 Floating CircularArc（AFC）
//  L1>0, L2=0 → SpiralIn + CircularArc（SC 型）
//  L1=0, L2>0 → CircularArc + SpiralOut（CS 型）
//  L1>0, L2>0 → SpiralIn + CircularArc + SpiralOut（完整 SCS）
//
int HorizontalAlignmentEdit::addSCS(int    tangentIdxBefore,
                                    int    tangentIdxAfter,
                                    double radius,
                                    double spiralLength1,
                                    double spiralLength2)
{
    // Validate tangent references
    if (tangentIdxBefore < 0 || tangentIdxBefore >= m_elems.size()) {
        qWarning() << "[HorizontalAlignmentEdit] addSCS(L1/L2): tangentIdxBefore out of range:"
                   << tangentIdxBefore;
        return -1;
    }
    if (tangentIdxAfter < 0 || tangentIdxAfter >= m_elems.size()) {
        qWarning() << "[HorizontalAlignmentEdit] addSCS(L1/L2): tangentIdxAfter out of range:"
                   << tangentIdxAfter;
        return -1;
    }
    if (m_elems[tangentIdxBefore].type != EditableElementType::Tangent) {
        qWarning() << "[HorizontalAlignmentEdit] addSCS(L1/L2): tangentIdxBefore is not a Tangent";
        return -1;
    }
    if (m_elems[tangentIdxAfter].type != EditableElementType::Tangent) {
        qWarning() << "[HorizontalAlignmentEdit] addSCS(L1/L2): tangentIdxAfter is not a Tangent";
        return -1;
    }
    if (std::abs(radius) < 1e-9) {
        qWarning() << "[HorizontalAlignmentEdit] addSCS(L1/L2): radius ≈ 0";
        return -1;
    }

    // L1=L2=0 → 退化為 AFC（addFloatingCurve 內部自行推 undo）
    if (spiralLength1 < 1e-9 && spiralLength2 < 1e-9) {
        return addFloatingCurve(tangentIdxBefore, tangentIdxAfter, radius);
    }

    const int firstIdx = m_elems.size();
    const QJsonObject before = parentDocument() ? parentDocument()->toJson() : QJsonObject();

    // ── 入螺旋 (SpiralIn) — 僅當 L1 > 0 ────────────────────────────────────
    if (spiralLength1 > 1e-9) {
        EditableElement spiralIn;
        spiralIn.type             = EditableElementType::SpiralIn;
        spiralIn.mode             = ConstraintMode::Floating;
        spiralIn.radius           = std::abs(radius);
        spiralIn.length           = spiralLength1;
        spiralIn.tangentIdxBefore = tangentIdxBefore;
        spiralIn.tangentIdxAfter  = tangentIdxAfter;
        m_elems.append(spiralIn);
    }

    // ── 圓弧 (CircularArc) ────────────────────────────────────────────────
    EditableElement arc;
    arc.type             = EditableElementType::CircularArc;
    arc.mode             = ConstraintMode::Floating;
    arc.radius           = std::abs(radius);
    arc.tangentIdxBefore = tangentIdxBefore;
    arc.tangentIdxAfter  = tangentIdxAfter;
    m_elems.append(arc);

    // ── 出螺旋 (SpiralOut) — 僅當 L2 > 0 ────────────────────────────────────
    if (spiralLength2 > 1e-9) {
        EditableElement spiralOut;
        spiralOut.type             = EditableElementType::SpiralOut;
        spiralOut.mode             = ConstraintMode::Floating;
        spiralOut.radius           = std::abs(radius);
        spiralOut.length           = spiralLength2;
        spiralOut.tangentIdxBefore = tangentIdxBefore;
        spiralOut.tangentIdxAfter  = tangentIdxAfter;
        m_elems.append(spiralOut);
    }

    if (parentDocument()) {
        command::AlignmentEditCommand::push(parentDocument(),
                                            before, parentDocument()->toJson(),
                                            "Add SCS (asymmetric)");
    }
    return firstIdx;
}

// ── addSCS (完整版：獨立螺旋長度 + 獨立螺旋類型) ──────────────────────────────
//
//  L1=L2=0 → 退化為 AFC（單一 Floating CircularArc）
//  L1>0, L2=0 → SpiralIn + CircularArc（SC 型）
//  L1=0, L2>0 → CircularArc + SpiralOut（CS 型）
//  L1>0, L2>0 → SpiralIn + CircularArc + SpiralOut（完整 SCS）
//
//  type1 = 入螺旋類型（SpiralIn 元素）
//  type2 = 出螺旋類型（SpiralOut 元素）
//  兩者皆記錄於 SpiralIn 元素的 spiralType1 / spiralType2，方便 Solver 查詢。
//
int HorizontalAlignmentEdit::addSCS(int        tangentIdxBefore,
                                    int        tangentIdxAfter,
                                    double     radius,
                                    double     spiralLength1,
                                    double     spiralLength2,
                                    SpiralType type1,
                                    SpiralType type2)
{
    // Validate tangent references
    if (tangentIdxBefore < 0 || tangentIdxBefore >= m_elems.size()) {
        qWarning() << "[HorizontalAlignmentEdit] addSCS(full): tangentIdxBefore out of range:"
                   << tangentIdxBefore;
        return -1;
    }
    if (tangentIdxAfter < 0 || tangentIdxAfter >= m_elems.size()) {
        qWarning() << "[HorizontalAlignmentEdit] addSCS(full): tangentIdxAfter out of range:"
                   << tangentIdxAfter;
        return -1;
    }
    if (m_elems[tangentIdxBefore].type != EditableElementType::Tangent) {
        qWarning() << "[HorizontalAlignmentEdit] addSCS(full): tangentIdxBefore is not a Tangent";
        return -1;
    }
    if (m_elems[tangentIdxAfter].type != EditableElementType::Tangent) {
        qWarning() << "[HorizontalAlignmentEdit] addSCS(full): tangentIdxAfter is not a Tangent";
        return -1;
    }
    if (std::abs(radius) < 1e-9) {
        qWarning() << "[HorizontalAlignmentEdit] addSCS(full): radius ≈ 0";
        return -1;
    }

    // L1=L2=0 → 退化為 AFC
    if (spiralLength1 < 1e-9 && spiralLength2 < 1e-9) {
        return addFloatingCurve(tangentIdxBefore, tangentIdxAfter, radius);
    }

    const int firstIdx = m_elems.size();

    // ── 入螺旋 (SpiralIn) — 僅當 L1 > 0 ────────────────────────────────────
    if (spiralLength1 > 1e-9) {
        EditableElement spiralIn;
        spiralIn.type             = EditableElementType::SpiralIn;
        spiralIn.mode             = ConstraintMode::Floating;
        spiralIn.radius           = std::abs(radius);
        spiralIn.length           = spiralLength1;
        spiralIn.tangentIdxBefore = tangentIdxBefore;
        spiralIn.tangentIdxAfter  = tangentIdxAfter;
        spiralIn.spiralType1      = type1;   // 入螺旋類型（自身使用）
        spiralIn.spiralType2      = type2;   // 出螺旋類型（由此攜帶供 Solver 查詢）
        m_elems.append(spiralIn);
    }

    // ── 圓弧 (CircularArc) ────────────────────────────────────────────────
    EditableElement arc;
    arc.type             = EditableElementType::CircularArc;
    arc.mode             = ConstraintMode::Floating;
    arc.radius           = std::abs(radius);
    arc.tangentIdxBefore = tangentIdxBefore;
    arc.tangentIdxAfter  = tangentIdxAfter;
    m_elems.append(arc);

    // ── 出螺旋 (SpiralOut) — 僅當 L2 > 0 ────────────────────────────────────
    if (spiralLength2 > 1e-9) {
        EditableElement spiralOut;
        spiralOut.type             = EditableElementType::SpiralOut;
        spiralOut.mode             = ConstraintMode::Floating;
        spiralOut.radius           = std::abs(radius);
        spiralOut.length           = spiralLength2;
        spiralOut.tangentIdxBefore = tangentIdxBefore;
        spiralOut.tangentIdxAfter  = tangentIdxAfter;
        spiralOut.spiralType1      = type2;  // SpiralOut 自身類型記在 spiralType1
        spiralOut.spiralType2      = type2;  // 保持一致
        m_elems.append(spiralOut);
    }

    return firstIdx;
}

// ── 元素操作 ──────────────────────────────────────────────────────────────────

void HorizontalAlignmentEdit::movePI(int idx, QPointF newPos)
{
    if (idx < 0 || idx >= m_elems.size()) return;

    const QJsonObject before = parentDocument() ? parentDocument()->toJson() : QJsonObject();

    auto& e = m_elems[idx];

    switch (e.type) {
    case EditableElementType::Tangent:
        e.endPI  = newPos;
        e.length = QLineF(e.startPI, newPos).length();
        break;

    case EditableElementType::CircularArc:
        e.startPI = newPos;
        e.solved  = false;
        break;

    default:
        e.startPI = newPos;
        break;
    }

    if (parentDocument()) {
        // mergeId=1: 連續拖曳合併成一筆 Undo 記錄
        auto* cmd = new command::AlignmentEditCommand(
            parentDocument(), before, parentDocument()->toJson(), "Move PI", /*mergeId=*/1);
        auto* app = core::Application::instance();
        if (app && app->uiManager() && app->uiManager()->undoStack())
            app->uiManager()->undoStack()->push(cmd);
    }
}

void HorizontalAlignmentEdit::moveStartPI(int idx, QPointF newPos)
{
    if (idx < 0 || idx >= m_elems.size()) return;

    const QJsonObject before = parentDocument() ? parentDocument()->toJson() : QJsonObject();

    auto& e = m_elems[idx];
    e.startPI = newPos;
    if (e.type == EditableElementType::Tangent)
        e.length = QLineF(newPos, e.endPI).length();
    e.solved = false;

    if (parentDocument()) {
        // mergeId=2: 與 movePI(mergeId=1) 的拖曳互不合併
        auto* cmd = new command::AlignmentEditCommand(
            parentDocument(), before, parentDocument()->toJson(), "Move Start PI", /*mergeId=*/2);
        auto* app = core::Application::instance();
        if (app && app->uiManager() && app->uiManager()->undoStack())
            app->uiManager()->undoStack()->push(cmd);
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
    //
    //  LC/CA groups: the solver writes back the solved Clothoid length Ls
    //  into elems[i].length via const_cast so that toJson() persists it.
    //  We pass m_elems directly; the solver's const_cast writes to the
    //  same QVector we own.
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
        elem["solved"]       = e.solved;
        elem["spiralType1"]  = static_cast<int>(e.spiralType1);
        elem["spiralType2"]  = static_cast<int>(e.spiralType2);
        elem["tangentIdxBefore"]  = e.tangentIdxBefore;
        elem["tangentIdxAfter"]   = e.tangentIdxAfter;
        // LC/CA group markers: a SpiralIn with tangentIdxAfter==-1 adjacent to
        // a Fixed Arc is an LC group.  A SpiralOut with tangentIdxBefore==-1
        // adjacent to a Fixed Arc is a CA group.  Store explicit flags to
        // avoid fragile structural inference across save/load.
        const bool isLC = (e.type == EditableElementType::SpiralIn
                           && e.mode == ConstraintMode::Floating
                           && e.tangentIdxAfter == -1);
        const bool isCA = (e.type == EditableElementType::SpiralOut
                           && e.mode == ConstraintMode::Floating
                           && e.tangentIdxBefore == -1);
        if (isLC) elem["groupLC"] = true;
        if (isCA) elem["groupCA"] = true;
        arr.append(elem);
    }
    obj["elements"] = arr;
    return obj;
}

bool HorizontalAlignmentEdit::fromJson(const QJsonObject& obj)
{
    m_elems.clear();
    const QJsonArray arr = obj["elements"].toArray();
    for (const auto& v : arr) {
        const QJsonObject e = v.toObject();
        EditableElement elem;
        elem.type      = static_cast<EditableElementType>(e["type"].toInt());
        elem.mode      = static_cast<ConstraintMode>(e["mode"].toInt());
        elem.radius    = e["radius"].toDouble();
        elem.length    = e["length"].toDouble();
        elem.startPI   = QPointF(e["startX"].toDouble(),  e["startY"].toDouble());
        elem.endPI     = QPointF(e["endX"].toDouble(),    e["endY"].toDouble());
        // Step 18: arcCenter was serialized but not restored — fixed.
        elem.arcCenter = QPointF(e["centerX"].toDouble(), e["centerY"].toDouble());
        // Always reset to false — solve() recomputes this flag.
        // Loading a stale 'true' would leave Floating elements appearing
        // solved before the solver has actually run.
        elem.solved      = false;
        elem.spiralType1 = static_cast<SpiralType>(e["spiralType1"].toInt(0));
        elem.spiralType2 = static_cast<SpiralType>(e["spiralType2"].toInt(0));
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

// ── addVip ────────────────────────────────────────────────────────────────────
//  插入並維持 chainage 升冪排序，回傳最終插入的 index。
int VerticalAlignmentEdit::addVip(double chainage, double elevation, double lvc)
{
    VipRecord rec;
    rec.chainage  = chainage;
    rec.elevation = elevation;
    rec.lvc       = lvc;

    int insertIdx = m_vips.size();
    for (int i = 0; i < m_vips.size(); ++i) {
        if (chainage < m_vips[i].chainage) {
            insertIdx = i;
            break;
        }
    }
    m_vips.insert(insertIdx, rec);
    return insertIdx;
}

// ── moveVip ───────────────────────────────────────────────────────────────────
void VerticalAlignmentEdit::moveVip(int idx, double newChainage, double newElevation)
{
    if (idx < 0 || idx >= m_vips.size()) return;

    m_vips[idx].chainage  = newChainage;
    m_vips[idx].elevation = newElevation;

    // 維持 chainage 升冪：取出後重新插入
    VipRecord moved = m_vips.takeAt(idx);
    int insertIdx = m_vips.size();
    for (int i = 0; i < m_vips.size(); ++i) {
        if (moved.chainage < m_vips[i].chainage) {
            insertIdx = i;
            break;
        }
    }
    m_vips.insert(insertIdx, moved);
}

// ── removeVip ─────────────────────────────────────────────────────────────────
void VerticalAlignmentEdit::removeVip(int idx)
{
    if (idx < 0 || idx >= m_vips.size()) return;
    m_vips.removeAt(idx);
}

// ── setKValue ─────────────────────────────────────────────────────────────────
//  K 值定義：lvc = K × |Δg%|
//  Δg% = 出坡% − 入坡%（百分比單位，非小數）。
//  僅對中間 VIP（非首尾）有效。
void VerticalAlignmentEdit::setKValue(int vipIdx, double K)
{
    if (vipIdx <= 0 || vipIdx >= m_vips.size() - 1) {
        qWarning() << "[VerticalAlignmentEdit] setKValue: vipIdx" << vipIdx
                   << "is an endpoint — no VC can be assigned.";
        return;
    }
    if (K < 0.0) {
        qWarning() << "[VerticalAlignmentEdit] setKValue: K must be >= 0";
        return;
    }

    const double dch_in  = m_vips[vipIdx    ].chainage - m_vips[vipIdx - 1].chainage;
    const double dch_out = m_vips[vipIdx + 1].chainage - m_vips[vipIdx    ].chainage;

    if (std::abs(dch_in) < 1e-9 || std::abs(dch_out) < 1e-9) {
        qWarning() << "[VerticalAlignmentEdit] setKValue: zero chainage interval adjacent to VIP" << vipIdx;
        return;
    }

    const double g_in  = (m_vips[vipIdx    ].elevation - m_vips[vipIdx - 1].elevation) / dch_in  * 100.0;
    const double g_out = (m_vips[vipIdx + 1].elevation - m_vips[vipIdx    ].elevation) / dch_out * 100.0;
    const double deltaG = std::abs(g_out - g_in);  // |Δg%|

    m_vips[vipIdx].lvc = (deltaG < 1e-9) ? 0.0 : K * deltaG;
}

// ── solve ─────────────────────────────────────────────────────────────────────
//
//  將 m_vips 轉換成 VerticalAlignment::load() 接受的 VerticalAlignmentPoint 序列。
//
//  每個含有效 lvc 的中間 VIP 產生「2 筆記錄」（VC entry + VC exit）：
//
//    idx   : entry  — ch = vip.ch − lvc/2, grade = g_in
//    idx+1 : exit   — ch = vip.ch + lvc/2, grade = g_out,
//                     lvc = L, pviElevation = vip.el
//
//  VerticalAlignment::insideVC(idx) 檢查 grade[idx] != grade[idx+1]，
//  符合條件時呼叫 parabolaElev(idx, p)：
//
//    y(x) = pviElevation − (lvc·g₁/2) + g₁·x − (g₁−g₂)·x²/(2·lvc)
//         = y₀ + g₁·x + (g₂−g₁)/(2L)·x²            ← 附錄 C 公式
//
//  其中 x = p − vc_entry_ch，g₁ = grade[idx]，g₂ = grade[idx+2]。
//  grade[idx+2] 即 exit 記錄之後那筆（與 exit 同為 g_out），正確。
//
void VerticalAlignmentEdit::solve()
{
    const int n = m_vips.size();

    if (n < 2) {
        m_result->clear();
        emit changed();
        return;
    }

    // Step 1：計算各段坡度（% 單位）
    QVector<double> grades(n - 1);
    for (int i = 0; i < n - 1; ++i) {
        const double dch = m_vips[i + 1].chainage - m_vips[i].chainage;
        if (std::abs(dch) < 1e-9) {
            qWarning() << "[VerticalAlignmentEdit] solve: zero chainage interval between VIP"
                       << i << "and" << (i + 1);
            grades[i] = 0.0;
        } else {
            grades[i] = (m_vips[i + 1].elevation - m_vips[i].elevation) / dch * 100.0;
        }
    }

    // Step 2：沿指定坡段切線計算高程輔助函式
    //   以 m_vips[gradeIdx] 為基準點，沿 grades[gradeIdx] 延伸到 ch
    auto tangentEl = [&](int gradeIdx, double ch) -> double {
        return m_vips[gradeIdx].elevation
               + (ch - m_vips[gradeIdx].chainage) * grades[gradeIdx] / 100.0;
    };

    // Step 3：建立 pts 陣列
    QVector<VerticalAlignmentPoint> pts;
    pts.reserve(n + 2 * (n - 2));

    // 起始 VIP（切線起點）
    {
        VerticalAlignmentPoint p0;
        p0.chainage  = m_vips[0].chainage;
        p0.elevation = m_vips[0].elevation;
        p0.grade     = grades[0];
        pts.append(p0);
    }

    // 中間 VIP：各自產生 VC entry + VC exit 兩筆
    for (int i = 1; i < n - 1; ++i) {
        const double lvc = m_vips[i].lvc;

        if (lvc < 1e-6) {
            // lvc ≈ 0：以「微小 VC」模擬純折點，避免 parabolaElev 除以零。
            // 間距 tiny = 1e-6 m（0.001 mm），VC 長度可忽略不計。
            static const double tiny = 1e-6;

            VerticalAlignmentPoint before;
            before.chainage  = m_vips[i].chainage - tiny;
            before.elevation = tangentEl(i - 1, before.chainage);
            before.grade     = grades[i - 1];
            pts.append(before);

            VerticalAlignmentPoint after;
            after.chainage     = m_vips[i].chainage;
            after.elevation    = m_vips[i].elevation;
            after.grade        = grades[i];
            after.lvc          = tiny;         // 非零：避免 parabolaElev 除以零
            after.pviElevation = m_vips[i].elevation;
            pts.append(after);

        } else {
            const double vc_half     = lvc / 2.0;
            const double vc_start_ch = m_vips[i].chainage - vc_half;
            const double vc_end_ch   = m_vips[i].chainage + vc_half;

            // ── VC entry ──────────────────────────────────────────────────
            VerticalAlignmentPoint entry;
            entry.chainage  = vc_start_ch;
            entry.elevation = tangentEl(i - 1, vc_start_ch);
            entry.grade     = grades[i - 1];
            pts.append(entry);

            // ── VC exit（攜帶 lvc / pviElevation）─────────────────────────
            //  parabolaElev 讀取 m_pts[entry_idx+1].lvc / pviElevation
            //  以及 m_pts[entry_idx+2].grade（= exit 後一筆，與 exit 同 grade）
            const double deltaGpct = std::abs(grades[i] - grades[i - 1]);
            VerticalAlignmentPoint exitPt;
            exitPt.chainage     = vc_end_ch;
            exitPt.elevation    = tangentEl(i, vc_end_ch);
            exitPt.grade        = grades[i];
            exitPt.lvc          = lvc;
            exitPt.pviElevation = m_vips[i].elevation;
            exitPt.kValue       = (deltaGpct > 1e-9) ? lvc / deltaGpct : 0.0;
            exitPt.mo           = lvc * deltaGpct / 800.0;  // 標準中距公式
            pts.append(exitPt);
        }
    }

    // 終止 VIP（切線終點）
    {
        VerticalAlignmentPoint pN;
        pN.chainage  = m_vips[n - 1].chainage;
        pN.elevation = m_vips[n - 1].elevation;
        pN.grade     = grades[n - 2];
        pts.append(pN);
    }

    m_result->load(pts);
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
    // Step 17: give children a back-pointer for Undo push
    m_horizontal->m_parentDoc = this;
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
    // Always call sub-editor fromJson unconditionally, passing an empty
    // QJsonObject when the key is absent.  This ensures that calling
    // fromJson(QJsonObject{}) (e.g. from NewCommand) fully clears all
    // PI points, VIP points, and solved results — not just the ones
    // covered by keys that happen to exist in the supplied object.
    bool ok = true;
    ok &= m_horizontal->fromJson(obj.value("horizontal").toObject());
    ok &= m_vertical->fromJson(obj.value("vertical").toObject());

    // Re-solve so AlignmentRenderer gets refreshed after load/clear.
    // When the editors are empty, solve() calls result->clear() and
    // emits changed() — which triggers AlignmentRenderer::refresh().
    m_horizontal->solve();
    m_vertical->solve();

    return ok;
}

void AlignmentDocument::syncToOCAF(aicad::cad::Document* /*doc*/)
{
    // TODO Step 5: 呼叫 AlignmentRenderer 同步 AIS 物件至 OCAF Document
}

// ============================================================================
//  addLC  ─ Fixed Tangent → Clothoid(未知長度) → Fixed CircularArc
//
//  The SpiralIn element is inserted immediately BEFORE the Fixed Arc in the
//  element list so that Pass 2c in solve() can find it via  arcI = i + 1.
//  All tangentIdxBefore / tangentIdxAfter values stored in existing elements
//  that referenced indices >= arcIdx are bumped by 1 to remain valid.
// ============================================================================

int HorizontalAlignmentEdit::addLC(int tangentIdx, int arcIdx,
                                    SpiralType spiralType)
{
    if (tangentIdx < 0 || tangentIdx >= m_elems.size()) {
        qWarning() << "[HorizontalAlignmentEdit] addLC: tangentIdx out of range:" << tangentIdx;
        return -1;
    }
    if (arcIdx < 0 || arcIdx >= m_elems.size()) {
        qWarning() << "[HorizontalAlignmentEdit] addLC: arcIdx out of range:" << arcIdx;
        return -1;
    }
    if (m_elems[tangentIdx].type != EditableElementType::Tangent) {
        qWarning() << "[HorizontalAlignmentEdit] addLC: element at tangentIdx is not a Tangent";
        return -1;
    }
    if (m_elems[arcIdx].type != EditableElementType::CircularArc) {
        qWarning() << "[HorizontalAlignmentEdit] addLC: element at arcIdx is not a CircularArc";
        return -1;
    }
    if (m_elems[arcIdx].mode != ConstraintMode::Fixed) {
        qWarning() << "[HorizontalAlignmentEdit] addLC: arc must be Fixed (arcIdx=" << arcIdx << ")";
        return -1;
    }
    if (m_elems[tangentIdx].mode != ConstraintMode::Fixed) {
        qWarning() << "[HorizontalAlignmentEdit] addLC: tangent must be Fixed (tangentIdx=" << tangentIdx << ")";
        return -1;
    }

    const QJsonObject before = parentDocument() ? parentDocument()->toJson() : QJsonObject();

    // Build the SpiralIn element (mode=Floating; Ls=0, solver will compute it)
    EditableElement spiralIn;
    spiralIn.type             = EditableElementType::SpiralIn;
    spiralIn.mode             = ConstraintMode::Floating;
    spiralIn.radius           = std::abs(m_elems[arcIdx].radius);
    spiralIn.length           = 0.0;   // unknown — solver sets it after solve()
    spiralIn.tangentIdxBefore = tangentIdx;
    spiralIn.tangentIdxAfter  = -1;    // not used for LC (no exit tangent)
    spiralIn.spiralType1      = spiralType;
    spiralIn.spiralType2      = spiralType;

    // Insert before arcIdx so that pass 2c finds: elems[spiralInIdx+1] == Fixed Arc
    m_elems.insert(arcIdx, spiralIn);
    const int spiralInIdx = arcIdx;   // inserted at this position
    // arcIdx is now arcIdx+1 in the list (shifted by insertion)

    // Fix up all stored indices that pointed to positions >= arcIdx
    // (the inserted spiral shifted everything at arcIdx and beyond by +1)
    const int insertedAt = arcIdx;
    for (int i = 0; i < m_elems.size(); ++i) {
        if (i == spiralInIdx) continue;  // the newly inserted element itself
        auto& e = m_elems[i];
        if (e.tangentIdxBefore >= insertedAt) ++e.tangentIdxBefore;
        if (e.tangentIdxAfter  >= insertedAt) ++e.tangentIdxAfter;
    }
    // Also fix spiralIn's own tangentIdxBefore if tangentIdx >= insertedAt
    // (tangentIdx was passed in before insertion, so if tangentIdx >= arcIdx it shifted)
    if (tangentIdx >= insertedAt) {
        m_elems[spiralInIdx].tangentIdxBefore = tangentIdx + 1;
    }

    if (parentDocument()) {
        command::AlignmentEditCommand::push(parentDocument(),
                                            before, parentDocument()->toJson(),
                                            "Add LC (Line-Clothoid)");
    }
    return spiralInIdx;
}

// ============================================================================
//  addCA  ─ Fixed CircularArc → Clothoid(未知長度) → Fixed Tangent
//
//  The SpiralOut element is inserted immediately AFTER the Fixed Arc so that
//  Pass 2d in solve() finds it via  arcI = i - 1.
// ============================================================================

int HorizontalAlignmentEdit::addCA(int arcIdx, int tangentIdx,
                                    SpiralType spiralType)
{
    if (arcIdx < 0 || arcIdx >= m_elems.size()) {
        qWarning() << "[HorizontalAlignmentEdit] addCA: arcIdx out of range:" << arcIdx;
        return -1;
    }
    if (tangentIdx < 0 || tangentIdx >= m_elems.size()) {
        qWarning() << "[HorizontalAlignmentEdit] addCA: tangentIdx out of range:" << tangentIdx;
        return -1;
    }
    if (m_elems[arcIdx].type != EditableElementType::CircularArc) {
        qWarning() << "[HorizontalAlignmentEdit] addCA: element at arcIdx is not a CircularArc";
        return -1;
    }
    if (m_elems[arcIdx].mode != ConstraintMode::Fixed) {
        qWarning() << "[HorizontalAlignmentEdit] addCA: arc must be Fixed (arcIdx=" << arcIdx << ")";
        return -1;
    }
    if (m_elems[tangentIdx].type != EditableElementType::Tangent) {
        qWarning() << "[HorizontalAlignmentEdit] addCA: element at tangentIdx is not a Tangent";
        return -1;
    }
    if (m_elems[tangentIdx].mode != ConstraintMode::Fixed) {
        qWarning() << "[HorizontalAlignmentEdit] addCA: tangent must be Fixed (tangentIdx=" << tangentIdx << ")";
        return -1;
    }

    const QJsonObject before = parentDocument() ? parentDocument()->toJson() : QJsonObject();

    // Build the SpiralOut element
    EditableElement spiralOut;
    spiralOut.type             = EditableElementType::SpiralOut;
    spiralOut.mode             = ConstraintMode::Floating;
    spiralOut.radius           = std::abs(m_elems[arcIdx].radius);
    spiralOut.length           = 0.0;   // unknown — solver sets it
    spiralOut.tangentIdxBefore = -1;    // not used for CA
    spiralOut.tangentIdxAfter  = tangentIdx;
    spiralOut.spiralType1      = spiralType;
    spiralOut.spiralType2      = spiralType;

    // Insert immediately AFTER arcIdx so pass 2d finds: elems[spiralOutIdx-1] == Fixed Arc
    const int insertPos    = arcIdx + 1;
    m_elems.insert(insertPos, spiralOut);
    const int spiralOutIdx = insertPos;

    // Fix up stored indices shifted by the insertion
    for (int i = 0; i < m_elems.size(); ++i) {
        if (i == spiralOutIdx) continue;
        auto& e = m_elems[i];
        if (e.tangentIdxBefore >= insertPos) ++e.tangentIdxBefore;
        if (e.tangentIdxAfter  >= insertPos) ++e.tangentIdxAfter;
    }
    // Fix spiralOut's own tangentIdxAfter if tangentIdx >= insertPos
    if (tangentIdx >= insertPos) {
        m_elems[spiralOutIdx].tangentIdxAfter = tangentIdx + 1;
    }

    if (parentDocument()) {
        command::AlignmentEditCommand::push(parentDocument(),
                                            before, parentDocument()->toJson(),
                                            "Add CA (Clothoid-Arc)");
    }
    return spiralOutIdx;
}

// ============================================================================
//  addACA  ─ Fixed Arc₁ → Clothoid(未知長度) → Fixed Arc₂
//
//  The SpiralIn element is inserted between arc1Idx and arc2Idx.  Its
//  tangentIdxBefore = arc1Idx and tangentIdxAfter = arc2Idx (both reference
//  CircularArc elements — Pass 2e uses this to distinguish ACA from LC/CA).
//
//  After insertion both arc indices shift if needed; the stored indices in
//  all other elements are patched accordingly.
// ============================================================================

int HorizontalAlignmentEdit::addACA(int arc1Idx, int arc2Idx,
                                     SpiralType spiralType)
{
    if (arc1Idx < 0 || arc1Idx >= m_elems.size()) {
        qWarning() << "[HorizontalAlignmentEdit] addACA: arc1Idx out of range:" << arc1Idx;
        return -1;
    }
    if (arc2Idx < 0 || arc2Idx >= m_elems.size()) {
        qWarning() << "[HorizontalAlignmentEdit] addACA: arc2Idx out of range:" << arc2Idx;
        return -1;
    }
    if (m_elems[arc1Idx].type != EditableElementType::CircularArc) {
        qWarning() << "[HorizontalAlignmentEdit] addACA: element at arc1Idx is not a CircularArc";
        return -1;
    }
    if (m_elems[arc2Idx].type != EditableElementType::CircularArc) {
        qWarning() << "[HorizontalAlignmentEdit] addACA: element at arc2Idx is not a CircularArc";
        return -1;
    }
    if (m_elems[arc1Idx].mode != ConstraintMode::Fixed) {
        qWarning() << "[HorizontalAlignmentEdit] addACA: arc1 must be Fixed (arc1Idx=" << arc1Idx << ")";
        return -1;
    }
    if (m_elems[arc2Idx].mode != ConstraintMode::Fixed) {
        qWarning() << "[HorizontalAlignmentEdit] addACA: arc2 must be Fixed (arc2Idx=" << arc2Idx << ")";
        return -1;
    }
    if (std::abs(m_elems[arc1Idx].radius) < 1e-9 ||
        std::abs(m_elems[arc2Idx].radius) < 1e-9) {
        qWarning() << "[HorizontalAlignmentEdit] addACA: arc radius ≈ 0";
        return -1;
    }
    if (std::abs(std::abs(m_elems[arc1Idx].radius) - std::abs(m_elems[arc2Idx].radius)) < 1e-6) {
        qWarning() << "[HorizontalAlignmentEdit] addACA: R1 ≈ R2 — degenerate (EggTransition undefined)";
        return -1;
    }

    const QJsonObject before = parentDocument() ? parentDocument()->toJson() : QJsonObject();

    // Build the SpiralIn element
    // tangentIdxBefore / tangentIdxAfter are overloaded here to carry arc indices.
    // Pass 2e checks that both referenced elements are CircularArc (not Tangent).
    EditableElement spiralIn;
    spiralIn.type             = EditableElementType::SpiralIn;
    spiralIn.mode             = ConstraintMode::Floating;
    spiralIn.radius           = std::abs(m_elems[arc1Idx].radius);  // entry radius
    spiralIn.length           = 0.0;   // unknown — solver sets it
    spiralIn.tangentIdxBefore = arc1Idx;   // ← Arc₁ index (not a tangent)
    spiralIn.tangentIdxAfter  = arc2Idx;   // ← Arc₂ index (not a tangent)
    spiralIn.spiralType1      = spiralType;
    spiralIn.spiralType2      = spiralType;

    // Insert between arc1Idx and arc2Idx.
    // We insert AFTER arc1Idx (i.e. at arc1Idx + 1).
    const int insertPos  = arc1Idx + 1;
    m_elems.insert(insertPos, spiralIn);
    const int spiralInIdx = insertPos;

    // Fix up all stored indices that were >= insertPos (shifted by +1)
    for (int i = 0; i < m_elems.size(); ++i) {
        if (i == spiralInIdx) continue;
        auto& e = m_elems[i];
        if (e.tangentIdxBefore >= insertPos) ++e.tangentIdxBefore;
        if (e.tangentIdxAfter  >= insertPos) ++e.tangentIdxAfter;
    }
    // Fix spiralIn's own stored arc indices if they were >= insertPos
    if (arc1Idx >= insertPos) m_elems[spiralInIdx].tangentIdxBefore = arc1Idx + 1;
    if (arc2Idx >= insertPos) m_elems[spiralInIdx].tangentIdxAfter  = arc2Idx + 1;

    if (parentDocument()) {
        command::AlignmentEditCommand::push(parentDocument(),
                                            before, parentDocument()->toJson(),
                                            "Add ACA (Arc-Clothoid-Arc)");
    }
    return spiralInIdx;
}

} // namespace railway
} // namespace aicad