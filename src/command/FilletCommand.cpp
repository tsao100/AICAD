/**
 * @file FilletCommand.cpp
 * @brief 見 FilletCommand.h 檔頭說明。
 */
#include "FilletCommand.h"
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

double FilletCommand::s_lastRadius = 0.0;

namespace {
bool isFixedReferenceUuid(const QString& uuid)
{
    return uuid.startsWith("sketch_xaxis:") ||
           uuid.startsWith("sketch_yaxis:") ||
           uuid.startsWith("sketch_origin:");
}
} // namespace

FilletCommand::FilletCommand()
    : Command("FILLET", "Fillet two straight lines (alias: F)")
{
}

QString FilletCommand::getUsage() const
{
    return "Usage: FILLET — select two straight lines (type R to set the "
           "fillet radius, default is 0 or the last value used). "
           "Arc/circle participants are not supported yet.";
}

cad::Sketch* FilletCommand::activeSketch() const
{
    return core::Application::instance()->activeSketch();
}

// ─────────────────────────────────────────────────────────────────────────
// execute
// ─────────────────────────────────────────────────────────────────────────

CommandResult FilletCommand::execute(const CommandContext& /*ctx*/)
{
    m_state       = State::Idle;
    m_returnState = State::WaitFirstObject;
    m_radius      = s_lastRadius;  // 帶入前一次執行設定的半徑值（首次執行為 0）
    m_line1Uuid.clear();
    m_clickPt1 = QVector2D();

    Sketch* sk = activeSketch();
    if (!sk) {
        auto* cmdMgr = core::CommandLineManager::instance();
        if (cmdMgr) cmdMgr->printError("No active sketch. Enter sketch edit mode first.");
        return CommandResult::Failure("No active sketch.");
    }

    // FILLET 一律走互動流程（兩次點選，中途可用 R 改半徑），不支援模式 A
    // 預先選取——預先選取兩條線的「先後順序」與「各自點擊位置」無從得知，
    // 這兩者對圓角計算是必要資訊，因此忽略 ctx.args。
    setWaitingForInput();
    beginFirstObjectStage();
    return CommandResult::Success("Waiting for first line...");
}

// ─────────────────────────────────────────────────────────────────────────
// 選取兩條直線（半徑可隨時用 R 修改）
// ─────────────────────────────────────────────────────────────────────────

void FilletCommand::beginFirstObjectStage()
{
    m_state = State::WaitFirstObject;
    m_line1Uuid.clear();
    m_clickPt1 = QVector2D();

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetGeom);

    subscribeGeomPicked();
    subscribeStringInput();
    subscribeCancelled();

    showPickPrompt("first line");
}

void FilletCommand::showPickPrompt(const QString& which)
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (!cmdMgr) return;
    cmdMgr->showPrompt(QString("[FILLET] R=%1 — Select %2 (type R to change radius):")
                            .arg(m_radius).arg(which));
    // waitForInput() 是一次性的：GEOM_PICKED（滑鼠點選幾何）走獨立於命令列
    // 文字輸入之外的路徑，不受影響、隨時有效；但要讓使用者隨時能打 R 觸發
    // 改半徑，必須每次重新進入「等待選取」的提示狀態時都重新呼叫一次，
    // 否則下一次打字會被 CommandLineManager 當成新指令執行。
    cmdMgr->waitForInput(core::InputType::String);
}

void FilletCommand::subscribeGeomPicked()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::GEOM_PICKED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onGeomPicked(v); },
                                      Qt::QueuedConnection);
        });
}

