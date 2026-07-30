#include "GeneralDimCommand.h"
#include "cad/sketch/AnnotationStandardsChecker.h"
#include "ConstraintCommands.h"  // reportSolveResult
#include "../core/Application.h"
#include "../core/CommandLineManager.h"
#include "../core/EventBus.h"
#include "../core/ParameterStore.h"
#include "../cad/Sketch.h"
#include "../ui/UIManager.h"
#include "../view/CadView.h"
#include <QMetaObject>
#include <QTimer>
#include <cmath>
#include <Geom_Circle.hxx>

namespace aicad::command {

using namespace cad;

// ─────────────────────────────────────────────────────────────────────────────
// ctor
// ─────────────────────────────────────────────────────────────────────────────

GeneralDimCommand::GeneralDimCommand()
    : Command("GDIM", "General Dimension：自動判斷尺寸類型") {}

// ─────────────────────────────────────────────────────────────────────────────
// 輔助
// ─────────────────────────────────────────────────────────────────────────────

cad::Sketch* GeneralDimCommand::activeSketch() const {
    return core::Application::instance()->activeSketch();
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
        if (m_useSupplementAngle) ang = M_PI - ang;        // 補角（Auto_DIM.md 第三節）
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

            // ── 依滑鼠偏移方向動態決定 F / H / V ─────────────────────────
            using CT = cad::ConstraintType;
            using GH = cad::GeomHandle;

            // PointToLine / LineToLine / FixedAngleDim 等不做 H/V 切換
            const bool isFHV =
                (m_type == CT::FixedDistance  ||
                 m_type == CT::FixedHorizDist ||
                 m_type == CT::FixedVertDist  ||
                 m_type == CT::FixedLength)
                && m_distMode != cad::DistanceMode::PointToLine
                && m_distMode != cad::DistanceMode::LineToLine;
            if (!isFHV) return;

            auto* sk = activeSketch();
            if (!sk) return;

            // ── 取兩端點（不管 m_refs 現在有幾個）──────────────────────
            // m_originalLineRef 儲存 FixedLength 的原始單 ref，
            // 切換 H/V 時 m_refs 會被替換成 Start+End 兩個 refs。
            // 先確保 m_originalLineRef 有效
            const bool wasFixedLength =
                !m_originalLineRef.geomUuid.isEmpty();
            const bool isCurrentlyExpanded =
                wasFixedLength && m_refs.size() == 2;

            // 取線段兩端點（用於計算 AB 方向和 offset）
            QVector2D pa, pb;
            QString   lineUuid;
            if (wasFixedLength) {
                lineUuid = m_originalLineRef.geomUuid;
            } else if (!m_refs.isEmpty()) {
                lineUuid = m_refs[0].geomUuid;
            }

            if (!lineUuid.isEmpty()) {
                auto* geom = sk->findGeometry(lineUuid);
                if (auto* ln = dynamic_cast<const cad::SketchLine*>(geom)) {
                    pa = ln->start;
                    pb = ln->end;
                } else if (m_refs.size() >= 2) {
                    pa = m_refs[0].resolvePosition(sk);
                    pb = m_refs[1].resolvePosition(sk);
                } else return;
            } else if (m_refs.size() >= 2) {
                pa = m_refs[0].resolvePosition(sk);
                pb = m_refs[1].resolvePosition(sk);
            } else return;

            QVector2D ab    = pb - pa;
            float     abLen = ab.length();
            if (abLen < 1e-4f) return;

            QVector2D offset(static_cast<float>(m_dimOffsetX),
                             static_cast<float>(m_dimOffsetY));
            if (offset.length() < 1e-3f) return;

            // AB 法向量
            QVector2D perpAB(-ab.y() / abLen, ab.x() / abLen);

            // ── 判斷 F / H / V ────────────────────────────────────────────
            // offset 和 AB 法向夾角 < 30° → F；否則依 |x| vs |y| → H/V
            float cosToPerp = std::abs(
                QVector2D::dotProduct(offset.normalized(), perpAB));

            CT newType;
            if (cosToPerp >= 0.866f) {
                // 靠近 AB 法向 → FixedLength（單線）或 FixedDistance（兩點）
                newType = wasFixedLength ? CT::FixedLength : CT::FixedDistance;
            } else {
                newType = (std::abs(offset.y()) >= std::abs(offset.x()))
                          ? CT::FixedHorizDist
                          : CT::FixedVertDist;
            }

            if (newType == m_type) return;  // 無變化，不更新

            // ── 切換 m_refs ───────────────────────────────────────────────
            if (wasFixedLength && newType != CT::FixedLength) {
                // FixedLength → H/V：展開成 Start + End 兩個 refs
                if (!isCurrentlyExpanded) {
                    auto* geom = sk->findGeometry(m_originalLineRef.geomUuid);
                    if (auto* ln = dynamic_cast<const cad::SketchLine*>(geom)) {
                        // 找到線段端點的 UUID
                        QString startUuid = ln->startUuid;
                        QString endUuid   = ln->endUuid;
                        if (!startUuid.isEmpty() && !endUuid.isEmpty()) {
                            m_refs.clear();
                            m_refs.append(cad::GeomRef(startUuid, GH::WholeGeom));
                            m_refs.append(cad::GeomRef(endUuid,   GH::WholeGeom));
                        } else {
                            // fallback：用 geomUuid + Start/End handle
                            m_refs.clear();
                            m_refs.append(cad::GeomRef(m_originalLineRef.geomUuid, GH::Start));
                            m_refs.append(cad::GeomRef(m_originalLineRef.geomUuid, GH::End));
                        }
                    }
                }
            } else if (wasFixedLength && newType == CT::FixedLength) {
                // H/V → FixedLength：還原成單個 WholeGeom ref
                m_refs.clear();
                m_refs.append(m_originalLineRef);
            }

            m_type = newType;
            m_measuredValue = measureCurrentValue();
            updateDimPreview(nullptr);
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
    bus->unsubscribe(core::Events::CANDIDATE_CYCLE,     this);  // Phase 2
}

void GeneralDimCommand::subscribeCandidateCycle() {
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::CANDIDATE_CYCLE, this,
        [this](const QVariant& v) {
            // 直接處理（不排隊），確保 Tab/Space 連續按下時反應即時
            onCandidateCycle(v);
        });
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

