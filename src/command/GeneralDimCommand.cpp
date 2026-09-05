#include "GeneralDimCommand.h"
#include "cad/sketch/AnnotationStandardsChecker.h"
#include "ConstraintCommands.h"  // reportSolveResult
#include "../ui/DimExpressionDialog.h"
#include "../core/Application.h"
#include "../core/CommandLineManager.h"
#include "../core/EventBus.h"
#include "../core/ParameterStore.h"
#include "../cad/Sketch.h"
#include "../ui/UIManager.h"
#include "../view/CadView.h"
#include <QMetaObject>
#include <algorithm>
#include <cmath>
#include <optional>
#include <Geom_Circle.hxx>

namespace aicad::command {

using namespace cad;

// ─────────────────────────────────────────────────────────────────────────────
// ctor
// ─────────────────────────────────────────────────────────────────────────────

GeneralDimCommand::GeneralDimCommand()
    : Command("GDIM", "General Dimension：自動判斷尺寸類型") {}

bool GeneralDimCommand::canCancel() const {
    // 只有 Idle（尚未鎖定任何幾何）才允許 CommandManager 真正終止指令；
    // 其餘階段（Anchored / WaitDimPlace / WaitValue）的右鍵／Esc 一律由
    // onCancelled()/backTo*() 處理成「退一步」，指令物件不會被摧毀。
    return m_state == State::Idle;
}

// ─────────────────────────────────────────────────────────────────────────────
// 輔助
// ─────────────────────────────────────────────────────────────────────────────

cad::Sketch* GeneralDimCommand::activeSketch() const {
    return core::Application::instance()->activeSketch();
}

namespace {
// 角度標註（FixedAngleDim/FixedAngle）專用：算出兩條線（無限延伸）的交點
// （apex）。跟 DimPreviewOverlay::paintDimPreview()／
// AIS_DimensionLine::drawAngleDim() 用的是同一套公式，三處算出來的 apex
// 必須一致，拖曳預覽、確認當下的 offset 基準、最終顯示才會是同一個點。
std::optional<QVector2D> angleApex2D(const cad::Sketch* sk, const QList<cad::GeomRef>& refs) {
    if (!sk || refs.size() < 2) return std::nullopt;
    auto* geomA = sk->findGeometry(refs[0].geomUuid);
    auto* geomB = sk->findGeometry(refs[1].geomUuid);
    auto* lnA = dynamic_cast<const cad::SketchLine*>(geomA);
    auto* lnB = dynamic_cast<const cad::SketchLine*>(geomB);
    if (!lnA || !lnB) return std::nullopt;

    QVector2D dirA = lnA->end - lnA->start;
    QVector2D dirB = lnB->end - lnB->start;
    float lenA = dirA.length();
    float lenB = dirB.length();
    if (lenA <= 1e-6f || lenB <= 1e-6f) return std::nullopt;
    dirA /= lenA;
    dirB /= lenB;

    float cross = dirA.x() * dirB.y() - dirA.y() * dirB.x();
    if (std::abs(cross) < 1e-6f)
        return (lnA->start + lnA->end) * 0.5f;   // 平行：退化為線 A 中點

    QVector2D ab = lnB->start - lnA->start;
    float t = (ab.x() * dirB.y() - ab.y() * dirB.x()) / cross;
    return lnA->start + dirA * t;
}
}

// 點 P 到直線 (A,B) 的垂距
static double pointToLineDistance(const QVector2D& P,
                                   const QVector2D& A,
                                   const QVector2D& B)
{
    QVector2D AB = B - A;
    float len = AB.length();
    if (len < 1e-7f) return static_cast<double>((P - A).length());
    // 叉積 / 線長
    float cross = AB.x() * (A.y() - P.y()) - AB.y() * (A.x() - P.x());
    return static_cast<double>(std::abs(cross) / len);
}

/// 第二選 handle 正規化：當第一選（anchor）是整條幾何（WholeGeom），
/// 第二選若是同一類型幾何但 OSnap 吸附到端點（handle = Start/End），
/// 強制改為 WholeGeom，確保 GeneralDimClassifier::inferPair() 走到正確分支。
static void normalizeSecondRef(const cad::GeomRef& first, cad::GeomRef& second,
                                const cad::Sketch* sk)
{
    if (!sk) return;
    auto* geomFirst  = sk->findGeometry(first.geomUuid);
    auto* geomSecond = sk->findGeometry(second.geomUuid);
    const bool firstIsWhole =
        geomFirst && first.handle == GeomHandle::WholeGeom;
    const bool secondIsGeom =
        geomSecond &&
        (geomSecond->type == SketchGeometryType::Line ||
         geomSecond->type == SketchGeometryType::Arc  ||
         geomSecond->type == SketchGeometryType::Circle);
    if (firstIsWhole && secondIsGeom && second.handle != GeomHandle::WholeGeom)
        second = GeomRef(second.geomUuid, GeomHandle::WholeGeom);
}

/// ⚠️ 修正：草圖平面參考幾何（X 軸／Y 軸／原點）需能參與 GDIM 約束
/// （例如「與 X 軸的夾角」「與原點的距離」）。
///
/// 問題根因：CadView 在 GetGeom/GDIM 模式下對這三個參考物件的拾取／hover
/// 走的是 OCCT DetectedInteractive 的 fallback 分支（見 CadView::
/// handlePointInput() / mouseMoveEvent()），送進 GEOM_PICKED / GEOM_HOVER
/// 事件的 geomUuid 是 CadView 顯示層用的「虛擬 UUID」——"sketch_xaxis:<id>"
/// ／"sketch_yaxis:<id>"／"sketch_origin:<id>"（見 CadView::showSketchAxes()
/// 的 aisToGeomUuid 登記），而不是 Sketch 內真正的幾何/點 UUID。
/// GeneralDimClassifier::isPointLike()／inferSingle()／inferPair() 全部
/// 透過 Sketch::point()／Sketch::findGeometry() 以「真實 UUID」查找幾何，
/// 對這種虛擬 UUID 一律查無資料、回傳 nullopt——結果是使用者點擊 X 軸／
/// Y 軸／原點時 GDIM 完全沒有反應（無法標註角度、距離）。
///
/// 修正方式：比照 ConstraintCommands.cpp::resolveAxisUuid()／
/// SketchPanel.cpp 既有作法，在虛擬 UUID 進入 GeneralDimCommand 的
/// 分類／鎖定流程之前，先用 Sketch::xAxisGeomUuid()／yAxisGeomUuid()／
/// originPointUuid()（lazy-init，需要時才建立對應的 construction 幾何並
/// 以 Fixed 約束固定）換成真正的 UUID。換成真實 UUID 後，X/Y 軸本質上
/// 就是一條（Fixed）SketchLine、原點是一個（Fixed）SketchPoint，後續
/// isPointLike/inferSingle/inferPair 完全不需要額外規則即可正確處理
/// 「點＋軸線→垂距」「軸線＋軸線／軸線＋一般線→夾角」「點＋原點→水平/
/// 垂直/對齊距離」等所有既有分類邏輯。
///
/// ⚠️ 草圖是 2D 平面：只有 X 軸／Y 軸／原點這三個參考幾何在草圖檢視中
/// 可被拾取（見 CadView::showSketchAxes()，僅建立 X/Y 軸與原點三個 AIS
/// 物件），沒有可供 GDIM 選取的「草圖 Z 軸」——Z 軸（草圖平面法向量）
/// 不在草圖的 2D 自由度／約束系統範圍內，因此本函式與 GDIM 都不處理
/// Z 軸。
static QString resolveAxisRefUuid(const cad::Sketch* sketch, const QString& uuid)
{
    if (!sketch || uuid.isEmpty()) return uuid;
    // Sketch::xAxisGeomUuid() 等為非 const（lazy-init 時會修改 Sketch 內部
    // 狀態），這裡的 sketch 指標在呼叫端一律來自 activeSketch()（非 const），
    // const_cast 僅為了讓本函式簽章能同時服務 const/非 const 呼叫情境。
    auto* sk = const_cast<cad::Sketch*>(sketch);
    const QString skId = sk->id();
    if (uuid == "sketch_xaxis:"  + skId) return sk->xAxisGeomUuid();
    if (uuid == "sketch_yaxis:"  + skId) return sk->yAxisGeomUuid();
    if (uuid == "sketch_origin:" + skId) return sk->originPointUuid();
    return uuid;
}

/// 純粹供命令列訊息顯示用：判斷「轉換前」的原始 UUID 是否為 X 軸／Y 軸／
/// 原點的虛擬 UUID，是的話回傳對應中文標籤；否則回傳空字串。必須在呼叫
/// resolveAxisRefUuid() 換成真實 UUID 之前，用原始字串比對——換過之後就
/// 只是一條普通的 construction line／point，無法再分辨它原本是哪個軸。
static QString axisRefLabel(const cad::Sketch* sketch, const QString& rawUuid)
{
    if (!sketch || rawUuid.isEmpty()) return {};
    const QString skId = sketch->id();
    if (rawUuid == "sketch_xaxis:"  + skId) return QStringLiteral("X 軸");
    if (rawUuid == "sketch_yaxis:"  + skId) return QStringLiteral("Y 軸");
    if (rawUuid == "sketch_origin:" + skId) return QStringLiteral("原點");
    return {};
}

QString GeneralDimCommand::constraintTypeName() const {
    switch (m_type) {
    case ConstraintType::FixedLength:    return "FixedLength";
    case ConstraintType::FixedDiameter:  return "FixedDiameter";
    case ConstraintType::FixedHorizDist: return "FixedHorizDist";
    case ConstraintType::FixedVertDist:  return "FixedVertDist";
    case ConstraintType::FixedArcLength: return "FixedArcLength";
    case ConstraintType::CoordinateDim:  return "CoordinateDim";
    case ConstraintType::FixedRadius:    return "FixedRadius";
    case ConstraintType::FixedDistance:  return "FixedDistance";
    case ConstraintType::FixedAngleDim:  return "FixedAngleDim";
    case ConstraintType::FixedX:         return "FixedX";
    case ConstraintType::FixedY:         return "FixedY";
    default: return "Constraint";
    }
}

double GeneralDimCommand::measureCurrentValue() const {
    Sketch* sk = activeSketch();
    if (!sk || m_refs.isEmpty()) return 0.0;

    const GeomRef& r0 = m_refs[0];
    auto* g0 = sk->findGeometry(r0.geomUuid);
    if (!g0) return 0.0;

    switch (m_type) {
    case ConstraintType::FixedLength: {
        auto* line = dynamic_cast<const SketchLine*>(g0);
        if (!line) return 0.0;
        return static_cast<double>((line->end - line->start).length());
    }
    case ConstraintType::FixedDiameter: {
        auto* circ = dynamic_cast<const SketchCircle*>(g0);
        if (circ) return circ->radius * 2.0;
        // arc
        auto* arc = dynamic_cast<const SketchArc*>(g0);
        if (arc && !arc->curve.IsNull()) {
            auto base = Handle(Geom_Circle)::DownCast(arc->curve->BasisCurve());
            if (!base.IsNull()) return base->Radius() * 2.0;
        }
        return 0.0;
    }
    case ConstraintType::FixedRadius: {
        auto* circ = dynamic_cast<const SketchCircle*>(g0);
        if (circ) return circ->radius;
        auto* arc = dynamic_cast<const SketchArc*>(g0);
        if (arc && !arc->curve.IsNull()) {
            auto base = Handle(Geom_Circle)::DownCast(arc->curve->BasisCurve());
            if (!base.IsNull()) return base->Radius();
        }
        return 0.0;
    }
    case ConstraintType::FixedArcLength: {
        auto* arc = dynamic_cast<const SketchArc*>(g0);
        if (arc && !arc->curve.IsNull()) {
            auto base = Handle(Geom_Circle)::DownCast(arc->curve->BasisCurve());
            if (!base.IsNull()) {
                double r  = base->Radius();
                double t0 = arc->curve->FirstParameter();
                double t1 = arc->curve->LastParameter();
                return r * std::abs(t1 - t0);
            }
        }
        return 0.0;
    }
    case ConstraintType::FixedHorizDist:
    case ConstraintType::FixedVertDist:
    case ConstraintType::FixedDistance: {
        if (m_refs.size() < 2) return 0.0;

        if (m_distMode == cad::DistanceMode::PointToLine) {
            // refs[0]=點, refs[1]=整條線 WholeGeom → 垂距
            QVector2D pt = m_refs[0].resolvePosition(sk);
            auto* geomB  = sk->findGeometry(m_refs[1].geomUuid);
            if (!geomB) return 0.0;
            auto* ln = dynamic_cast<const cad::SketchLine*>(geomB);
            if (!ln) return 0.0;
            return pointToLineDistance(pt, ln->start, ln->end);
        }

        if (m_distMode == cad::DistanceMode::LineToLine) {
            // refs[0]=線A WholeGeom, refs[1]=線B WholeGeom → 平行線間距
            auto* geomA = sk->findGeometry(m_refs[0].geomUuid);
            auto* geomB = sk->findGeometry(m_refs[1].geomUuid);
            auto* lnA = dynamic_cast<const cad::SketchLine*>(geomA);
            auto* lnB = dynamic_cast<const cad::SketchLine*>(geomB);
            if (!lnA || !lnB) return 0.0;
            // 用 A 的起點到 B 線的垂距
            return pointToLineDistance(lnA->start, lnB->start, lnB->end);
        }

        QVector2D pa = m_refs[0].resolvePosition(sk);
        QVector2D pb = m_refs[1].resolvePosition(sk);
        if (m_type == ConstraintType::FixedHorizDist)
            return std::abs(static_cast<double>(pb.x() - pa.x()));
        if (m_type == ConstraintType::FixedVertDist)
            return std::abs(static_cast<double>(pb.y() - pa.y()));
        return static_cast<double>((pb - pa).length());
    }
    case ConstraintType::CoordinateDim: {
        if (m_refs.isEmpty()) return 0.0;
        QVector2D p = m_refs[0].resolvePosition(sk);
        return static_cast<double>(p.x());  // value = x
    }
    case ConstraintType::FixedX: {
        if (m_refs.isEmpty()) return 0.0;
        QVector2D p = m_refs[0].resolvePosition(sk);
        return static_cast<double>(p.x());
    }
    case ConstraintType::FixedY: {
        if (m_refs.isEmpty()) return 0.0;
        QVector2D p = m_refs[0].resolvePosition(sk);
        return static_cast<double>(p.y());
    }
    case ConstraintType::FixedAngleDim:
    case ConstraintType::FixedAngle: {
        // 兩線夾角：refs[0]=lineA, refs[1]=lineB，以弧度儲存
        if (m_refs.size() < 2) return 0.0;
        auto* geomA = sk->findGeometry(m_refs[0].geomUuid);
        auto* geomB = sk->findGeometry(m_refs[1].geomUuid);
        auto* lineA = dynamic_cast<const cad::SketchLine*>(geomA);
        auto* lineB = dynamic_cast<const cad::SketchLine*>(geomB);
        if (!lineA || !lineB) return 0.0;
        QVector2D dirA = (lineA->end - lineA->start).normalized();
        QVector2D dirB = (lineB->end - lineB->start).normalized();
        double dot   = static_cast<double>(QVector2D::dotProduct(dirA, dirB));
        double cross = static_cast<double>(dirA.x() * dirB.y() - dirA.y() * dirB.x());
        double ang   = std::atan2(std::abs(cross), dot);  // 0..π
        if (m_useSupplementAngle) ang = M_PI - ang;        // 補角
        return ang;  // 弧度
    }
    default:
        return 0.0;
    }
}

double GeneralDimCommand::measureCurrentValue2() const {
    // Only for CoordinateDim — returns Y coordinate
    if (m_type != ConstraintType::CoordinateDim) return 0.0;
    Sketch* sk = activeSketch();
    if (!sk || m_refs.isEmpty()) return 0.0;
    QVector2D p = m_refs[0].resolvePosition(sk);
    return static_cast<double>(p.y());
}

QVector2D GeneralDimCommand::refMidpoint2D() const {
    Sketch* sk = activeSketch();
    if (!sk || m_refs.isEmpty()) return {};

    // 角度標註（FixedAngleDim/FixedAngle）：offset 的基準點必須是兩線的
    // 交點（apex），不能沿用下面「refs 位置平均」這個對其他型別合理、對
    // 角度標註卻沒有意義的定義——GeomRef::resolvedPointUuid() 對 Line 只
    // 處理 Start/End，WholeGeom 沒對應到，resolvePosition() 會退回
    // geom->points[0]（線段的「起點」），兩線起點的平均通常離兩線真正的
    // 交點很遠。這個基準點會被 CadView::beginPlaceDimLine() 用來換算
    // offsetX/Y（= 滑鼠 - 這個基準點），offsetX/Y 之後又在
    // AIS_DimensionLine::drawAngleDim() 裡被直接當成「滑鼠相對 apex 的
    // 偏移」使用（算角弧半徑、以及判斷角弧該落在哪一側）——基準點跟 apex
    // 對不上，這兩者就全部跟著算錯，這正是「完成的尺寸線位置不是臨近滑鼠
    // 點下的位置」的根本原因之一。
    if (m_type == cad::ConstraintType::FixedAngleDim ||
        m_type == cad::ConstraintType::FixedAngle) {
        if (auto apex = angleApex2D(sk, m_refs))
            return *apex;
    }

    QVector2D sum;
    for (const auto& r : m_refs)
        sum += r.resolvePosition(sk);
    return sum / static_cast<float>(m_refs.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// EventBus subscribe / unsubscribe
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::subscribeGeomPicked() {
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::GEOM_PICKED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onGeomPicked(v); },
                                      Qt::QueuedConnection);
        });
}