void FilletCommand::onGeomPicked(const QVariant& payload)
{
    if (m_state != State::WaitFirstObject && m_state != State::WaitSecondObject) return;

    const QVariantMap map = payload.toMap();
    const QString uuid = map.value("geomUuid").toString();
    const QPointF ptF = map.value("point").value<QPointF>();
    const QVector2D clickPt(float(ptF.x()), float(ptF.y()));

    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk = activeSketch();

    if (uuid.isEmpty() || isFixedReferenceUuid(uuid) || !sk) {
        if (cmdMgr) cmdMgr->printWarning("⚠️  No selectable geometry at that point.");
        return;
    }

    // MVP 範圍限制：只支援直線，選到別的型別當場提示、停留在同一階段。
    if (!dynamic_cast<SketchLine*>(sk->findGeometry(uuid))) {
        if (cmdMgr) cmdMgr->printWarning("⚠️  FILLET currently only supports straight lines.");
        return;
    }

    if (m_state == State::WaitFirstObject) {
        m_line1Uuid = uuid;
        m_clickPt1  = clickPt;
        m_state = State::WaitSecondObject;
        showPickPrompt("second line");
        return;
    }

    // State::WaitSecondObject
    if (uuid == m_line1Uuid) {
        if (cmdMgr) cmdMgr->printWarning("⚠️  Select a different line for the second object.");
        return;
    }

    const QString line1Uuid = m_line1Uuid;   // filletAt() 呼叫後 m_line1Uuid 已由 cleanup() 清空
    const trimext::FilletResult r =
        trimext::filletAt(sk, line1Uuid, uuid, m_radius, m_clickPt1, clickPt);

    if (r.success) {
        s_lastRadius = m_radius;  // 記住這次的半徑，供下次執行 FILLET 帶入

        if (!r.arcUuid.isEmpty()) {
            // 半徑 > 0：疊加 Coincident × 2（line 端點 ↔ 弧端點）、
            // Tangent × 2（弧 ↔ 各線）、FixedRadius × 1（見 FilletCommand.h
            // 檔頭「約束處理」說明；filletAt() 內部已把弧的起訖點建成獨立
            // 新點，就是為了讓這裡能疊加明確、可編輯/可刪除的約束）。
            sk->addConstraint(SketchConstraint::makeCoincident(
                GeomRef(r.line1PointUuid, GeomHandle::WholeGeom),
                GeomRef(r.arcStartUuid,   GeomHandle::WholeGeom)));
            sk->addConstraint(SketchConstraint::makeCoincident(
                GeomRef(r.line2PointUuid, GeomHandle::WholeGeom),
                GeomRef(r.arcEndUuid,     GeomHandle::WholeGeom)));
            // ⚠️ makeTangent(geomA, geomB) 對應到 ConstraintSolver.cpp 的
            // TangentEquation，該方程式寫死假設 refs[0]＝線、refs[1]＝圓/弧
            // （dist(center,line)=r，見該處註解），GeomVarLayout::indexFor()
            // 又是純粹依 handle 名稱查表、不檢查實際幾何型別——參數順序一旦
            // 寫反（弧在前、線在後），會把線的區域變數硬當成圓心/半徑去讀，
            // 讀到超出該線實際配置範圍的 index，導致 QVector 越界崩潰。
            // 這裡務必是「線在前、弧在後」。
            sk->addConstraint(SketchConstraint::makeTangent(line1Uuid, r.arcUuid));
            sk->addConstraint(SketchConstraint::makeTangent(uuid,      r.arcUuid));

            // 半徑尺寸箭頭畫在弧的中點方向（見 filletAt() 的 arcMidDir 說明），
            // 而不是預設的草圖 X 軸方向——makeFixedRadius() 回傳的是一般化的
            // 約束，dimLineOffsetX/Y 預設 0，畫的時候會退回 X 軸方向，所以
            // 這裡建構後、加進 sketch 之前先設定好。
            SketchConstraint radiusConstraint = SketchConstraint::makeFixedRadius(r.arcUuid, m_radius);
            radiusConstraint.dimLineOffsetX = r.arcMidDir.x();
            radiusConstraint.dimLineOffsetY = r.arcMidDir.y();
            sk->addConstraint(radiusConstraint);
            sk->solveConstraints();
            Q_EMIT sk->rebuildRequested();
        }
        // 半徑 = 0（無插入弧）：filletAt() 內部已經另外補上一條 Coincident
        // 約束把兩線端點接起來，這裡不需要再做任何事。

        if (cmdMgr) {
            const QString note = r.removedExistingCoincident
                ? " (removed pre-existing coincident constraint at the corner)"
                : "";
            cmdMgr->printSuccess(QString("✅ Filleted (R=%1)%2.").arg(m_radius).arg(note));
        }
    } else {
        if (cmdMgr) cmdMgr->printWarning(
            "⚠️  Cannot fillet: lines are parallel/collinear, or radius is unreasonable.");
    }

    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// 半徑（R 指令隨時可觸發，設定完成後回到原本的選取階段）
// ─────────────────────────────────────────────────────────────────────────

void FilletCommand::subscribeStringInput()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::STRING_INPUT, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onStringInput(v); },
                                      Qt::QueuedConnection);
        });
}

