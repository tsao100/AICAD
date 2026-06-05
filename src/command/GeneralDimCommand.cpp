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
    // 僅追蹤最後預覽偏移，供 commitDimension 使用（不操作 AIS overlay）
    bus->subscribe(core::Events::DIM_LINE_PREVIEW, this,
        [this](const QVariant& v) {
            QVariantMap m = v.toMap();
            m_dimOffsetX = m.value("offsetX").toDouble();
            m_dimOffsetY = m.value("offsetY").toDouble();
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

    if (tempRefs.isEmpty()) { cadView->clearDimPreview(); return; }

    auto result = GeneralDimClassifier::classify(tempRefs, sk);
    if (!result.valid) { cadView->clearDimPreview(); return; }

    // 暫時切換 m_refs/m_type/m_distMode 量測值
    QList<GeomRef> savedRefs = m_refs;
    ConstraintType savedType = m_type;
    DistanceMode   savedMode = m_distMode;
    m_refs     = tempRefs;
    m_type     = result.type;
    m_distMode = result.distMode;
    double val = measureCurrentValue();
    m_refs     = savedRefs;
    m_type     = savedType;
    m_distMode = savedMode;

    view::CadView::DimPreviewInfo info;
    info.refs     = tempRefs;
    info.type     = result.type;
    info.distMode = result.distMode;
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
    // 只在 Idle（未選任何幾何）或 WaitSecond（已選1個）時更新預覽
    if (m_state != State::Idle && m_state != State::WaitSecond) return;

    QVariantMap map    = payload.toMap();
    QString hoverUuid  = map.value("geomUuid").toString();
    int     hoverHandle= map.value("handle", -1).toInt();

    if (hoverUuid.isEmpty()) {
        // 滑鼠沒有 hover 到任何幾何
        if (m_state == State::Idle) {
            clearDimPreview();
        }
        // WaitSecond 狀態仍保留第一個選取的單選預覽
        return;
    }

    GeomRef extraRef(hoverUuid, static_cast<GeomHandle>(hoverHandle));

    if (m_state == State::Idle) {
        // 尚未選任何幾何：用 hover 幾何單獨預覽
        QList<GeomRef> tempRefs = { extraRef };
        auto* sk = activeSketch();
        auto result = GeneralDimClassifier::classify(tempRefs, sk);
        if (!result.valid || result.needMore) {
            clearDimPreview();
            return;
        }
        // 暫時設置量測
        QList<GeomRef> savedRefs = m_refs;
        ConstraintType savedType = m_type;
        DistanceMode   savedMode = m_distMode;
        m_refs = tempRefs; m_type = result.type; m_distMode = result.distMode;
        double val = measureCurrentValue();
        m_refs = savedRefs; m_type = savedType; m_distMode = savedMode;

        view::CadView::DimPreviewInfo info;
        info.refs = tempRefs; info.type = result.type;
        info.distMode = result.distMode; info.value = val; info.valid = true;
        auto* cadView = core::Application::instance()->uiManager()->cadView();
        if (cadView) cadView->setDimPreview(info);
    } else {
        // WaitSecond：已選1個，hover 第2個 → 顯示雙選預覽
        updateDimPreview(&extraRef);
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

    auto result = GeneralDimClassifier::classify(m_refs, sk ? sk : activeSketch());

    if (result.needMore && m_refs.size() == 1) {
        transitionToWaitSecond();
        return;
    }

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

    if (m_state == State::WaitArcType) {
        if (input.compare("L", Qt::CaseInsensitive) == 0)
            m_type = ConstraintType::FixedArcLength;
        else
            m_type = ConstraintType::FixedRadius;
        transitionToWaitDimPlace();
        return;
    }

    if (m_state == State::WaitValue) {
        if (input.isEmpty()) {
            // 採用量測值
            m_pendingValue = m_measuredValue;
        } else {
            bool isNum;
            double v = input.toDouble(&isNum);
            if (isNum) {
                m_pendingValue = v;
                m_pendingExpr.clear();
            } else {
                Sketch* sk = activeSketch();
                if (!sk) { cleanup(); return; }
                auto [ok, ev] = sk->parameterStore()->evaluate(input);
                if (!ok) {
                    if (cmdMgr) cmdMgr->printError(
                        QString("Unknown expression: '%1'").arg(input));
                    return;  // 保持 WaitValue，讓使用者重試
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
    m_measuredValue  = measureCurrentValue();
    m_measuredValue2 = measureCurrentValue2();

    auto* app     = core::Application::instance();
    auto* ui      = app ? app->uiManager() : nullptr;
    auto* cadView = ui  ? ui->cadView()    : nullptr;

    if (cadView) {
        // 先更新預覽資訊（確保 paintDimPreview 有正確的 refs/type/value）
        updateDimPreview(nullptr);
        // 再切換 mode → CadView 的 mouseMoveEvent 會持續觸發 repaint
        cadView->setMode(view::InteractionMode::PlaceDimLine);
        cadView->beginPlaceDimLine(refMidpoint2D());
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
    c.dimLineOffsetX = m_dimOffsetX;
    c.dimLineOffsetY = m_dimOffsetY;

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
    m_pendingValue   = 0.0;
    m_pendingExpr.clear();
    m_dimOffsetX     = 0.0;
    m_dimOffsetY     = 0.0;
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