void GeneralDimCommand::subscribeStringInput() {
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::STRING_INPUT, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onStringInput(v); },
                                      Qt::QueuedConnection);
        });
}

void GeneralDimCommand::subscribeDimConfirmed() {
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::DIM_LINE_CONFIRMED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onDimConfirmed(v); },
                                      Qt::QueuedConnection);
        });
}

void GeneralDimCommand::subscribePreview() {
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::DIM_LINE_PREVIEW, this,
        [this](const QVariant& v) {
            // ★ 用 QueuedConnection 排隊，確保與 onDimConfirmed 執行順序一致：
            //   若點擊當下 CONFIRMED 先排隊，PREVIEW 就不會覆蓋已確認的 offset
            QMetaObject::invokeMethod(this, [this, v] {
                if (m_state != State::WaitDimPlace) return;
                QVariantMap m = v.toMap();
                m_dimOffsetX = m.value("offsetX").toDouble();
                m_dimOffsetY = m.value("offsetY").toDouble();

                // ── 點＋點（含線的兩端點）：WaitDimPlace 階段持續依「滑鼠與兩點
                // 中點」的相對方位在 水平/垂直/對齊(或線長) 間即時重新分類 ──────
                // m_dimAnchor2D 在 transitionToWaitDimPlace() 已設為 refMidpoint2D()
                // （點＋點時即兩點中點），offset 即 mousePtAbs - mid，直接重用
                // GeneralDimClassifier::inferPair() 保持與 Anchored 階段 hover
                // 預覽同一套分類邏輯，避免重複實作 classifyZone。
                // ⚠️ 修正：改用 m_hvaRefA/m_hvaRefB（貫穿整個 WaitDimPlace 期間
                // 固定不變）而非 m_refs[0]/m_refs[1]／`m_refs.size() >= 2`
                // 門檻——m_refs 本身會隨分類結果改變元素數（線長只有 1 個
                // WholeGeom ref），拿它當下一輪分類輸入會在分類成線長後，
                // 下次滑鼠移動時 m_refs[1] 存取越界。同時，每次重新分類後
                // 也要一併更新 m_refs 本身（不能只改 m_type），否則型別與
                // 實際套用的 refs 對不上。
                if (m_isPointPairHVA) {
                    Sketch* sk = activeSketch();
                    if (sk) {
                        QVector2D mousePtAbs(
                            static_cast<float>(m_dimAnchor2D.x() + m_dimOffsetX),
                            static_cast<float>(m_dimAnchor2D.y() + m_dimOffsetY));
                        auto pairInf = GeneralDimClassifier::inferPair(
                            m_hvaRefA, m_hvaRefB, sk, mousePtAbs);
                        if (pairInf) {
                            auto pairCt = annotationKindToConstraintType(pairInf->kind);
                            if (pairCt) {
                                m_type     = *pairCt;
                                m_distMode = pairInf->distMode;
                                m_refs     = pairInf->pairedRefs.isEmpty()
                                    ? QList<GeomRef>{ m_hvaRefA, m_hvaRefB }
                                    : pairInf->pairedRefs;
                            }
                        }
                    }
                }

                // ── 夾角型別：依滑鼠落在兩線交叉出的 4 個扇區中的哪一個，
                // 動態切換 夾角／補角 ──────────────────────────────────────
                // （無選單版第 B 組第 16 項：「移動決定角度標註弧的半徑與象限」）
                // 型別本身在點擊第二條線時就已鎖定（見 lockPairGeom），這裡只
                // 調整 useSupplementAngle，不再像舊版那樣於拖曳中重新判斷型別。
                //
                // ⚠️ 舊版用「offset 是否在角平分線同一側」的一條線（垂直於
                // 平分線）粗略二分整個平面，只能大致分成「偏向夾角」／
                // 「偏向補角」兩半，分界線跟兩線本身的位置對不齊，靠近
                // dirA/dirB 邊界時容易分類錯——而且用來畫角弧的
                // DimPreviewOverlay／AIS_DimensionLine::drawAngleDim 兩處，
                // 各自改用「精確 4 扇區」判斷滑鼠落在哪個扇區來決定畫哪一段
                // 弧之後，這裡若還用粗略二分，算出來的 m_useSupplementAngle
                // （決定標籤顯示的角度數值）就可能跟實際畫出來的扇區對不
                // 上——即使弧的「位置」已經修好，數值卻可能還是夾角/補角
                // 標反。改成跟畫弧同一套精確判斷：算出滑鼠落在哪個扇區，
                // 再看該扇區大小是 ang（夾角本身）還是 π-ang（補角）。
                if (m_type == cad::ConstraintType::FixedAngleDim && m_refs.size() >= 2) {
                    auto* sk = activeSketch();
                    if (sk) {
                        auto* geomA = sk->findGeometry(m_refs[0].geomUuid);
                        auto* geomB = sk->findGeometry(m_refs[1].geomUuid);
                        auto* lineA = dynamic_cast<const cad::SketchLine*>(geomA);
                        auto* lineB = dynamic_cast<const cad::SketchLine*>(geomB);
                        if (lineA && lineB) {
                            QVector2D dirA = (lineA->end - lineA->start).normalized();
                            QVector2D dirB = (lineB->end - lineB->start).normalized();

                            QVector2D offset(static_cast<float>(m_dimOffsetX),
                                             static_cast<float>(m_dimOffsetY));
                            if (offset.lengthSquared() > 1e-6f) {
                                double dot   = static_cast<double>(QVector2D::dotProduct(dirA, dirB));
                                double cross = static_cast<double>(dirA.x() * dirB.y() - dirA.y() * dirB.x());
                                double ang   = std::atan2(std::abs(cross), dot);  // 0..π，夾角本身

                                auto norm2pi = [](double a) {
                                    while (a < 0.0)        a += 2.0 * M_PI;
                                    while (a >= 2.0 * M_PI) a -= 2.0 * M_PI;
                                    return a;
                                };
                                double rays[4] = {
                                    norm2pi(std::atan2(static_cast<double>(dirA.y()),  static_cast<double>(dirA.x()))),
                                    norm2pi(std::atan2(static_cast<double>(-dirA.y()), static_cast<double>(-dirA.x()))),
                                    norm2pi(std::atan2(static_cast<double>(dirB.y()),  static_cast<double>(dirB.x()))),
                                    norm2pi(std::atan2(static_cast<double>(-dirB.y()), static_cast<double>(-dirB.x())))
                                };
                                std::sort(std::begin(rays), std::end(rays));

                                double angM = norm2pi(std::atan2(static_cast<double>(offset.y()),
                                                                  static_cast<double>(offset.x())));
                                double sectorSize = (rays[0] + 2.0 * M_PI) - rays[3]; // 預設：繞回第一段
                                for (int i = 0; i < 3; ++i) {
                                    if (angM >= rays[i] && angM < rays[i + 1]) {
                                        sectorSize = rays[i + 1] - rays[i];
                                        break;
                                    }
                                }
                                // 該扇區大小比較接近 ang 還是 π-ang，決定是否用補角
                                m_useSupplementAngle =
                                    std::abs(sectorSize - ang) > std::abs(sectorSize - (M_PI - ang));
                            }
                        }
                    }
                }

                m_measuredValue = measureCurrentValue();
                pushPreview(m_refs, m_type, m_distMode);
            }, Qt::QueuedConnection);
        });
}

