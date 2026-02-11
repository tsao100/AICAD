/**
 * @file PointInputCommand.cpp
 * @brief PointInputCommand 實作
 */

#include "command/PointInputCommand.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "command/CommandTypes.h"

using namespace aicad::core;

namespace aicad {
namespace command {

// -----------------------------------------------------------------------
// 建構 / 析構
// -----------------------------------------------------------------------

PointInputCommand::PointInputCommand(const QString& name,
                                     const QString& description,
                                     QObject* parent)
    : Command(name, description, parent)
{
    // 成員變數已在宣告處初始化（m_requiredPoints = -1, m_isFinishing = false）
}

PointInputCommand::~PointInputCommand()
{
    // 析構時確保退訂，避免 EventBus 持有懸空指標
    unsubscribePointEvents();
}

// -----------------------------------------------------------------------
// 預設虛函數實作
// -----------------------------------------------------------------------

void PointInputCommand::onCommandFinishing(FinishReason /*reason*/)
{
    // 預設空實作，子類視需求 override
}

// -----------------------------------------------------------------------
// 事件訂閱管理
// -----------------------------------------------------------------------

void PointInputCommand::subscribePointEvents()
{
    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    // ── 左鍵點擊取得座標 ──────────────────────────────────────────────
    // 透過 QueuedConnection 確保 handler 在 Qt 主執行緒（GUI thread）執行，
    // 避免跨執行緒存取 UI / OpenGL 資源造成競態條件。
    bus->subscribe(Events::POINT_ACQUIRED, this,
                   [this](const QVariant& data) {
                       QVector2D pt = data.toMap()["point"].value<QVector2D>();
                       QMetaObject::invokeMethod(
                           this,
                           [this, pt]() { handlePointAcquired(pt); },
                           Qt::QueuedConnection
                           );
                   });

    // ── RMB / Space：主動完成，保留已建立的幾何 ─────────────────────
    bus->subscribe(Events::COMMAND_FINISHED, this,
                   [this](const QVariant&) {
                       QMetaObject::invokeMethod(
                           this,
                           [this]() { handleFinishRequested(); },
                           Qt::QueuedConnection
                           );
                   });

    // ── ESC：取消命令 ────────────────────────────────────────────────
    bus->subscribe(Events::POINT_CANCELLED, this,
                   [this](const QVariant&) {
                       QMetaObject::invokeMethod(
                           this,
                           [this]() { handleCancelled(); },
                           Qt::QueuedConnection
                           );
                   });
}

void PointInputCommand::unsubscribePointEvents()
{
    // 一次退訂此 subscriber 的所有事件，
    // 防止命令結束後仍收到殘留的 POINT_ACQUIRED / COMMAND_FINISH / POINT_CANCELLED
    Application::instance()->eventBus()->unsubscribeAll(this);
}

// -----------------------------------------------------------------------
// 內部事件處理
// -----------------------------------------------------------------------

void PointInputCommand::handlePointAcquired(QVector2D point)
{
    if (m_isFinishing) return;  // 防止重入

    m_points.push_back(point);

    // 通知子類：新點已加入（可用於即時預覽、橡皮筋更新等）
    onPointAdded(m_points);

    // 定點數模式：達到要求後自動結束
    if (m_requiredPoints > 0 && m_points.size() >= m_requiredPoints) {
        m_isFinishing = true;
        onFinished(m_points);
        Q_EMIT finished(CommandResult::Success("Done"));
        return;
    }

    // 尚未結束：輸出下一個點的提示（m_points.size() = 下一個點的 0-based 索引）
    outputMessage(promptForNextPoint(m_points.size()));
}

void PointInputCommand::handleFinishRequested()
{
    // RMB 或 Space：使用者主動完成連續輸入
    doFinish(FinishReason::UserFinished);
}

void PointInputCommand::handleCancelled()
{
    // ESC：使用者取消命令
    doFinish(FinishReason::UserCancelled);
}

void PointInputCommand::doFinish(FinishReason reason)
{
    if (m_isFinishing) return;  // 防止 ESC 連發等重入情形
    m_isFinishing = true;

    // 讓子類處理收尾邏輯（例如輸出統計、視需求撤銷幾何）
    onCommandFinishing(reason);

    // 通知 CommandManager 命令已完成
    const QString msg = (reason == FinishReason::UserFinished)
                            ? "Line command finished"
                            : "Line command cancelled";
    Q_EMIT finished(CommandResult::Success(msg));
}

} // namespace command
} // namespace aicad
