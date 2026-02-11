/**
 * @file LineCommand.cpp
 * @brief 連續繪製直線命令實作（折線模式）
 *
 * 無限點輸入模式（m_requiredPoints = -1）：
 *   - 每次收到新點（第 2 點起），立刻透過 EventBus 建立一段直線
 *   - 使用者按 RMB 或 Space → handleFinishRequested → onCommandFinishing(UserFinished)
 *   - 使用者按 ESC           → handleCancelled       → onCommandFinishing(UserCancelled)
 *
 * 所有事件訂閱 / 退訂 / 防重入邏輯由基類 PointInputCommand 負責。
 */

#include "command/LineCommand.h"
#include "command/CommandTypes.h"
#include "core/Application.h"
#include "core/EventBus.h"

using namespace aicad::core;

namespace aicad {
namespace command {

// -----------------------------------------------------------------------
// 建構 / 析構
// -----------------------------------------------------------------------

LineCommand::LineCommand(QObject* parent)
    : PointInputCommand("line", "Draw Line", parent)
{
    // 無限模式：持續等待點輸入，直到 RMB / Space / ESC
    // 每收到一個新點（第 2 點起）即在 onPointAdded() 中即時建線，
    // 因此不需要等到 onFinished() 才建立幾何。
    m_requiredPoints = -1;
}

LineCommand::~LineCommand()
{
}

// -----------------------------------------------------------------------
// Command 介面實作
// -----------------------------------------------------------------------

CommandResult LineCommand::execute(const CommandContext& context)
{
    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    // --- 非互動模式：命令列直接帶入座標 (x1 y1 x2 y2) ---
    if (context.args.size() >= 4) {
        QVariantMap request;
        request["commandId"] = "line";
        request["args"]      = QVariant::fromValue(context.args);
        bus->publish("command.request-sketch-line", request);
        return CommandResult::Success("Line creation requested");
    }

    // --- 互動模式：進入無限點輸入流程 ---

    // 重置基類狀態（以防命令被重複 execute）
    m_points.clear();
    m_isFinishing = false;

    // 要求視圖切換至草繪模式，並啟用直線橡皮筋
    QVariantMap viewSetup;
    viewSetup["mode"]           = "sketching";
    viewSetup["rubberBandMode"] = "line";
    bus->publish("command.request-view-setup", viewSetup);

    // 標記命令為「執行中」，讓 CommandManager 知道命令尚未結束
    setState(CommandState::Running);

    // 訂閱三個事件：POINT_ACQUIRED / COMMAND_FINISH（RMB/Space）/ POINT_CANCELLED（ESC）
    subscribePointEvents();

    // 輸出起點提示
    const QString firstPrompt = promptForNextPoint(0);
    outputMessage(firstPrompt);
    bus->publish(Events::COMMAND_PROMPT, firstPrompt);
    bus->publish(Events::COMMAND_LOG,    firstPrompt);

    return CommandResult::Success("Waiting for input");
}

void LineCommand::cleanup()
{
    qDebug() << "[LineCommand] Cleanup started";

    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    // 退訂所有事件：必須在改變視圖模式之前，防止在 cleanup 途中收到殘留事件
    unsubscribePointEvents();
    qDebug() << "[LineCommand] Unsubscribed from EventBus";

    // 通知視圖清除橡皮筋；setIdleMode = false 表示保持草繪模式
    QVariantMap cleanupRequest;
    cleanupRequest["clearRubberBand"] = true;
    cleanupRequest["setIdleMode"]     = false;
    bus->publish("command.request-cleanup", cleanupRequest);

    // 重置狀態旗標（m_points 由基類管理，不在此清理）
    m_isFinishing = false;

    qDebug() << "[LineCommand] Cleanup completed";
}

QString LineCommand::getUsage() const
{
    return "Usage: line [x1 y1 x2 y2] | Click points, RMB/Space to finish, ESC to cancel";
}

// -----------------------------------------------------------------------
// PointInputCommand 純虛函數實作
// -----------------------------------------------------------------------

void LineCommand::onPointAdded(const QVector<QVector2D>& points)
{
    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    const QVector2D& latestPoint = points.last();

    if (points.size() == 1) {
        // ── 第 1 個點：起點 ──────────────────────────────────────────
        // 尚無前一點，僅需更新橡皮筋錨點並提示使用者輸入下一點。
        QVariantMap rubberUpdate;
        rubberUpdate["action"] = "clearAndAdd";
        rubberUpdate["point"]  = QVariant::fromValue(latestPoint);
        bus->publish("command.update-rubber-band", rubberUpdate);

        outputMessage(QString("First point: (%1, %2)")
                          .arg(latestPoint.x()).arg(latestPoint.y()));

    } else {
        // ── 第 2…N 個點：立刻建立前一點 → 本點的線段 ─────────────────
        const QVector2D& prevPoint = points[points.size() - 2];

        // 發布「建線」事件，由 Sketch 層處理幾何建立
        QVariantMap lineData;
        lineData["startPoint"] = QVariant::fromValue(prevPoint);
        lineData["endPoint"]   = QVariant::fromValue(latestPoint);
        bus->publish("command.create-sketch-line", lineData);

        outputMessage(QString("Segment %1: (%2,%3) → (%4,%5)")
                          .arg(points.size() - 1)        // 第幾段線（1-based）
                          .arg(prevPoint.x())   .arg(prevPoint.y())
                          .arg(latestPoint.x()) .arg(latestPoint.y()));

        // 將橡皮筋錨點移至最新端點，方便預覽下一段
        QVariantMap rubberUpdate;
        rubberUpdate["action"] = "clearAndAdd";
        rubberUpdate["point"]  = QVariant::fromValue(latestPoint);
        bus->publish("command.update-rubber-band", rubberUpdate);
    }
}

void LineCommand::onFinished(const QVector<QVector2D>& /*points*/)
{
    // 無限模式（m_requiredPoints = -1）下此函數不會被呼叫。
    // 若日後切換為定點數模式（m_requiredPoints > 0），需在此補充最終建線邏輯。
}

QString LineCommand::promptForNextPoint(int index) const
{
    if (index == 0)
        return "Specify first point:";

    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();
    QString nextPrompt = QString("Specify next point [%1 segment(s)] – RMB or Space to finish, ESC to cancel:")
                              .arg(index);
    bus->publish(Events::COMMAND_PROMPT, nextPrompt);
    bus->publish(Events::COMMAND_LOG,    nextPrompt);

    // index >= 1：已有起點，提示後續端點並說明結束方式
    return nextPrompt;  // index = 目前已建線段數（尚未建的那段還沒算進去）
}

void LineCommand::onCommandFinishing(FinishReason reason)
{
    // 計算已成功建立的線段數（需至少兩點才能成段）
    const int segmentCount = qMax(0, m_points.size() - 1);

    if (reason == FinishReason::UserFinished) {
        // RMB 或 Space：正常完成
        outputMessage(QString("Line command finished. %1 segment(s) created.")
                          .arg(segmentCount));
    } else {
        // ESC：取消
        // 若已建立線段，幾何仍保留在模型中（由 Sketch 層管理）；
        // 如需撤銷，可在此發布 undo 事件。
        outputMessage(segmentCount > 0
                          ? QString("Line command cancelled. %1 segment(s) remain.")
                                .arg(segmentCount)
                          : "Line command cancelled.");
    }
}

} // namespace command
} // namespace aicad