void GeneralDimCommand::subscribeCancelled() {
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onCancelled(v); },
                                      Qt::QueuedConnection);
        });
}

void GeneralDimCommand::subscribeGeomHover() {
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::GEOM_HOVER, this,
        [this](const QVariant& v) {
            // 直接在主執行緒處理（hover 頻率高，避免 QueuedConnection 積壓）
            onGeomHover(v);
        });
}

void GeneralDimCommand::unsubscribeGeomPicked() {
    auto* bus = core::Application::instance()->eventBus();
    if (bus) bus->unsubscribe(core::Events::GEOM_PICKED, this);
}

void GeneralDimCommand::unsubscribeAll() {
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->unsubscribe(core::Events::GEOM_PICKED,        this);
    bus->unsubscribe(core::Events::GEOM_HOVER,         this);
    bus->unsubscribe(core::Events::STRING_INPUT,        this);
    bus->unsubscribe(core::Events::DIM_LINE_CONFIRMED,  this);
    bus->unsubscribe(core::Events::DIM_LINE_PREVIEW,    this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED,   this);
}

// ─────────────────────────────────────────────────────────────────────────────
// execute — 命令進入點
// ─────────────────────────────────────────────────────────────────────────────

CommandResult GeneralDimCommand::execute(const CommandContext& ctx)
{
    auto* app    = core::Application::instance();
    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk   = app ? app->activeSketch() : nullptr;
    if (!sk) {
        if (cmdMgr) cmdMgr->printError("No active sketch. Enter sketch edit mode first.");
        return CommandResult::Failure("No active sketch.");
    }

    cleanup();  // 確保前次殘留狀態清除

    m_driving    = true;
    m_hasPending = false;
    m_pendingValue = 0.0;
    m_pendingExpr.clear();
    m_pendingIsRawUserInput = false;

    // 若命令列帶入數值/表達式，預先記錄
    if (!ctx.args.isEmpty()) {
        QString expr = ctx.args[0];
        if (expr == "-measured") {
            m_driving = false;
        } else {
            bool isNum;
            double v = expr.toDouble(&isNum);
            if (isNum) {
                m_pendingValue = v;
                m_hasPending   = true;
                m_pendingIsRawUserInput = true;
            } else if (expr != "-measured") {
                auto [ok, ev] = sk->parameterStore()->evaluate(expr);
                if (!ok) {
                    if (cmdMgr) cmdMgr->printError(
                        QString("Unknown expression: '%1'").arg(expr));
                    return CommandResult::Failure("Unknown expression.");
                }
                m_pendingValue = ev;
                m_pendingExpr  = expr;
                m_hasPending   = true;
                m_pendingIsRawUserInput = true;
                if (cmdMgr) cmdMgr->printMessage(
                    QString("  Expression '%1' = %2").arg(expr).arg(ev));
            }
        }
        if (ctx.args.contains("-measured"))
            m_driving = false;
    }

    m_state = State::Idle;
    m_refs.clear();
    m_anchorRef = cad::GeomRef{};
    m_hasLastHoverRef = false;  // 每次指令重新啟動，清空舊的 hover 配對記憶

    // 進入 GetGeom 模式
    auto* ui = app->uiManager();
    if (ui && ui->cadView()) {
        ui->cadView()->setMode(view::InteractionMode::GetGeom);
        // 無選單版第 1 節：讓「整個圓／整條弧」的內部 hover/點擊也能被偵測到
        // （見 CadView::setGdimWholeGeomHitTestEnabled() 說明）。
        ui->cadView()->setGdimWholeGeomHitTestEnabled(true);
    }

    // ★ 必須設為 Running，命令才會持續存活等待使用者互動
    setState(CommandState::Running);

    subscribeGeomPicked();
    subscribeGeomHover();   // Idle 階段也要 hover 預覽
    subscribeCancelled();

    if (cmdMgr) cmdMgr->showPrompt(GeneralDimClassifier::initialPrompt());
    return CommandResult::Success("Waiting for input");
}

