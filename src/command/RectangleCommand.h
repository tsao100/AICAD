/**
 * @file RectangleCommand.h
 * @brief 矩形繪製命令
 * @author Kaufen
 * @date 2024-12-04
 */

#ifndef AICAD_COMMANDS_RECTANGLECOMMAND_H
#define AICAD_COMMANDS_RECTANGLECOMMAND_H

#include "core/Command.h"
#include <QVector2D>

namespace aicad {
namespace commands {

/**
 * @brief 矩形繪製命令
 * 
 * 用法:
 * - rectangle           (互動模式，點擊兩個角點)
 * - rectangle x1 y1 x2 y2  (命令列模式，提供座標)
 * 
 * 範例:
 * @code
 * rectangle 0 0 100 50
 * @endcode
 */
class RectangleCommand : public core::Command {
    Q_OBJECT
    
public:
    /**
     * @brief 建構子
     */
    explicit RectangleCommand(QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~RectangleCommand() override;
    
    /**
     * @brief 執行命令
     */
    core::CommandResult execute(const core::CommandContext& context) override;
    
    /**
     * @brief 驗證參數
     */
    bool validateParameters(const core::CommandContext& context) const override;
    
    /**
     * @brief 取得使用說明
     */
    QString getUsage() const override;
    
private:
    /**
     * @brief 互動模式：透過點擊取得點
     */
    core::CommandResult executeInteractive();
    
    /**
     * @brief 命令列模式：從參數建立矩形
     */
    core::CommandResult executeWithCoordinates(const QVector<double>& coords);
    
    /**
     * @brief 建立矩形幾何
     */
    bool createRectangle(const QVector2D& corner1, const QVector2D& corner2);
};

} // namespace commands
} // namespace aicad

#endif // AICAD_COMMANDS_RECTANGLECOMMAND_H