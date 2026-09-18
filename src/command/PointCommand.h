#ifndef AICAD_COMMAND_POINTCOMMAND_H
#define AICAD_COMMAND_POINTCOMMAND_H

#include "Command.h"
#include "command/CommandFactory.h"
#include <QVector2D>

namespace aicad {
namespace command {

/**
 * @brief POINT 命令 — 在目前作用中的草圖平面上建立獨立的 SketchPoint
 *        （SketchPoint::Origin::Explicit）。
 *
 * 位置指定方式（兩種可並用，實際上共用同一個 POINT_ACQUIRED 進入點）：
 *   1. 滑鼠點選：直接在畫面上點擊要放置點的位置（走 CadView 既有的
 *      snap → Events::POINT_ACQUIRED 流程，與 LINE/CIRCLE/POLYGON 等
 *      指令一致）。
 *   2. 鍵盤輸入座標：在命令列輸入 "x,y"（或 "x y"）後按 Enter。做法是呼叫
 *      CommandLineManager::waitForInput(InputType::Point)，讓
 *      CommandLineManager 把輸入的文字發布為 Events::COORDINATE_INPUT；
 *      UIManager.cpp 裡已有現成的橋接（"COORDINATE_INPUT → POINT_ACQUIRED
 *      橋接"，同時支援 TM2 大座標與 Local 相對座標自動判斷），會把它轉成
 *      Events::POINT_ACQUIRED 事件重新發布。因此 PointCommand 本身完全
 *      不需要另外解析座標文字，只要訂閱 POINT_ACQUIRED 就能同時涵蓋滑鼠與
 *      鍵盤兩種輸入方式（與 alignment 系列指令使用鍵盤輸入座標的既有作法
 *      一致）。
 *
 * 互動流程比照多數 CAD 軟體的 POINT／DOT 指令：每成功放置一點後立即回到
 * 「指定下一點」的提示，可連續放置多個點，直到使用者按 ESC／右鍵取消為止。
 */
class PointCommand : public Command {
    Q_OBJECT  // ✅ MOC will process this header

public:
    explicit PointCommand(QObject* parent = nullptr);
    ~PointCommand() override;

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    // ✅ Event handlers (not slots - using EventBus)
    void handlePointAcquired(QVector2D point);
    void handleCancelled();
    void cleanup() override;

    /// 顯示「指定點的位置」提示，並讓命令列進入等待座標輸入的狀態
    /// （同時保留滑鼠點選的獨立 POINT_ACQUIRED 通道）。
    void promptForNextPoint();

    int  m_placedCount;   ///< 這次執行 POINT 命令已放置的點數（結束訊息用）
    bool m_isFinishing;
};

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_POINTCOMMAND_H