// ─────────────────────────────────────────────────────────────────────────────
// pushPreview / clearDimPreview
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::pushPreview(const QList<cad::GeomRef>& refs, cad::ConstraintType type,
                                     cad::DistanceMode mode)
{
    auto* app     = core::Application::instance();
    auto* ui      = app ? app->uiManager() : nullptr;
    auto* cadView = ui  ? ui->cadView()    : nullptr;
    if (!cadView) return;

    Sketch* sk = activeSketch();
    if (!sk || refs.isEmpty()) { cadView->clearDimPreview(); return; }

    // 用 refs/type/mode 量測數值——暫時代入 m_refs/m_type/m_distMode 呼叫
    // measureCurrentValue()（沿用既有量測邏輯），算完立刻還原，
    // 避免污染呼叫端（例如 Idle hover）尚未鎖定的狀態。
    QList<GeomRef> savedRefs = m_refs;
    ConstraintType savedType = m_type;
    DistanceMode   savedMode = m_distMode;
    m_refs = refs; m_type = type; m_distMode = mode;
    double val = measureCurrentValue();
    m_refs = savedRefs; m_type = savedType; m_distMode = savedMode;

    view::CadView::DimPreviewInfo info;
    info.refs     = refs;
    info.type     = type;
    info.distMode = mode;
    info.value    = val;
    info.valid    = true;
    cadView->setDimPreview(info);
}

void GeneralDimCommand::clearDimPreview()
{
    auto* app     = core::Application::instance();
    auto* ui      = app ? app->uiManager() : nullptr;
    auto* cadView = ui  ? ui->cadView()    : nullptr;
    if (cadView) cadView->clearDimPreview();
}

// ─────────────────────────────────────────────────────────────────────────────
// onGeomHover — Idle：hover 顯示單幾何型別預覽
//               Anchored：持續依滑鼠位置在單/雙幾何型別間即時切換預覽
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::onGeomHover(const QVariant& payload)
{
    QVariantMap map     = payload.toMap();
    QString hoverUuid   = map.value("geomUuid").toString();
    int     hoverHandle = map.value("handle", -1).toInt();
    QVector2D mousePt   = map.value("point").value<QVector2D>();

    Sketch* sk = activeSketch();

    // X 軸／Y 軸／原點：CadView 送來的是顯示層虛擬 UUID，需先換成 Sketch
    // 內真實幾何/點 UUID，才能被下面的 isPointLike/inferSingle/inferPair
    // 正確辨識（見 resolveAxisRefUuid() 說明）。
    if (sk && !hoverUuid.isEmpty())
        hoverUuid = resolveAxisRefUuid(sk, hoverUuid);

    if (m_state == State::Idle) {
        if (hoverUuid.isEmpty() || !sk) { clearDimPreview(); m_hasLastHoverRef = false; return; }
        GeomRef hoverRef(hoverUuid, static_cast<GeomHandle>(hoverHandle));

        // ⚠️ 新增：純 hover（完全不點擊）也能預覽「兩個幾何配對」的型別——
        // 先 hover 到一條線（或點/圓/弧），再把滑鼠移到「另一個」可配對的
        // 幾何上（例如另一條不平行的線）時，直接比照已點擊建立 anchor 後
        // 的 hover 配對邏輯（見下面 Anchored 分支），用 canPair()/inferPair()
        // 判斷並預覽配對型別（例如兩線夾角、線間距、點到線垂距…），完全
        // 不需要先點擊任何一個。
        if (m_hasLastHoverRef) {
            GeomRef candidate = hoverRef;
            normalizeSecondRef(m_lastHoverRef, candidate, sk);
            const bool sameAsLast =
                (candidate.geomUuid == m_lastHoverRef.geomUuid
                 && candidate.handle == m_lastHoverRef.handle);
            if (!sameAsLast && GeneralDimClassifier::canPair(m_lastHoverRef, candidate, sk)) {
                auto inf = GeneralDimClassifier::inferPair(m_lastHoverRef, candidate, sk, mousePt);
                if (inf) {
                    auto ct = annotationKindToConstraintType(inf->kind);
                    if (ct) {
                        QList<GeomRef> refs = inf->pairedRefs.isEmpty()
                            ? QList<GeomRef>{ m_lastHoverRef, candidate } : inf->pairedRefs;
                        pushPreview(refs, *ct, inf->distMode);
                        return;
                    }
                }
            }
        }

        // 沒有可配對的「上一個 hover 錨點」，或這次 hover 到的東西無法與它
        // 配對 → 更新軟性錨點為目前 hover 到的東西，回到單一幾何預覽
        // （含線→自我配對成 Start/End、預覽 線長/水平/垂直距離，見
        // lineWholeGeomEndpoints() 說明）。
        m_lastHoverRef    = hoverRef;
        m_hasLastHoverRef = true;

        GeomRef startRef, endRef;
        if (lineWholeGeomEndpoints(hoverRef, sk, startRef, endRef)) {
            auto inf = GeneralDimClassifier::inferPair(startRef, endRef, sk, mousePt);
            if (!inf) { clearDimPreview(); return; }
            auto ct = annotationKindToConstraintType(inf->kind);
            if (!ct) { clearDimPreview(); return; }
            QList<GeomRef> refs = inf->pairedRefs.isEmpty()
                ? QList<GeomRef>{ startRef, endRef } : inf->pairedRefs;
            pushPreview(refs, *ct, inf->distMode);
            return;
        }

        auto inf = GeneralDimClassifier::inferSingle(hoverRef, sk, mousePt);
        if (!inf) { clearDimPreview(); return; }
        auto ct = annotationKindToConstraintType(inf->kind);
        if (!ct) { clearDimPreview(); return; }
        pushPreview({ hoverRef }, *ct, inf->distMode);
        return;
    }

    if (m_state == State::Anchored) {
        if (!sk || m_refs.isEmpty()) return;
        const GeomRef anchor = m_anchorRef;

        // 見 m_stickyPairedRef 註解：先用「目前 hover 到的東西」更新/清除
        // sticky 候選，但分類本身一律用 sticky 候選（若有）+ 目前滑鼠位置，
        // 不要求滑鼠必須停留在候選幾何正上方。
        if (!hoverUuid.isEmpty()) {
            GeomRef candidate(hoverUuid, static_cast<GeomHandle>(hoverHandle));
            normalizeSecondRef(anchor, candidate, sk);
            const bool sameAsAnchor =
                (candidate.geomUuid == anchor.geomUuid && candidate.handle == anchor.handle);
            if (sameAsAnchor) {
                // 滑鼠移回錨點本身：視為取消目前的配對候選，回到單幾何預覽。
                m_stickyPairedRef.reset();
            } else if (GeneralDimClassifier::canPair(anchor, candidate, sk)) {
                m_stickyPairedRef = candidate;
            }
            // 其餘情況（hover 到不可配對的東西）：維持既有 sticky 候選不變，
            // 讓使用者可以繼續往空白處移動滑鼠切換 H/V/Align。
        }

        if (m_stickyPairedRef) {
            auto inf = GeneralDimClassifier::inferPair(anchor, *m_stickyPairedRef, sk, mousePt);
            if (inf) {
                auto ct = annotationKindToConstraintType(inf->kind);
                if (ct) {
                    QList<GeomRef> refs = inf->pairedRefs.isEmpty()
                        ? QList<GeomRef>{ anchor, *m_stickyPairedRef } : inf->pairedRefs;
                    m_type = *ct;
                    m_distMode = inf->distMode;
                    m_useSupplementAngle = inf->useSupplementAngle;
                    pushPreview(refs, m_type, m_distMode);
                    return;
                }
            }
        }

        // 沒有可配對的第二幾何 → 依起點 + 滑鼠位置即時切換單幾何型別預覽
        // ⚠️ 修正：若起點本身是整條線（WholeGeom，例如使用者第一次點擊就
        // 點在線段上），同樣要用 lineWholeGeomEndpoints() 組出正確的
        // Start/End refs 給水平/垂直距離用，不能一律 `{ anchor }`（原因與
        // onGeomHover() Idle 分支、lockSingleGeom() 相同，見那兩處的修正
        // 說明）。
        GeomRef startRef, endRef;
        if (lineWholeGeomEndpoints(anchor, sk, startRef, endRef)) {
            auto inf = GeneralDimClassifier::inferPair(startRef, endRef, sk, mousePt);
            if (inf) {
                auto ct = annotationKindToConstraintType(inf->kind);
                if (ct) {
                    m_type = *ct;
                    m_distMode = inf->distMode;
                    QList<GeomRef> refs = inf->pairedRefs.isEmpty()
                        ? QList<GeomRef>{ startRef, endRef } : inf->pairedRefs;
                    pushPreview(refs, m_type, m_distMode);
                }
            }
            return;
        }

        auto inf = GeneralDimClassifier::inferSingle(anchor, sk, mousePt);
        if (inf) {
            auto ct = annotationKindToConstraintType(inf->kind);
            if (ct) {
                m_type = *ct;
                m_distMode = inf->distMode;
                m_useSupplementAngle = false;
                pushPreview({ anchor }, m_type, m_distMode);
            }
        }
        return;
    }

    // WaitDimPlace / WaitValue：不訂閱 GEOM_HOVER，不會走到這裡
}

