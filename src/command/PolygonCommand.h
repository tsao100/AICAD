#ifndef AICAD_COMMAND_POLYGONCOMMAND_H
#define AICAD_COMMAND_POLYGONCOMMAND_H

#include "Command.h"
#include "command/CommandFactory.h"
#include <QVector2D>

namespace aicad {
namespace command {

class PolygonCommand : public Command {
    Q_OBJECT  // ✅ MOC will process this header

public:
    explicit PolygonCommand(QObject* parent = nullptr);
    ~PolygonCommand() override;

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    // ✅ Event handlers (not slots - using EventBus)
    void handlePointAcquired(QVector2D point);
    void handleCancelled();
    void cleanup() override;

    // Helper method to create polygon geometry
    // @param startAngleRad 第一個頂點相對於中心的角度（弧度）；由使用者點下
    //        的半徑點方向決定，而非固定在正上方，讓多邊形的第一個頂點跟著
    //        滑鼠/點下的位置走。
    void createPolygon(const QVector2D& center, double radius, int sides, double startAngleRad);

    // ── S（Sides）選項：允許使用者在指定中心點之前輸入邊數 ────────────────
    void promptForCenter();               // 顯示「指定中心點或 [Sides(S)]」提示
    void subscribeOptionSelected();
    void onOptionSelected(const QVariant& payload);
    void subscribeSidesNumberInput();
    void onSidesNumberInput(const QVariant& payload);

    // ── QSettings 持久化（類似 VBA GetSetting/SaveSetting）─────────────────
    static int loadLastSides();
    static void saveLastSides(int sides);

    QVector2D m_centerPoint;
    bool m_hasCenterPoint;
    int m_sides;
    bool m_isFinishing;
    bool m_waitingForSidesInput;   ///< 目前是否正在等待使用者輸入新邊數
};

REGISTER_COMMAND("polygon", PolygonCommand);

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_POLYGONCOMMAND_H
