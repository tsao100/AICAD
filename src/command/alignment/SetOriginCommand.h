#pragma once

/**
 * @file SetOriginCommand.h
 * @brief SETORIGIN (alias: SO) — 設定 TM2 二度分帶座標原點
 *
 * 使用者以命令列輸入一個 TM2 參考點（東向 E、北向 N），
 * 並在視圖上點選對應的模型點（用 screenToPlaneD() 取得模型座標），
 * 命令計算差值並呼叫 CadView::setCoordinateOffset()，
 * 使後續所有 Alignment 輸入座標自動套用 TM2 偏移。
 *
 * 互動流程：
 *   1. 使用者輸入 TM2 東向值（NUMBER_INPUT）
 *   2. 使用者輸入 TM2 北向值（NUMBER_INPUT）
 *   3. 使用者點選對應模型點（POINT_ACQUIRED，此時尚未套用偏移，
 *      故 CadView 發佈的是「原始模型座標」QPointF）
 *   4. 命令計算 offset = TM2 − 模型點，呼叫 setCoordinateOffset()
 *
 * 若使用者直接輸入兩個數字（以逗號或空白分隔），步驟 1 + 2 合併。
 *
 * @note 命令別名在 CommandAlias.cpp 中登記為 "SO"。
 */

#include "command/Command.h"
#include "command/CommandFactory.h"
#include "command/CommandTypes.h"
#include <QPointF>

namespace aicad {
namespace command {

class SetOriginCommand : public Command
{
    Q_OBJECT

public:
    explicit SetOriginCommand(QObject* parent = nullptr);
    ~SetOriginCommand() override = default;

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    enum class Step {
        WaitingForEasting,
        WaitingForNorthing,
        WaitingForModelPoint
    };

    void handleNumberInput(const QString& text);
    void handlePointAcquired(const QPointF& point);
    void handleCancelled();
    void cleanup() override;

    Step    m_step     = Step::WaitingForEasting;
    double  m_easting  = 0.0;
    double  m_northing = 0.0;
    bool    m_isFinishing = false;
};

REGISTER_COMMAND("setorigin", SetOriginCommand);

} // namespace command
} // namespace aicad
