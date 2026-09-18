/**
 * @file PointCommand.cpp
 * @brief 見 PointCommand.h 檔頭說明——建立獨立 SketchPoint 的互動命令。
 *
 * REGISTER_COMMAND 放在本 .cpp（而非標頭檔）——比照 ChamferCommand.cpp 的
 * 既有慣例：巨集展開為 file-scope 的 static 初始化，若放在標頭檔，任何
 * #include 這個標頭的編譯單元都會各自產生一份同名 static 變數與註冊呼叫，
 * 有 ODR 風險且可能造成靜默的重複/遺漏註冊。
 */

#include "command/PointCommand.h"
#include "command/CommandTypes.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "core/CommandLineManager.h"
#include "cad/Sketch.h"
#include <QtMath>

using namespace aicad::core;

namespace aicad {
namespace command {

PointCommand::PointCommand(QObject* parent)
    : Command("point", "Draw Sketch Point", parent)
    , m_placedCount(0)
    , m_isFinishing(false)
{
}

PointCommand::~PointCommand() {
}

CommandResult PointCommand::execute(const CommandContext& context) {
    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // ── Non-interactive mode（帶座標參數）：point x y [x2 y2 ...] ─────────
    // 允許一次呼叫放置一個或多個點（成對座標），方便巨集/測試使用。
    if (context.args.size() >= 2) {
        cad::Sketch* sketch = app->activeSketch();
        if (!sketch) {
            return CommandResult::Failure("No active sketch");
        }
        if (context.args.size() % 2 != 0) {
            return CommandResult::Failure(
                "Invalid parameters. Usage: point x1 y1 [x2 y2 ...]");
        }

        int created = 0;
        for (int i = 0; i + 1 < context.args.size(); i += 2) {
            bool okX = false, okY = false;
            double x = context.args[i].toDouble(&okX);
            double y = context.args[i + 1].toDouble(&okY);
            if (!okX || !okY) {
                return CommandResult::Failure(
                    QString("Invalid coordinate: %1 %2")
                        .arg(context.args[i], context.args[i + 1]));
            }
            sketch->addExplicitPoint(QVector2D(x, y));
            ++created;
        }
        bus->publish(Events::FEATURE_UPDATED, sketch->name());
        return CommandResult::Success(QString("%1 point(s) created").arg(created));
    }

    // ── Interactive mode ──────────────────────────────────────────────────
    m_placedCount = 0;
    m_isFinishing = false;

    // ✅ Request view setup via EventBus（比照 LineCommand/CircleCommand/
    //    PolygonCommand 的既有作法，切到 sketching 互動模式）
    QVariantMap viewSetup;
    viewSetup["mode"] = "sketching";
    bus->publish("command.request-view-setup", viewSetup);

    // ✅ Subscribe with Qt::QueuedConnection for safety
    // 這個通道同時涵蓋滑鼠點選（CadView 直接發布）與鍵盤輸入座標（透過
    // UIManager.cpp 既有的 COORDINATE_INPUT → POINT_ACQUIRED 橋接，見
    // PointCommand.h 檔頭說明），兩者共用同一個 handlePointAcquired()。
    bus->subscribe(Events::POINT_ACQUIRED, this,
                   [this](const QVariant& data) {
                       QVariantMap map = data.toMap();
                       QPointF _ptF = map["point"].value<QPointF>();
                       QVector2D point(static_cast<float>(_ptF.x()), static_cast<float>(_ptF.y()));

                       QMetaObject::invokeMethod(this, [this, point]() {
                           this->handlePointAcquired(point);
                       }, Qt::QueuedConnection);
                   });

    bus->subscribe(Events::POINT_CANCELLED, this,
                   [this](const QVariant&) {
                       QMetaObject::invokeMethod(this, [this]() {
                           this->handleCancelled();
                       }, Qt::QueuedConnection);
                   });

    setState(CommandState::Running);

    promptForNextPoint();

    return CommandResult::Success("Waiting for input");
}

void PointCommand::promptForNextPoint()
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (!cmdMgr) return;

