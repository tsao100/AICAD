/**
 * @file OffsetCommand.cpp
 * @brief 見 OffsetCommand.h 檔頭說明。
 */
#include "OffsetCommand.h"
#include "TrimExtendHelper.h"

#include "../core/Application.h"
#include "../core/CommandLineManager.h"
#include "../core/EventBus.h"
#include "../cad/Sketch.h"
#include "../ui/UIManager.h"
#include "../view/CadView.h"

#include <QMetaObject>
#include <QPointF>

namespace aicad {
namespace command {

using namespace cad;

double OffsetCommand::s_lastDistance = 0.0;

namespace {
bool isFixedReferenceUuid(const QString& uuid)
{
    return uuid.startsWith("sketch_xaxis:") ||
           uuid.startsWith("sketch_yaxis:") ||
           uuid.startsWith("sketch_origin:");
}
} // namespace

OffsetCommand::OffsetCommand()
    : Command("OFFSET", "Offset a line/circle/arc by a distance (alias: O)")
{
}

QString OffsetCommand::getUsage() const
{
    return "Usage: OFFSET — select a line, circle, or arc, then click the side "
           "to offset toward. Set the distance by typing a number directly, "
           "clicking two points to measure it, or typing D/DISTANCE (default "
           "is the last value used). Selecting a line that's connected to "
           "other lines offsets the whole connected chain. Press Enter/"
           "right-click/Esc to finish. Polyline/Spline/Ellipse are not "
           "supported yet.";
}

cad::Sketch* OffsetCommand::activeSketch() const
{
    return core::Application::instance()->activeSketch();
}

// ─────────────────────────────────────────────────────────────────────────
// execute
// ─────────────────────────────────────────────────────────────────────────

CommandResult OffsetCommand::execute(const CommandContext& /*ctx*/)
{
    m_state       = State::Idle;
    m_returnState = State::WaitCurve;
    m_distance    = s_lastDistance;  // 帶入前一次執行設定的距離值（首次執行為 0）
    m_curveUuid.clear();
    m_distMeasurePt1 = QVector2D();
    m_offsetCount = 0;

    Sketch* sk = activeSketch();
    if (!sk) {
        auto* cmdMgr = core::CommandLineManager::instance();
        if (cmdMgr) cmdMgr->printError("No active sketch. Enter sketch edit mode first.");
        return CommandResult::Failure("No active sketch.");
    }

    setWaitingForInput();

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetGeom);

    // 訂閱只在這裡做一次（指令生命週期內持續有效），不要放進
    // beginCurveStage()——OFFSET 是連續迴圈、beginCurveStage() 每次偏移後
    // 都會再呼叫一次，若訂閱動作也跟著重複，會造成同一個點擊觸發好幾次
    // 處理（EventBus::subscribe() 不會自動去重複，見 FilletCommand 對照
    // 說明——FILLET 因為不是迴圈、beginFirstObjectStage() 只呼叫一次，
    // 沒有這個風險，OFFSET 這裡必須特別注意）。
    subscribeGeomPicked();
    subscribePointAcquired();
    subscribeStringInput();
    subscribeCancelled();

    beginCurveStage();
    return CommandResult::Success("Waiting for curve to offset...");
}

// ─────────────────────────────────────────────────────────────────────────
// 選取來源曲線 → 點選偏移方向（距離可隨時用 D 修改；完成一次後回到選取
// 來源曲線，直到使用者按 Enter／右鍵／Esc 結束）
// ─────────────────────────────────────────────────────────────────────────

void OffsetCommand::beginCurveStage()
{
    m_state = State::WaitCurve;
    m_curveUuid.clear();
    showPickPrompt("curve to offset (Line/Circle/Arc)");
}

void OffsetCommand::showPickPrompt(const QString& which)
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (!cmdMgr) return;
    cmdMgr->showPrompt(QString("[OFFSET] D=%1 — Select %2 (type a number or click two "
                               "points to set distance, Enter/right-click to finish):")
                            .arg(m_distance).arg(which));
    // waitForInput() 是一次性的：GEOM_PICKED／POINT_ACQUIRED（滑鼠點選）走
    // 獨立於命令列文字輸入之外的路徑，不受影響、隨時有效；但要讓使用者
    // 隨時能打字改距離，必須每次重新進入「等待選取/點選」的提示狀態時都
    // 重新呼叫一次，否則下一次打字會被 CommandLineManager 當成新指令
    // 執行。
    cmdMgr->waitForInput(core::InputType::String);
}

