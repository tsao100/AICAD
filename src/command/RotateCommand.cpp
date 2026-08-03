/**
 * @file RotateCommand.cpp
 * @brief 見 RotateCommand.h 檔頭說明。
 */
#include "RotateCommand.h"
#include "CommandRubberBandHelper.h"

#include "../core/Application.h"
#include "../core/CommandLineManager.h"
#include "../core/EventBus.h"
#include "../cad/Sketch.h"
#include "../cad/sketch/SketchGeomTransformUtil.h"
#include "../ui/UIManager.h"
#include "../view/CadView.h"

#include <QMetaObject>
#include <QPointF>
#include <QtMath>
#include <cmath>

namespace aicad {
namespace command {

using namespace cad;

namespace {
bool isFixedReferenceUuid(const QString& uuid)
{
    return uuid.startsWith("sketch_xaxis:") ||
           uuid.startsWith("sketch_yaxis:") ||
           uuid.startsWith("sketch_origin:");
}
} // namespace

RotateCommand::RotateCommand()
    : Command("ROTATE", "Rotate selected sketch geometry about a base point (alias: RO)")
{
}

QString RotateCommand::getUsage() const
{
    return "Usage: ROTATE — select geometry first then run ROTATE, "
           "or run ROTATE then click objects and press Enter, "
           "then specify base point and rotation angle in degrees "
           "(counter-clockwise positive).";
}

cad::Sketch* RotateCommand::activeSketch() const
{
    return core::Application::instance()->activeSketch();
}

// ─────────────────────────────────────────────────────────────────────────
// execute
// ─────────────────────────────────────────────────────────────────────────

CommandResult RotateCommand::execute(const CommandContext& ctx)
{
    m_state = State::Idle;
    m_selection.clear();
    m_basePoint = QVector2D();

    Sketch* sk = activeSketch();
    if (!sk) {
        auto* cmdMgr = core::CommandLineManager::instance();
        if (cmdMgr) cmdMgr->printError("No active sketch. Enter sketch edit mode first.");
        return CommandResult::Failure("No active sketch.");
    }

    if (!ctx.args.isEmpty()) {
        QStringList preSelected;
        for (const QString& uuid : ctx.args) {
            if (uuid.isEmpty() || isFixedReferenceUuid(uuid)) continue;
            if (sk->findGeometry(uuid)) preSelected.append(uuid);
        }
        if (preSelected.isEmpty()) {
            auto* cmdMgr = core::CommandLineManager::instance();
            if (cmdMgr) cmdMgr->printWarning("⚠️  No valid geometry in selection.");
            return CommandResult::Failure("No valid geometry in selection.");
        }
        setWaitingForInput();
        onSelectionConfirmed(preSelected);
        return CommandResult::Success("Waiting for base point...");
    }

    setWaitingForInput();
    beginSelection(sk);
    return CommandResult::Success("Waiting for selection...");
}

// ─────────────────────────────────────────────────────────────────────────
// 選取階段
// ─────────────────────────────────────────────────────────────────────────

void RotateCommand::beginSelection(cad::Sketch* sketch)
{
    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetGeom);

    m_picker = new SketchSelectionPicker(this);
    connect(m_picker, &SketchSelectionPicker::confirmed,
            this, &RotateCommand::onSelectionConfirmed);
    connect(m_picker, &SketchSelectionPicker::cancelled,
            this, &RotateCommand::onSelectionCancelled);

    m_picker->begin(sketch, SketchSelectionPicker::Mode::PickMultiple,
                    "[ROTATE] Select objects, then press Enter:");
}

void RotateCommand::onSelectionConfirmed(const QStringList& uuids)
{
    if (uuids.isEmpty()) {
        onSelectionCancelled();
        return;
    }
    m_selection = uuids;
    beginBasePointStage();
}

void RotateCommand::onSelectionCancelled()
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->printMessage("ROTATE cancelled.");
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// 取基準點
// ─────────────────────────────────────────────────────────────────────────

void RotateCommand::beginBasePointStage()
{
    m_state = State::WaitBasePoint;

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetPoint);

    subscribePointAcquired();
    subscribeCancelled();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->showPrompt("[ROTATE] Specify base point:");
}

void RotateCommand::subscribePointAcquired()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::POINT_ACQUIRED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onPointAcquired(v); },
                                      Qt::QueuedConnection);
        });
}

