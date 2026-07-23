#include "AlignmentDocument.h"
#include "AlignmentSolver.h"
#include "command/alignment/AlignmentEditCommand.h"
#include "core/Application.h"
#include "ui/UIManager.h"

#include "core/geometry/ProjectOrigin.h"
#include <QJsonArray>
#include <QMessageBox>
#include <QLineF>
#include <QtDebug>
#include <algorithm>
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

int HorizontalAlignmentEdit::insertElementsOrdered(int pos, const QVector<EditableElement>& newElems)
{
    const int count = newElems.size();
    if (count == 0) return pos;

    // Fix up existing elements' tangent references BEFORE inserting,
    // shifting anything that pointed at or past pos by +count.
    for (auto& other : m_elems) {
        if (other.tangentIdxBefore >= pos) other.tangentIdxBefore += count;
        if (other.tangentIdxAfter  >= pos) other.tangentIdxAfter  += count;
    }

    // Insert the new group, applying the same shift to any of their own
    // tangent references that pointed at or past pos (captured by the
    // caller before this call, so still using pre-shift indices).
    for (int k = 0; k < count; ++k) {
        EditableElement e = newElems[k];
        if (e.tangentIdxBefore >= pos) e.tangentIdxBefore += count;
        if (e.tangentIdxAfter  >= pos) e.tangentIdxAfter  += count;
        m_elems.insert(pos + k, e);
    }

    return pos;
}

int HorizontalAlignmentEdit::removeFloatingBetween(int tangentIdxBefore, int tangentIdxAfter)
{
    // Everything physically sitting between the two Tangent elements *is*
    // the existing floating group (AFC arc, or SCS spiral/arc/spiral) — it
    // was always inserted right after tangentIdxBefore by insertElementsOrdered().
    // Matching on position rather than re-checking each element's own stored
    // tangentIdxBefore/After avoids silently missing the old group if those
    // fields were ever out of sync with the current index layout.
    const int start = tangentIdxBefore + 1;
    const int count = tangentIdxAfter - start;   // elements strictly between them
    if (count <= 0) return 0;

    // Safety check: only remove if the whole gap is Floating. A Fixed
    // element there (e.g. a Fixed CircularArc from an LC/CA/ACA group) means
    // this isn't a plain "two tangents + floating group" gap, so leave it
    // alone rather than risk deleting something that isn't a float.
    for (int i = start; i < tangentIdxAfter; ++i) {
        if (m_elems.at(i).mode != ConstraintMode::Floating) {
            qWarning() << "[HorizontalAlignmentEdit] removeFloatingBetween:"
                       << "non-Floating element at index" << i
                       << "between tangents" << tangentIdxBefore << "and" << tangentIdxAfter
                       << "— leaving gap untouched.";
            return 0;
        }
    }

    m_elems.remove(start, count);

    // Fix up every remaining element's tangent references, mirroring the
    // shift insertElementsOrdered() applies on insert.
    for (auto& other : m_elems) {
        if (other.tangentIdxBefore >= start) other.tangentIdxBefore -= count;
        if (other.tangentIdxAfter  >= start) other.tangentIdxAfter  -= count;
    }

    return count;
}

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

int HorizontalAlignmentEdit::addFixedSpiral(EditableElementType dir, QPointF start, QPointF end,
                                            double length, SpiralType spiralType, double radius)
{
    const QJsonObject before = parentDocument() ? parentDocument()->toJson() : QJsonObject();

    EditableElement e;
    e.type        = dir;   // SpiralIn 或 SpiralOut（僅供方向標示，不影響求解）
    e.mode        = ConstraintMode::Fixed;
    e.startPI     = start;
    e.endPI       = end;
    e.length      = length;
    e.radius      = std::abs(radius);
    e.spiralType1 = spiralType;
    e.spiralType2 = spiralType;
    // tangentIdxBefore/After 維持 -1：不依附任何 Tangent，Pass 2b/2c/2e 的
    // Floating 群組偵測皆先檢查 mode==Floating，此元素為 Fixed 故必然略過。
    m_elems.append(e);

    if (parentDocument()) {
        command::AlignmentEditCommand::push(parentDocument(),
                                            before, parentDocument()->toJson(),
                                            "Add Fixed Spiral");
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

    // Validation above has already guaranteed this call will succeed, so it
    // is now safe to replace any prior AFC/SCS floating group already
    // occupying this tangent gap rather than stacking a second one on top.
    const int removed = removeFloatingBetween(tangentIdxBefore, tangentIdxAfter);
    if (removed > 0) tangentIdxAfter -= removed;

    EditableElement e;
    e.type             = EditableElementType::CircularArc;
    e.mode             = ConstraintMode::Floating;
    e.radius           = std::abs(radius);
    e.tangentIdxBefore = tangentIdxBefore;
    e.tangentIdxAfter  = tangentIdxAfter;
    // startPI / endPI left at default (0,0); AlignmentSolver fills them in.

    // Insert immediately after tangentIdxBefore so storage order matches
    // alignment order (see insertElementsOrdered() for full rationale).
    const int newIdx = insertElementsOrdered(tangentIdxBefore + 1, { e });

    if (parentDocument()) {
        command::AlignmentEditCommand::push(parentDocument(),
                                            before, parentDocument()->toJson(),
                                            "Add Floating Curve");
    }
    return newIdx;
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

    const QJsonObject before = parentDocument() ? parentDocument()->toJson() : QJsonObject();

    // Validation above has already guaranteed this call will succeed, so it
    // is now safe to replace any prior AFC/SCS floating group already
    // occupying this tangent gap rather than stacking a second one on top.
    const int removed = removeFloatingBetween(tangentIdxBefore, tangentIdxAfter);
    if (removed > 0) tangentIdxAfter -= removed;

    // ── 入螺旋 (SpiralIn) ──────────────────────────────────────────────────
    EditableElement spiralIn;
    spiralIn.type             = EditableElementType::SpiralIn;
    spiralIn.mode             = ConstraintMode::Floating;
    spiralIn.radius           = std::abs(radius);
    spiralIn.length           = std::abs(spiralLength);
    spiralIn.tangentIdxBefore = tangentIdxBefore;
    spiralIn.tangentIdxAfter  = tangentIdxAfter;

    // ── 圓弧 (CircularArc) ────────────────────────────────────────────────
    EditableElement arc;
    arc.type             = EditableElementType::CircularArc;
    arc.mode             = ConstraintMode::Floating;
    arc.radius           = std::abs(radius);
    arc.tangentIdxBefore = tangentIdxBefore;
    arc.tangentIdxAfter  = tangentIdxAfter;

    // ── 出螺旋 (SpiralOut) ────────────────────────────────────────────────
    EditableElement spiralOut;
    spiralOut.type             = EditableElementType::SpiralOut;
    spiralOut.mode             = ConstraintMode::Floating;
    spiralOut.radius           = std::abs(radius);
    spiralOut.length           = std::abs(spiralLength);
    spiralOut.tangentIdxBefore = tangentIdxBefore;
    spiralOut.tangentIdxAfter  = tangentIdxAfter;

    // Insert in alignment (chainage) order, not at the end — see
    // insertElementsOrdered() for the full rationale.
    const int spiralInIdx = insertElementsOrdered(tangentIdxBefore + 1,
                                                  { spiralIn, arc, spiralOut });

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

    const QJsonObject before = parentDocument() ? parentDocument()->toJson() : QJsonObject();

    // Validation above has already guaranteed this call will succeed, so it
    // is now safe to replace any prior AFC/SCS floating group already
    // occupying this tangent gap rather than stacking a second one on top.
    const int removed = removeFloatingBetween(tangentIdxBefore, tangentIdxAfter);
    if (removed > 0) tangentIdxAfter -= removed;

    QVector<EditableElement> group;

    // ── 入螺旋 (SpiralIn) — 僅當 L1 > 0 ────────────────────────────────────
    if (spiralLength1 > 1e-9) {
        EditableElement spiralIn;
        spiralIn.type             = EditableElementType::SpiralIn;
        spiralIn.mode             = ConstraintMode::Floating;
        spiralIn.radius           = std::abs(radius);
        spiralIn.length           = spiralLength1;
        spiralIn.tangentIdxBefore = tangentIdxBefore;
        spiralIn.tangentIdxAfter  = tangentIdxAfter;
        group.append(spiralIn);
    }

    // ── 圓弧 (CircularArc) ────────────────────────────────────────────────
    EditableElement arc;
    arc.type             = EditableElementType::CircularArc;
    arc.mode             = ConstraintMode::Floating;
    arc.radius           = std::abs(radius);
    arc.tangentIdxBefore = tangentIdxBefore;
    arc.tangentIdxAfter  = tangentIdxAfter;
    group.append(arc);

    // ── 出螺旋 (SpiralOut) — 僅當 L2 > 0 ────────────────────────────────────
    if (spiralLength2 > 1e-9) {
        EditableElement spiralOut;
        spiralOut.type             = EditableElementType::SpiralOut;
        spiralOut.mode             = ConstraintMode::Floating;
        spiralOut.radius           = std::abs(radius);
        spiralOut.length           = spiralLength2;
        spiralOut.tangentIdxBefore = tangentIdxBefore;
        spiralOut.tangentIdxAfter  = tangentIdxAfter;
        group.append(spiralOut);
    }

    // Insert in alignment (chainage) order, not at the end — see
    // insertElementsOrdered() for the full rationale.
    const int firstIdx = insertElementsOrdered(tangentIdxBefore + 1, group);

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

    // Validation above has already guaranteed this call will succeed, so it
    // is now safe to replace any prior AFC/SCS floating group already
    // occupying this tangent gap rather than stacking a second one on top.
    const int removed = removeFloatingBetween(tangentIdxBefore, tangentIdxAfter);
    if (removed > 0) tangentIdxAfter -= removed;

    QVector<EditableElement> group;

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
        group.append(spiralIn);
    }

    // ── 圓弧 (CircularArc) ────────────────────────────────────────────────
    EditableElement arc;
    arc.type             = EditableElementType::CircularArc;
    arc.mode             = ConstraintMode::Floating;
    arc.radius           = std::abs(radius);
    arc.tangentIdxBefore = tangentIdxBefore;
    arc.tangentIdxAfter  = tangentIdxAfter;
    group.append(arc);

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
        group.append(spiralOut);
    }

    // Insert in alignment (chainage) order, not at the end — see
    // insertElementsOrdered() for the full rationale.
    const int firstIdx = insertElementsOrdered(tangentIdxBefore + 1, group);

    return firstIdx;
}