bool OffsetCommand::applyNewDistance(double d)
{
    if (d <= 0.0) return false;
    m_distance = d;
    return true;
}

void OffsetCommand::subscribeGeomPicked()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::GEOM_PICKED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onGeomPicked(v); },
                                      Qt::QueuedConnection);
        });
}

void OffsetCommand::onGeomPicked(const QVariant& payload)
{
    if (m_state != State::WaitCurve) return;

    const QVariantMap map = payload.toMap();
    const QString uuid = map.value("geomUuid").toString();

    auto* cmdMgr = core::CommandLineManager::instance();

    if (uuid.isEmpty()) {
        // 點在空白處（沒有點在既有幾何上）：視為「直接點兩點決定距離」
        // 的第一點，不當成選取失敗——這一輪點擊本身就已經有意義，直接
        // 拿它當量測起點，不必再要求使用者多打一個字重新觸發。
        const QPointF ptF = map.value("point").value<QPointF>();
        m_distMeasurePt1 = QVector2D(float(ptF.x()), float(ptF.y()));
        m_state = State::WaitDistancePoint2;
        if (cmdMgr) {
            cmdMgr->showPrompt("[OFFSET] Click the second point to measure the distance:");
            cmdMgr->waitForInput(core::InputType::String);
        }
        return;
    }

    Sketch* sk = activeSketch();
    if (isFixedReferenceUuid(uuid) || !sk) {
        if (cmdMgr) cmdMgr->printWarning("⚠️  No selectable geometry at that point.");
        return;
    }

    // MVP 範圍限制：只支援 Line／Circle／Arc，選到別的型別當場提示、停留
    // 在同一階段。
    auto* g = sk->findGeometry(uuid);
    const bool supported = dynamic_cast<SketchLine*>(g)   != nullptr ||
                           dynamic_cast<SketchCircle*>(g) != nullptr ||
                           dynamic_cast<SketchArc*>(g)    != nullptr;
    if (!supported) {
        if (cmdMgr) cmdMgr->printWarning(
            "⚠️  OFFSET currently only supports lines, circles, and arcs.");
        return;
    }

    m_curveUuid = uuid;
    m_state = State::WaitSide;
    showPickPrompt("the side to offset toward");
}

// ─────────────────────────────────────────────────────────────────────────
// 點選偏移方向
// ─────────────────────────────────────────────────────────────────────────

void OffsetCommand::subscribePointAcquired()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::POINT_ACQUIRED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onPointAcquired(v); },
                                      Qt::QueuedConnection);
        });
}

