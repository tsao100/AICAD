/**
 * @file RectCommand.cpp
 * @brief 繪製矩形命令實作
 *
 * 關鍵設計備忘：
 *   基類 handlePointAcquired() 的呼叫順序：
 *     1. m_points.push_back(point)
 *     2. onPointAdded(m_points)          ← 每一個點都呼叫，包含最後一點
 *     3. if size >= requiredPoints:
 *            m_isFinishing = true
 *            onFinished(m_points)        ← 僅在達到上限後呼叫
 *            emit finished()
 *
 *   因此 onPointAdded 對 size==2 也會被呼叫，不可加 assert(size==1)。
 *   幾何建立集中在 onFinished()，onPointAdded 只做視覺回饋。
 */

#include "command/RectCommand.h"
#include "command/CommandTypes.h"
#include "core/Application.h"
#include "core/EventBus.h"

using namespace aicad::core;

namespace aicad {
namespace command {

// -----------------------------------------------------------------------
// 建構 / 析構
// -----------------------------------------------------------------------

RectCommand::RectCommand(QObject* parent)
    : PointInputCommand("rect", "Draw Rectangle", parent)
{
    // 定點數模式：恰好 2 個對角點
    // 第 2 點加入後，基類自動呼叫 onFinished() 並發出 finished()
    m_requiredPoints = 2;
}

RectCommand::~RectCommand()
{
    // 基類析構子已呼叫 unsubscribePointEvents()，此處不需重複
}

// -----------------------------------------------------------------------
// Command 介面實作
// -----------------------------------------------------------------------

CommandResult RectCommand::execute(const CommandContext& context)
{
    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    // --- 非互動模式：命令列直接帶入座標 (x1 y1 x2 y2) ---
    if (context.args.size() >= 4) {
        const QVector2D c1(context.args[0].toFloat(), context.args[1].toFloat());
        const QVector2D c2(context.args[2].toFloat(), context.args[3].toFloat());

        // 防禦：兩點相同則不建立
        if (c1 == c2) {
            return CommandResult::Failure("Two corners must be different points");
        }

        createRect(c1, c2);
        return CommandResult::Success("Rectangle created");
    }

    // --- 互動模式：進入兩點輸入流程 ---

    // 重置基類狀態（避免命令物件被複用時殘留舊資料）
    m_points.clear();
    m_isFinishing = false;

    // 要求視圖切換至草繪模式，並啟用矩形橡皮筋預覽
    QVariantMap viewSetup;
    viewSetup["mode"]           = "sketching";
    viewSetup["rubberBandMode"] = "rect";
    bus->publish("command.request-view-setup", viewSetup);

    // 標記命令為執行中，讓 CommandManager 知道尚未結束
    setState(CommandState::Running);

    // 訂閱三個事件（由基類處理，均透過 QueuedConnection 在主執行緒執行）：
    //   POINT_ACQUIRED   → handlePointAcquired → onPointAdded / onFinished
    //   COMMAND_FINISH   → handleFinishRequested → onCommandFinishing(UserFinished)
    //   POINT_CANCELLED  → handleCancelled       → onCommandFinishing(UserCancelled)
    subscribePointEvents();

    // 輸出起角提示
    const QString firstPrompt = promptForNextPoint(0);
    outputMessage(firstPrompt);
    bus->publish(Events::COMMAND_PROMPT, firstPrompt);
    bus->publish(Events::COMMAND_LOG,    firstPrompt);

    return CommandResult::Success("Waiting for input");
}

void RectCommand::cleanup()
{
    qDebug() << "[RectCommand] Cleanup started";

    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    // 退訂所有事件：必須在改變視圖模式之前執行，
    // 防止 cleanup 途中收到殘留的 POINT_ACQUIRED 事件
    unsubscribePointEvents();
    qDebug() << "[RectCommand] Unsubscribed from EventBus";

    // 清除橡皮筋；setIdleMode = false 表示保持草繪模式
    QVariantMap cleanupRequest;
    cleanupRequest["clearRubberBand"] = true;
    cleanupRequest["setIdleMode"]     = false;
    bus->publish("command.request-cleanup", cleanupRequest);

    // 重置旗標（m_points 由基類管理）
    m_isFinishing = false;

    qDebug() << "[RectCommand] Cleanup completed";
}

QString RectCommand::getUsage() const
{
    return "Usage: rect [x1 y1 x2 y2] | Click two opposite corners, ESC to cancel";
}

// -----------------------------------------------------------------------
// PointInputCommand 純虛函數實作
// -----------------------------------------------------------------------

void RectCommand::onPointAdded(const QVector<QVector2D>& points)
{
    // ⚠ 此函數對 size==1 及 size==2 都會被呼叫：
    //   size==1：第 1 點剛加入，尚未達到 requiredPoints=2，需要視覺回饋
    //   size==2：第 2 點剛加入，即將進入 onFinished()，此處不做任何事
    //   size> 2：防禦性保護（正常情況不應出現）

    if (points.isEmpty()) return;   // 防禦：不應發生，但避免 crash

    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    const int size = points.size();

    if (size == 1) {
        // 第 1 點：設定橡皮筋錨點，讓視圖層從此點開始動態預覽矩形範圍
        const QVector2D& corner1 = points.first();

        QVariantMap rubberUpdate;
        rubberUpdate["action"] = "clearAndAdd";
        rubberUpdate["point"]  = QVariant::fromValue(corner1);
        bus->publish("command.update-rubber-band", rubberUpdate);

        outputMessage(QString("First corner: (%1, %2)")
                          .arg(corner1.x()).arg(corner1.y()));

        // 提示訊息由基類在 onPointAdded() 返回後從 promptForNextPoint(1) 輸出
        // （此處不重複呼叫，避免雙重輸出）

    } else if (size == 2) {
        // 第 2 點：onFinished() 隨後會處理幾何建立，此處僅做靜默處理
        // 不更新橡皮筋（即將被 cleanup 清除），不輸出額外訊息
        // （幾何建立與完成訊息由 onFinished() 統一輸出）
    }

    // size > 2：m_requiredPoints = 2，正常情況不會到達此分支
    // 若因 bug 造成重入，直接 return 即可，不會 crash
}

void RectCommand::onFinished(const QVector<QVector2D>& points)
{
    // 基類保證此時 points.size() == m_requiredPoints == 2
    // 但仍加防禦性檢查，避免未來基類邏輯變動時 crash
    if (points.size() < 2) {
        qWarning() << "[RectCommand] onFinished called with insufficient points:"
                   << points.size();
        return;
    }

    const QVector2D& c1 = points[0];
    const QVector2D& c2 = points[1];

    // 兩點相同則不建立（例如使用者雙擊同一位置）
    if (c1 == c2) {
        outputMessage("Rectangle skipped: two corners are the same point.");
        return;
    }

    createRect(c1, c2);
}

QString RectCommand::promptForNextPoint(int index) const
{
    switch (index) {
    case 0:  return "Specify first corner:";
    case 1:  return "Specify opposite corner or ESC to cancel:";
    default: return QString();  // 不應出現（m_requiredPoints = 2）
    }
}

void RectCommand::onCommandFinishing(FinishReason reason)
{
    // 定點數模式下，此函數只在「正常完成之前」發生的 RMB/Space/ESC 才被呼叫：
    //   - 正常完成（size == 2）走 onFinished()，不會到這裡
    //   - 提前結束（size < 2）才走這裡

    const int collected = m_points.size();   // 0 或 1

    if (reason == FinishReason::UserFinished) {
        // RMB / Space 在取得 2 點之前觸發
        outputMessage(QString("Rectangle command ended early (%1/2 points). Nothing created.")
                          .arg(collected));
    } else {
        // ESC 取消
        outputMessage(collected == 0
                          ? "Rectangle command cancelled."
                          : QString("Rectangle command cancelled after %1 point(s). Nothing created.")
                                .arg(collected));
    }
}

// -----------------------------------------------------------------------
// 私有輔助函數
// -----------------------------------------------------------------------

void RectCommand::createRect(const QVector2D& corner1, const QVector2D& corner2)
{
    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    // 計算軸對齊 BBox：無論使用者從哪個方向拖拽都正確
    const float x1 = qMin(corner1.x(), corner2.x());
    const float y1 = qMin(corner1.y(), corner2.y());
    const float x2 = qMax(corner1.x(), corner2.x());
    const float y2 = qMax(corner1.y(), corner2.y());

    // 四個頂點
    const QVector2D bl(x1, y1);  // Bottom-Left  左下
    const QVector2D br(x2, y1);  // Bottom-Right 右下
    const QVector2D tr(x2, y2);  // Top-Right    右上
    const QVector2D tl(x1, y2);  // Top-Left     左上

    // 4 條邊（逆時針閉合）
    // 使用 struct 初始化列表而非 QPair，避免部分編譯器對 QPair<QVector2D,QVector2D> 的警告
    struct Edge { QVector2D start; QVector2D end; };
    const Edge edges[4] = {
        { bl, br },  // 底邊
        { br, tr },  // 右邊
        { tr, tl },  // 頂邊
        { tl, bl },  // 左邊
    };

    for (const Edge& e : edges) {
        QVariantMap lineData;
        lineData["startPoint"] = QVariant::fromValue(e.start);
        lineData["endPoint"]   = QVariant::fromValue(e.end);
        bus->publish("command.create-sketch-line", lineData);
    }

    outputMessage(QString("Rectangle created: (%1,%2) to (%3,%4)  [W=%5, H=%6]")
                      .arg(x1).arg(y1)
                      .arg(x2).arg(y2)
                      .arg(x2 - x1)
                      .arg(y2 - y1));
}

} // namespace command
} // namespace aicad