// ============================================================================
//  addCompoundChain  ─  S0 C0 S1 C1 ... Sn 複合鏈結（Phase 1）
//
//  arcs.size()==1 → 直接 delegate 給既有 addSCS()，零回歸風險。
//  arcs.size()>=2 → 建立 2N+1 個 Floating EditableElement，交由
//    AlignmentSolver::solve() 內新增的 Pass 2b' 分支（偵測到 SpiralIn 之後
//    緊接不只一組 (CircularArc, SpiralOut) 就走 solveCompoundChain()）求解。
// ============================================================================
int HorizontalAlignmentEdit::addCompoundChain(int tangentIdxBefore,
                                              int tangentIdxAfter,
                                              const CompoundChainSpec& spec)
{
    // ── 驗證邊界切線 ─────────────────────────────────────────────────────────
    if (tangentIdxBefore < 0 || tangentIdxBefore >= m_elems.size()) {
        qWarning() << "[HorizontalAlignmentEdit] addCompoundChain: tangentIdxBefore out of range:"
                   << tangentIdxBefore;
        return -1;
    }
    if (tangentIdxAfter < 0 || tangentIdxAfter >= m_elems.size()) {
        qWarning() << "[HorizontalAlignmentEdit] addCompoundChain: tangentIdxAfter out of range:"
                   << tangentIdxAfter;
        return -1;
    }
    if (m_elems[tangentIdxBefore].type != EditableElementType::Tangent) {
        qWarning() << "[HorizontalAlignmentEdit] addCompoundChain: tangentIdxBefore is not a Tangent";
        return -1;
    }
    if (m_elems[tangentIdxAfter].type != EditableElementType::Tangent) {
        qWarning() << "[HorizontalAlignmentEdit] addCompoundChain: tangentIdxAfter is not a Tangent";
        return -1;
    }

    const int N = spec.arcs.size();
    if (N < 1) {
        qWarning() << "[HorizontalAlignmentEdit] addCompoundChain: spec.arcs is empty";
        return -1;
    }
    if (spec.spirals.size() != N + 1) {
        qWarning() << "[HorizontalAlignmentEdit] addCompoundChain: spec.spirals.size() must be"
                   << (N + 1) << "got" << spec.spirals.size();
        return -1;
    }
    for (int k = 0; k < N; ++k) {
        if (spec.arcs[k].radiusIsUnknown) continue;   // 佔位值，允許任意（含 0）
        if (std::abs(spec.arcs[k].radius) < 1e-9) {
            qWarning() << "[HorizontalAlignmentEdit] addCompoundChain: arcs[" << k << "].radius ≈ 0";
            return -1;
        }
    }
    // 所有緩和曲線長度皆由使用者直接給定（solveCompoundChain 為封閉解，
    // 見 CompoundChainSpec 註解）；length==0 表示省略該段緩和曲線，任何
    // 位置（含中段）皆可為 0，語意與既有 addSCS() 的 L1=0/L2=0 一致。

    // ── Phase 4：驗證「未知數」設定（最多 1 個；指定時所有弧心角須釘死）──
    int unknownCount = 0;
    for (const auto& s : spec.spirals) if (s.lengthIsUnknown) ++unknownCount;
    for (const auto& a : spec.arcs)    if (a.radiusIsUnknown) ++unknownCount;
    if (unknownCount > 1) {
        qWarning() << "[HorizontalAlignmentEdit] addCompoundChain: at most 1 unknown"
                      " (lengthIsUnknown/radiusIsUnknown) is supported, got" << unknownCount
                   << "-- only 1 closure equation (Δθ) is available, see AlignmentSolver.h"
                      " CompoundChainUnknown";
        return -1;
    }
    if (unknownCount == 1) {
        if (N == 1) {
            qWarning() << "[HorizontalAlignmentEdit] addCompoundChain: unknown solving requires"
                          " N>=2 (single-arc case delegates to addSCS(), which has no unknown"
                          " solving support)";
            return -1;
        }
        for (int k = 0; k < N; ++k) {
            if (std::abs(spec.arcs[k].centralAngle) <= 1e-12) {
                qWarning() << "[HorizontalAlignmentEdit] addCompoundChain: an unknown is specified,"
                              " so every arc's centralAngle must be pinned (non-zero); arc" << k
                           << "is left at 0 (auto-split), which absorbs Δθ and leaves no equation"
                              " to solve the unknown from";
                return -1;
            }
        }
    }

    // ── N==1：退化為既有 addSCS()，保證單弧案例零回歸風險 ───────────────────
    if (N == 1) {
        return addSCS(tangentIdxBefore, tangentIdxAfter,
                      spec.arcs[0].radius,
                      spec.spirals[0].length, spec.spirals[1].length,
                      spec.spirals[0].type,  spec.spirals[1].type);
    }

    const QJsonObject before = parentDocument() ? parentDocument()->toJson() : QJsonObject();

    // 比照 addSCS：先移除同一組邊界之間既有的 Floating 群組，避免疊加。
    const int removed = removeFloatingBetween(tangentIdxBefore, tangentIdxAfter);
    if (removed > 0) tangentIdxAfter -= removed;

    // ── 建立 2N+1 個元素：S0 C0 S1 C1 ... C(N-1) SN ────────────────────────
    //  中段緩和曲線（S1..S(N-1)）在 EditableElementType 裡沒有專屬型別，
    //  一律標記為 SpiralOut——solve() Pass 2b' 只用「SpiralIn 開頭、後面
    //  接連續 (CircularArc, SpiralOut) 配對」的結構做偵測，型別標記本身不
    //  影響幾何求解（幾何完全由 solveCompoundChain() 依 spec 決定）。
    QVector<EditableElement> group;
    group.reserve(2 * N + 1);

    {
        EditableElement s0;
        s0.type             = EditableElementType::SpiralIn;
        s0.mode             = ConstraintMode::Floating;
        s0.radius           = std::abs(spec.arcs[0].radius);
        s0.length           = spec.spirals[0].length;   // 可能為 0（省略此段緩和曲線）
        s0.tangentIdxBefore = tangentIdxBefore;
        s0.tangentIdxAfter  = tangentIdxAfter;
        s0.spiralType1      = spec.spirals[0].type;
        s0.spiralType2      = spec.spirals[0].type;
        s0.isCompoundUnknown = spec.spirals[0].lengthIsUnknown;
        group.append(s0);
    }

    for (int k = 0; k < N; ++k) {
        EditableElement arc;
        arc.type             = EditableElementType::CircularArc;
        arc.mode             = ConstraintMode::Floating;
        arc.radius           = std::abs(spec.arcs[k].radius);
        arc.centralAngle     = std::abs(spec.arcs[k].centralAngle);  // 0 = 自動平分
        arc.isCompoundUnknown = spec.arcs[k].radiusIsUnknown;
        arc.tangentIdxBefore = tangentIdxBefore;
        arc.tangentIdxAfter  = tangentIdxAfter;
        group.append(arc);

        EditableElement sNext;
        sNext.type             = EditableElementType::SpiralOut;
        sNext.mode             = ConstraintMode::Floating;
        sNext.radius           = std::abs(spec.arcs[k].radius);  // 僅供顯示參考；中段緩和曲線的雙曲率幾何由 solveCompoundChain 內部以 EggTransitionElement 處理
        sNext.length           = spec.spirals[k + 1].length;     // 可能為 0（省略此段緩和曲線）
        sNext.isCompoundUnknown = spec.spirals[k + 1].lengthIsUnknown;
        sNext.tangentIdxBefore = tangentIdxBefore;
        sNext.tangentIdxAfter  = tangentIdxAfter;
        sNext.spiralType1      = spec.spirals[k + 1].type;
        sNext.spiralType2      = spec.spirals[k + 1].type;
        group.append(sNext);
    }

    const int firstIdx = insertElementsOrdered(tangentIdxBefore + 1, group);

    if (parentDocument()) {
        command::AlignmentEditCommand::push(parentDocument(),
                                            before, parentDocument()->toJson(),
                                            QStringLiteral("Add Compound Chain (%1 arcs)").arg(N));
    }
    return firstIdx;
}