void OffsetCommand::onPointAcquired(const QVariant& payload)
{
    if (m_state == State::WaitDistancePoint2) {
        const QVariantMap map = payload.toMap();
        const QPointF ptF = map.value("point").value<QPointF>();
        const QVector2D pt2(float(ptF.x()), float(ptF.y()));

        const double measured = double((pt2 - m_distMeasurePt1).length());
        auto* cmdMgr = core::CommandLineManager::instance();
        if (!applyNewDistance(measured)) {
            if (cmdMgr) cmdMgr->printError(
                "The two points are too close together — click a second point "
                "farther away:");
            return;  // 停留在 WaitDistancePoint2，m_distMeasurePt1 不變，重新點第二點
        }

        if (cmdMgr) cmdMgr->printSuccess(
            QString("✅ Distance set to %1 (measured between two points).").arg(m_distance));
        m_state = State::WaitCurve;
        showPickPrompt("curve to offset (Line/Circle/Arc)");
        return;
    }

    if (m_state != State::WaitSide) return;

    auto* cmdMgr = core::CommandLineManager::instance();
    if (m_distance <= 0.0) {
        if (cmdMgr) cmdMgr->printWarning(
            "⚠️  Offset distance must be positive — type a number, or click two "
            "points, to set it.");
        return;  // 停留在 WaitSide，讓使用者先設定距離
    }

    const QVariantMap map = payload.toMap();
    const QPointF ptF = map.value("point").value<QPointF>();
    const QVector2D sidePt(float(ptF.x()), float(ptF.y()));

    Sketch* sk = activeSketch();
    bool created = false;

    if (sk) {
        auto* srcGeom = sk->findGeometry(m_curveUuid);
        if (dynamic_cast<SketchLine*>(srcGeom) || dynamic_cast<SketchArc*>(srcGeom)) {
            // Line／Arc：走連續鏈偏移（單一、沒有相連鄰居的曲線，鏈長度
            // 自然就是 1，等同單曲線偏移——見 offsetChainAt() 文件說明）。
            const trimext::OffsetChainResult cr =
                trimext::offsetChainAt(sk, m_curveUuid, m_distance, sidePt);
            if (cr.success) {
                s_lastDistance = m_distance;
                ++m_offsetCount;

                for (const auto& seg : cr.segments) {
                    if (seg.isArc) {
                        // Concentric／FixedRadius 是 Arc 整體的性質，不受
                        // 轉角調整影響，每一個 Arc 段都提供。
                        sk->addConstraint(SketchConstraint::makeConcentric(
                            seg.sourceUuid, seg.newUuid));
                        sk->addConstraint(SketchConstraint::makeFixedRadius(
                            seg.newUuid, seg.newRadius));
                    } else {
                        sk->addConstraint(SketchConstraint::makeParallel(
                            seg.sourceUuid, seg.newUuid));
                        if (!seg.sourceRefPointUuid.isEmpty() && !seg.newRefPointUuid.isEmpty()) {
                            sk->addConstraint(SketchConstraint::makeFixedDistance(
                                GeomRef(seg.sourceRefPointUuid, GeomHandle::WholeGeom),
                                GeomRef(seg.newRefPointUuid,    GeomHandle::WholeGeom),
                                m_distance));
                        }
                    }
                }
                for (const auto& joint : cr.joints) {
                    sk->addConstraint(SketchConstraint::makeCoincident(
                        GeomRef(joint.first,  GeomHandle::WholeGeom),
                        GeomRef(joint.second, GeomHandle::WholeGeom)));
                }
                sk->solveConstraints();
                Q_EMIT sk->rebuildRequested();

                created = true;
                if (cmdMgr) {
                    cmdMgr->printSuccess(QString("✅ Offset created (D=%1, %2 segment(s)).")
                                              .arg(m_distance).arg(cr.segments.size()));
                }
            }
        } else {
            // Circle：單一曲線偏移（不參與鏈——本來就是獨立封閉曲線）。
            const trimext::OffsetResult r = trimext::offsetAt(sk, m_curveUuid, m_distance, sidePt);
            if (r.success) {
                s_lastDistance = m_distance;
                ++m_offsetCount;

                if (r.newRadius > 0.0) {
                    sk->addConstraint(SketchConstraint::makeConcentric(m_curveUuid, r.newCurveUuid));
                    sk->addConstraint(SketchConstraint::makeFixedRadius(r.newCurveUuid, r.newRadius));
                    sk->solveConstraints();
                    Q_EMIT sk->rebuildRequested();
                }

                created = true;
                if (cmdMgr) cmdMgr->printSuccess(QString("✅ Offset created (D=%1).").arg(m_distance));
            }
        }
    }

    if (!created && cmdMgr) {
        cmdMgr->printWarning(
            "⚠️  Cannot offset: unsupported geometry, or the result is degenerate "
            "(e.g. offsetting a circle/arc inward past its center).");
    }

    // 回到選取來源曲線階段，繼續下一次 OFFSET（比照 TRIM 的連續迴圈
    // UX，按 Enter／右鍵／Esc 才會結束——見 onCancelled()）。
    beginCurveStage();
}

// ─────────────────────────────────────────────────────────────────────────
// 距離（D 指令隨時可觸發，設定完成後回到原本正在等待的階段）
// ─────────────────────────────────────────────────────────────────────────

void OffsetCommand::subscribeStringInput()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::STRING_INPUT, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onStringInput(v); },
                                      Qt::QueuedConnection);
        });
}