void RotateCommand::onPointAcquired(const QVariant& payload)
{
    const QVariantMap map = payload.toMap();
    const QPointF ptF = map.value("point").value<QPointF>();
    const QVector2D pt(float(ptF.x()), float(ptF.y()));

    if (m_state == State::WaitBasePoint) {
        m_basePoint = pt;
        beginAngleStage();

        // 基準點→游標的橡皮筋線，線的角度就是目前預覽的旋轉角度——拖曳
        // 時可以直接看到角度，跟直接輸入角度視覺上是一致的。
        if (Sketch* sk = activeSketch())
            rb::armLinePreview(sk, m_basePoint);
        return;
    }

    if (m_state == State::WaitAngle) {
        // 用滑鼠點一個點來指定旋轉角度：角度＝從基準點指向該點的方位角
        // （與直接輸入同樣角度的數字效果相同，兩種輸入方式共用同一條
        // Transform2D::rotation() 路徑）。與 MOVE/COPY 的「取第二點」概念
        // 類似，但這裡取的是角度而不是位移向量。
        if ((pt - m_basePoint).lengthSquared() < 1e-8f) {
            // 點在基準點正上方（幾乎重合），角度無意義，忽略這次點擊，
            // 讓使用者可以繼續移動滑鼠或改用數字輸入。
            return;
        }
        const double radians = std::atan2(double(pt.y() - m_basePoint.y()),
                                          double(pt.x() - m_basePoint.x()));
        applyRotation(qRadiansToDegrees(radians));
    }
}

// ─────────────────────────────────────────────────────────────────────────
// 取旋轉角度
// ─────────────────────────────────────────────────────────────────────────

void RotateCommand::beginAngleStage()
{
    m_state = State::WaitAngle;

    // 基準點階段已經在 GetPoint 模式並訂閱 POINT_ACQUIRED，這裡不重複
    // 呼叫 subscribePointAcquired()（該訂閱在 beginBasePointStage() 就
    // 已存在，一路延續到本階段，讓使用者可以直接點第二點決定角度，也
    // 可以改成打數字——兩種輸入方式同時有效）。

    subscribeNumberInput();

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt(
            "[ROTATE] Specify rotation angle: click a point, or type degrees (CCW+):");
        cmdMgr->waitForInput(core::InputType::Number);
    }
}

void RotateCommand::applyRotation(double degrees)
{
    Sketch* sk = activeSketch();
    auto* cmdMgr = core::CommandLineManager::instance();

    if (sk && !m_selection.isEmpty()) {
        const double radians = qDegreesToRadians(degrees);
        const auto xf = cad::transform::Transform2D::rotation(m_basePoint, radians);
        cad::transform::applyToSelection(sk, m_selection, xf);

        if (cmdMgr)
            cmdMgr->printSuccess(
                QString("✅ Rotated %1 object(s) by %2°.").arg(m_selection.size()).arg(degrees));
    }

    cleanup();
}

void RotateCommand::subscribeNumberInput()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::NUMBER_INPUT, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onNumberInput(v); },
                                      Qt::QueuedConnection);
        });
}

void RotateCommand::onNumberInput(const QVariant& payload)
{
    if (m_state != State::WaitAngle) return;

    bool ok = false;
    const double degrees = payload.toString().trimmed().toDouble(&ok);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (!ok) {
        if (cmdMgr) {
            cmdMgr->printError("Invalid angle. Enter a number (degrees):");
            cmdMgr->waitForInput(core::InputType::Number);
        }
        return;  // 停留在 WaitAngle，等待重新輸入
    }

    applyRotation(degrees);
}

// ─────────────────────────────────────────────────────────────────────────
// 取消
// ─────────────────────────────────────────────────────────────────────────

void RotateCommand::subscribeCancelled()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onCancelled(v); },
                                      Qt::QueuedConnection);
        });
}

void RotateCommand::onCancelled(const QVariant&)
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->printMessage("ROTATE cancelled.");
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// unsubscribeAll / cleanup
// ─────────────────────────────────────────────────────────────────────────

void RotateCommand::unsubscribeAll()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->unsubscribe(core::Events::POINT_ACQUIRED,    this);
    bus->unsubscribe(core::Events::NUMBER_INPUT,      this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED, this);
}

void RotateCommand::cleanup()
{
    unsubscribeAll();
    rb::disarm();

    if (m_picker) {
        m_picker->abortSilently();
        m_picker->deleteLater();
        m_picker = nullptr;
    }

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->clearPrompt();

    m_state = State::Idle;
    m_selection.clear();
    m_basePoint = QVector2D();

    if (state() == CommandState::Running)
        complete(CommandResult::Success());
}

} // namespace command
} // namespace aicad