    // InputType::Point：CommandLineManager 在這個狀態下會把使用者打的
    // "x,y"／"x y" 文字發布為 Events::COORDINATE_INPUT（見
    // CommandLineManager.cpp），再由 UIManager.cpp 既有的橋接轉成
    // Events::POINT_ACQUIRED；滑鼠點擊則是另一條獨立通道，兩者互不影響、
    // 可以並存（與 alignment 系列指令支援鍵盤輸入座標的既有作法一致）。
    cmdMgr->showPrompt(m_placedCount == 0
        ? QStringLiteral("[POINT] Specify point location:")
        : QStringLiteral("[POINT] Specify next point (or press ESC to finish):"));
    cmdMgr->waitForInput(core::InputType::Point);
}

// ✅ Handle point acquisition
void PointCommand::handlePointAcquired(QVector2D point)
{
    if (m_isFinishing) return;

    qDebug() << "[PointCommand] Point acquired:" << point.x() << "," << point.y();

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    cad::Sketch* sketch = app->activeSketch();
    if (!sketch) {
        outputMessage("No active sketch to add point to.");
        m_isFinishing = true;
        Q_EMIT finished(CommandResult::Failure("No active sketch"));
        return;
    }

    // 建立一個獨立、需要立即顯示在畫面上的 SketchPoint
    // （addExplicitPoint() 會自己 emit geometryChanged()/rebuildRequested()，
    // 與 TrimExtendHelper 等內部使用的低階 addPoint() 不同——見 Sketch.h/
    // Sketch.cpp 的說明。若誤用 addPoint()，點會建立了卻完全不顯示。）
    QString newPtUuid = sketch->addExplicitPoint(point);
    ++m_placedCount;

    // 🔍 診斷用 log（追查「POINT 指令畫的點存檔重開後消失」用）：確認這個
    // 點當下真的進了 sketch->m_geometries，並回報目前 sketch 裡總共有
    // 幾個 Explicit 來源的點——若這裡的數字跟存檔時 Sketch::toJson() 印出
    // 的數字對不上，問題就出在這兩個時間點之間；若兩者一致但重開後數字
    // 變少，問題就在 fromJson() 的載入端。
    {
        int explicitCount = 0;
        for (auto* p : sketch->points())
            if (p->origin == cad::SketchPoint::Origin::Explicit) ++explicitCount;
        qDebug() << "[PointCommand] Created point uuid=" << newPtUuid
                  << "in sketch=" << sketch->name()
                  << "-> sketch now has" << explicitCount
                  << "Explicit-origin point(s) total,"
                  << sketch->geometries().size() << "geometries total.";
    }

    bus->publish(Events::FEATURE_UPDATED, sketch->name());

    outputMessage(QString("Point created at (%1, %2).")
                      .arg(point.x(), 0, 'f', 3)
                      .arg(point.y(), 0, 'f', 3));

    // 比照多數 CAD 軟體的 POINT 指令：可連續放置多點，放完一點立刻回到
    // 「指定下一點」提示，直到使用者按 ESC／右鍵才結束指令。
    promptForNextPoint();
}

// ✅ Handle cancellation
void PointCommand::handleCancelled() {
    qDebug() << "[PointCommand] Cancelled via EventBus";

    m_isFinishing = true;

    QString msg = m_placedCount > 0
        ? QString("Point command finished (%1 point(s) placed).").arg(m_placedCount)
        : QString("Point command cancelled.");

    Q_EMIT finished(CommandResult::Success(msg));
}

void PointCommand::cleanup() {
    qDebug() << "[PointCommand] Cleanup started";

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // ✅ Unsubscribe FIRST
    bus->unsubscribeAll(this);

    // ✅ Request cleanup via EventBus（沒有 rubber band 需要清，但比照其他
    //    互動指令的既有慣例統一發布，讓 CadView 有機會做其他收尾）
    QVariantMap cleanupRequest;
    cleanupRequest["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", cleanupRequest);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->clearPrompt();

    m_isFinishing = false;
    qDebug() << "[PointCommand] Cleanup completed";
}

QString PointCommand::getUsage() const {
    return "Usage: point [x1 y1 [x2 y2 ...]]\n"
           "       Interactive: click to place a point, or type coordinates\n"
           "       (\"x,y\" or \"x y\") at the command line. Places points\n"
           "       continuously until ESC / right-click.\n"
           "       Non-interactive: point 0 0 (creates a point at origin)";
}

REGISTER_COMMAND("point", PointCommand);

} // namespace command
} // namespace aicad
