#include "command/PolygonCommand.h"
#include "command/Command.h"
#include "command/CommandTypes.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "core/CommandLineManager.h"
#include <QtMath>
#include <QSettings>

using namespace aicad::core;

namespace aicad {
namespace command {

namespace {
// QSettings 落地位置／機碼，比照專案內其他命令/對話框的既有慣例
// （AddSpiralCalcDialog、ExportAlignmentCommand…皆使用 ("AICAD","AICAD")），
// 概念上類似 VBA 的 GetSetting/SaveSetting：跨次執行、跨程式重啟都能記住
// 上一次使用的邊數。
const char* kSettingsOrg   = "AICAD";
const char* kSettingsApp   = "AICAD";
const char* kSettingsGroup = "PolygonCommand";
const char* kKeySides      = "sides";
const int   kDefaultSides  = 6;
}

PolygonCommand::PolygonCommand(QObject* parent)
    : Command("polygon", "Draw Regular Polygon", parent)
    , m_hasCenterPoint(false)
    , m_sides(kDefaultSides)
    , m_isFinishing(false)
    , m_waitingForSidesInput(false)
{
}

PolygonCommand::~PolygonCommand() {
}

CommandResult PolygonCommand::execute(const CommandContext& context) {
    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // Non-interactive mode (with coordinates)
    // Format: polygon centerX centerY radius [sides]
    if (context.args.size() >= 3) {
        bool ok1, ok2, ok3, ok4 = true;
        double centerX = context.args[0].toDouble(&ok1);
        double centerY = context.args[1].toDouble(&ok2);
        double radius = context.args[2].toDouble(&ok3);
        int sides = loadLastSides();

        if (context.args.size() >= 4) {
            sides = context.args[3].toInt(&ok4);
        }

        if (ok1 && ok2 && ok3 && ok4 && sides >= 3 && radius > 0) {
            QVector2D center(centerX, centerY);
            // 非互動模式沒有「點下的半徑點」可以決定方向，維持原本固定在
            // 正上方（12 點鐘方向）的第一個頂點。
            createPolygon(center, radius, sides, -M_PI / 2.0);
            saveLastSides(sides);
            return CommandResult::Success(QString("Polygon with %1 sides created").arg(sides));
        } else {
            return CommandResult::Failure("Invalid parameters. Sides must be >= 3, radius > 0");
        }
    }

    // Interactive mode
    m_hasCenterPoint = false;
    // ★ 帶入上一次執行 POLYGON 後設定的邊數（QSettings 持久化，類似
    //   VBA 的 GetSetting），而非每次都重置回預設六邊形。
    m_sides = loadLastSides();
    m_isFinishing = false;
    m_waitingForSidesInput = false;

    // ✅ Request view setup via EventBus
    QVariantMap viewSetup;
    viewSetup["mode"] = "sketching";
    viewSetup["rubberBandMode"] = "polygon";
    bus->publish("command.request-view-setup", viewSetup);

    // 讓即時預覽從一開始就知道目前的邊數（不然預覽會先用 RubberBand 內建
    // 的預設值，直到使用者中途按 S 才會更新，會顯示錯誤的邊數）。
    QVariantMap sidesParams;
    sidesParams["action"] = "setParams";
    sidesParams["polygonSides"] = m_sides;
    bus->publish("command.update-rubber-band", sidesParams);

    // ✅ Subscribe with Qt::QueuedConnection for safety
    bus->subscribe(Events::POINT_ACQUIRED, this,
                   [this](const QVariant& data) {
                       QVariantMap map = data.toMap();
                       QPointF _ptF = map["point"].value<QPointF>();
                       QVector2D point(static_cast<float>(_ptF.x()), static_cast<float>(_ptF.y()));

                       // ✅ Use QMetaObject::invokeMethod for thread safety
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

    // ── S（Sides）選項：讓使用者在指定中心點之前先輸入邊數 ─────────────────
    // 訂閱一次即可：後續每次 promptForCenter() 重新掛號 waitForInput(Option)
    // 都會沿用同一份訂閱，比照 ChamferCommand::begin2D()/beginFirstObjectStage()
    // 的既有作法。
    subscribeOptionSelected();

    setState(CommandState::Running);

    promptForCenter();

    return CommandResult::Success("Waiting for input");
}

// ✅ Handle point acquisition
void PolygonCommand::handlePointAcquired(QVector2D point)
{
    if (m_isFinishing) return;

    qDebug() << "[PolygonCommand] Point acquired:"
             << point.x() << "," << point.y();

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    if (!m_hasCenterPoint) {
        // === First point - center ===
        m_centerPoint = point;
        m_hasCenterPoint = true;

        // ✅ Update rubber band with center point
        QVariantMap rubberUpdate;
        rubberUpdate["action"] = "clearAndAdd";
        rubberUpdate["point"] = QVariant::fromValue(QPointF(point.x(), point.y()));
        rubberUpdate["polygonCenter"] = QVariant::fromValue(point);
        rubberUpdate["polygonSides"] = m_sides;
        bus->publish("command.update-rubber-band", rubberUpdate);

        outputMessage(QString("Center point: (%1, %2). Specify radius point:")
                          .arg(point.x()).arg(point.y()));

        // 改用 CommandLineManager::showPrompt()（而非直接 bus->publish
        // COMMAND_PROMPT）：一併清掉「指定中心點」階段解析出的 [Sides(S)]
        // 選項按鈕/比對表，避免使用者在「指定半徑點」階段誤打 S 又比對到
        // 舊選項（onOptionSelected() 內雖然也會因 m_hasCenterPoint==true
        // 而忽略，但提示列上殘留的選項按鈕會造成混淆）。
        auto* cmdMgr = core::CommandLineManager::instance();
        if (cmdMgr) {
            cmdMgr->showPrompt("Specify radius point or press ESC to cancel:");
            cmdMgr->waitForInput(core::InputType::String);
        }
        return;
    }

    // === Second point - determines radius AND first-vertex direction ===
    QVector2D delta = point - m_centerPoint;
    double radius = delta.length();

    if (radius < 0.001) {
        outputMessage("Radius too small, please specify a point further from center");
        return;
    }

    // 第一個頂點的角度依照使用者點下的半徑點方向決定（而非固定在正上方），
    // 讓多邊形的第一個頂點跟著滑鼠移動/點下的位置走，與即時預覽
    // （RubberBand::updatePolygon()，同樣改用 atan2 計算）保持一致。
    double startAngle = qAtan2(delta.y(), delta.x());

    // ✅ Create the polygon
    createPolygon(m_centerPoint, radius, m_sides, startAngle);

    outputMessage(QString("Polygon with %1 sides created (center: %2,%3, radius: %4)")
                      .arg(m_sides)
                      .arg(m_centerPoint.x()).arg(m_centerPoint.y())
                      .arg(radius, 0, 'f', 2));

    // Finish command
    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("Polygon command completed"));
}

// ✅ Handle cancellation
void PolygonCommand::handleCancelled() {
    qDebug() << "[PolygonCommand] Cancelled via EventBus";

    m_isFinishing = true;  // ✅ Set flag to prevent further point handling

    // ✅ Just emit finished with current state
    Q_EMIT finished(CommandResult::Success("Polygon command cancelled"));
}

// ✅ Create polygon geometry and publish to EventBus
void PolygonCommand::createPolygon(const QVector2D& center, double radius, int sides, double startAngleRad)
{
    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // Calculate polygon vertices
    QVector<QVector2D> vertices;
    double angleStep = 2.0 * M_PI / sides;

    for (int i = 0; i < sides; ++i) {
        double angle = startAngleRad + i * angleStep;
        double x = center.x() + radius * qCos(angle);
        double y = center.y() + radius * qSin(angle);
        vertices.append(QVector2D(x, y));
    }

    // ✅ Create polygon via EventBus
    QVariantMap polygonData;
    polygonData["center"] = QVariant::fromValue(center);
    polygonData["radius"] = radius;
    polygonData["sides"] = sides;

    // Convert vertices to QVariantList for event transmission
    QVariantList verticesList;
    for (const auto& vertex : vertices) {
        verticesList.append(QVariant::fromValue(vertex));
    }
    polygonData["vertices"] = verticesList;

    bus->publish("command.create-sketch-polygon", polygonData);
}

// ─────────────────────────────────────────────────────────────────────────
// promptForCenter — 顯示「指定中心點或 [Sides(S)]」提示，並讓命令列進入
// 「等待選項/文字」狀態，使 S 選項可用（滑鼠點取中心點走的是獨立的
// POINT_ACQUIRED 通道，兩者互不影響、可以並存，比照 ChamferCommand）。
// ─────────────────────────────────────────────────────────────────────────

void PolygonCommand::promptForCenter()
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (!cmdMgr) return;

    cmdMgr->showPrompt(
        QString("[POLYGON] Specify center point or [Sides(S)] <%1>:").arg(m_sides));
    cmdMgr->waitForInput(core::InputType::Option);
}

void PolygonCommand::subscribeOptionSelected()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::OPTION_SELECTED, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onOptionSelected(v); },
                                      Qt::QueuedConnection);
        });
}

