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
#include <cmath>
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

                // ── 夾角型別：依滑鼠落在角平分線的哪一側，動態切換 夾角／補角 ──
                // （無選單版第 B 組第 16 項：「移動決定角度標註弧的半徑與象限」）
                // 型別本身在點擊第二條線時就已鎖定（見 lockPairGeom），這裡只
                // 調整 useSupplementAngle，不再像舊版那樣於拖曳中重新判斷型別。
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
                            QVector2D bisector = dirA + dirB;
                            if (bisector.lengthSquared() < 1e-8f)
                                bisector = QVector2D(-dirA.y(), dirA.x());  // 近乎反向：改用法向量
                            bisector.normalize();

                            QVector2D offset(static_cast<float>(m_dimOffsetX),
                                             static_cast<float>(m_dimOffsetY));
                            if (offset.lengthSquared() > 1e-6f) {
                                bool onBisectorSide =
                                    QVector2D::dotProduct(offset.normalized(), bisector) >= 0.0f;
                                m_useSupplementAngle = !onBisectorSide;
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

    if (m_state == State::Idle) {
        if (hoverUuid.isEmpty() || !sk) { clearDimPreview(); return; }
        GeomRef hoverRef(hoverUuid, static_cast<GeomHandle>(hoverHandle));
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

        bool paired = false;
        GeomRef hoverRef;
        if (!hoverUuid.isEmpty()) {
            hoverRef = GeomRef(hoverUuid, static_cast<GeomHandle>(hoverHandle));
            normalizeSecondRef(anchor, hoverRef, sk);
            const bool sameAsAnchor =
                (hoverRef.geomUuid == anchor.geomUuid && hoverRef.handle == anchor.handle);
            if (!sameAsAnchor)
                paired = GeneralDimClassifier::canPair(anchor, hoverRef, sk);
        }

        if (paired) {
            auto inf = GeneralDimClassifier::inferPair(anchor, hoverRef, sk, mousePt);
            if (inf) {
                auto ct = annotationKindToConstraintType(inf->kind);
                if (ct) {
                    QList<GeomRef> refs = inf->pairedRefs.isEmpty()
                        ? QList<GeomRef>{ anchor, hoverRef } : inf->pairedRefs;
                    m_type = *ct;
                    m_distMode = inf->distMode;
                    m_useSupplementAngle = inf->useSupplementAngle;
                    pushPreview(refs, m_type, m_distMode);
                    return;
                }
            }
        }

        // 沒有可配對的第二幾何 → 依起點 + 滑鼠位置即時切換單幾何型別預覽
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
    QVector2D   mousePt= map.value("point").value<QVector2D>();

    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk   = activeSketch();

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

        // 起點鎖定後立刻進入連續判斷階段：依當下滑鼠位置顯示單幾何預覽
        // （無選單版第 0 節：「起點鎖定後，系統立刻進入一個連續判斷階段」）
        if (sk) {
            auto inf = GeneralDimClassifier::inferSingle(anchor, sk, mousePt);
            if (inf) {
                auto ct = annotationKindToConstraintType(inf->kind);
                if (ct) {
                    m_type = *ct; m_distMode = inf->distMode; m_useSupplementAngle = false;
                    pushPreview(m_refs, m_type, m_distMode);
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

    // 落在空白處，或落在無法與起點配對的幾何上 → 直接依滑鼠位置鎖定單幾何
    // 型別，並用這次點擊的位置同時定位（無選單版第 0 節）
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

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr)
        cmdMgr->printMessage(QString("  類型: %1").arg(constraintTypeName()));

    transitionToWaitDimPlace();
}

void GeneralDimCommand::lockSingleGeom(const cad::GeomRef& anchor, const QVector2D& mousePt)
{
    Sketch* sk = activeSketch();
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
// Esc / 右鍵：依目前狀態退回上一步（無選單版第 D 組）
// ─────────────────────────────────────────────────────────────────────────────

void GeneralDimCommand::backToIdle()
{
    // 25：Anchored（已鎖定起點，尚未第二次點擊）→ Idle，清空起點
    m_refs.clear();
    m_anchorRef = cad::GeomRef{};
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