    // 進入 GetGeom 模式
    auto* ui = app->uiManager();
    if (ui && ui->cadView())
        ui->cadView()->setMode(view::InteractionMode::GetGeom);

    // ★ 必須設為 Running，命令才會持續存活等待使用者互動
    setState(CommandState::Running);

    subscribeGeomPicked();
    subscribeGeomHover();   // Idle 階段也要 hover 預覽
    subscribeCancelled();

    if (cmdMgr) cmdMgr->showPrompt(GeneralDimClassifier::initialPrompt());
    return CommandResult::Success("Waiting for input");
}

// ─────────────────────────────────────────────────────────────────────────────
// updateDimPreview / clearDimPreview
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::updateDimPreview(const cad::GeomRef* extraRef)
{
    auto* app     = core::Application::instance();
    auto* ui      = app ? app->uiManager() : nullptr;
    auto* cadView = ui  ? ui->cadView()    : nullptr;
    if (!cadView) return;

    Sketch* sk = activeSketch();
    if (!sk) { cadView->clearDimPreview(); return; }

    // 組合臨時 refs
    QList<GeomRef> tempRefs = m_refs;
    if (extraRef && !extraRef->geomUuid.isEmpty())
        tempRefs.append(*extraRef);

    // ── 第二選 handle 正規化 ─────────────────────────────────────────────────
    // 當第一選是整條幾何（WholeGeom），第二選若是同類型幾何但 OSnap 吸附到端點
    // （handle = Start/End），強制改為 WholeGeom，確保 classifyPair 走正確分支
    if (tempRefs.size() == 2 && sk) {
        const GeomRef& first  = tempRefs[0];
        GeomRef&       second = tempRefs[1];
        auto* geomFirst  = sk->findGeometry(first.geomUuid);
        auto* geomSecond = sk->findGeometry(second.geomUuid);
        const bool firstIsWhole =
            geomFirst && first.handle == GeomHandle::WholeGeom;
        const bool secondIsGeom =
            geomSecond &&
            (geomSecond->type == SketchGeometryType::Line   ||
             geomSecond->type == SketchGeometryType::Arc    ||
             geomSecond->type == SketchGeometryType::Circle);
        if (firstIsWhole && secondIsGeom &&
            second.handle != GeomHandle::WholeGeom)
        {
            second = GeomRef(second.geomUuid, GeomHandle::WholeGeom);
        }
    }

    if (tempRefs.isEmpty()) { cadView->clearDimPreview(); return; }

    // WaitDimPlace / WaitCandidate 狀態：m_type/m_distMode 已確定
    // （WaitDimPlace 由 subscribePreview 動態設好 F/H/V；WaitCandidate 由
    // applyHighlightedCandidate() 依目前高亮候選設定），直接使用，
    // 不重新 classify（classify 會覆蓋掉使用者目前的選擇/高亮）
    ConstraintType useType;
    DistanceMode   useMode;
    if (m_state == State::WaitDimPlace || m_state == State::WaitCandidate) {
        useType = m_type;
        useMode = m_distMode;
    } else {
        auto candidates = GeneralDimClassifier::classifyAll(tempRefs, sk);
        if (candidates.isEmpty()) { cadView->clearDimPreview(); return; }
        auto ct = annotationKindToConstraintType(candidates[0].kind);
        if (!ct) { cadView->clearDimPreview(); return; }
        useType = *ct;
        useMode = candidates[0].distMode;
    }

    // 用 useType/useMode 量測數值
    QList<GeomRef> savedRefs = m_refs;
    ConstraintType savedType = m_type;
    DistanceMode   savedMode = m_distMode;
    m_refs     = tempRefs;
    m_type     = useType;
    m_distMode = useMode;
    double val = measureCurrentValue();
    m_refs     = savedRefs;
    m_type     = savedType;
    m_distMode = savedMode;

    view::CadView::DimPreviewInfo info;
    info.refs     = tempRefs;
    info.type     = useType;
    info.distMode = useMode;
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
// onGeomHover  — GetGeom 模式下 hover 時即時更新預覽
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::onGeomHover(const QVariant& payload)
{
    QVariantMap map    = payload.toMap();
    QString hoverUuid  = map.value("geomUuid").toString();
    int     hoverHandle= map.value("handle", -1).toInt();
    QVector2D mousePt  = map.value("point").value<QVector2D>();

    // Auto_DIM.md 第八節：WaitCandidate 狀態下，若目前只選了一個幾何
    // （圓/弧），用滑鼠位置即時切換 半徑↔直徑 / 半徑↔弧長，不需要按
    // Tab/Space。其餘情況（Line/Point 的候選、或已有 2 個 refs）沒有
    // 自然的滑鼠位置對應關係，維持原本的 Tab/Space/快捷字母方式。
    if (m_state == State::WaitCandidate) {
        if (m_refs.size() == 1 && !m_candidates.isEmpty()) {
            auto* sk = activeSketch();
            int idx = GeneralDimClassifier::pickCandidateByMouse(
                m_candidates, m_refs[0], sk, mousePt);
            if (idx >= 0 && idx != m_candidateIndex) {
                m_candidateIndex = idx;
                applyHighlightedCandidate();
            }
        }
        return;  // 不繼續往下走 Idle/WaitSecond 的預覽邏輯
    }
    if (m_state != State::Idle && m_state != State::WaitSecond) return;

    if (hoverUuid.isEmpty()) {
        if (m_state == State::Idle) clearDimPreview();
        return;
    }

    GeomRef extraRef(hoverUuid, static_cast<GeomHandle>(hoverHandle));
    auto* sk = activeSketch();

    if (m_state == State::Idle) {
        // 尚未選任何幾何：用 hover 幾何顯示預覽（取第一個候選，例如整條線→長度、
        // 圓→直徑、弧→半徑，與 classifyAll() 回傳陣列的排序一致）
        QList<GeomRef> tempRefs = { extraRef };
        auto candidates = GeneralDimClassifier::classifyAll(tempRefs, sk);
        if (candidates.isEmpty()) { clearDimPreview(); return; }

        auto ct = annotationKindToConstraintType(candidates[0].kind);
        if (!ct) { clearDimPreview(); return; }  // 例如點狀首選需要第二點，不預覽
        ConstraintType previewType = *ct;
        DistanceMode   previewMode = candidates[0].distMode;

        QList<GeomRef> savedRefs = m_refs;
        ConstraintType savedType = m_type;
        DistanceMode   savedMode = m_distMode;
        m_refs = tempRefs; m_type = previewType; m_distMode = previewMode;
        double val = measureCurrentValue();
        m_refs = savedRefs; m_type = savedType; m_distMode = savedMode;

        view::CadView::DimPreviewInfo info;
        info.refs = tempRefs; info.type = previewType;
        info.distMode = previewMode; info.value = val; info.valid = true;
        auto* cadView = core::Application::instance()->uiManager()->cadView();
        if (cadView) cadView->setDimPreview(info);

    } else {
        // WaitSecond：已選1個，hover 第2個
        // 先用正規化後的 tempRefs 做 classify 確認合法性
        QList<GeomRef> tempRefs = m_refs;
        tempRefs.append(extraRef);

        // 正規化：第一選是 WholeGeom 時，第二選幾何強制 WholeGeom
        if (tempRefs.size() == 2 && sk) {
            const GeomRef& first  = tempRefs[0];
            GeomRef&       second = tempRefs[1];
            auto* geomFirst  = sk->findGeometry(first.geomUuid);
            auto* geomSecond = sk->findGeometry(second.geomUuid);
            const bool firstIsWhole =
                geomFirst && first.handle == GeomHandle::WholeGeom;
            const bool secondIsGeom =
                geomSecond &&
                (geomSecond->type == SketchGeometryType::Line   ||
                 geomSecond->type == SketchGeometryType::Arc    ||
                 geomSecond->type == SketchGeometryType::Circle);
            if (firstIsWhole && secondIsGeom &&
                second.handle != GeomHandle::WholeGeom)
            {
                second = GeomRef(second.geomUuid, GeomHandle::WholeGeom);
            }
        }

        auto candidates = GeneralDimClassifier::classifyAll(tempRefs, sk);
        if (candidates.isEmpty()) {
            updateDimPreview(nullptr);
            return;
        }

        // 傳正規化後的第二個 ref（updateDimPreview 內部也會再做一次正規化）
        GeomRef secondNorm = (tempRefs.size() == 2) ? tempRefs[1] : extraRef;
        updateDimPreview(&secondNorm);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onGeomPicked
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::onGeomPicked(const QVariant& payload)
{
    // Auto_DIM.md 精神：滑鼠為主。WaitCandidate 狀態下再次點擊「同一個」
    // 幾何元素，視同滑鼠確認目前高亮候選（不需要按 Enter 或輸入快捷字母）。
    // 點擊到其他幾何則忽略（避免誤觸打亂目前已確定的分類結果）。
    if (m_state == State::WaitCandidate) {
        QVariantMap map = payload.toMap();
        QString  uuid   = map.value("geomUuid").toString();
        int      handle = map.value("handle", static_cast<int>(GeomHandle::WholeGeom)).toInt();
        if (!m_refs.isEmpty() && uuid == m_refs.last().geomUuid
            && static_cast<GeomHandle>(handle) == m_refs.last().handle) {
            confirmCandidate(m_candidateIndex);
        }
        return;
    }

    if (m_state != State::Idle && m_state != State::WaitSecond)
        return;

    QVariantMap map = payload.toMap();
    QString  uuid   = map.value("geomUuid").toString();
    int      handle = map.value("handle", static_cast<int>(GeomHandle::WholeGeom)).toInt();

    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk   = activeSketch();

    // ── 點選空白處：建立自由點 ───────────────────────────────────────────────
    if (uuid.isEmpty()) {
        // 不允許自由點：必須選到 SketchPoint 或幾何元素
        if (cmdMgr)
            cmdMgr->printError("請選取草圖上的點或幾何元素（不允許點選空白處）");
        return;
    }

    // ── 解析幾何資訊 ─────────────────────────────────────────────────────────
    auto gh = static_cast<GeomHandle>(handle);
    GeomRef ref(uuid, gh);

    if (sk) {
        auto* geom = sk->findGeometry(uuid);
        bool  isPt = (!geom && sk->point(uuid));

        QString geomDesc;
        if (isPt) {
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

        QVector2D pos = ref.resolvePosition(sk);
        QString   ptUuid = ref.resolvedPointUuid(sk);

        // 命令列：簡潔使用者訊息
        if (cmdMgr && !geomDesc.isEmpty())
            cmdMgr->printMessage(
                QString("  選取[%1]: %2  (%3, %4)")
                .arg(m_refs.size() + 1)
                .arg(geomDesc)
                .arg(static_cast<double>(pos.x()), 0, 'f', 2)
                .arg(static_cast<double>(pos.y()), 0, 'f', 2));

        // qDebug：UUID 偵錯
        qDebug() << "[GDIM] pick#" << (m_refs.size()+1)
                 << "geomUuid=" << uuid << "gh=" << handle
                 << "ptUuid=" << ptUuid
                 << "pos=(" << pos.x() << "," << pos.y() << ")";
    }

    m_refs.append(ref);

    // ── WaitSecond 第二選 handle 正規化 ──────────────────────────────────────
    // 當第一選是整條線/弧/圓（WholeGeom），第二選如果是同一類型幾何的端點
    // （OSnap 吸附到線端、弧端），應把 handle 正規化為 WholeGeom，
    // 確保 classifyPair 能走到「線+線」或「線+弧/圓」分支
    if (m_refs.size() == 2) {
        auto* sk2 = sk ? sk : activeSketch();
        if (sk2) {
            const GeomRef& first  = m_refs[0];
            GeomRef&       second = m_refs[1];

            auto* geomFirst  = sk2->findGeometry(first.geomUuid);
            auto* geomSecond = sk2->findGeometry(second.geomUuid);

            // 第一選是 WholeGeom 的線/弧/圓
            const bool firstIsWholeGeom =
                geomFirst && first.handle == GeomHandle::WholeGeom;

            // 第二選是同類型的線段（handle 可能是 Start/End/WholeGeom）
            const bool secondIsLine =
                geomSecond && geomSecond->type == SketchGeometryType::Line;
            const bool secondIsArc =
                geomSecond && geomSecond->type == SketchGeometryType::Arc;
            const bool secondIsCircle =
                geomSecond && geomSecond->type == SketchGeometryType::Circle;

            if (firstIsWholeGeom &&
                (secondIsLine || secondIsArc || secondIsCircle) &&
                second.handle != GeomHandle::WholeGeom)
            {
                // 把第二選的 handle 正規化為 WholeGeom
                second = GeomRef(second.geomUuid, GeomHandle::WholeGeom);
            }
        }
    }

    auto candidates = GeneralDimClassifier::classifyAll(m_refs, sk ? sk : activeSketch());

    // ── 無合法候選：撤銷最後一次選取 ─────────────────────────────────────────
    // （例如雙選時選了同一物件的不同 handle，或選到無法解析的幾何）
    if (candidates.isEmpty()) {
        m_refs.removeLast();
        if (cmdMgr)
            cmdMgr->printError(
                m_refs.isEmpty() ? "無法辨識此幾何元素，請重新選擇"
                                 : "無法與前一個選取組合，請重新選擇");
        return;
    }

    // ── 使用者需求：「選第一條線就是線長，不用按 L」──────────────────────────
    // 單一幾何選取時，若能立即判斷出唯一的預設候選（Line→線長；
    // Circle/Arc→滑鼠位置判斷半徑/直徑/弧長，見 pickCandidateByMouse），
    // 就不進 WaitCandidate 選單，立即顯示預覽並排一個短暫延遲後自動確認
    // ——這段延遲留給使用者「點第二個幾何改配對」的機會（見下方：第二次
    // GEOM_PICKED 到達時 m_refs.size()==2，會讓這個延遲確認失效，改走
    // 兩個幾何的配對分類）。真正有歧義、滑鼠也判斷不出來的情況
    // （例如單一點的 X/Y/座標）才維持原本 WaitCandidate + Tab 循環。
    if (m_refs.size() == 1) {
        QVector2D mousePt = map.value("point").value<QVector2D>();
        int idx = GeneralDimClassifier::pickCandidateByMouse(candidates, ref, sk, mousePt);
        if (idx < 0) {
            int nonSecondIdx = -1, nonSecondCount = 0;
            for (int i = 0; i < candidates.size(); ++i) {
                if (!candidates[i].needsSecondPick) { ++nonSecondCount; nonSecondIdx = i; }
            }
            if (nonSecondCount == 1) idx = nonSecondIdx;
        }

        if (idx >= 0) {
            m_candidates     = candidates;
            m_candidateIndex = idx;
            applyHighlightedCandidate();  // 立即顯示預覽（零鍵盤操作）

            constexpr int kAutoConfirmDelayMs = 350;  // 給「點第二個幾何改配對」的緩衝時間
            ++m_pendingConfirmToken;
            int token = m_pendingConfirmToken;
            QTimer::singleShot(kAutoConfirmDelayMs, this, [this, token, idx]() {
                if (token != m_pendingConfirmToken) return;  // 已被第二個幾何/取消動作作廢
                if (m_state != State::Idle) return;
                confirmCandidate(idx);
            });
            return;  // 停留在 Idle，GEOM_PICKED 繼續訂閱，等待可能的第二次選取
        }
        // idx < 0：真的有歧義（例如點的 X/Y/座標），落到下面走 WaitCandidate
    } else {
        // 這次點擊是「第二個幾何」，讓任何來自第一次點擊的延遲自動確認失效
        ++m_pendingConfirmToken;
    }

    // ── GDIM v2 Phase 2：其餘情況（歧義的單一幾何、或雙選配對）一律進入
    //    WaitCandidate 列出全部候選，由 Tab/Space 循環、快捷字母、或
    //    Enter/再次點擊 確認 ────────────────────────────────────────────────
    transitionToWaitCandidate(candidates);
}

// ─────────────────────────────────────────────────────────────────────────────
// onStringInput
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::onStringInput(const QVariant& payload)
{
    auto* cmdMgr = core::CommandLineManager::instance();
    QString input = payload.toString().trimmed();

    // ── WaitCandidate：GDIM v2 Phase 2 多候選引擎 ───────────────────────────
    // - 空白輸入（直接按 Enter）：確認目前高亮候選（TAB/SPACE 循環後的結果）
    // - 輸入單一字母（沿用舊選單快捷鍵，如 L/D/R/X/Y/C/A）：直接跳到並確認
    //   該候選，不需要先 Tab 循環過去——維持老手使用者的肌肉記憶
    if (m_state == State::WaitCandidate) {
        int confirmIndex = m_candidateIndex;

        if (!input.isEmpty()) {
            QChar key = input.trimmed().at(0).toUpper();
            int found = -1;
            for (int i = 0; i < m_candidates.size(); ++i) {
                if (m_candidates[i].shortcut.toUpper() == key) { found = i; break; }
            }
            if (found < 0) {
                if (cmdMgr)
                    cmdMgr->printError(
                        QString("無效選項，請輸入：%1，或按 Tab/Space 切換候選")
                        .arg(GeneralDimClassifier::candidatesPrompt(m_candidates, m_candidateIndex)));
                return;  // 留在 WaitCandidate
            }
            confirmIndex = found;
        }

        confirmCandidate(confirmIndex);
        return;
    }

    // ── WaitValue：使用者輸入尺寸數值 ────────────────────────────────────────
    if (m_state == State::WaitValue) {
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

    transitionToWaitValue();
}

// ─────────────────────────────────────────────────────────────────────────────
// onCancelled
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::onCancelled(const QVariant&) {
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────────
// 狀態轉換
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::transitionToWaitCandidate(
    const QList<cad::GeneralDimClassifier::Candidate>& candidates)
{
    m_state          = State::WaitCandidate;
    m_candidates     = candidates;
    m_candidateIndex = 0;

    // Auto_DIM.md 精神：滑鼠為主，鍵盤只在輸入數據時使用。
    // 只有一個候選、且不需要再選第二個幾何時（例如兩點距離、兩線角度、
    // 兩線間距...絕大多數「2 個幾何」的情況都只有單一候選），
    // 不需要使用者再按任何鍵確認型別，直接進下一步。
    if (candidates.size() == 1 && !candidates[0].needsSecondPick) {
        confirmCandidate(0);
        return;
    }

    subscribeStringInput();     // Enter / 快捷字母 確認（保留給鍵盤慣用者/邊緣情況）
    subscribeCandidateCycle();  // Tab / Space 循環（見 CadView::keyPressEvent）
    // GEOM_HOVER 在 Idle/WaitSecond 已訂閱過；WaitCandidate 繼續沿用同一份
    // 訂閱，讓 onGeomHover() 能在候選選擇階段也用滑鼠位置即時切換候選
    // （見 onGeomHover 的 WaitCandidate 分支，Auto_DIM.md 第八節）
    subscribeGeomHover();

    applyHighlightedCandidate();
}

void GeneralDimCommand::confirmCandidate(int index) {
    if (index < 0 || index >= m_candidates.size()) return;
    const auto& chosen = m_candidates[index];
    m_candidateIndex = index;

    // 若此候選帶有 pairedRefs（例如線段的水平/垂直投影，實際量測對象是
    // 該線的兩個端點，不是線本身的 WholeGeom 參考），確認後永久替換
    // m_refs——之後 measureCurrentValue()/commitDimension() 都直接沿用
    // m_refs，不需要另外改動這些既有函式。
    if (!chosen.pairedRefs.isEmpty())
        m_refs = chosen.pairedRefs;

    applyHighlightedCandidate();  // 確保 m_type/m_distMode 對應到 index

    auto* cmdMgr = core::CommandLineManager::instance();
    if (chosen.needsSecondPick) {
        // 選「量距第二點」→ 進 WaitSecond
        transitionToWaitSecond();
    } else {
        if (cmdMgr)
            cmdMgr->printMessage(
                QString("  類型: %1").arg(constraintTypeName()));
        updateDimPreview(nullptr);
        transitionToWaitDimPlace();
    }
}

/// 依 m_candidateIndex 把候選內容套用到 m_type/m_distMode，更新預覽與命令列提示。
/// AnnotationKind → ConstraintType 的轉換沿用 Phase 1 提供的
/// annotationKindToConstraintType()，讓 GeneralDimCommand 內部（測量/預覽/
/// commit）維持既有、已驗證過的 ConstraintType 邏輯不變。
void GeneralDimCommand::applyHighlightedCandidate()
{
    if (m_candidates.isEmpty()) return;
    const auto& c = m_candidates[m_candidateIndex];

    auto ct = cad::annotationKindToConstraintType(c.kind);
    if (ct) m_type = *ct;
    m_distMode = c.distMode;
    m_useSupplementAngle = c.useSupplementAngle;

    if (!c.pairedRefs.isEmpty()) {
        // 例如「水平投影/垂直投影」：暫時代入衍生的端點 refs 算預覽，
        // 算完立刻還原，這樣使用者還能 Tab 回其他候選（例如「線長」），
        // 不會因為預覽而永久改掉目前的選取狀態
        QList<cad::GeomRef> saved = m_refs;
        m_refs = c.pairedRefs;
        updateDimPreview(nullptr);
        m_refs = saved;
    } else {
        updateDimPreview(nullptr);
    }

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt(
            GeneralDimClassifier::candidatesPrompt(m_candidates, m_candidateIndex));
        cmdMgr->waitForInput(core::InputType::String);
    }
}

void GeneralDimCommand::onCandidateCycle(const QVariant& payload)
{
    if (m_state != State::WaitCandidate) return;
    if (m_candidates.isEmpty()) return;

    int direction = payload.toMap().value("direction", 1).toInt();
    int n = m_candidates.size();
    m_candidateIndex = ((m_candidateIndex + direction) % n + n) % n;  // 正確處理負數循環

    applyHighlightedCandidate();
}

void GeneralDimCommand::transitionToWaitSecond()
{
    m_state = State::WaitSecond;

    // 訂閱 hover，讓滑鼠移到第二個幾何時即時顯示預覽
    subscribeGeomHover();

    // 立即顯示第一個已選幾何的單選預覽（如線段長度、圓半徑等）
    updateDimPreview(nullptr);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->showPrompt("GDIM 再選一個幾何元素（或按 Enter 採用單選）");
}

void GeneralDimCommand::transitionToWaitDimPlace()
{
    m_state = State::WaitDimPlace;

    // FixedLength：儲存原始單 ref，供 subscribePreview 切換 H/V 時展開用
    if (m_type == cad::ConstraintType::FixedLength && m_refs.size() == 1)
        m_originalLineRef = m_refs[0];
    else
        m_originalLineRef = cad::GeomRef{};  // 清除

    m_measuredValue  = measureCurrentValue();
    m_measuredValue2 = measureCurrentValue2();

    auto* app     = core::Application::instance();
    auto* ui      = app ? app->uiManager() : nullptr;
    auto* cadView = ui  ? ui->cadView()    : nullptr;

    if (cadView) {
        // 先更新預覽資訊（確保 paintDimPreview 有正確的 refs/type/value）
        updateDimPreview(nullptr);
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
    auto* ui      = app ? app->uiManager() : nullptr;
    auto* cadView = ui  ? ui->cadView()    : nullptr;
    if (cadView)
        cadView->setMode(view::InteractionMode::Sketching);

    // 若 execute() 時已帶入數值，跳過輸入直接 commit
    if (m_hasPending) {
        commitDimension();
        return;
    }

    const bool isAngleType = (m_type == ConstraintType::FixedAngleDim ||
                              m_type == ConstraintType::FixedAngle);
    // 角度類型：m_measuredValue 內部為弧度，提示文字改顯示「度」讓使用者輸入直覺一致
    double defVal = isAngleType ? (m_measuredValue * 180.0 / M_PI) : m_measuredValue;
    QString defStr = QString::number(defVal, 'f', 2) + (isAngleType ? QStringLiteral("°") : QString());
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt(
            QString("GDIM 輸入數值（Enter = %1）").arg(defStr));
        cmdMgr->waitForInput(core::InputType::String);
    }
    subscribeStringInput();
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

    // GDIM v2 Phase 6：重複尺寸偵測——commit 前檢查是否已存在相同標註，
    // 有的話僅提醒（不阻擋），避免使用者誤以為建立失敗；仍會照常建立/更新
    // （若剛好是同一組 refs+kind 再次標註，屬於使用者刻意調整位置等正常操作）。
    auto akind = cad::constraintTypeToAnnotationKind(m_type);
    if (akind) {
        if (sk->findDuplicateAnnotation(m_refs, *akind)) {
            if (cmdMgr)
                cmdMgr->printMessage(
                    tr("⚠ 已存在相同標註（%1），仍會建立此標註").arg(constraintTypeName()),
                    core::MessageType::Warning);
        }
    }

    // GDIM v2 Phase 1/6：優先走 SketchAnnotation 統一路徑（Phase 1 提供的
    // annotationKindToConstraintType()/toImplicitConstraint() 確保產生的
    // 隱含約束與過去直接 addConstraint() 完全等價）。只有在 m_type 是純
    // 幾何約束型別（理論上 GDIM 不會走到，此處僅作防呆 fallback）時，
    // 才退回舊的 addConstraint() 路徑。
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

        // GDIM v2 Phase 9：規範檢查（優先度最低，只提醒不阻擋）
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
    m_candidates.clear();
    m_candidateIndex = 0;
    m_useSupplementAngle = false;
    ++m_pendingConfirmToken;  // 讓任何待定的延遲自動確認（見 onGeomPicked）失效
    m_originalLineRef = cad::GeomRef{};
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
    }

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->clearPrompt();

    // ★ 通知 CommandManager 命令已結束
    if (state() == CommandState::Running)
        complete(CommandResult::Success());
}

} // namespace aicad::command