void FilletCommand::onStringInput(const QVariant& payload)
{
    if (m_state != State::WaitFirstObject && m_state != State::WaitSecondObject) return;

    const QString kw = payload.toString().trimmed().toUpper();
    auto* cmdMgr = core::CommandLineManager::instance();

    if (kw != "R" && kw != "RADIUS") {
        if (cmdMgr) {
            cmdMgr->printError(QString("Unrecognized input \"%1\" — select a line, "
                                       "or type R to set the fillet radius.")
                                    .arg(payload.toString()));
        }
        // 停留在原本的選取階段，重新等待（R 依然可用，滑鼠點選也不受影響）。
        const QString which = (m_state == State::WaitFirstObject) ? "first line" : "second line";
        showPickPrompt(which);
        return;
    }

    m_returnState = m_state;
    beginRadiusOverrideStage();
}

void FilletCommand::beginRadiusOverrideStage()
{
    m_state = State::WaitRadiusOverride;

    subscribeNumberInput();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt(QString("[FILLET] Specify fillet radius (current: %1, "
                                   "0 = trim to intersection):").arg(m_radius));
        cmdMgr->waitForInput(core::InputType::Number);
    }
}

void FilletCommand::subscribeNumberInput()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::NUMBER_INPUT, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onNumberInput(v); },
                                      Qt::QueuedConnection);
        });
}

void FilletCommand::onNumberInput(const QVariant& payload)
{
    if (m_state != State::WaitRadiusOverride) return;

    bool ok = false;
    const double r = payload.toString().trimmed().toDouble(&ok);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (!ok || r < 0.0) {
        if (cmdMgr) {
            cmdMgr->printError("Invalid radius. Enter a non-negative number:");
            cmdMgr->waitForInput(core::InputType::Number);
        }
        return;  // 停留在 WaitRadiusOverride
    }

    m_radius = r;

    // NUMBER_INPUT 只在設定半徑期間需要，設定完成後立刻取消訂閱，避免
    // 之後在選取階段誤把別的數字輸入（目前用不到，但保留彈性）當成半徑。
    auto* bus = core::Application::instance()->eventBus();
    if (bus) bus->unsubscribe(core::Events::NUMBER_INPUT, this);

    // 回到原本正在等待的選取階段（不重新開始整個指令，也不清掉已經選好
    // 的第一條線）。
    m_state = m_returnState;
    showPickPrompt((m_state == State::WaitFirstObject) ? "first line" : "second line");
}

// ─────────────────────────────────────────────────────────────────────────
// 取消
// ─────────────────────────────────────────────────────────────────────────

void FilletCommand::subscribeCancelled()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onCancelled(v); },
                                      Qt::QueuedConnection);
        });
}

void FilletCommand::onCancelled(const QVariant&)
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->printMessage("FILLET cancelled.");
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// unsubscribeAll / cleanup
// ─────────────────────────────────────────────────────────────────────────

void FilletCommand::unsubscribeAll()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->unsubscribe(core::Events::NUMBER_INPUT,      this);
    bus->unsubscribe(core::Events::GEOM_PICKED,       this);
    bus->unsubscribe(core::Events::STRING_INPUT,      this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED, this);
}

void FilletCommand::cleanup()
{
    unsubscribeAll();

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->clearPrompt();

    m_state       = State::Idle;
    m_returnState = State::WaitFirstObject;
    m_radius      = 0.0;
    m_line1Uuid.clear();
    m_clickPt1 = QVector2D();

    if (state() == CommandState::Running)
        complete(CommandResult::Success());
}

} // namespace command
} // namespace aicad
