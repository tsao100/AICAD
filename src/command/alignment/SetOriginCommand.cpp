/**
 * @file SetOriginCommand.cpp
 * @brief SETORIGIN (alias: SO) — 設定 TM2 二度分帶座標原點
 *
 * 設計原則（模仿 AlignmentSCSCommand）：
 *   - 只訂閱 NUMBER_INPUT 和 POINT_ACQUIRED
 *   - 每個步驟結束後呼叫 waitForInput(Number)，讓 CommandLineManager
 *     把下一次輸入路由到 NUMBER_INPUT（不論是數字還是 "E,N" 字串）
 *   - POINT_ACQUIRED 供滑鼠點選；WaitingForModelPoint 時兩者都接受
 *
 * 互動流程：
 *   execute()
 *     → waitForInput(Number)
 *   NUMBER_INPUT — 東向 or "E,N" 合併
 *     → 若兩個值：直接進 WaitingForModelPoint
 *     → 若一個值：進 WaitingForNorthing → waitForInput(Number)
 *   NUMBER_INPUT — 北向
 *     → 進 WaitingForModelPoint → waitForInput(Number)
 *   WaitingForModelPoint：
 *     NUMBER_INPUT — 解析 "X,Y" → apply offset
 *     POINT_ACQUIRED — 滑鼠點選 → apply offset
 */

#include "command/alignment/SetOriginCommand.h"
#include "core/Application.h"
#include "core/CommandLineManager.h"
#include "core/EventBus.h"
#include "core/geometry/ProjectOrigin.h"
#include <QDebug>
#include <QRegularExpression>

using namespace aicad::core;