// ─────────────────────────────────────────────────────────────────────────────
// onGeomPicked — Idle：鎖定起點，進 Anchored
//                Anchored：依這次點擊落點鎖定單幾何或雙幾何型別
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::onGeomPicked(const QVariant& payload)
{
    if (m_state != State::Idle && m_state != State::Anchored)
        return;

    QVariantMap map    = payload.toMap();
    QString     uuid   = map.value("geomUuid").toString();
    int         handle = map.value("handle", static_cast<int>(GeomHandle::WholeGeom)).toInt();
    // ⚠️ 型別分類（半徑/直徑、H/V/Align、X/Y/XY）一律用 rawPoint（未被
    // OSnap 吸附的原始游標位置），才會跟 onGeomHover() 的即時預覽用同一份
    // 座標基準，避免「hover 預覽顯示半徑，點擊卻因為吸附到圓周上的點而
    // 變成直徑」這種預覽與實際點擊結果不一致的問題（見 CadView::
    // handlePointInput() 的 rawPlanePt 說明）。若上游（舊版或其他來源）
    // 沒有帶 rawPoint，退回 point 以維持相容。
    QVector2D mousePt = map.contains("rawPoint")
                        ? map.value("rawPoint").value<QVector2D>()
                        : map.value("point").value<QVector2D>();

    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk   = activeSketch();

    // X 軸／Y 軸／原點：同 onGeomHover()，先把顯示層虛擬 UUID 換成 Sketch
    // 內真實幾何/點 UUID（見 resolveAxisRefUuid() 說明），起點鎖定與第二次
    // 點擊配對才能正確辨識、參與約束。標籤（axisLabel）必須在轉換前先取，
    // 供下面的訊息顯示用。
    const QString axisLabel = (sk && !uuid.isEmpty()) ? axisRefLabel(sk, uuid) : QString();
    if (sk && !uuid.isEmpty())
        uuid = resolveAxisRefUuid(sk, uuid);

    if (m_state == State::Idle) {
        // 起點必須選到 SketchPoint 或幾何元素，不允許點選空白處
        if (uuid.isEmpty()) {
            if (cmdMgr)
                cmdMgr->printError("請選取草圖上的點或幾何元素作為起點（不允許點選空白處）");
            return;
        }

        auto gh = static_cast<GeomHandle>(handle);
        GeomRef anchor(uuid, gh);

        if (sk) {
            auto* geom = sk->findGeometry(uuid);
            bool  isPt = (!geom && sk->point(uuid));

            QString geomDesc;
            if (!axisLabel.isEmpty()) {
                geomDesc = axisLabel;
            } else if (isPt) {
                geomDesc = "點";
            } else if (geom) {
                switch (geom->type) {
                case SketchGeometryType::Point:  geomDesc = "點";   break;
                case SketchGeometryType::Line:   geomDesc = "線段"; break;
                case SketchGeometryType::Circle: geomDesc = "圓";   break;
                case SketchGeometryType::Arc:    geomDesc = "弧";   break;
                default:                         geomDesc = "幾何"; break;
                }
                switch (gh) {
                case GeomHandle::Start:  geomDesc += " 起點"; break;
                case GeomHandle::End:    geomDesc += " 終點"; break;
                case GeomHandle::Center: geomDesc += " 圓心"; break;
                default: break;
                }
            }

            QVector2D pos = anchor.resolvePosition(sk);
            if (cmdMgr && !geomDesc.isEmpty())
                cmdMgr->printMessage(
                    QString("  起點: %1  (%2, %3)")
                    .arg(geomDesc)
                    .arg(static_cast<double>(pos.x()), 0, 'f', 2)
                    .arg(static_cast<double>(pos.y()), 0, 'f', 2));

            qDebug() << "[GDIM] anchor geomUuid=" << uuid << "gh=" << handle
                     << "pos=(" << pos.x() << "," << pos.y() << ")";
        }

        m_anchorRef = anchor;
        m_refs = { anchor };
        m_state = State::Anchored;
        m_stickyPairedRef.reset();  // 新錨點，清空舊的 sticky 配對候選

        // 起點鎖定後立刻進入連續判斷階段：依當下滑鼠位置顯示單幾何預覽
        // （無選單版第 0 節：「起點鎖定後，系統立刻進入一個連續判斷階段」）
        // ⚠️ 修正：若起點本身就是整條線（WholeGeom），同樣要用
        // lineWholeGeomEndpoints() 組出正確的 Start/End refs（理由與
        // onGeomHover()／上面 Anchored 分支的修正相同）；否則此處先顯示的
        // 這一格初始預覽（在下一次滑鼠移動被 onGeomHover() 覆蓋前）會用
        // 錯誤的單一 WholeGeom ref 顯示水平/垂直距離，可能短暫顯示不出來
        // 或造成瞬間的錯誤畫面。
        if (sk) {
            GeomRef startRef, endRef;
            if (lineWholeGeomEndpoints(anchor, sk, startRef, endRef)) {
                auto inf = GeneralDimClassifier::inferPair(startRef, endRef, sk, mousePt);
                if (inf) {
                    auto ct = annotationKindToConstraintType(inf->kind);
                    if (ct) {
                        m_type = *ct; m_distMode = inf->distMode;
                        QList<GeomRef> refs = inf->pairedRefs.isEmpty()
                            ? QList<GeomRef>{ startRef, endRef } : inf->pairedRefs;
                        pushPreview(refs, m_type, m_distMode);
                    }
                }
            } else {
                auto inf = GeneralDimClassifier::inferSingle(anchor, sk, mousePt);
                if (inf) {
                    auto ct = annotationKindToConstraintType(inf->kind);
                    if (ct) {
                        m_type = *ct; m_distMode = inf->distMode; m_useSupplementAngle = false;
                        pushPreview(m_refs, m_type, m_distMode);
                    }
                }
            }
        }

        if (cmdMgr)
            cmdMgr->showPrompt("GDIM 移動滑鼠選第二個幾何（配對）或移到空白處點擊（單幾何，同時定型與定位）");
        return;
    }

    // ── Anchored：這是第 2 次點擊 ────────────────────────────────────────────
    const GeomRef anchor = m_anchorRef;

    if (!uuid.isEmpty() && sk) {
        GeomRef secondRef(uuid, static_cast<GeomHandle>(handle));
        normalizeSecondRef(anchor, secondRef, sk);
        const bool sameAsAnchor =
            (secondRef.geomUuid == anchor.geomUuid && secondRef.handle == anchor.handle);

        if (!sameAsAnchor && GeneralDimClassifier::canPair(anchor, secondRef, sk)) {
            // 落在「另一個可配對的幾何」上 → 鎖定雙幾何型別，進 WaitDimPlace
            lockPairGeom(anchor, secondRef, mousePt);
            return;
        }
    }

    // ⚠️ 修正（issue #5）：這次點擊本身沒有命中任何可配對的幾何，但如果
    // hover 階段已經有 sticky 配對候選（見 m_stickyPairedRef 說明——使用者
    // 不需要把滑鼠停留在第二個點正上方才能維持配對狀態），這次點擊仍應
    // 視為「確認目前 sticky 候選的配對」，而不是退回單幾何流程。這樣才會
    // 跟 onGeomHover() 當下顯示的預覽一致（使用者看到什麼型別的預覽，
    // 點下去就得到什麼型別）。
    if (m_stickyPairedRef && GeneralDimClassifier::canPair(anchor, *m_stickyPairedRef, sk)) {
        lockPairGeom(anchor, *m_stickyPairedRef, mousePt);
        return;
    }

    // 落在空白處，或落在無法與起點配對的幾何上、也沒有 sticky 候選 →
    // 直接依滑鼠位置鎖定單幾何型別，並用這次點擊的位置同時定位
    // （無選單版第 0 節）
    lockSingleGeom(anchor, mousePt);
}

// ─────────────────────────────────────────────────────────────────────────────
// lockPairGeom / lockSingleGeom
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::lockPairGeom(const cad::GeomRef& anchor, const cad::GeomRef& second,
                                      const QVector2D& mousePt)
{
    Sketch* sk = activeSketch();
    auto inf = GeneralDimClassifier::inferPair(anchor, second, sk, mousePt);
    if (!inf) {
        // 理論上不會發生：onGeomPicked 已用 canPair() 確認過合法性，
        // 這裡僅作防呆 fallback，退回單幾何流程避免命令卡死。
        lockSingleGeom(anchor, mousePt);
        return;
    }
    auto ct = annotationKindToConstraintType(inf->kind);
    if (!ct) { lockSingleGeom(anchor, mousePt); return; }

    m_refs = inf->pairedRefs.isEmpty()
        ? QList<GeomRef>{ anchor, second } : inf->pairedRefs;
    m_type = *ct;
    m_distMode = inf->distMode;
    m_useSupplementAngle = inf->useSupplementAngle;
    m_wasSingleGeomFlow = false;

    // 點＋點（含同一條線的兩端點）配對：判斷依據是「anchor/second 兩個
    // ref 本身是否都是點狀（isPointLike）」，而不是這次點擊當下 inferPair()
    // 剛好回傳的 pairedRefs 是否為空。
    // ⚠️ 修正：先前用 `inf->pairedRefs.isEmpty()` 判斷，但同一條線的
    // Start/End 配對在「對角帶」（線長）時，inferPair() 會把 pairedRefs
    // 設成單一 WholeGeom ref（見該函式 sameLine 特例）——若這次點擊剛好
    // 落在對角帶，會誤判成「非點對點配對」，導致 m_isPointPairHVA 被鎖
    // 死為 false，WaitDimPlace 階段之後即使把滑鼠移到明確的上下/左右方向
    // 也不會重新分類成水平/垂直距離，等於這次新增的「選一線等同選兩點」
    // 功能只有第一次點擊當下的方位有效、之後就失效。改為直接檢查兩個
    // ref 本身是否為點狀，與該次分類結果（哪個 zone、pairedRefs 是否為空）
    // 無關，才能在整個 WaitDimPlace 期間穩定持續重分類。
    m_isPointPairHVA = GeneralDimClassifier::isPointLike(anchor, sk)
                     && GeneralDimClassifier::isPointLike(second, sk);
    if (m_isPointPairHVA) { m_hvaRefA = anchor; m_hvaRefB = second; }

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr)
        cmdMgr->printMessage(QString("  類型: %1").arg(constraintTypeName()));

    transitionToWaitDimPlace();
}

