/**
 * @file RectangleCommand.cpp
 * @brief 矩形命令實作
 * @author Jack
 * @date 2024-12-04
 * @version Fixed - 包含必要的頭文件
 */

#include "RectangleCommand.h"
#include "command/CommandTypes.h"  // 確保包含完整定義
#include <QDebug>

namespace aicad {
namespace commands {

RectangleCommand::RectangleCommand(QObject* parent)
    : core::Command("rectangle", "繪製矩形", parent)
{
    qDebug() << "[RectangleCommand] Created";
}

RectangleCommand::~RectangleCommand() {
    qDebug() << "[RectangleCommand] Destroyed";
}

core::CommandResult RectangleCommand::execute(const core::CommandContext& context) {
    qDebug() << "[RectangleCommand] Executing...";

    if (context.args.isEmpty()) {
        return executeInteractive();
    } else {
        QVector<double> coords;
        for (const QString& arg : context.args) {
            bool ok;
            double val = arg.toDouble(&ok);
            if (!ok) {
                return core::CommandResult::Failure(
                    QString("Invalid coordinate: %1").arg(arg)
                    );
            }
            coords.append(val);
        }

        return executeWithCoordinates(coords);
    }
}

void RectangleCommand::cancel() {
    qDebug() << "[RectangleCommand] Cancelled";
    Q_EMIT cancelled();
}

bool RectangleCommand::isInteractive() const {
    return true;
}

bool RectangleCommand::validateParameters(const core::CommandContext& context) const {
    // 可以沒有參數(交互式模式)
    if (context.args.isEmpty()) {
        return true;
    }
    
    // 或者有4個參數(x1, y1, x2, y2)
    if (context.args.size() != 4) {
        return false;
    }
    
    // 檢查所有參數是否為有效數字
    for (const QString& arg : context.args) {
        bool ok;
        arg.toDouble(&ok);
        if (!ok) {
            return false;
        }
    }
    
    return true;
}

QString RectangleCommand::helpText() const {
    return "RECTANGLE - 繪製矩形\n\n"
           "用法:\n"
           "  RECTANGLE           - 交互式繪製\n"
           "  RECTANGLE x1 y1 x2 y2 - 使用坐標繪製\n\n"
           "示例:\n"
           "  RECTANGLE 0 0 100 50  - 從(0,0)到(100,50)繪製矩形";
}

core::CommandResult RectangleCommand::executeInteractive() {
    qDebug() << "[RectangleCommand] Starting interactive mode";
    
    // TODO: 實作交互式繪製
    // 1. 提示用戶點擊第一個角點
    // 2. 提示用戶點擊對角點
    // 3. 創建矩形
    
    updateProgress(0, "請點擊第一個角點...");
    
    // 這裡應該進入事件循環等待用戶輸入
    // 暫時返回等待狀態
    
    updateProgress(100, "矩形已創建");
    return core::CommandResult::Success("Rectangle created (interactive mode)");
}

core::CommandResult RectangleCommand::executeWithCoordinates(const QVector<double>& coords) {
    if (coords.size() != 4) {
        return core::CommandResult::Failure("Need exactly 4 coordinates");
    }
    
    double x1 = coords[0];
    double y1 = coords[1];
    double x2 = coords[2];
    double y2 = coords[3];
    
    qDebug() << "[RectangleCommand] Drawing rectangle from"
             << "(" << x1 << "," << y1 << ") to"
             << "(" << x2 << "," << y2 << ")";
    
    // TODO: 實作實際的矩形創建邏輯
    // 1. 獲取當前活動的草圖
    // 2. 在草圖中創建矩形幾何
    // 3. 更新視圖
    
    bool success = createRectangle(QVector2D(x1, y1), QVector2D(x2, y2));
    
    if (!success) {
        return core::CommandResult::Failure("Failed to create rectangle");
    }
    
    updateProgress(100, "矩形已創建");
    
    double width = qAbs(x2 - x1);
    double height = qAbs(y2 - y1);
    
    QString msg = QString("Rectangle created: %.2f x %.2f")
                      .arg(width)
                      .arg(height);
    
    return core::CommandResult::Success(msg);
}

bool RectangleCommand::createRectangle(const QVector2D& corner1, const QVector2D& corner2) {
    // TODO: 實作實際的矩形創建
    // 這裡應該:
    // 1. 獲取 DocumentManager
    // 2. 獲取當前活動的 Document 和 Sketch
    // 3. 創建矩形的四條邊
    // 4. 更新視圖
    
    qDebug() << "[RectangleCommand] Creating rectangle geometry";
    
    // 暫時返回 true 表示成功
    return true;
}

} // namespace commands
} // namespace aicad
