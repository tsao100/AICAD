/**
 * @file SetOriginCommand.cpp
 * @brief SETORIGIN (alias: SO) — 設定 TM2 二度分帶座標原點
 *
 * 實作說明：
 *   - 步驟 1、2：透過 NUMBER_INPUT 分別接收 TM2 東向值與北向值
 *   - 步驟 3：透過 POINT_ACQUIRED 接收使用者在視圖上點選的模型點
 *     （此時 CadView 的 coordOffset 尚未套用，故為「原始模型座標」）
 *   - 偏移量 = TM2 參考座標 − 模型原始座標
 *   - 呼叫 CadView::setCoordinateOffset() 後，後續所有 Alignment 輸入
 *     的 screenToPlaneD() 結果將自動加上偏移，對齊 TM2 絕對座標系
 *
 * EventBus 訂閱規則（與其他 Alignment 命令相同）：
 *   - 訂閱一次，整個命令生命週期有效
 *   - unsubscribeAll 只在 cleanup() 呼叫
 *   - handler 透過 QMetaObject::invokeMethod queued dispatch
 */

#include "command/alignment/SetOriginCommand.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "view/CadView.h"
#include <QDebug>
#include <QRegularExpression>

using namespace aicad::core;

namespace aicad {
namespace command {

SetOriginCommand::SetOriginCommand(QObject* parent)
    : Command("setorigin", "Set TM2 Coordinate Origin", parent)
{}

// ────────────────────────────────────────────────────────────────────────────
//  execute
// ────────────────────────────────────────────────────────────────────────────

CommandResult SetOriginCommand::execute(const CommandContext& context)
{
    m_step        = Step::WaitingForEasting;
    m_isFinishing = false;
    m_easting     = 0.0;
    m_northing    = 0.0;

    EventBus* bus = Application::instance()->eventBus();

    // 要求 CadView 切換到 Navigation 模式（不開 Sketching，不顯示 rubber band）
    QVariantMap viewSetup;
    viewSetup["mode"] = "navigation";
    bus->publish("command.request-view-setup", viewSetup);

    // ── 訂閱數值輸入（東向、北向） ──────────────────────────────────────────
    bus->subscribe(Events::NUMBER_INPUT, this,
        [this](const QVariant& data) {
            QString text = data.toString();
            QMetaObject::invokeMethod(this, [this, text]() {
                handleNumberInput(text);
            }, Qt::QueuedConnection);
        });

    // ── 訂閱點輸入（模型對應點） ─────────────────────────────────────────────
    //    此時 CadView 仍在 Navigation 模式，
    //    handlePointInput() 使用 screenToPlaneD()（無偏移），
    //    發佈的 QPointF 即為模型原始座標
    bus->subscribe(Events::POINT_ACQUIRED, this,
        [this](const QVariant& data) {
            QVariantMap map = data.toMap();
            // Navigation 模式下 CadView 發佈 QPointF（目前 offset 尚為 0）
            QPointF pt = map["point"].value<QPointF>();
            QMetaObject::invokeMethod(this, [this, pt]() {
                handlePointAcquired(pt);
            }, Qt::QueuedConnection);
        });

    // ── 取消（ESC / 右鍵） ───────────────────────────────────────────────────
    bus->subscribe(Events::POINT_CANCELLED, this,
        [this](const QVariant&) {
            QMetaObject::invokeMethod(this, [this]() {
                handleCancelled();
            }, Qt::QueuedConnection);
        });

    setState(CommandState::Running);

    bus->publish(Events::COMMAND_PROMPT,
                 tr("Set TM2 Origin — Enter Easting (E) value [m]:"));
    outputMessage("Set TM2 Origin — Enter Easting (E) value [m]:");
    return CommandResult::Success("Waiting for easting input");
}

// ────────────────────────────────────────────────────────────────────────────
//  handleNumberInput
// ────────────────────────────────────────────────────────────────────────────

void SetOriginCommand::handleNumberInput(const QString& text)
{
    if (m_isFinishing) return;

    bool ok = false;
    EventBus* bus = Application::instance()->eventBus();

    // 支援一次輸入兩個數字（以逗號或空格分隔），例如 "2650000, 200000"
    if (m_step == Step::WaitingForEasting) {
        QString trimmed = text.trimmed();
        // 嘗試解析逗號或空格分隔的兩個值
        QStringList parts = trimmed.split(QRegularExpression(QString("[,\\s]+")), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            double e = parts[0].toDouble(&ok);
            if (!ok) {
                outputMessage(QString("Invalid easting value: \"%1\"").arg(parts[0]));
                bus->publish(Events::COMMAND_PROMPT,
                             tr("Enter Easting (E) value [m]:"));
                return;
            }
            double n = parts[1].toDouble(&ok);
            if (!ok) {
                outputMessage(QString("Invalid northing value: \"%1\"").arg(parts[1]));
                bus->publish(Events::COMMAND_PROMPT,
                             tr("Enter Easting (E) value [m]:"));
                return;
            }
            m_easting  = e;
            m_northing = n;
            m_step = Step::WaitingForModelPoint;

            outputMessage(QString("TM2 reference: E=%1, N=%2 — Click the corresponding model point:")
                              .arg(m_easting,  0, 'f', 3)
                              .arg(m_northing, 0, 'f', 3));
            bus->publish(Events::COMMAND_PROMPT,
                         tr("Click the model point that corresponds to E=%1, N=%2:")
                             .arg(m_easting, 0, 'f', 3)
                             .arg(m_northing, 0, 'f', 3));
            return;
        }

        // 單一值：東向
        double e = trimmed.toDouble(&ok);
        if (!ok) {
            outputMessage(QString("Invalid value: \"%1\" — Enter a number.").arg(text));
            bus->publish(Events::COMMAND_PROMPT,
                         tr("Enter Easting (E) value [m]:"));
            return;
        }
        m_easting = e;
        m_step = Step::WaitingForNorthing;

        outputMessage(QString("Easting = %1 m — Enter Northing (N) value [m]:")
                          .arg(m_easting, 0, 'f', 3));
        bus->publish(Events::COMMAND_PROMPT,
                     tr("Enter Northing (N) value [m]:"));
        return;
    }

    if (m_step == Step::WaitingForNorthing) {
        double n = text.trimmed().toDouble(&ok);
        if (!ok) {
            outputMessage(QString("Invalid value: \"%1\" — Enter a number.").arg(text));
            bus->publish(Events::COMMAND_PROMPT,
                         tr("Enter Northing (N) value [m]:"));
            return;
        }
        m_northing = n;
        m_step = Step::WaitingForModelPoint;

        outputMessage(QString("TM2 reference: E=%1, N=%2 — Click the corresponding model point:")
                          .arg(m_easting,  0, 'f', 3)
                          .arg(m_northing, 0, 'f', 3));
        bus->publish(Events::COMMAND_PROMPT,
                     tr("Click the model point that corresponds to E=%1, N=%2:")
                         .arg(m_easting, 0, 'f', 3)
                         .arg(m_northing, 0, 'f', 3));
        return;
    }

    // WaitingForModelPoint 時收到數字 → 忽略，提示使用者點選
    outputMessage("Please click the model point in the view (not a number input).");
}

// ────────────────────────────────────────────────────────────────────────────
//  handlePointAcquired
// ────────────────────────────────────────────────────────────────────────────

void SetOriginCommand::handlePointAcquired(const QPointF& point)
{
    if (m_isFinishing) return;

    if (m_step != Step::WaitingForModelPoint) {
        // 尚未完成東向/北向輸入，點擊無效
        outputMessage("Please enter Easting and Northing values first.");
        return;
    }

    // ── 計算偏移量 ────────────────────────────────────────────────────────────
    //    offset = TM2 參考座標 − 使用者點選的模型原始座標
    const double offsetE = m_easting  - point.x();
    const double offsetN = m_northing - point.y();

    // ── 套用到 CadView ────────────────────────────────────────────────────────
    EventBus* bus = Application::instance()->eventBus();
    QVariantMap setOffsetData;
    setOffsetData["easting"]  = offsetE;
    setOffsetData["northing"] = offsetN;
    bus->publish("command.set-coordinate-offset", setOffsetData);

    outputMessage(
        QString("TM2 origin set:  model(%1, %2) → TM2(%3, %4)\n"
                "  Offset applied: ΔE=%5, ΔN=%6")
            .arg(point.x(),  0, 'f', 3)
            .arg(point.y(),  0, 'f', 3)
            .arg(m_easting,  0, 'f', 3)
            .arg(m_northing, 0, 'f', 3)
            .arg(offsetE,    0, 'f', 3)
            .arg(offsetN,    0, 'f', 3));

    qDebug() << "[SetOrigin] Coordinate offset set:"
             << "E=" << offsetE << "N=" << offsetN;

    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("TM2 coordinate origin set successfully"));
}

// ────────────────────────────────────────────────────────────────────────────
//  handleCancelled
// ────────────────────────────────────────────────────────────────────────────

void SetOriginCommand::handleCancelled()
{
    qDebug() << "[SetOrigin] Cancelled";
    m_isFinishing = true;
    Q_EMIT finished(CommandResult::Success("SetOrigin cancelled"));
}

// ────────────────────────────────────────────────────────────────────────────
//  cleanup
// ────────────────────────────────────────────────────────────────────────────

void SetOriginCommand::cleanup()
{
    EventBus* bus = Application::instance()->eventBus();
    bus->unsubscribeAll(this);

    QVariantMap rb;
    rb["clearRubberBand"] = true;
    bus->publish("command.request-cleanup", rb);

    m_step        = Step::WaitingForEasting;
    m_isFinishing = false;
    m_easting     = 0.0;
    m_northing    = 0.0;
}

// ────────────────────────────────────────────────────────────────────────────
//  getUsage
// ────────────────────────────────────────────────────────────────────────────

QString SetOriginCommand::getUsage() const
{
    return "Usage: SO  (SetOrigin)\n"
           "  Step 1: Enter TM2 Easting (E) value in metres\n"
           "  Step 2: Enter TM2 Northing (N) value in metres\n"
           "         (or enter both as \"E, N\" in step 1)\n"
           "  Step 3: Click the corresponding point in the model view\n"
           "  Result: All subsequent Alignment coordinates will be\n"
           "          offset to align with TM2 absolute coordinates.\n"
           "\n"
           "  Example:\n"
           "    SO\n"
           "    > 2650000          (or: 2650000, 200000)\n"
           "    > 200000\n"
           "    > [click origin point in view]";
}

} // namespace command
} // namespace aicad