void PolygonCommand::onOptionSelected(const QVariant& payload)
{
    if (m_isFinishing) return;
    // 邊數只能在指定中心點「之前」變更（比照 AutoCAD POLYGON 的既有互動
    // 順序），指定中心點之後即進入取半徑點階段，不再提供這個選項。
    if (m_hasCenterPoint) return;

    const QString opt = payload.toString().trimmed();
    if (opt.compare(QStringLiteral("S"),     Qt::CaseInsensitive) != 0 &&
        opt.compare(QStringLiteral("Sides"), Qt::CaseInsensitive) != 0) {
        return;
    }

    subscribeSidesNumberInput();
    m_waitingForSidesInput = true;

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt(QString("[POLYGON] Enter number of sides <%1>:").arg(m_sides));
        cmdMgr->waitForInput(core::InputType::Number);
    }
}

void PolygonCommand::subscribeSidesNumberInput()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;
    bus->subscribe(core::Events::NUMBER_INPUT, this,
        [this](const QVariant& v) {
            QMetaObject::invokeMethod(this, [this, v] { onSidesNumberInput(v); },
                                      Qt::QueuedConnection);
        });
}

void PolygonCommand::onSidesNumberInput(const QVariant& payload)
{
    if (!m_waitingForSidesInput) return;

    // 一次性訂閱：處理完這次輸入就取消，避免之後（例如取半徑點階段）
    // 誤把其他來源的 NUMBER_INPUT 當成邊數（目前 POLYGON 沒有其他階段會
    // 用到 Number 輸入，但比照專案慣例——見 FilletCommand/ChamferCommand
    // 的 dist1/dist2 階段——仍在使用後立即取消訂閱，行為明確）。
    auto* bus = core::Application::instance()->eventBus();
    if (bus) bus->unsubscribe(core::Events::NUMBER_INPUT, this);
    m_waitingForSidesInput = false;

    auto* cmdMgr = core::CommandLineManager::instance();
    const QString text = payload.toString().trimmed();

    if (!text.isEmpty()) {
        bool ok = false;
        int v = text.toInt(&ok);
        if (!ok || v < 3) {
            if (cmdMgr) cmdMgr->printError("Invalid number of sides. Must be an integer >= 3.");
            // 邊數無效：維持原邊數，回到「指定中心點」提示重新開始。
        } else {
            m_sides = v;
        }
    }
    // 空字串（直接按 Enter）＝沿用目前的 m_sides（提示列 <...> 內顯示的值）。

    // 記住這次設定，供下一次執行 POLYGON 時帶入（QSettings 持久化，
    // 類似 VBA 的 SaveSetting，重開 AICAD 仍保留）。
    saveLastSides(m_sides);

    // 讓即時預覽（RubberBand）同步採用新的邊數。
    if (bus) {
        QVariantMap params;
        params["action"] = "setParams";
        params["polygonSides"] = m_sides;
        bus->publish("command.update-rubber-band", params);
    }

    promptForCenter();
}