// ── seedFromRawPoints ────────────────────────────────────────────────────────
//  由稠密的 TS/SC/CS/CC/TC/ST 關鍵點序列反推可互動編輯的元素鏈。
//
//  Tangent 錨點與 IP 交點規則（詳見標頭檔註解）：
//    規則 1：兩個真正 Tangent（或 SS 虛擬零長度切線）之間夾有 C／SCS／
//            SC／CS 等曲線群組時，兩側 Tangent 的座標一律改為「兩切線
//            （依各自記錄的方位角延伸為無限直線）之交點」，而非原始資料
//            中量測到的 TS/ST 座標——這樣 Tangent 的 startPI/endPI 才是
//            真正的 IP（Intersection Point），grip／資料表看到的角點才會
//            落在設計意圖的轉折點上，而不是曲線邊界上。
//    規則 2：線形起點或終點若不是以真正 Tangent 開始/結束（即該端的 C／S
//            群組只有一側有 Tangent），將該端點本身（座標＋方位角，均為
//            量測所得，視為絕對固定）視為一條「虛擬 Tangent 直線」，與
//            相鄰的真正 Tangent 依規則 1 計算 IP，作為該群組另一側的角
//            點；虛擬直線本身固定不動（Fixed，solve() 不會移動它），只有
//            面向曲線群組那一端的角點會隨鄰近 Tangent／半徑變動而改變。
//            若該端最外側元素本身是緩和曲線（S），因為虛擬線另一側完全
//            沒有資料，無法得知原始設計上它是接續 Tangent 還是圓弧；目前
//            仍依群組內是否存在圓弧（C）成員來決定半徑來源（與內部 SCS
//            群組相同邏輯），若群組內完全沒有圓弧成員（裸露 S，無從得知
//            半徑）才會記錄警告並略過。
namespace {
SpiralType rawCurveTypeToSpiralType(const QString& s)
{
    if (s == QLatin1String("HALFSINE")) return SpiralType::HalfSine;
    if (s == QLatin1String("PARABOLA")) return SpiralType::Parabola;
    if (s == QLatin1String("CUBICJPN")) return SpiralType::CubicJPN;
    if (s == QLatin1String("CUBICECI")) return SpiralType::CubicECI;
    return SpiralType::Clothoid;
}

/**
 * @brief 兩條「點 + 方位角」定義的無限直線之交點（IP）。
 * @param ok  平行（或近似平行，無唯一交點）時回傳 false。
 */
QPointF intersectAzLines(const QPointF& p1, double az1,
                         const QPointF& p2, double az2, bool* ok)
{
    const double dx1 = std::sin(az1), dy1 = std::cos(az1);
    const double dx2 = std::sin(az2), dy2 = std::cos(az2);
    const double denom = dx1 * dy2 - dy1 * dx2;
    if (std::abs(denom) < 1e-9) {
        if (ok) *ok = false;
        return 0.5 * (p1 + p2);
    }
    const double ex = p2.x() - p1.x();
    const double ey = p2.y() - p1.y();
    const double t  = (ex * dy2 - ey * dx2) / denom;
    if (ok) *ok = true;
    return QPointF(p1.x() + t * dx1, p1.y() + t * dy1);
}
} // namespace

