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
    // 其餘成員變數已在宣告處初始化（m_requiredPoints = -1, m_isFinishing = false）
}

PointInputCommand::~PointInputCommand()
{
    // 析構時確保退訂，避免 EventBus 持有懸空指標
    unsubscribePointEvents();
}

// -----------------------------------------------------------------------
// 事件訂閱管理
// -----------------------------------------------------------------------

void PointInputCommand::subscribePointEvents()
{
    Application* app = Application::instance();
    EventBus*    bus = app->eventBus();

    // 訂閱「點已取得」事件
    // 使用 lambda 包裝，並透過 QMetaObject::invokeMethod + QueuedConnection
    // 確保 handlePointAcquired 在 Qt 主執行緒（GUI thread）中執行，
    // 避免跨執行緒存取 UI / OpenGL 資源造成競態條件。
    bus->subscribe(Events::POINT_ACQUIRED, this,
                   [this](const QVariant& data) {
                       // 從事件資料中取出 QVector2D 座標
                       QVector2D pt = data.toMap()["point"].value<QVector2D>();

                       // 排入主執行緒佇列執行
                       QMetaObject::invokeMethod(
                           this,
                           [this, pt]() { handlePointAcquired(pt); },
                           Qt::QueuedConnection
                           );
                   });

    // 訂閱「點輸入取消」事件（使用者按下 ESC 或右鍵取消）
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
    // unsubscribeAll 會移除此 subscriber 對所有事件的訂閱，
    // 防止命令結束後仍收到殘留事件
    Application::instance()->eventBus()->unsubscribeAll(this);
}

// -----------------------------------------------------------------------
// 內部事件處理
// -----------------------------------------------------------------------

void PointInputCommand::handlePointAcquired(QVector2D point)
{
    // 若命令已進入結束流程，丟棄後續事件（防止重入）
    if (m_isFinishing)
        return;

    // 將新點加入序列
    m_points.push_back(point);

    // 通知子類有新點加入（可用於即時預覽、更新橡皮筋等）
    onPointAdded(m_points);

    // 判斷是否已達所需點數（-1 表示無限制，永遠不滿足此條件）
    if (m_requiredPoints > 0 &&
        m_points.size() >= m_requiredPoints)
    {
        m_isFinishing = true;

        // 通知子類執行最終建立幾何的邏輯
        onFinished(m_points);

        // 發出 finished() 信號，通知 CommandManager 命令已完成
        Q_EMIT finished(CommandResult::Success("Done"));
        return;
    }

    // 尚未達到所需點數，輸出下一個點的提示
    // m_points.size() 此時即為「下一個點」的 0-based 索引
    outputMessage(promptForNextPoint(m_points.size()));
}

void PointInputCommand::handleCancelled()
{
    // 防止重入（例如 ESC 事件連發）
    if (m_isFinishing)
        return;

    m_isFinishing = true;

    // 使用者主動取消，以成功結果發出 finished() 信號
    // （命令本身執行正常，只是使用者選擇結束輸入）
    Q_EMIT finished(CommandResult::Success("Command cancelled"));
}

} // namespace command
} // namespace aicad