// ─────────────────────────────────────────────────────────────────────────
// QSettings 持久化（GetSetting / SaveSetting 風格）
// ─────────────────────────────────────────────────────────────────────────

int PolygonCommand::loadLastSides()
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.beginGroup(kSettingsGroup);
    int sides = settings.value(kKeySides, kDefaultSides).toInt();
    settings.endGroup();
    if (sides < 3) sides = kDefaultSides;
    return sides;
}

void PolygonCommand::saveLastSides(int sides)
{
    if (sides < 3) return;
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.beginGroup(kSettingsGroup);
    settings.setValue(kKeySides, sides);
    settings.endGroup();
}

void PolygonCommand::cleanup() {
    qDebug() << "[PolygonCommand] Cleanup started";

    Application* app = Application::instance();
    EventBus* bus = app->eventBus();

    // ✅ Unsubscribe FIRST, before changing view mode
    bus->unsubscribeAll(this);
    qDebug() << "[PolygonCommand] Unsubscribed from EventBus";

    // ✅ Request cleanup via EventBus
    QVariantMap cleanupRequest;
    cleanupRequest["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", cleanupRequest);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->clearPrompt();

    m_hasCenterPoint = false;
    m_isFinishing = false;
    m_waitingForSidesInput = false;
    qDebug() << "[PolygonCommand] Cleanup completed";
}

QString PolygonCommand::getUsage() const {
    return "Usage: polygon [centerX centerY radius sides]\n"
           "       Interactive: Specify center point (or type S to set the number of\n"
           "       sides first), then radius point — the first vertex follows the\n"
           "       radius point/mouse direction.\n"
           "       Non-interactive: polygon 0 0 100 6 (creates hexagon)\n"
           "       sides must be >= 3 (default/last used value is remembered)";
}

} // namespace command
} // namespace aicad