void OffsetCommand::onStringInput(const QVariant& payload)
{
    if (m_state != State::WaitCurve && m_state != State::WaitSide) return;

    const QString raw = payload.toString().trimmed();
    auto* cmdMgr = core::CommandLineManager::instance();
    const QString which = (m_state == State::WaitCurve) ? "curve to offset (Line/Circle/Arc)"
                                                          : "the side to offset toward";

    // 不用先打 D：直接輸入合法正數就當作新距離套用。
    bool isNum = false;
    const double asNumber = raw.toDouble(&isNum);
    if (isNum) {
        if (!applyNewDistance(asNumber)) {
            if (cmdMgr) cmdMgr->printError("Distance must be positive. Try again:");
        } else if (cmdMgr) {
            cmdMgr->printSuccess(QString("Distance set to %1.").arg(m_distance));
        }
        showPickPrompt(which);
        return;
    }

    const QString kw = raw.toUpper();
    if (kw == "D" || kw == "DISTANCE") {
        // 保留明確指令的用法（非必要，上面已經可以直接輸入數字）。
        m_returnState = m_state;
        beginDistanceOverrideStage();
        return;
    }

    if (cmdMgr) {
        cmdMgr->printError(QString("Unrecognized input \"%1\" — select geometry, type a "
                                   "distance value, click two points to measure it, or "
                                   "press Enter/right-click to finish.").arg(raw));
    }
    showPickPrompt(which);
}

void OffsetCommand::beginDistanceOverrideStage()
{
    m_state = State::WaitDistanceOverride;

    subscribeNumberInput();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt(QString("[OFFSET] Specify offset distance (current: %1):")
                                .arg(m_distance));
        cmdMgr->waitForInput(core::InputType::Number);
    }
}

void OffsetCommand::subscribeNumberInput()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::NUMBER_INPUT, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onNumberInput(v); },
                                      Qt::QueuedConnection);
        });
}

void OffsetCommand::onNumberInput(const QVariant& payload)
{
    if (m_state != State::WaitDistanceOverride) return;

    bool ok = false;
    const double d = payload.toString().trimmed().toDouble(&ok);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (!ok || !applyNewDistance(d)) {
        if (cmdMgr) {
            cmdMgr->printError("Invalid distance. Enter a positive number:");
            cmdMgr->waitForInput(core::InputType::Number);
        }
        return;  // 停留在 WaitDistanceOverride
    }

    // NUMBER_INPUT 只在設定距離期間需要，設定完成後立刻取消訂閱，避免
    // 之後在選取/點選階段誤把別的數字輸入當成距離。
    auto* bus = core::Application::instance()->eventBus();
    if (bus) bus->unsubscribe(core::Events::NUMBER_INPUT, this);

    // 回到原本正在等待的階段（不重新開始整個指令，也不清掉已經選好的
    // 來源曲線——如果是在 WaitSide 階段觸發，改完距離後繼續點方向）。
    m_state = m_returnState;
    showPickPrompt(m_state == State::WaitCurve ? "curve to offset (Line/Circle/Arc)"
                                                 : "the side to offset toward");
}

// ─────────────────────────────────────────────────────────────────────────
// 取消／結束（Enter／右鍵／Esc 皆經由 COMMAND_CANCELLED 走這裡，比照
// TrimCommand::onCancelled() 的既有慣例：依目前已完成的偏移數量決定要印
// 「完成摘要」還是「純取消」，兩種情況都會結束整個指令）
// ─────────────────────────────────────────────────────────────────────────

void OffsetCommand::subscribeCancelled()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onCancelled(v); },
                                      Qt::QueuedConnection);
        });
}

void OffsetCommand::onCancelled(const QVariant&)
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        if (m_offsetCount > 0)
            cmdMgr->printSuccess(QString("✅ OFFSET finished, %1 object(s) created.")
                                      .arg(m_offsetCount));
        else
            cmdMgr->printMessage("OFFSET cancelled.");
    }
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// unsubscribeAll / cleanup
// ─────────────────────────────────────────────────────────────────────────

void OffsetCommand::unsubscribeAll()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->unsubscribe(core::Events::NUMBER_INPUT,      this);
    bus->unsubscribe(core::Events::GEOM_PICKED,       this);
    bus->unsubscribe(core::Events::POINT_ACQUIRED,    this);
    bus->unsubscribe(core::Events::STRING_INPUT,      this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED, this);
}

void OffsetCommand::cleanup()
{
    unsubscribeAll();

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->clearPrompt();

    m_state       = State::Idle;
    m_returnState = State::WaitCurve;
    m_distance    = 0.0;
    m_curveUuid.clear();
    m_distMeasurePt1 = QVector2D();
    m_offsetCount = 0;

    if (state() == CommandState::Running)
        complete(CommandResult::Success());
}

} // namespace command
} // namespace aicad
