#ifndef AICAD_COMMAND_ARCCOMMAND_H
#define AICAD_COMMAND_ARCCOMMAND_H

#include "Command.h"
#include "command/CommandFactory.h"
#include <QVector2D>

namespace aicad {
namespace command {

/**
 * ArcCommand - 過三點畫弧 (3-point arc)
 *
 * 操作流程:
 *   1. 指定起點 (start point)
 *   2. 指定中間點 (mid point, 弧上任意一點)
 *   3. 指定終點 (end point) → 建立弧線
 */
class ArcCommand : public Command {
    Q_OBJECT

public:
    explicit ArcCommand(QObject* parent = nullptr);
    ~ArcCommand() override;

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    void handlePointAcquired(QVector2D point);
    void handleCancelled();
    void cleanup() override;

    /**
     * 以三點求圓心與半徑
     * @param p1 起點
     * @param p2 中間點
     * @param p3 終點
     * @param center 輸出圓心
     * @param radius 輸出半徑
     * @return 是否計算成功 (三點不共線才可求圓)
     */
    static bool calcCircleFrom3Points(const QVector2D& p1,
                                      const QVector2D& p2,
                                      const QVector2D& p3,
                                      QVector2D& center,
                                      float& radius);

    // 三點狀態
    enum class PickState {
        WaitingForStart,
        WaitingForMid,
        WaitingForEnd
    };

    QVector2D m_startPoint;
    QVector2D m_midPoint;
    PickState m_pickState;
    bool      m_isFinishing;
};

REGISTER_COMMAND("arc", ArcCommand);

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_ARCCOMMAND_H
