#include "GeneralDimCommand.h"
#include "ConstraintCommands.h"  // reportSolveResult
#include "../core/Application.h"
#include "../core/CommandLineManager.h"
#include "../core/EventBus.h"
#include "../core/ParameterStore.h"
#include "../cad/Sketch.h"
#include "../ui/UIManager.h"
#include "../view/CadView.h"
#include <QMetaObject>
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

// 點 P 到線段 (A,B) 的投影點（在直線上，不 clamp 到端點）
static QVector2D projectPointOnLine(const QVector2D& P,
                                     const QVector2D& A,
                                     const QVector2D& B)
{
    QVector2D AB = B - A;
    float len2 = QVector2D::dotProduct(AB, AB);
    if (len2 < 1e-10f) return A;
    float t = QVector2D::dotProduct(P - A, AB) / len2;
    return A + AB * t;
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
        double ang   = std::atan2(std::abs(cross), dot);  // 0..π/2，銳角
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

    // WaitDimPlace 狀態：m_type/m_distMode 已由 subscribePreview 動態設好（F/H/V），
    // 直接使用，不重新 classify（classify 會覆蓋掉使用者的選擇）
    ConstraintType useType;
    DistanceMode   useMode;
    if (m_state == State::WaitDimPlace) {
        useType = m_type;
        useMode = m_distMode;
    } else {
        auto result = GeneralDimClassifier::classify(tempRefs, sk);
        if (!result.valid) { cadView->clearDimPreview(); return; }
        useType = result.type;
        useMode = result.distMode;
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
    // WaitMenu 狀態下 hover 不更新預覽（等待鍵盤輸入）
    if (m_state == State::WaitMenu) return;
    if (m_state != State::Idle && m_state != State::WaitSecond) return;

    QVariantMap map    = payload.toMap();
    QString hoverUuid  = map.value("geomUuid").toString();
    int     hoverHandle= map.value("handle", -1).toInt();

    if (hoverUuid.isEmpty()) {
        if (m_state == State::Idle) clearDimPreview();
        return;
    }

    GeomRef extraRef(hoverUuid, static_cast<GeomHandle>(hoverHandle));
    auto* sk = activeSketch();

    if (m_state == State::Idle) {
        // 尚未選任何幾何：用 hover 幾何顯示預覽
        // 對 needMenu 情況，用預設類型預覽（整條線→長度，圓→直徑，弧→半徑）
        QList<GeomRef> tempRefs = { extraRef };
        auto result = GeneralDimClassifier::classify(tempRefs, sk);

        ConstraintType previewType = result.type;
        DistanceMode   previewMode = result.distMode;

        if (result.needMenu) {
            // 為 hover 選一個合理的預覽類型
            using MK = GeneralDimClassifier::MenuKey;
            switch (result.menuKey) {
            case MK::LineType:
                previewType = cad::ConstraintType::FixedLength;
                break;
            case MK::CircleType:
                previewType = cad::ConstraintType::FixedDiameter;
                break;
            case MK::ArcType:
                previewType = cad::ConstraintType::FixedRadius;
                break;
            case MK::PointCoord:
                // 點類 hover 不顯示預覽（不知道要顯示哪種）
                clearDimPreview();
                return;
            default:
                clearDimPreview();
                return;
            }
        } else if (!result.valid) {
            clearDimPreview();
            return;
        }

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

        auto result = GeneralDimClassifier::classify(tempRefs, sk);
        if (!result.valid) {
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

    auto result = GeneralDimClassifier::classify(m_refs, sk ? sk : activeSketch());

    // ── 非法組合（例如選了同一物件的不同 handle）：撤銷，留在 WaitSecond ────
    if (m_refs.size() == 2 && !result.valid && !result.needMore && !result.needMenu) {
        m_refs.removeLast();
        if (cmdMgr)
            cmdMgr->printError("無法與前一個選取組合，請重新選擇");
        return;
    }

    // ── 需要再選第二個 ────────────────────────────────────────────────────────
    if (result.needMore && m_refs.size() == 1) {
        transitionToWaitSecond();
        return;
    }

    // ── 需要彈出選單讓使用者選類型 ───────────────────────────────────────────
    if (result.needMenu) {
        using MK = GeneralDimClassifier::MenuKey;
        using CT = cad::ConstraintType;

        QList<MenuOption> opts;
        QString prompt = result.nextPrompt;

        switch (result.menuKey) {

        case MK::LineType:
            // 整條線：線長 / 量距第二點
            opts.append({"L", "線長",      CT::FixedLength,  false});
            opts.append({"D", "量距第二點", CT::FixedDistance, true});
            break;

        case MK::CircleType:
            // 整個圓：直徑 / 半徑
            opts.append({"D", "直徑 Ø",  CT::FixedDiameter, false});
            opts.append({"R", "半徑 R",  CT::FixedRadius,   false});
            break;

        case MK::ArcType:
            // 整條弧：半徑 / 弧長
            opts.append({"R", "半徑 R",  CT::FixedRadius,   false});
            opts.append({"L", "弧長 ~",  CT::FixedArcLength,false});
            break;

        case MK::PointCoord:
            // 點/圓心/弧圓心：X / Y / XY / 量距第二點
            opts.append({"X", "X 座標",   CT::FixedX,        false});
            opts.append({"Y", "Y 座標",   CT::FixedY,        false});
            opts.append({"C", "XY 座標",  CT::CoordinateDim, false});
            opts.append({"D", "量距第二點",CT::FixedDistance, true});
            break;

        default:
            break;
        }

        if (!opts.isEmpty()) {
            transitionToWaitMenu(opts, prompt);
            return;
        }
    }

    // ── 已分類完成，進入 DimPlace ─────────────────────────────────────────────
    if (result.valid) {
        m_type     = result.type;
        m_distMode = result.distMode;

        if (cmdMgr)
            cmdMgr->printMessage(
                QString("  類型: %1").arg(constraintTypeName()));

        updateDimPreview(nullptr);

        if (m_type == ConstraintType::FixedRadius ||
            m_type == ConstraintType::FixedArcLength) {
            transitionToWaitArcType();
        } else {
            transitionToWaitDimPlace();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onStringInput
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::onStringInput(const QVariant& payload)
{
    auto* cmdMgr = core::CommandLineManager::instance();
    QString input = payload.toString().trimmed();

    // ── WaitMenu：使用者從選單選擇束制類型 ───────────────────────────────────
    if (m_state == State::WaitMenu) {
        QString key = input.toUpper();

        // 找符合的選項
        const MenuOption* chosen = nullptr;
        for (const auto& opt : m_menuOptions) {
            if (opt.key.compare(key, Qt::CaseInsensitive) == 0) {
                chosen = &opt;
                break;
            }
        }

        if (!chosen) {
            // 無效輸入，列出選項重試
            if (cmdMgr) {
                QStringList keys;
                for (const auto& o : m_menuOptions)
                    keys << QString("%1=%2").arg(o.key).arg(o.label);
                cmdMgr->printError(
                    QString("無效選項，請輸入：%1").arg(keys.join(" / ")));
            }
            return;  // 留在 WaitMenu
        }

        m_type = chosen->type;

        if (chosen->needSecond) {
            // 選「量距第二點」→ 進 WaitSecond
            transitionToWaitSecond();
        } else {
            // 直接確定類型，進 WaitDimPlace
            if (cmdMgr)
                cmdMgr->printMessage(
                    QString("  類型: %1").arg(constraintTypeName()));
            updateDimPreview(nullptr);
            transitionToWaitDimPlace();
        }
        return;
    }

    // ── WaitArcType（舊路徑，保留相容）────────────────────────────────────────
    if (m_state == State::WaitArcType) {
        if (input.compare("L", Qt::CaseInsensitive) == 0)
            m_type = cad::ConstraintType::FixedArcLength;
        else
            m_type = cad::ConstraintType::FixedRadius;
        transitionToWaitDimPlace();
        return;
    }

    // ── WaitValue：使用者輸入尺寸數值 ────────────────────────────────────────
    if (m_state == State::WaitValue) {
        if (input.isEmpty()) {
            m_pendingValue = m_measuredValue;
        } else {
            bool isNum;
            double v = input.toDouble(&isNum);
            if (isNum) {
                m_pendingValue = v;
                m_pendingExpr.clear();
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

void GeneralDimCommand::transitionToWaitMenu(
    const QList<MenuOption>& options, const QString& prompt)
{
    m_state       = State::WaitMenu;
    m_menuOptions = options;

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt(prompt);
        cmdMgr->waitForInput(core::InputType::String);
    }
    subscribeStringInput();
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

void GeneralDimCommand::transitionToWaitArcType()
{
    m_state = State::WaitArcType;
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt("GDIM 弧：半徑(R)/弧長(L)？");
        cmdMgr->waitForInput(core::InputType::String);
    }
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

    QString defStr = QString::number(m_measuredValue, 'f', 2);
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
    c.value          = m_pendingValue;
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
    sk->addConstraint(c);
    SolveResult result = sk->solveConstraints();
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
    m_menuOptions.clear();
    m_originalLineRef = cad::GeomRef{};
    m_pendingValue   = 0.0;
    m_pendingExpr.clear();
    m_dimOffsetX     = 0.0;
    m_dimOffsetY     = 0.0;
    m_dimAnchor2D    = QVector2D{};
    m_hasPending     = false;

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