bool GeneralDimCommand::lineWholeGeomEndpoints(const cad::GeomRef& r, cad::Sketch* sk,
                                               cad::GeomRef& outStart, cad::GeomRef& outEnd) const
{
    // ⚠️ 修正：先前用 `r.handle != WholeGeom` 判斷「不是整條線」，過於
    // 嚴格——GEOM_HOVER/GEOM_PICKED 送來的 handle 在大多數情況下確實是
    // WholeGeom（見 CadView 對 DetectedInteractive 命中的處理），但無法
    // 保證所有來源都嚴格一致（例如透過 X 軸／Y 軸／原點虛擬 UUID 轉換、
    // 或未來其他呼叫路徑）。改用與 GeneralDimClassifier::inferSingle()
    // 完全一致、已驗證過的判斷方式：只要不是 Start／End，就視為「整條
    // 線」，避免兩處各自用不同的嚴格程度判斷同一件事、其中一處沒跟著
    // 更新就不同步。
    if (!sk) return false;
    if (r.handle == cad::GeomHandle::Start || r.handle == cad::GeomHandle::End) return false;
    auto* geom = sk->findGeometry(r.geomUuid);
    if (!geom || geom->type != cad::SketchGeometryType::Line) return false;
    outStart = cad::GeomRef(r.geomUuid, cad::GeomHandle::Start);
    outEnd   = cad::GeomRef(r.geomUuid, cad::GeomHandle::End);
    return true;
}

void GeneralDimCommand::lockSingleGeom(const cad::GeomRef& anchor, const QVector2D& mousePt)
{
    Sketch* sk = activeSketch();

    // ⚠️ 新增：整條線（WholeGeom）視為「同時選取其 Start/End 兩個端點」，
    // 讓使用者可依滑鼠位置在 線長／水平距離／垂直距離 之間選擇——與直接
    // 點選線段兩端點（點+點流程）完全等價的使用者體驗（本次需求：GDIM
    // 選一線後，也等同點兩點可由滑鼠位置選中形式）。轉呼叫 lockPairGeom()，
    // 同時重用其 WaitDimPlace 期間即時重分類的邏輯（見 subscribePreview()
    // 裡的 m_isPointPairHVA 分支），而不是像其餘單幾何型別那樣「點下去
    // 同時定型定位」。
    cad::GeomRef startRef, endRef;
    if (lineWholeGeomEndpoints(anchor, sk, startRef, endRef)) {
        lockPairGeom(startRef, endRef, mousePt);
        return;
    }

    auto inf = GeneralDimClassifier::inferSingle(anchor, sk, mousePt);
    auto* cmdMgr = core::CommandLineManager::instance();
    if (!inf) {
        if (cmdMgr) cmdMgr->printError("無法辨識此幾何元素的標註型別");
        return;
    }
    auto ct = annotationKindToConstraintType(inf->kind);
    if (!ct) {
        if (cmdMgr) cmdMgr->printError("無法辨識此幾何元素的標註型別");
        return;
    }

    m_refs = { anchor };
    m_type = *ct;
    m_distMode = inf->distMode;
    m_useSupplementAngle = false;
    m_wasSingleGeomFlow = true;
    m_isPointPairHVA = false;  // 單幾何流程不需要 WaitDimPlace 期間的 H/V/Align 重分類

    // 單幾何：這次點擊「同時完成定型與定位」——不進 WaitDimPlace，
    // 直接以起點與這次點擊的滑鼠位置算出 offset，直接進 WaitValue。
    m_measuredValue  = measureCurrentValue();
    m_measuredValue2 = measureCurrentValue2();
    m_dimAnchor2D    = refMidpoint2D();
    QVector2D offset = mousePt - m_dimAnchor2D;
    m_dimOffsetX = static_cast<double>(offset.x());
    m_dimOffsetY = static_cast<double>(offset.y());

    if (cmdMgr)
        cmdMgr->printMessage(QString("  類型: %1").arg(constraintTypeName()));

    pushPreview(m_refs, m_type, m_distMode);

    // 離開 Anchored：不再需要 GEOM_PICKED / GEOM_HOVER
    unsubscribeGeomPicked();
    { auto* bus = core::Application::instance()->eventBus();
      if (bus) bus->unsubscribe(core::Events::GEOM_HOVER, this); }

    auto* app     = core::Application::instance();
    auto* ui      = app ? app->uiManager() : nullptr;
    auto* cadView = ui  ? ui->cadView()    : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    transitionToWaitValue();
}

// ─────────────────────────────────────────────────────────────────────────────
// onStringInput
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::onStringInput(const QVariant& payload)
{
    auto* cmdMgr = core::CommandLineManager::instance();
    QString input = payload.toString().trimmed();

    if (m_state != State::WaitValue) return;

    if (input.isEmpty()) {
        m_pendingValue = m_measuredValue;
        m_pendingIsRawUserInput = false;   // 沿用量測值（角度已是弧度），不再轉換
    } else {
        bool isNum;
        double v = input.toDouble(&isNum);
        if (isNum) {
            m_pendingValue = v;
            m_pendingExpr.clear();
            m_pendingIsRawUserInput = true;
        } else {
            cad::Sketch* sk = activeSketch();
            if (!sk) { cleanup(); return; }
            auto [ok, ev] = sk->parameterStore()->evaluate(input);
            if (!ok) {
                if (cmdMgr) cmdMgr->printError(
                    QString("Unknown expression: '%1'").arg(input));
                return;
            }
            m_pendingValue = ev;
            m_pendingExpr  = input;
            m_pendingIsRawUserInput = true;
        }
    }
    commitDimension();
}

// ─────────────────────────────────────────────────────────────────────────────
// onDimConfirmed
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::onDimConfirmed(const QVariant& payload)
{
    if (m_state != State::WaitDimPlace) return;

    // ★ 立即取消 DIM_LINE_PREVIEW 訂閱，防止後續非同步的 preview 事件
    //   覆蓋點擊當下已確認的 offset 值
    auto* bus = core::Application::instance()->eventBus();
    if (bus) bus->unsubscribe(core::Events::DIM_LINE_PREVIEW, this);

    QVariantMap map = payload.toMap();
    m_dimOffsetX = map.value("offsetX").toDouble();
    m_dimOffsetY = map.value("offsetY").toDouble();

    // 點＋點（含線的兩端點）：確保「確定」當下這一刻的滑鼠位置也套用一次
    // H/V/Align(或線長) 重分類——理論上前一個 DIM_LINE_PREVIEW 事件已經處理
    // 過幾乎相同的位置，但點擊瞬間可能沒有先觸發 PREVIEW（例如滑鼠沒有
    // 移動、直接點擊），這裡再做一次確保「顯示的預覽」與「實際鎖定的型別」
    // 一致。⚠️ 同上：改用 m_hvaRefA/m_hvaRefB，並同步更新 m_refs（理由見
    // subscribePreview() 內同一段修正說明）。
    if (m_isPointPairHVA) {
        Sketch* sk = activeSketch();
        if (sk) {
            QVector2D mousePtAbs(
                static_cast<float>(m_dimAnchor2D.x() + m_dimOffsetX),
                static_cast<float>(m_dimAnchor2D.y() + m_dimOffsetY));
            auto pairInf = GeneralDimClassifier::inferPair(m_hvaRefA, m_hvaRefB, sk, mousePtAbs);
            if (pairInf) {
                auto pairCt = annotationKindToConstraintType(pairInf->kind);
                if (pairCt) {
                    m_type     = *pairCt;
                    m_distMode = pairInf->distMode;
                    m_refs     = pairInf->pairedRefs.isEmpty()
                        ? QList<GeomRef>{ m_hvaRefA, m_hvaRefB }
                        : pairInf->pairedRefs;
                }
            }
        }
    }

    transitionToWaitValue();
}