namespace aicad {
namespace command {

SetOriginCommand::SetOriginCommand(QObject* parent)
    : Command("setorigin", "Set TM2 Coordinate Origin", parent)
{}

// ─────────────────────────────────────────────────────────────────────────────
//  execute
// ─────────────────────────────────────────────────────────────────────────────
CommandResult SetOriginCommand::execute(const CommandContext& /*context*/)
{
    m_step        = Step::WaitingForEasting;
    m_isFinishing = false;
    m_easting     = 0.0;
    m_northing    = 0.0;

    EventBus* bus = Application::instance()->eventBus();

    // ── 只訂閱 NUMBER_INPUT（模仿 AlignmentSCSCommand）──────────────────────
    // CommandLineManager 在 waitForInput(Number) 後將所有文字輸入路由到此事件，
    // 不論是純數字還是 "E,N" 格式字串。
    bus->subscribe(Events::NUMBER_INPUT, this,
        [this](const QVariant& data) {
            const QString text = data.toString();
            QMetaObject::invokeMethod(this, [this, text]() {
                handleNumberInput(text);
            }, Qt::QueuedConnection);
        });

    // ── POINT_ACQUIRED：滑鼠點選（Navigation 模式，CadView 發佈 QPointF）────
    bus->subscribe(Events::POINT_ACQUIRED, this,
        [this](const QVariant& data) {
            if (m_step != Step::WaitingForModelPoint) return;
            const QPointF pt = data.toMap()["point"].value<QPointF>();
            QMetaObject::invokeMethod(this, [this, pt]() {
                applyOffset(pt);
            }, Qt::QueuedConnection);
        });

    // ── 取消 ────────────────────────────────────────────────────────────────
    bus->subscribe(Events::POINT_CANCELLED, this,
        [this](const QVariant&) {
            QMetaObject::invokeMethod(this, [this]() {
                m_isFinishing = true;
                Q_EMIT finished(CommandResult::Success("SetOrigin cancelled"));
            }, Qt::QueuedConnection);
        });

    setState(CommandState::Running);

    outputMessage(
        "Set TM2 Origin\n"
        "  Enter Easting [m], or \"E,N\" for both (e.g. 248170.787,2652129.936):");
    bus->publish(Events::COMMAND_PROMPT,
                 tr("Enter Easting [m] (or E,N):"));

    // ── 告知 CommandLineManager 期待數值輸入 ─────────────────────────────────
    CommandLineManager::instance()->waitForInput(aicad::core::InputType::Number);

    return CommandResult::Success("Waiting for easting input");
}

// ─────────────────────────────────────────────────────────────────────────────
//  handleNumberInput  —  所有文字輸入統一在此解析
// ─────────────────────────────────────────────────────────────────────────────
void SetOriginCommand::handleNumberInput(const QString& text)
{
    if (m_isFinishing) return;

    EventBus* bus = Application::instance()->eventBus();
    const QString t = text.trimmed();

    // ── WaitingForEasting ────────────────────────────────────────────────────
    if (m_step == Step::WaitingForEasting) {
        // 嘗試解析 "E,N" 或 "E N"（逗號或空白）
        const QStringList parts =
            t.split(QRegularExpression(QString(R"([,\s]+)")), Qt::SkipEmptyParts);

        if (parts.size() >= 2) {
            // 兩個值一次輸入
            bool okE = false, okN = false;
            const double e = parts[0].toDouble(&okE);
            const double n = parts[1].toDouble(&okN);
            if (!okE || !okN) {
                outputMessage(
                    QString("Cannot parse \"%1\" — enter E,N as numbers.").arg(t));
                bus->publish(Events::COMMAND_PROMPT, tr("Enter Easting [m] (or E,N):"));
                CommandLineManager::instance()->waitForInput(aicad::core::InputType::Number);
                return;
            }
            m_easting  = e;
            m_northing = n;
            m_step = Step::WaitingForModelPoint;
            promptModelPoint(bus);
            return;
        }

        // 單一值：僅東向
        bool ok = false;
        const double e = t.toDouble(&ok);
        if (!ok) {
            outputMessage(
                QString("Cannot parse \"%1\" — enter a number.").arg(t));
            bus->publish(Events::COMMAND_PROMPT, tr("Enter Easting [m] (or E,N):"));
            CommandLineManager::instance()->waitForInput(aicad::core::InputType::Number);
            return;
        }
        m_easting = e;
        m_step = Step::WaitingForNorthing;

        outputMessage(QString("Easting = %1 m  —  Enter Northing [m]:")
                          .arg(m_easting, 0, 'f', 3));
        bus->publish(Events::COMMAND_PROMPT, tr("Enter Northing [m]:"));
        CommandLineManager::instance()->waitForInput(aicad::core::InputType::Number);
        return;
    }

    // ── WaitingForNorthing ───────────────────────────────────────────────────
    if (m_step == Step::WaitingForNorthing) {
        bool ok = false;
        const double n = t.toDouble(&ok);
        if (!ok) {
            outputMessage(
                QString("Cannot parse \"%1\" — enter a number.").arg(t));
            bus->publish(Events::COMMAND_PROMPT, tr("Enter Northing [m]:"));
            CommandLineManager::instance()->waitForInput(aicad::core::InputType::Number);
            return;
        }
        m_northing = n;
        m_step = Step::WaitingForModelPoint;
        promptModelPoint(bus);
        return;
    }

    // ── WaitingForModelPoint（文字輸入座標）──────────────────────────────────
    if (m_step == Step::WaitingForModelPoint) {
        // 接受 "X,Y" 或 "X Y" 作為模型點座標
        const QStringList parts =
            t.split(QRegularExpression(QString(R"([,\s]+)")), Qt::SkipEmptyParts);

        if (parts.size() >= 2) {
            bool okX = false, okY = false;
            const double x = parts[0].toDouble(&okX);
            const double y = parts[1].toDouble(&okY);
            if (!okX || !okY) {
                outputMessage(
                    QString("Cannot parse \"%1\" — enter X,Y as numbers.").arg(t));
                bus->publish(Events::COMMAND_PROMPT,
                             tr("Click model point (or type X,Y):"));
                CommandLineManager::instance()->waitForInput(aicad::core::InputType::Number);
                return;
            }
            applyOffset(QPointF(x, y));
            return;
        }

        // 單一值無法判斷座標，提示重輸
        outputMessage(
            QString("Need two values — enter X,Y (e.g. 0,0), or click in view."));
        bus->publish(Events::COMMAND_PROMPT,
                     tr("Click model point (or type X,Y):"));
        CommandLineManager::instance()->waitForInput(aicad::core::InputType::Number);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  promptModelPoint
// ─────────────────────────────────────────────────────────────────────────────
void SetOriginCommand::promptModelPoint(EventBus* bus)
{
    outputMessage(
        QString("E=%1  N=%2\n"
                "  Click the corresponding model point, or type \"X,Y\":")
            .arg(m_easting,  0, 'f', 3)
            .arg(m_northing, 0, 'f', 3));
    bus->publish(Events::COMMAND_PROMPT,
                 tr("Click model point (or type X,Y):"));
    // 繼續等數值（文字 "X,Y" 走 NUMBER_INPUT；滑鼠走 POINT_ACQUIRED）
    CommandLineManager::instance()->waitForInput(aicad::core::InputType::Number);
}

// ─────────────────────────────────────────────────────────────────────────────
//  applyOffset
// ─────────────────────────────────────────────────────────────────────────────
void SetOriginCommand::applyOffset(const QPointF& modelPt)
{
    if (m_isFinishing) return;

    // ── Phase 2: 直接寫入 ProjectOrigin 單例 ─────────────────────────────
    // origin = TM2 target − model point（即 CadView Local 原點在 TM2 系中的位置）
    // ProjectOrigin::setOrigin() 會自動發布 Events::PROJECT_ORIGIN_CHANGED，
    // UIManager 與 CadView 再透過訂閱同步。
    const double originE = m_easting  - modelPt.x();
    const double originN = m_northing - modelPt.y();

    using namespace aicad::core::geometry;
    ProjectOrigin::instance().setOrigin(originE, originN, 0.0);

    // Phase 0 fix: 統一發布 PROJECT_ORIGIN_CHANGED（不再發布舊字串事件）
    // UIManager 透過訂閱 PROJECT_ORIGIN_CHANGED 同步 CadView。

    outputMessage(
        QString("TM2 origin set:\n"
                "  Model point  (%1, %2)\n"
                "  TM2 target   E=%3  N=%4\n"
                "  OriginE=%5  OriginN=%6")
            .arg(modelPt.x(), 0, 'f', 3).arg(modelPt.y(), 0, 'f', 3)
            .arg(m_easting,   0, 'f', 3).arg(m_northing,  0, 'f', 3)
            .arg(originE,     0, 'f', 3).arg(originN,     0, 'f', 3));

    qDebug() << "[SetOrigin] ProjectOrigin set: E=" << originE << "N=" << originN;

    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("TM2 coordinate origin set"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  cleanup / getUsage
// ─────────────────────────────────────────────────────────────────────────────
void SetOriginCommand::cleanup()
{
    Application::instance()->eventBus()->unsubscribeAll(this);
    m_step        = Step::WaitingForEasting;
    m_isFinishing = false;
    m_easting     = 0.0;
    m_northing    = 0.0;
}

QString SetOriginCommand::getUsage() const
{
    return
        "SETORIGIN  (alias: SO)\n"
        "\n"
        "Set the TM2 coordinate origin for Alignment editing.\n"
        "\n"
        "Step 1 — Enter Easting [m]  (or both as \"E,N\"):\n"
        "   248170.787          → asks for Northing next\n"
        "   248170.787,2652129.936 → jumps to step 3\n"
        "\n"
        "Step 2 — Enter Northing [m]:\n"
        "   2652129.936\n"
        "\n"
        "Step 3 — Specify model point:\n"
        "   click in the view, OR type \"X,Y\" (e.g. 0,0)\n"
        "\n"
        "The offset = TM2 target − model point is applied to CadView.\n"
        "Subsequent Alignment clicks will report TM2 absolute coordinates.";
}

} // namespace command
} // namespace aicad