bool HorizontalAlignmentEdit::seedFromRawPoints(const QVector<AlignmentPoint>& rawPts)
{
    if (!m_elems.isEmpty()) return false;   // 已有資料，不覆蓋
    if (rawPts.size() < 2)  return false;

    // 建置期間不寫入 Undo（避免匯入直接灌爆 undo stack）；完成後才還原。
    AlignmentDocument* savedParent = m_parentDoc;
    m_parentDoc = nullptr;

    struct BufItem   { QChar elemType; int ptIdx; };
    struct GroupSpec { QVector<BufItem> items; int anchorBefore; int anchorAfter; };

    // 一個 Anchor 代表一條「Tangent 直線」的錨點：
    //   pt/azimuth   — 用來與鄰近 Anchor 計算 IP 的（點＋方位角）。
    //   ownStart/End — 該側沒有曲線群組可算 IP 時的備援端點：
    //                    真正 Tangent（isRealTangent=true）用其量測到的
    //                    實際起訖點；SS／首尾虛擬錨點則用同一個點。
    struct AnchorSpec {
        int     ptIdx = -1;
        QPointF pt;
        double  azimuth = 0.0;
        QPointF ownStart;
        QPointF ownEnd;
        bool    isRealTangent = false;
        // 僅線形起訖點的虛擬建構線適用：另一側（不在檔案資料範圍內）是否
        // 為圓弧（規則 2 的 T/C 判斷）。SS／真正 Tangent 恆為 false。
        bool    boundaryIsArc = false;
        // 是否為兩段緩和曲線直接相接的 SS 交會點（迴圈內偵測 tsc=="SS"
        // 時建立）。與線形起訖點的邊界虛擬 Tangent 不同：SS 兩側都緊鄰著
        // 曲線群組，錨點座標／方位角本身就是實際量測到的資料（相接點本
        // 身就是精確已知的），不是靠外推得到的邊界猜測值，Pass 2a 據此
        // 走精度更高的專用路徑（見下方）。
        bool    isSS = false;
    };

    QVector<AnchorSpec> anchors;
    QVector<GroupSpec>  groups;
    QVector<BufItem>    curBuffer;
    int lastAnchorIdx = -1;

    auto ptXY = [](const AlignmentPoint& p) { return QPointF(p.easting, p.northing); };

    auto pushPointAnchor = [&](int ptIdx) -> int {
        AnchorSpec a;
        a.ptIdx    = ptIdx;
        a.pt       = ptXY(rawPts[ptIdx]);
        a.azimuth  = rawPts[ptIdx].azimuth;
        a.ownStart = a.pt;
        a.ownEnd   = a.pt;
        a.isRealTangent = false;
        anchors.append(a);
        return anchors.size() - 1;
    };

    auto pushTangentAnchor = [&](int startIdx, int endIdx) -> int {
        AnchorSpec a;
        a.ptIdx    = startIdx;
        a.pt       = ptXY(rawPts[startIdx]);
        a.azimuth  = rawPts[startIdx].azimuth;
        a.ownStart = ptXY(rawPts[startIdx]);
        a.ownEnd   = ptXY(rawPts[endIdx]);
        a.isRealTangent = true;
        anchors.append(a);
        return anchors.size() - 1;
    };

    auto flushToAnchor = [&](int newAnchorIdx) {
        if (!curBuffer.isEmpty()) {
            groups.append({ curBuffer, lastAnchorIdx, newAnchorIdx });
            curBuffer.clear();
        }
        lastAnchorIdx = newAnchorIdx;
    };

    const int n = rawPts.size();

    // 開頭若非以真正 Tangent 起始，且不是 SS（由迴圈內的 SS 分支處理），
    // 先補上一個「虛擬 Tangent 直線」錨點（規則 2），固定於首點座標＋
    // 方位角，供第一段曲線群組計算 IP。規則 2：讀取首點 tsc 的第一碼
    // （抵達本點的元素型別，即檔案資料範圍外的「虛擬」鄰居）判斷該建構
    // 線另一側是 T 還是 C。
    const AlignmentPoint& firstPt = rawPts.first();
    const bool firstIsRealTangent = firstPt.tsc.size() >= 2 && firstPt.tsc[1] == QChar('T');
    const bool firstIsSS          = firstPt.tsc == QLatin1String("SS");
    if (!firstIsRealTangent && !firstIsSS) {
        lastAnchorIdx = pushPointAnchor(0);
        anchors[lastAnchorIdx].boundaryIsArc =
            (firstPt.tsc.size() >= 1 && firstPt.tsc[0] == QChar('C'));
    }

    // ── Pass 1：掃描關鍵點，建立 Anchor／曲線群組清單（尚未建立元素）───────
    for (int i = 0; i < n - 1; ++i) {
        const AlignmentPoint& cur = rawPts[i];

        if (cur.tsc == QLatin1String("SS")) {
            // 兩段緩和曲線在此直接相接：先把目前累積的曲線群組收尾到這個
            // 新的零長度虛擬 Tangent 錨點，再從這個錨點開始累積下一段。
            const int ssAnchor = pushPointAnchor(i);
            anchors[ssAnchor].isSS = true;
            flushToAnchor(ssAnchor);
        }

        if (cur.tsc.size() < 2) {
            qWarning() << "[HorizontalAlignmentEdit] seedFromRawPoints: invalid"
                          " TSC code at raw point" << i << "-- skipped";
            continue;
        }
        const QChar elemType = cur.tsc[1];

        if (elemType == QChar('T')) {
            if (lastAnchorIdx >= 0 && curBuffer.isEmpty() && anchors[lastAnchorIdx].isRealTangent) {
                // 與上一個真正 Tangent 直接相連（中間沒有曲線群組）：視為
                // 同一段直線的延伸，只更新其備援終點，不新增 Anchor。
                anchors[lastAnchorIdx].ownEnd = ptXY(rawPts[i + 1]);
            } else {
                const int tanAnchor = pushTangentAnchor(i, i + 1);
                flushToAnchor(tanAnchor);
            }
        } else {
            curBuffer.append({ elemType, i });
        }
    }

    // 收尾：若序列不是以真正 Tangent 結束（仍有累積中的曲線群組），補上
    // 末端的虛擬 Tangent 直線錨點（規則 2）。規則 2：讀取末點 tsc 的第二碼
    // （離開本點的元素型別，即檔案資料範圍外的「虛擬」鄰居）判斷該建構
    // 線另一側是 T 還是 C。
    if (!curBuffer.isEmpty()) {
        const AlignmentPoint& lastPt = rawPts.last();
        const int endAnchor = pushPointAnchor(n - 1);
        anchors[endAnchor].boundaryIsArc =
            (lastPt.tsc.size() >= 2 && lastPt.tsc[1] == QChar('C'));
        flushToAnchor(endAnchor);
    }

    if (anchors.isEmpty()) {
        m_parentDoc = savedParent;
        return false;
    }

    // ── Pass 2a：計算每個 Anchor 兩側的角點（規則 1／2 的 IP 交點）───────
    QVector<bool> groupBefore(anchors.size(), false), groupAfter(anchors.size(), false);
    for (const GroupSpec& g : groups) {
        if (g.anchorBefore >= 0) groupAfter[g.anchorBefore] = true;
        if (g.anchorAfter  >= 0) groupBefore[g.anchorAfter] = true;
    }

    QVector<QPointF> cornerBefore(anchors.size()), cornerAfter(anchors.size());
    for (int k = 0; k < anchors.size(); ++k) {
        if (anchors[k].isSS) {
            // SS 交會點：兩側都緊鄰曲線群組，理論上 cornerBefore/cornerAfter
            // 應該重合於同一點（緩和曲線在此直接相接，中間沒有直線）。但
            // 若各自獨立用 intersectAzLines() 對前一個/後一個 Anchor 做線
            // 交點運算，會把「鄰近 Tangent 的量測誤差」也牽連進來，兩次
            // 交點算出來的結果不會恰好相同，於是在兩段緩和曲線中間插入一
            // 小段本不存在的直線（長度通常只有數公釐～數公分，但在圖面
            // 上看得出來）。
            //
            // SS 點本身的座標／方位角是原始資料裡最精確、最直接量測到的
            // 值（不是外推出來的），沒有理由捨棄它去換算一個精度更差的
            // 交點。因此這裡不做 intersectAzLines()，直接把 cornerBefore／
            // cornerAfter 都釘在 SS 點自己的座標上；為了讓 addFixedTangent()
            // 建出的 Fixed Tangent 仍有明確方向（azimuthOf() 需要頭尾兩點
            // 不同，否則退化成 atan2(0,0)=0，方位角會整個錯掉），沿著 SS
            // 點自己的方位角外插一個遠低於任何實際繪圖／資料表顯示精度
            // 的極小位移（1 微米），實務上等同於同一點。
            constexpr double kSSEpsilon = 1.0e-6;
            const QPointF unit(std::sin(anchors[k].azimuth), std::cos(anchors[k].azimuth));
            cornerBefore[k] = anchors[k].pt;
            cornerAfter[k]  = anchors[k].pt + kSSEpsilon * unit;
            continue;
        }

        if (groupBefore[k] && k > 0) {
            bool ok = false;
            QPointF ip = intersectAzLines(anchors[k - 1].pt, anchors[k - 1].azimuth,
                                          anchors[k].pt,     anchors[k].azimuth, &ok);
            if (!ok) {
                qWarning() << "[HorizontalAlignmentEdit] seedFromRawPoints: tangents"
                              " parallel around raw point" << anchors[k].ptIdx
                           << "-- IP undefined, falling back to raw boundary point";
                ip = anchors[k].ownStart;
            }
            cornerBefore[k] = ip;
        } else {
            cornerBefore[k] = anchors[k].ownStart;
        }

        if (groupAfter[k] && k + 1 < anchors.size()) {
            bool ok = false;
            QPointF ip = intersectAzLines(anchors[k].pt,     anchors[k].azimuth,
                                          anchors[k + 1].pt, anchors[k + 1].azimuth, &ok);
            if (!ok) {
                qWarning() << "[HorizontalAlignmentEdit] seedFromRawPoints: tangents"
                              " parallel around raw point" << anchors[k].ptIdx
                           << "-- IP undefined, falling back to raw boundary point";
                ip = anchors[k].ownEnd;
            }
            cornerAfter[k] = ip;
        } else {
            cornerAfter[k] = anchors[k].ownEnd;
        }
    }

    // ── Pass 2b：依角點建立 Fixed Tangent 元素 ───────────────────────────
    QVector<int> tangentElemIdx(anchors.size(), -1);
    for (int k = 0; k < anchors.size(); ++k) {
        tangentElemIdx[k] = addFixedTangent(cornerBefore[k], cornerAfter[k]);
        // 非真正 Tangent（SS 交會點／線形起訖點的虛擬建構線）：標記
        // isConstructionLine，並記錄規則 2 的 T/C 判斷（僅線形起訖點適用，
        // SS 恆為 false）。
        if (!anchors[k].isRealTangent) {
            EditableElement& te = m_elems[tangentElemIdx[k]];
            te.isConstructionLine = true;
            te.constructionIsArc  = anchors[k].boundaryIsArc;
            te.isSSJunction        = anchors[k].isSS;
        }
    }

    // ── Pass 2c：依附曲線群組到對應的 Fixed Tangent 之間 ──────────────────
    // 無法對應的複合／Egg 型態（cCount>=2）退化為個別 Fixed CircularArc
    // （略過中間的 Egg 緩和曲線並記錄警告）。
    //
    // 重要：addSCS()/addFloatingCurve() 內部透過 insertElementsOrdered()
    // 把新元素插入 tanBefore+1 的位置，這會把「插入點之後」所有既有元素在
    // m_elems 裡的實際 index 往後推移。insertElementsOrdered() 只會修正
    // m_elems 內每個元素自身的 tangentIdxBefore/After 欄位，並不知道、也
    // 無法觸及這裡的區域變數 tangentElemIdx（anchor index -> m_elems
    // index 的對照表）。若不手動同步，第一個曲線群組建立後，
    // tangentElemIdx 裡「插入點之後」的每一筆都會過期一格（或多格），導致
    // 後續群組（例如線形中第二段以後的彎道）附掛到錯的元素上（往往落在
    // 剛插入的 Floating CircularArc/Spiral 本身，型別檢查失敗，
    // addFloatingCurve()/addSCS() 直接回傳 -1 並記錄警告，該曲線群組於是
    // 整段被靜默略過，既不會出現在資料表也不會有 grip）。因此每次呼叫
    // 完 addSCS()/addFloatingCurve() 後，都必須依實際插入的元素數量同步
    // 更新 tangentElemIdx，才能讓後面的群組取得正確的 tangent index。
    for (const GroupSpec& g : groups) {
        const QVector<BufItem>& buf = g.items;
        if (buf.isEmpty()) continue;

        const int tanBefore = (g.anchorBefore >= 0) ? tangentElemIdx[g.anchorBefore] : -1;
        const int tanAfter  = (g.anchorAfter  >= 0) ? tangentElemIdx[g.anchorAfter]  : -1;
        if (tanBefore < 0 || tanAfter < 0) {
            qWarning() << "[HorizontalAlignmentEdit] seedFromRawPoints: curve group"
                          " without a bounding tangent -- skipped at raw point"
                       << buf.first().ptIdx;
            continue;
        }

        const int cCount = static_cast<int>(std::count_if(buf.begin(), buf.end(),
            [](const BufItem& b) { return b.elemType == QChar('C'); }));

        if (cCount == 1) {
            // 標準型態：0~1 段入螺旋 + 1 段圓弧 + 0~1 段出螺旋
            // （TC/CT、TS-SC-CS-ST 及其不對稱組合 SC/CS；規則 2 的邊界
            //  群組同樣適用 -- 半徑一律取自群組內的圓弧成員）。
            int cPos = -1;
            for (int k = 0; k < buf.size(); ++k)
                if (buf[k].elemType == QChar('C')) { cPos = k; break; }

            const AlignmentPoint& cPt = rawPts[buf[cPos].ptIdx];
            const double radius = std::abs(cPt.radius);
            if (radius < 1e-6) {
                qWarning() << "[HorizontalAlignmentEdit] seedFromRawPoints:"
                              " degenerate radius at raw point" << buf[cPos].ptIdx
                           << "-- curve group skipped";
                continue;
            }

            double L1 = 0.0, L2 = 0.0;
            SpiralType t1 = SpiralType::Clothoid, t2 = SpiralType::Clothoid;
            if (cPos - 1 >= 0 && buf[cPos - 1].elemType == QChar('S')) {
                const AlignmentPoint& sPt = rawPts[buf[cPos - 1].ptIdx];
                L1 = sPt.length;
                t1 = rawCurveTypeToSpiralType(sPt.curveType);
            }
            if (cPos + 1 < buf.size() && buf[cPos + 1].elemType == QChar('S')) {
                const AlignmentPoint& sPt = rawPts[buf[cPos + 1].ptIdx];
                L2 = sPt.length;
                t2 = rawCurveTypeToSpiralType(sPt.curveType);
            }

            // 兩端 Tangent 夾角（真實轉角 Δ = 圓弧弧心角 + 緩和曲線各自的
            // L/(2R) 轉角，直接由量測到的半徑／弧長算出，不經過方位角相減
            // 再 fold 到 (-π, π] 的正規化步驟）若 >= 180 度，addSCS() 依附
            // 的「切線交點」重建法會失效：T = R·tan(Δ/2) 在 Δ→180° 時發散
            // （交點跑到無窮遠或錯誤的一側），無法用來定位 Floating 圓弧。
            // 此時改為忠實使用量測到的圓弧兩端座標／圓心直接建立 Fixed
            // CircularArc（fixedArc），若群組內仍有緩和曲線，則以
            // addLC()/addCA()（Floating Spiral，長度由 solver 反解，仿
            // Line-Clothoid／Clothoid-Arc 既有模式）分別接在 Fixed Tangent
            // 與這段 Fixed Arc 之間（floatSpiral），而非併入 addSCS() 的
            // Floating SCS 群組。
            const double arcCentralAngle = std::abs(cPt.length) / radius;
            const double spiralAngle1    = (L1 > 1e-9) ? L1 / (2.0 * radius) : 0.0;
            const double spiralAngle2    = (L2 > 1e-9) ? L2 / (2.0 * radius) : 0.0;
            const double totalDeflection = arcCentralAngle + spiralAngle1 + spiralAngle2;

            if (totalDeflection >= M_PI - 1e-9) {
                const AlignmentPoint& p0 = rawPts[buf[cPos].ptIdx];
                const AlignmentPoint& p1 = rawPts[buf[cPos].ptIdx + 1];
                CircularArcElement arcElem(p0.radius);
                Placement place{ p0.chainage, p0.easting, p0.northing, p0.azimuth };
                arcElem.setPlacement(place);
                arcElem.setLength(p0.length);

                int arcIdx = addFixedCurve(ptXY(p0), ptXY(p1), arcElem.centreXY(), radius);

                if (arcIdx >= 0 && L1 > 1e-9) {
                    const int lcIdx = addLC(tanBefore, arcIdx, t1);
                    if (lcIdx >= 0) ++arcIdx;   // spiralIn inserted immediately before the arc
                }
                if (arcIdx >= 0 && L2 > 1e-9) {
                    addCA(arcIdx, tanAfter, t2);
                }

                // Both addFixedCurve() and addLC()/addCA() here always insert
                // at (or push past) the current end of m_elems -- by
                // construction every entry in tangentElemIdx still refers to
                // a strictly earlier index, so unlike the addSCS()/
                // addCompoundChain() branches, no tangentElemIdx shift is
                // required for later groups in this loop.
                qDebug() << "[HorizontalAlignmentEdit] seedFromRawPoints:"
                            " deflection angle >= 180 deg at raw point"
                         << buf.first().ptIdx << "-- reconstructed as Fixed"
                            " CircularArc" << ((L1 > 1e-9 || L2 > 1e-9)
                                ? "+ floating transition spiral(s)" : "")
                         << "instead of tangent-intersection Floating SCS";
                continue;
            }

            {
                const int sizeBefore = m_elems.size();
                addSCS(tanBefore, tanAfter, radius, L1, L2, t1, t2);
                const int inserted = m_elems.size() - sizeBefore;
                if (inserted > 0) {
                    // Mirror the shift insertElementsOrdered() just applied
                    // (insertion position = tanBefore + 1) onto our own
                    // anchor->m_elems index table, or every later group in
                    // this loop will resolve to the wrong element (see the
                    // Pass 2c comment above).
                    const int insertPos = tanBefore + 1;
                    for (int& idx : tangentElemIdx) {
                        if (idx >= insertPos) idx += inserted;
                    }
                }
            }
            continue;
        }

        if (cCount == 0) {
            // 裸露緩和曲線（群組內完全沒有圓弧成員）：常見於規則 2 的邊界
            // 群組，也就是這個曲線群組的其中一端是線形起訖點的虛擬建構線
            // （isConstructionLine），另一側完全沒有資料。查詢兩端建構線是
            // 否已標記「另一側是圓弧」（constructionIsArc，規則 2 的 T/C
            // 判斷），據此決定能否、以及如何建立。
            const bool beforeIsArcConstruction =
                m_elems[tanBefore].isConstructionLine && m_elems[tanBefore].constructionIsArc;
            const bool afterIsArcConstruction =
                m_elems[tanAfter].isConstructionLine && m_elems[tanAfter].constructionIsArc;

            if (!beforeIsArcConstruction && !afterIsArcConstruction) {
                // 兩端都不是「已知另一側是圓弧」的建構線：可能單純是內部
                // 的裸露 S（極罕見的 T-S-T 型態），或建構線另一側其實是
                // 切線（constructionIsArc=false）——這兩種情況都沒有圓弧
                // 半徑來源，無從重建。
                qWarning() << "[HorizontalAlignmentEdit] seedFromRawPoints: bare"
                              " spiral group with no circular arc member at raw point"
                           << buf.first().ptIdx << "-- radius/orientation unknown"
                              " (neither boundary is an arc-type construction line),"
                              " skipped";
                continue;
            }
            if (buf.size() != 1) {
                // 理論上規則 2 的邊界群組裸露時只會有單一 S 成員（若有第二
                // 個 S 應該已經被 SS 錨點切開）；多於一個成員代表遇到未預期
                // 的組合，保守起見不猜測，記錄警告並略過。
                qWarning() << "[HorizontalAlignmentEdit] seedFromRawPoints: bare"
                              " spiral group at raw point" << buf.first().ptIdx
                           << "has" << buf.size() << "elements (expected exactly 1)"
                              " -- unexpected pattern, skipped";
                continue;
            }

            // 規則 2（S 的情況）：已確認另一側是圓弧型建構線。半徑本應存在
            // 該虛擬圓弧上，但緩和曲線關鍵點自身的 radius 欄位依慣例恆為
            // 0（見標頭檔註解）；仍防禦性地嘗試讀取，若原始資料例外地有
            // 記錄則直接使用，否則已無其他來源可查，只能記錄警告並略過。
            const BufItem& sItem = buf.first();
            const AlignmentPoint& sPt = rawPts[sItem.ptIdx];
            const double radius = std::abs(sPt.radius);
            if (radius < 1e-6) {
                qWarning() << "[HorizontalAlignmentEdit] seedFromRawPoints: bare"
                              " spiral at raw point" << sItem.ptIdx << "connects to a"
                              " virtual (off-file) circular arc (construction line"
                              " T/C recorded), but its radius is not present in the"
                              " source data -- cannot reconstruct geometry, skipped";
                continue;
            }

            // 依虛擬建構線在群組哪一側，決定方向標示（僅供顯示，不影響
            // 幾何）：建構線在前（線形起點）→ SpiralIn；在後（線形終點）
            // → SpiralOut。以 Fixed 元素直接記錄量測到的兩端座標，不依附
            // 任何 Tangent，也不會被 Floating 群組偵測誤判。
            const bool constructionIsBefore = beforeIsArcConstruction;
            const QPointF p0 = ptXY(sPt);
            const QPointF p1 = ptXY(rawPts[sItem.ptIdx + 1]);
            const SpiralType stype = rawCurveTypeToSpiralType(sPt.curveType);
            const EditableElementType dir = constructionIsBefore
                ? EditableElementType::SpiralIn
                : EditableElementType::SpiralOut;
            addFixedSpiral(dir, p0, p1, sPt.length, stype, radius);
            continue;
        }

        // cCount >= 2：複合弧（CC，可能夾帶 Egg 緩和曲線）。優先嘗試以
        // CompoundChainSpec 精確重建為單一可編輯的 Floating 複合曲線特徵
        // （Phase 6）：每段圓弧的真實弧心角（= 該弧 AlignmentPoint.length /
        // radius，直接來自量測資料）透過 EditableElement::centralAngle 釘
        // 死，讓 solveCompoundChain() 忠實重現原始弧心角分配，而不是套用
        // 「平分剩餘轉角」的近似值——後者只在使用者手動新建複合曲線、沒有
        // 既有量測資料可參考時才適用。
        //
        // 若 buf 的排列不符合預期的交錯型態（例如非嚴格 S?/C 交替、半徑缺
        // 漏，或 addCompoundChain 本身失敗），保留原本「個別 Fixed
        // CircularArc、略過中間緩和曲線」的降級路徑，確保永遠有結果而非
        //整段憑空消失。
        {
            bool patternOk = true;
            for (const BufItem& b : buf) {
                if (b.elemType != QChar('S') && b.elemType != QChar('C')) { patternOk = false; break; }
            }
            if (patternOk) {
                for (int k = 0; k + 1 < buf.size(); ++k) {
                    if (buf[k].elemType == QChar('S') && buf[k + 1].elemType == QChar('S')) {
                        patternOk = false; break;
                    }
                }
            }

            if (patternOk) {
                CompoundChainSpec spec;
                spec.arcs.resize(cCount);
                spec.spirals.resize(cCount + 1);   // 預設 length=0（省略）

                bool geomOk = true;
                int  cursor = 0;
                if (buf[cursor].elemType == QChar('S')) {
                    const AlignmentPoint& sPt = rawPts[buf[cursor].ptIdx];
                    spec.spirals[0].length = sPt.length;
                    spec.spirals[0].type   = rawCurveTypeToSpiralType(sPt.curveType);
                    ++cursor;
                }
                for (int a = 0; a < cCount && geomOk; ++a) {
                    if (cursor >= buf.size() || buf[cursor].elemType != QChar('C')) {
                        geomOk = false; break;
                    }
                    const AlignmentPoint& cPt = rawPts[buf[cursor].ptIdx];
                    const double R = std::abs(cPt.radius);
                    if (R < 1e-6) { geomOk = false; break; }
                    spec.arcs[a].radius       = R;
                    spec.arcs[a].centralAngle = std::abs(cPt.length) / R;  // 真實弧心角（釘死）
                    ++cursor;
                    if (cursor < buf.size() && buf[cursor].elemType == QChar('S')) {
                        const AlignmentPoint& sPt = rawPts[buf[cursor].ptIdx];
                        spec.spirals[a + 1].length = sPt.length;
                        spec.spirals[a + 1].type   = rawCurveTypeToSpiralType(sPt.curveType);
                        ++cursor;
                    }
                    // 下一個緊接著又是 'C'（無中段緩和曲線）：spirals[a+1]
                    // 維持預設 0（省略），語意上兩弧直接相切。
                }
                if (geomOk && cursor == buf.size()) {
                    const int sizeBefore = m_elems.size();
                    const int idx = addCompoundChain(tanBefore, tanAfter, spec);
                    const int inserted = m_elems.size() - sizeBefore;
                    if (idx >= 0 && inserted > 0) {
                        // 比照 cCount==1 分支：同步 tangentElemIdx 位移。
                        const int insertPos = tanBefore + 1;
                        for (int& tidx : tangentElemIdx) {
                            if (tidx >= insertPos) tidx += inserted;
                        }
                        qDebug() << "[HorizontalAlignmentEdit] seedFromRawPoints: compound"
                                    " curve (CC) at raw point" << buf.first().ptIdx
                                 << "reconstructed as" << cCount << "-arc CompoundChainSpec,"
                                    " idx =" << idx;
                        continue;   // 已處理，跳過下面的降級路徑
                    }
                }
                // addCompoundChain 失敗或幾何資料不完整：繼續往下走降級路徑。
            }
        }

        qWarning() << "[HorizontalAlignmentEdit] seedFromRawPoints: compound"
                      " curve (CC / Egg) at raw point" << buf.first().ptIdx
                   << "could not be reconstructed as a CompoundChainSpec"
                      " (unexpected pattern or addCompoundChain failure) --"
                      " falling back to individual Fixed CircularArc,"
                      " transition curve(s) between them skipped";
        for (const BufItem& item : buf) {
            if (item.elemType != QChar('C')) continue;
            const AlignmentPoint& p0 = rawPts[item.ptIdx];
            const AlignmentPoint& p1 = rawPts[item.ptIdx + 1];
            CircularArcElement arcElem(p0.radius);
            Placement place{ p0.chainage, p0.easting, p0.northing, p0.azimuth };
            arcElem.setPlacement(place);
            arcElem.setLength(p0.length);
            addFixedCurve(ptXY(p0), ptXY(p1), arcElem.centreXY(), std::abs(p0.radius));
        }
    }

    solve();
    m_parentDoc = savedParent;
    return true;
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

void HorizontalAlignmentEdit::movePIDirect(int idx, QPointF newPos)
{
    if (idx < 0 || idx >= m_elems.size()) return;
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
}

void HorizontalAlignmentEdit::moveStartPIDirect(int idx, QPointF newPos)
{
    if (idx < 0 || idx >= m_elems.size()) return;
    auto& e = m_elems[idx];
    e.startPI = newPos;
    if (e.type == EditableElementType::Tangent)
        e.length = QLineF(newPos, e.endPI).length();
    e.solved = false;
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

void HorizontalAlignmentEdit::setLength(int idx, double length)
{
    if (idx < 0 || idx >= m_elems.size()) return;
    m_elems[idx].length = std::abs(length);
    m_elems[idx].solved = false;
}

void HorizontalAlignmentEdit::setSpiralType(int idx, SpiralType type, bool isExit)
{
    if (idx < 0 || idx >= m_elems.size()) return;
    auto& e = m_elems[idx];
    if (e.type == EditableElementType::SpiralIn) {
        // SCS 群組：SpiralIn 同時持有 spiralType1（entry）和 spiralType2（exit）
        if (isExit) e.spiralType2 = type;
        else        e.spiralType1 = type;
    } else if (e.type == EditableElementType::SpiralOut) {
        // CA 群組：SpiralOut 持有 spiralType2（exit）
        e.spiralType2 = type;
    } else {
        return;
    }
    e.solved = false;
}

void HorizontalAlignmentEdit::removeElement(int idx)
{
    if (idx < 0 || idx >= m_elems.size()) return;
    m_elems.removeAt(idx);
}

bool HorizontalAlignmentEdit::eraseElementAt(int elemIndex)
{
    if (elemIndex < 0 || elemIndex >= m_elems.size()) return false;

    const EditableElement el = m_elems.at(elemIndex);   // copy — m_elems mutates below

    const QJsonObject before = parentDocument() ? parentDocument()->toJson() : QJsonObject();

    if (el.mode == ConstraintMode::Floating) {
        // Floating 元素一定屬於某個群組（AFC 單弧，或 SCS 入螺旋／弧／
        // 出螺旋三者共用同一組 tangentIdxBefore/After）；整組一起移除，
        // 讓兩側 Fixed Tangent 直接以直線相接。
        if (el.tangentIdxBefore < 0 || el.tangentIdxAfter < 0) return false;
        const int removed = removeFloatingBetween(el.tangentIdxBefore, el.tangentIdxAfter);
        if (removed <= 0) return false;
    } else {
        // Fixed 元素：只有在沒有任何其他元素以它為 tangentIdxBefore/After
        // 時才能單獨刪除，否則會讓依附在它上面的 Floating 群組失去依附
        // 對象、幾何無法求解。
        for (const auto& other : m_elems) {
            if (other.tangentIdxBefore == elemIndex || other.tangentIdxAfter == elemIndex) {
                qWarning() << "[HorizontalAlignmentEdit] eraseElementAt:"
                              " element" << elemIndex
                           << "still anchors a floating curve group"
                              " -- erase that curve first.";
                return false;
            }
        }
        removeElement(elemIndex);
    }

    if (parentDocument()) {
        const QJsonObject after = parentDocument()->toJson();
        command::AlignmentEditCommand::push(parentDocument(), before, after, "Erase Alignment Element");
    }

    solve();   // emit changed() → AlignmentRenderer::refresh()
    return true;
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

    // ── 套用起始里程偏移（純顯示/輸出用途，不影響幾何）──────────────────────
    if (m_result && !m_result->isEmpty() &&
        (m_startChainage != 0.0 || m_startContChainage != 0.0)) {
        QVector<AlignmentPoint> pts = m_result->rawPoints();
        const double contOffset = m_startContChainage - m_startChainage;
        for (AlignmentPoint& p : pts) {
            p.chainage    += m_startChainage;
            p.contChainage = p.chainage + contOffset;
        }
        m_result->load(pts);
    }

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
        elem["centralAngle"] = e.centralAngle;
        elem["isCompoundUnknown"] = e.isCompoundUnknown;
        elem["tangentIdxBefore"]  = e.tangentIdxBefore;
        elem["tangentIdxAfter"]   = e.tangentIdxAfter;
        elem["isConstructionLine"] = e.isConstructionLine;
        elem["constructionIsArc"]  = e.constructionIsArc;
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
    obj["startChainage"]     = m_startChainage;
    obj["startContChainage"] = m_startContChainage;
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
        elem.centralAngle = e["centralAngle"].toDouble(0.0);  // 舊檔案無此欄位 → 0 = 自動平分（沿用既有行為）
        elem.isCompoundUnknown = e["isCompoundUnknown"].toBool(false);  // 舊檔案無此欄位 → false（沿用既有行為）
        elem.tangentIdxBefore = e["tangentIdxBefore"].toInt(-1);
        elem.tangentIdxAfter  = e["tangentIdxAfter"].toInt(-1);
        elem.isConstructionLine = e["isConstructionLine"].toBool(false);
        elem.constructionIsArc  = e["constructionIsArc"].toBool(false);
        m_elems.append(elem);
    }
    m_startChainage     = obj["startChainage"].toDouble(0.0);
    m_startContChainage = obj["startContChainage"].toDouble(0.0);
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

// ── setLvc ────────────────────────────────────────────────────────────────────
void VerticalAlignmentEdit::setLvc(int vipIdx, double lvc)
{
    if (vipIdx <= 0 || vipIdx >= m_vips.size() - 1) {
        qWarning() << "[VerticalAlignmentEdit] setLvc: vipIdx" << vipIdx
                   << "is an endpoint — no VC can be assigned.";
        return;
    }
    if (lvc < 0.0) lvc = 0.0;
    m_vips[vipIdx].lvc = lvc;
}

// ── seedFromDensePoints ────────────────────────────────────────────────────────
//  由稠密的 VerticalAlignmentPoint 序列反推 VIP 清單的唯一實作。
bool VerticalAlignmentEdit::seedFromDensePoints(const QVector<VerticalAlignmentPoint>& rawPts)
{
    if (!m_vips.isEmpty()) return false;   // 已有資料，不覆蓋
    if (rawPts.size() < 2)  return false;

    // ── 起始 VIP ─────────────────────────────────────────────────────────────
    addVip(rawPts.first().chainage, rawPts.first().elevation, 0.0);

    // ── 中間 VIP：掃描「VC exit 記錄」（lvc > threshold）────────────────────
    // solve() 輸出：
    //   - lvc > 0 的記錄 = VC exit（攜帶真實 lvc + pviElevation）
    //   - lvc = 1e-6（tiny）= 折點，還原為 lvc = 0
    constexpr double kLvcThreshold = 0.001;  // < 1 mm 視為折點

    for (int i = 1; i < rawPts.size() - 1; ++i) {
        const auto& pt = rawPts[i];
        if (pt.lvc <= 0.0) continue;  // 非 VC exit 記錄

        const double realLvc = (pt.lvc < kLvcThreshold) ? 0.0 : pt.lvc;
        // VC exit 里程 = pvi 里程 + lvc/2；pvi 里程 = exit.ch - lvc/2
        const double pviCh = pt.chainage - pt.lvc / 2.0;
        const double pviEl = (realLvc > 0.0) ? pt.pviElevation : pt.elevation;
        addVip(pviCh, pviEl, realLvc);
    }

    // ── 終止 VIP ─────────────────────────────────────────────────────────────
    addVip(rawPts.last().chainage, rawPts.last().elevation, 0.0);
    solve();
    return true;
}

double VerticalAlignmentEdit::vipChainage(int idx) const
{
    if (idx < 0 || idx >= m_vips.size()) return 0.0;
    return m_vips[idx].chainage;
}

double VerticalAlignmentEdit::vipElevation(int idx) const
{
    if (idx < 0 || idx >= m_vips.size()) return 0.0;
    return m_vips[idx].elevation;
}

double VerticalAlignmentEdit::vipLvc(int idx) const
{
    if (idx < 0 || idx >= m_vips.size()) return 0.0;
    return m_vips[idx].lvc;
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

// ─────────────────────────────────────────────────────────────────────────────
//  Phase 4: doc-origin accessors
// ─────────────────────────────────────────────────────────────────────────────
bool    AlignmentDocument::hasDocOrigin() const { return m_hasDocOrigin; }
double  AlignmentDocument::docOriginE()   const { return m_docOriginE; }
double  AlignmentDocument::docOriginN()   const { return m_docOriginN; }
QString AlignmentDocument::docEpsgCode()  const { return m_docEpsgCode; }

QJsonObject AlignmentDocument::toJson() const
{
    QJsonObject obj;
    obj["horizontal"]     = m_horizontal->toJson();
    obj["vertical"]       = m_vertical->toJson();
    // Phase 3: 持久化 TM2 ProjectOrigin（供下次開啟自動還原）
    obj["projectOrigin"]  = aicad::core::geometry::ProjectOrigin::instance().toJson();
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

    // Phase 4: 還原文件內嵌 origin，並與目前 ProjectOrigin 做衝突偵測
    if (obj.contains(QStringLiteral("projectOrigin"))) {
        const QJsonObject originObj = obj[QStringLiteral("projectOrigin")].toObject();
        m_hasDocOrigin = originObj[QStringLiteral("isSet")].toBool(false);
        m_docOriginE   = originObj[QStringLiteral("originE")].toDouble(0.0);
        m_docOriginN   = originObj[QStringLiteral("originN")].toDouble(0.0);
        m_docEpsgCode  = originObj[QStringLiteral("epsgCode")].toString(QStringLiteral("EPSG:3826"));

        auto& globalOrigin = aicad::core::geometry::ProjectOrigin::instance();
        if (m_hasDocOrigin) {
            if (!globalOrigin.isSet()) {
                // 目前尚未設定 → 直接套用檔案內 origin
                globalOrigin.fromJson(originObj);
                qDebug() << "[AlignmentDocument] Auto-applied doc origin:"
                         << m_docOriginE << m_docOriginN;
            } else {
                // 兩者不同 → 提示使用者
                const double dE = qAbs(globalOrigin.originE() - m_docOriginE);
                const double dN = qAbs(globalOrigin.originN() - m_docOriginN);
                constexpr double kTolerance = 1.0; // 1 m 差異即視為衝突
                if (dE > kTolerance || dN > kTolerance) {
                    const QString msg = QString(
                        "此檔案使用不同的 TM2 原點："
                        "  檔案原點  E=%1  N=%2"
                        "  目前原點  E=%3  N=%4"
                        "是否套用檔案內原點？"
                        "（選「否」將保持目前原點，Alignment 可能偏移）")
                        .arg(m_docOriginE,          0,'f',3)
                        .arg(m_docOriginN,          0,'f',3)
                        .arg(globalOrigin.originE(),0,'f',3)
                        .arg(globalOrigin.originN(),0,'f',3);
                    const auto reply = QMessageBox::question(
                        nullptr, tr("TM2 Origin 衝突"), msg,
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                    if (reply == QMessageBox::Yes)
                        globalOrigin.fromJson(originObj);
                }
            }
        }
    } else {
        // 舊檔案無 origin 鍵 → 向下相容，保持恆等轉換
        m_hasDocOrigin = false;
    }

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