// ─────────────────────────────────────────────────────────────────────────────
// onCancelled — 依目前狀態退回上一步（無選單版第 D 組，25–27）
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::onCancelled(const QVariant&) {
    switch (m_state) {
    case State::Anchored:
        backToIdle();
        return;
    case State::WaitDimPlace:
        backToAnchoredFromDimPlace();
        return;
    case State::WaitValue:
        if (m_wasSingleGeomFlow) backToAnchoredFromValue();
        else                     backToDimPlaceFromValue();
        return;
    case State::Idle:
    default:
        cleanup();
        return;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 狀態轉換
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::transitionToWaitDimPlace()
{
    m_state = State::WaitDimPlace;

    m_measuredValue  = measureCurrentValue();
    m_measuredValue2 = measureCurrentValue2();

    auto* app     = core::Application::instance();
    auto* ui      = app ? app->uiManager() : nullptr;
    auto* cadView = ui  ? ui->cadView()    : nullptr;

    if (cadView) {
        // 先更新預覽資訊（確保 paintDimPreview 有正確的 refs/type/value）
        pushPreview(m_refs, m_type, m_distMode);
        // 再切換 mode → CadView 的 mouseMoveEvent 會持續觸發 repaint
        m_dimAnchor2D = refMidpoint2D();  // ★ 記錄 anchor，commit 時修正 H/V offset 基準
        cadView->setMode(view::InteractionMode::PlaceDimLine);
        cadView->beginPlaceDimLine(m_dimAnchor2D);
    }

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->showPrompt("GDIM 移動滑鼠定位尺寸線，點擊確認位置");

    unsubscribeGeomPicked();
    // WaitDimPlace 不再需要 hover 預覽
    { auto* bus = core::Application::instance()->eventBus();
      if (bus) bus->unsubscribe(core::Events::GEOM_HOVER, this); }
    subscribeDimConfirmed();
    subscribePreview();
}

void GeneralDimCommand::transitionToWaitValue()
{
    m_state = State::WaitValue;

    auto* app     = core::Application::instance();
    // ⚠️ 這裡刻意用 uiMg（而非常見的 ui）當變數名：aicad::ui 同時是
    // namespace 名稱，下面會用到 ui::DimExpressionDialog，若局部變數也叫
    // ui 會把 namespace 遮蔽掉（先前已經在別處因為這個原因修過一次，這裡
    // 一併注意避免重蹈覆轍）。
    auto* uiMg    = app ? app->uiManager() : nullptr;
    auto* cadView = uiMg ? uiMg->cadView() : nullptr;
    if (cadView)
        cadView->setMode(view::InteractionMode::Sketching);

    // 若 execute() 時已帶入數值，跳過輸入直接 commit
    if (m_hasPending) {
        commitDimension();
        return;
    }

    // ⚠️ 修正（需求：取消 GDIM 命令列等待輸入數值/運算式，直接顯示編輯框）：
    // 先前這裡會 showPrompt() + waitForInput(String) + subscribeStringInput()，
    // 讓使用者在「命令列」打字輸入數值；現在改成直接彈出
    // DimExpressionDialog（非模態，支援「插入參考」點選畫面上其他尺寸標註
    // 取得其參數名稱），數值/運算式的解析邏輯與原本 onStringInput() 完全
    // 相同，只是輸入來源換成對話框（見 onValueDialogAccepted()）。
    //
    // 這裡順便直接算出「若採用預設值會得到的自動命名參數」（autoName），
    // 顯示在對話框標題——原本 commitDimension() 尾端另外彈出的
    // DimExpressionDialog（用來讓使用者事後補上引用其他尺寸的運算式）
    // 已經沒有必要：現在使用者一開始就能在這個對話框裡直接輸入/插入引用，
    // 不需要建立後再跳第二個對話框（已同步移除該處程式碼，見
    // commitDimension() 的說明）。
    cad::Sketch* sk = activeSketch();
    if (!sk) { cleanup(); return; }

    const bool isAngleType = (m_type == ConstraintType::FixedAngleDim ||
                              m_type == ConstraintType::FixedAngle);
    // 角度類型：m_measuredValue 內部為弧度，對話框預設值改顯示「度」讓使用者輸入直覺一致
    double defVal = isAngleType ? (m_measuredValue * 180.0 / M_PI) : m_measuredValue;
    QString defStr = QString::number(defVal, 'f', 3);

    QString prefix   = command::autoParamPrefix(m_type);
    QString autoName = prefix.isEmpty() ? QString() : command::nextAutoParamName(sk, prefix);

    // constraintUuid 傳空字串：此時約束尚未建立，DimExpressionDialog 只用
    // 這個值來排除「自己」（避免插入參考時選到自己），建立前本來就不會有
    // 這個問題，空字串不會誤配到任何既有約束的 uuid，安全。
    auto* dlg = new aicad::ui::DimExpressionDialog(sk, QString(), autoName, defStr, cadView);
    QObject::connect(dlg, &aicad::ui::DimExpressionDialog::expressionAccepted,
        this, [this](const QString&, const QString& expr) {
            onValueDialogAccepted(expr);
        });
    QObject::connect(dlg, &QDialog::rejected, this, [this]() {
        onValueDialogRejected();
    });
    dlg->show();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr)
        cmdMgr->showPrompt(tr("GDIM：請在彈出的編輯框輸入數值或運算式（Enter/確定 = %1）")
                               .arg(defStr));
}

void GeneralDimCommand::onValueDialogAccepted(const QString& expr)
{
    if (m_state != State::WaitValue) return;  // 防呆：對話框已經是上一輪殘留的，忽略

    auto* cmdMgr = core::CommandLineManager::instance();
    QString input = expr.trimmed();

    if (input.isEmpty()) {
        m_pendingValue = m_measuredValue;
        m_pendingIsRawUserInput = false;   // 沿用量測值（角度已是弧度），不再轉換
    } else {
        bool isNum;
        double v = input.toDouble(&isNum);
        if (isNum) {
            m_pendingValue = v;
            m_pendingExpr.clear();
            m_pendingIsRawUserInput = true;
        } else {
            cad::Sketch* sk = activeSketch();
            if (!sk) { cleanup(); return; }
            auto [ok, ev] = sk->parameterStore()->evaluate(input);
            if (!ok) {
                if (cmdMgr) cmdMgr->printError(
                    QString("Unknown expression: '%1'").arg(input));
                // 輸入無效：重新彈出對話框讓使用者修正，而不是整個 GDIM 取消。
                transitionToWaitValue();
                return;
            }
            m_pendingValue = ev;
            m_pendingExpr  = input;
            m_pendingIsRawUserInput = true;
        }
    }
    commitDimension();
}

void GeneralDimCommand::onValueDialogRejected()
{
    if (m_state != State::WaitValue) return;
    // 對話框按取消：比照原本 Esc 在 WaitValue 階段的既有行為（單幾何流程
    // 退回 Anchored、雙幾何流程退回 WaitDimPlace），沿用 onCancelled()。
    onCancelled(QVariant());
}

// ─────────────────────────────────────────────────────────────────────────────
// Esc / 右鍵：依目前狀態退回上一步（無選單版第 D 組）
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::backToIdle()
{
    // 25：Anchored（已鎖定起點，尚未第二次點擊）→ Idle，清空起點
    m_refs.clear();
    m_anchorRef = cad::GeomRef{};
    m_stickyPairedRef.reset();
    m_hasLastHoverRef = false;  // 清空 Idle 純 hover 配對用的軟性錨點記憶
    m_state = State::Idle;
    clearDimPreview();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->showPrompt(GeneralDimClassifier::initialPrompt());
    // GEOM_PICKED / GEOM_HOVER 從 Idle 進 Anchored 時從未取消訂閱，這裡不需重新訂閱
}

void GeneralDimCommand::backToAnchoredFromDimPlace()
{
    // 26：WaitDimPlace（已鎖定第二幾何，尚未點擊定位）→ Anchored，
    //     取消第二幾何鎖定，起點保留
    auto* bus = core::Application::instance()->eventBus();
    if (bus) {
        bus->unsubscribe(core::Events::DIM_LINE_CONFIRMED, this);
        bus->unsubscribe(core::Events::DIM_LINE_PREVIEW,   this);
    }

    m_refs = { m_anchorRef };
    m_state = State::Anchored;
    m_dimOffsetX = 0.0; m_dimOffsetY = 0.0;
    m_useSupplementAngle = false;
    m_isPointPairHVA = false;
    m_stickyPairedRef.reset();  // 取消目前的配對鎖定，強制重新 hover 選第二幾何

    auto* app     = core::Application::instance();
    auto* ui      = app ? app->uiManager() : nullptr;
    auto* cadView = ui  ? ui->cadView()    : nullptr;
    if (cadView) {
        cadView->clearDimPreview();
        cadView->setMode(view::InteractionMode::GetGeom);
    }

    subscribeGeomPicked();
    subscribeGeomHover();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr)
        cmdMgr->showPrompt("GDIM 移動滑鼠選第二個幾何（配對）或移到空白處點擊（單幾何，同時定型與定位）");
}

void GeneralDimCommand::backToAnchoredFromValue()
{
    // 27a：WaitValue（單幾何流程：第 2 次點擊同時定型定位）→ Anchored，
    //      整個「定型/定位」動作重來，起點保留
    auto* bus = core::Application::instance()->eventBus();
    if (bus) bus->unsubscribe(core::Events::STRING_INPUT, this);

    m_refs = { m_anchorRef };
    m_state = State::Anchored;
    m_dimOffsetX = 0.0; m_dimOffsetY = 0.0;
    m_stickyPairedRef.reset();

    auto* app     = core::Application::instance();
    auto* ui      = app ? app->uiManager() : nullptr;
    auto* cadView = ui  ? ui->cadView()    : nullptr;
    if (cadView) {
        cadView->clearDimPreview();
        cadView->setMode(view::InteractionMode::GetGeom);
    }

    subscribeGeomPicked();
    subscribeGeomHover();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr)
        cmdMgr->showPrompt("GDIM 移動滑鼠選第二個幾何（配對）或移到空白處點擊（單幾何，同時定型與定位）");
}

void GeneralDimCommand::backToDimPlaceFromValue()
{
    // 27b：WaitValue（雙幾何流程）→ WaitDimPlace，重新拖曳決定位置，
    //      第二幾何鎖定仍保留
    auto* bus = core::Application::instance()->eventBus();
    if (bus) bus->unsubscribe(core::Events::STRING_INPUT, this);

    m_state = State::WaitDimPlace;

    auto* app     = core::Application::instance();
    auto* ui      = app ? app->uiManager() : nullptr;
    auto* cadView = ui  ? ui->cadView()    : nullptr;
    if (cadView) {
        pushPreview(m_refs, m_type, m_distMode);
        cadView->setMode(view::InteractionMode::PlaceDimLine);
        cadView->beginPlaceDimLine(m_dimAnchor2D);
    }

    subscribeDimConfirmed();
    subscribePreview();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->showPrompt("GDIM 移動滑鼠定位尺寸線，點擊確認位置");
}

