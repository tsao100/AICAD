/**
 * @file LineCommand.cpp
 * @brief 繪製直線命令實作
 *
 * 繼承 PointInputCommand，蒐集兩個端點後透過 EventBus 建立直線幾何。
 * 所有事件訂閱 / 退訂 / 防重入邏輯均由基類 PointInputCommand 處理，
 * 本類只需關注「繪線業務邏輯」。
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
    // 直線只需恰好兩個點：起點 + 終點
    // 蒐集到第 2 個點後，基類會自動呼叫 onFinished() 並發出 finished()
    m_requiredPoints = 2;
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

    // --- 互動模式：進入點輸入流程 ---

    // 重置基類點序列（以防命令被重複執行）
    m_points.clear();
    m_isFinishing = false;

    // 要求視圖切換至草繪模式，並啟用直線橡皮筋
    QVariantMap viewSetup;
    viewSetup["mode"]           = "sketching";
    viewSetup["rubberBandMode"] = "line";
    bus->publish("command.request-view-setup", viewSetup);

    // 設定命令狀態為執行中，讓 CommandManager 知道命令尚未結束
    setState(CommandState::Running);

    // 訂閱 POINT_ACQUIRED / POINT_CANCELLED，開始等待使用者輸入
    // （由基類 PointInputCommand::subscribePointEvents 處理）
    subscribePointEvents();

    // 輸出第一個點的提示
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

    // 退訂所有 EventBus 事件，必須在改變視圖模式之前執行，
    // 確保不會在 cleanup 過程中再收到殘留事件
    unsubscribePointEvents();
    qDebug() << "[LineCommand] Unsubscribed from EventBus";

    // 通知視圖清除橡皮筋；保持草繪模式（setIdleMode = false）
    QVariantMap cleanupRequest;
    cleanupRequest["clearRubberBand"] = true;
    cleanupRequest["setIdleMode"]     = false;
    bus->publish("command.request-cleanup", cleanupRequest);

    // 重置狀態（m_points 由基類管理，此處不清理）
    m_isFinishing = false;

    qDebug() << "[LineCommand] Cleanup completed";
}

QString LineCommand::getUsage() const
{
    return "Usage: line [x1 y1 x2 y2]";
}

// -----------------------------------------------------------------------
// PointInputCommand 純虛函數實作
// -----------------------------------------------------------------------

void LineCommand::onPointAdded(const QVector<QVector2D>& points)
{
    // points.size() == 1：剛收到起點
    // points.size() >= 2：不會執行到此（m_requiredPoints=2 會先觸發 onFinished）
    //                     若未來改為多段線模式（m_requiredPoints=-1），才會走此分支

    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    const QVector2D& latestPoint = points.last();

    if (points.size() == 1) {
        // 收到起點：更新橡皮筋錨點，輸出下一步提示
        QVariantMap rubberUpdate;
        rubberUpdate["action"] = "clearAndAdd";
        rubberUpdate["point"]  = QVariant::fromValue(latestPoint);
        bus->publish("command.update-rubber-band", rubberUpdate);

        outputMessage(QString("First point: (%1, %2). Specify next point:")
                          .arg(latestPoint.x()).arg(latestPoint.y()));
    } else {
        // 多段線延伸模式（目前 m_requiredPoints=2 不會觸發，預留擴充）
        const QVector2D& prevPoint = points[points.size() - 2];

        // 建立前一段線
        QVariantMap lineData;
        lineData["startPoint"] = QVariant::fromValue(prevPoint);
        lineData["endPoint"]   = QVariant::fromValue(latestPoint);
        bus->publish("command.create-sketch-line", lineData);

        outputMessage(QString("Line created from (%1,%2) to (%3,%4)")
                          .arg(prevPoint.x()).arg(prevPoint.y())
                          .arg(latestPoint.x()).arg(latestPoint.y()));

        // 將橡皮筋錨點移至最新端點
        QVariantMap rubberUpdate;
        rubberUpdate["action"] = "clearAndAdd";
        rubberUpdate["point"]  = QVariant::fromValue(latestPoint);
        bus->publish("command.update-rubber-band", rubberUpdate);
    }
}

void LineCommand::onFinished(const QVector<QVector2D>& points)
{
    // 此時 points.size() == m_requiredPoints == 2
    // points[0] = 起點, points[1] = 終點
    Q_ASSERT(points.size() >= 2);

    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    const QVector2D& start = points[0];
    const QVector2D& end   = points[1];

    // 透過 EventBus 請求建立直線幾何
    QVariantMap lineData;
    lineData["startPoint"] = QVariant::fromValue(start);
    lineData["endPoint"]   = QVariant::fromValue(end);
    bus->publish("command.create-sketch-line", lineData);

    outputMessage(QString("Line created from (%1,%2) to (%3,%4)")
                      .arg(start.x()).arg(start.y())
                      .arg(end.x()).arg(end.y()));
}

QString LineCommand::promptForNextPoint(int index) const
{
    switch (index) {
    case 0:  return "Specify first point:";
    case 1:  return "Specify next point or press ESC to finish:";
    default: return QString("Specify point %1 or press ESC to finish:").arg(index + 1);
    }
}

} // namespace command
} // namespace aicad