// ─────────────────────────────────────────────────────────────────────────────
// commitDimension
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::commitDimension()
{
    Sketch* sk = activeSketch();
    if (!sk) { cleanup(); return; }

    SketchConstraint c;
    c.type           = m_type;
    c.refs           = m_refs;
    // 角度類型（FixedAngleDim/FixedAngle）：內部一律以弧度儲存（求解器與顯示皆假設弧度）。
    // 使用者輸入（literal number 或 expression 求值結果）視為「度」，需轉換；
    // 若 m_pendingValue 直接沿用 m_measuredValue（使用者按 Enter 採用量測值），
    // 該值本身已經是弧度，不能重複轉換。
    const bool isAngleType = (m_type == ConstraintType::FixedAngleDim ||
                              m_type == ConstraintType::FixedAngle);
    c.value          = (isAngleType && m_pendingIsRawUserInput)
                        ? (m_pendingValue * M_PI / 180.0)
                        : m_pendingValue;
    c.value2         = (m_type == ConstraintType::CoordinateDim)
                       ? m_measuredValue2 : 0.0;
    c.paramExpr      = m_pendingExpr;
    c.driving        = m_driving;
    c.distMode       = m_distMode;

    // ⚠️ 新增：尺寸約束自動命名參數（d1, d2, a1, a2…）。driving 的尺寸
    // 約束自動在 ParameterStore 註冊一個穩定名稱，讓其他尺寸約束的表達式
    // 可以直接引用它（既有的 EDITCON／applyDimensionEdit() 本來就支援
    // ParameterStore 表達式，這裡只是讓每個尺寸自動有一個好記的名稱可用，
    // 不用去翻 UUID）。使用者原本輸入的內容（純數字或表達式）改存成這個
    // 自動名稱「自己的」定義，約束本身的 paramExpr 換成這個名稱——之後
    // 編輯這條約束時（見 applyDimensionEdit() 對應的修正），會更新「這個
    // 名稱自己的定義」而不是把 paramExpr 換掉，讓名稱維持穩定、其他約束
    // 對它的引用才不會失效。CoordinateDim 有兩個數值，autoParamPrefix()
    // 回傳空字串，不會進到這裡，維持原行為。
    // autoName 提升到函式範圍：commit 完成後要用它彈出 DimExpressionDialog
    // （見函式尾端），並顯示在對話框標題「d3 =」。
    QString autoName;
    if (m_driving) {
        const QString prefix = command::autoParamPrefix(m_type);
        if (!prefix.isEmpty()) {
            if (auto* store = sk->parameterStore()) {
                autoName = command::nextAutoParamName(sk, prefix);
                if (!autoName.isEmpty()) {
                    // 註冊值一律用「使用者看到/輸入的單位」：角度是度（跟
                    // 手動輸入 paramExpr 時的慣例一致），其餘型別跟
                    // c.value 同單位。m_pendingValue 在「使用者直接輸入
                    // 數字」時就是度/原始單位；但若使用者按 Enter 採用
                    // 量測值（m_pendingIsRawUserInput == false）且是角度
                    // 類型，m_pendingValue 此時等於量測到的弧度，需要換算。
                    double rawValue = m_pendingValue;
                    if (isAngleType && !m_pendingIsRawUserInput)
                        rawValue = m_pendingValue * 180.0 / M_PI;

                    QString originalExpr = c.paramExpr.isEmpty()
                        ? QString::number(rawValue) : c.paramExpr;
                    store->setLocal(autoName, originalExpr);
                    c.paramExpr = autoName;
                }
            }
        }
    }

    // ★ H/V 情境：offset 是相對 anchor（起點）的偏移，
    //   但 drawHorizontalDim/drawVerticalDim 期望的是相對 abMid 的偏移。
    //   absMouse = anchor + offset，需要轉換：offsetFromMid = absMouse - abMid
    double finalOffsetX = m_dimOffsetX;
    double finalOffsetY = m_dimOffsetY;
    if (m_type == ConstraintType::FixedHorizDist ||
        m_type == ConstraintType::FixedVertDist) {
        // 計算 refs 的 abMid（兩端點平均）
        QVector2D abMid;
        if (m_refs.size() >= 2) {
            QVector2D p0 = m_refs[0].resolvePosition(sk);
            QVector2D p1 = m_refs[1].resolvePosition(sk);
            abMid = (p0 + p1) * 0.5f;
        } else if (!m_refs.isEmpty()) {
            abMid = m_refs[0].resolvePosition(sk);
        }
        // absMouse = anchor + offset；offsetFromMid = absMouse - abMid
        QVector2D absMouse(static_cast<float>(m_dimAnchor2D.x() + m_dimOffsetX),
                           static_cast<float>(m_dimAnchor2D.y() + m_dimOffsetY));
        finalOffsetX = static_cast<double>(absMouse.x() - abMid.x());
        finalOffsetY = static_cast<double>(absMouse.y() - abMid.y());
    }
    c.dimLineOffsetX = finalOffsetX;
    c.dimLineOffsetY = finalOffsetY;

    auto* cmdMgr = core::CommandLineManager::instance();

    // ── UUID 關聯確認（qDebug，不顯示在命令列）──────────────────────────────
    for (int i = 0; i < c.refs.size(); ++i) {
        const auto& r = c.refs[i];
        QString ptUuid   = r.resolvedPointUuid(sk);
        auto*   geom     = sk->findGeometry(r.geomUuid);
        QString geomDesc = geom
            ? QString("geom=%1").arg(r.geomUuid.left(8))
            : (sk->point(r.geomUuid) ? QString("pt=%1").arg(r.geomUuid.left(8)) : "?");
        qDebug() << "[GDIM] commit refs[" << i << "]" << geomDesc
                 << "ptUuid=" << ptUuid;
    }

    int dofBefore = sk->degreesOfFreedom();

    // 重複尺寸偵測——commit 前檢查是否已存在相同標註，有的話僅提醒（不阻擋），
    // 避免使用者誤以為建立失敗；仍會照常建立/更新。
    auto akind = cad::constraintTypeToAnnotationKind(m_type);
    if (akind) {
        if (sk->findDuplicateAnnotation(m_refs, *akind)) {
            if (cmdMgr)
                cmdMgr->printMessage(
                    tr("⚠ 已存在相同標註（%1），仍會建立此標註").arg(constraintTypeName()),
                    core::MessageType::Warning);
        }
    }

    // 優先走 SketchAnnotation 統一路徑（annotationKindToConstraintType()/
    // toImplicitConstraint() 確保產生的隱含約束與過去直接 addConstraint()
    // 完全等價）。只有在 m_type 是純幾何約束型別（理論上 GDIM 不會走到，
    // 此處僅作防呆 fallback）時，才退回舊的 addConstraint() 路徑。
    SolveResult result;
    if (akind) {
        cad::SketchAnnotation ann;
        ann.kind         = *akind;
        ann.refs         = c.refs;
        ann.value        = c.value;
        ann.value2       = c.value2;
        ann.paramExpr    = c.paramExpr;
        ann.driving      = c.driving;
        ann.distMode     = c.distMode;
        ann.dimLineOffset = QVector2D(static_cast<float>(c.dimLineOffsetX),
                                       static_cast<float>(c.dimLineOffsetY));

        // 規範檢查（優先度最低，只提醒不阻擋）
        for (const auto& w : cad::AnnotationStandardsChecker::checkAnnotation(ann, ann.value)) {
            if (cmdMgr)
                cmdMgr->printMessage(
                    tr("⚠ [%1] %2").arg(w.code, w.message),
                    core::MessageType::Warning);
        }

        sk->addAnnotation(ann);
        // addAnnotation() 內部已呼叫過一次 solveConstraints()（driving 標註皆如此），
        // 這裡再呼叫一次單純是為了取得 SolveResult 回傳值供下方訊息回報使用——
        // 此時系統已收斂在同一組數值，重複求解成本可忽略、無數值風險。
        result = sk->solveConstraints();
    } else {
        sk->addConstraint(c);
        result = sk->solveConstraints();
    }
    int dofAfter = sk->degreesOfFreedom();

    if (cmdMgr) {
        cmdMgr->printSuccess(
            QString("✅ %1 applied. DOF: %2 → %3")
            .arg(constraintTypeName()).arg(dofBefore).arg(dofAfter));
        reportSolveResult(result, cmdMgr);
    }

    // ⚠️ 移除：先前這裡在約束建立後自動彈出 DimExpressionDialog，讓使用者
    // 事後補上引用其他尺寸的運算式。現在改成 transitionToWaitValue() 一開始
    // 就直接彈出同一個對話框取值（取代命令列輸入，見該函式說明），使用者
    // 一開始就能輸入/插入引用，不需要建立後再跳出第二個對話框——否則每次
    // GDIM 都會連續看到兩個對話框，體驗反而更差。autoName 仍保留（上面用來
    // 把它設進 c.paramExpr／ann.paramExpr），只是不再用來彈窗。

    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────────
// cleanup
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::cleanup()
{
    unsubscribeAll();
    m_state          = State::Idle;
    m_refs.clear();
    m_anchorRef      = cad::GeomRef{};
    m_useSupplementAngle = false;
    m_wasSingleGeomFlow  = false;
    m_isPointPairHVA     = false;
    m_pendingValue   = 0.0;
    m_pendingExpr.clear();
    m_dimOffsetX     = 0.0;
    m_dimOffsetY     = 0.0;
    m_dimAnchor2D    = QVector2D{};
    m_hasPending     = false;
    m_pendingIsRawUserInput = false;
    auto* app     = core::Application::instance();
    auto* ui      = app ? app->uiManager() : nullptr;
    auto* cadView = ui  ? ui->cadView()    : nullptr;
    if (cadView) {
        cadView->clearDimPreview();
        cadView->setMode(view::InteractionMode::Sketching);
        cadView->setGdimWholeGeomHitTestEnabled(false);
    }

    // ⚠️ setMode(Sketching) 會觸發 UIManager 對 CadView::modeChanged 的連線，
    // 該連線在「離開 GetGeom」時會把所有 SketchPointAIS Deactivate()（見
    // UIManager 建構子內 modeChanged 連線的說明）。這與「SketchPoint 永遠
    // 顯示＋可選取」的設計原則牴觸——commitDimension() 成功時雖然
    // solveConstraints() 已經觸發過一次 reshowSketchEditOverlays()，但
    // 上面這行 setMode() 在其後執行，會把剛還原的狀態又蓋掉；取消流程
    // （未呼叫 solveConstraints()）更是完全沒有任何一次 reshow。因此這裡
    // 明確再呼叫一次，確保無論命令是「完成」或「取消」結束，SketchPoint
    // 都維持顯示＋可選取，不會被隱藏。
    if (ui) ui->reshowSketchEditOverlays(activeSketch());

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->clearPrompt();

    // ★ 通知 CommandManager 命令已結束
    if (state() == CommandState::Running)
        complete(CommandResult::Success());
}

} // namespace aicad::command
