/**
 * @file RectangleCommand.cpp
 * @brief RectangleCommand 實作
 * @author Kaufen
 * @date 2024-12-04
 */

#include "RectangleCommand.h"
#include "core/Application.h"
#include "core/DocumentManager.h"
#include "core/EventBus.h"

#include <QDebug>

namespace aicad {
namespace commands {

RectangleCommand::RectangleCommand(QObject* parent)
    : Command("rectangle", "Draw a rectangle", parent)
{
}

RectangleCommand::~RectangleCommand() {
}

core::CommandResult RectangleCommand::execute(const core::CommandContext& context) {
    setState(core::CommandState::Running);
    
    outputMessage("Rectangle command started");
    
    core::CommandResult result;
    
    if (context.args.isEmpty()) {
        // 互動模式
        result = executeInteractive();
    } else {
        // 命令列模式 - 解析座標
        QVector<double> coords;
        
        for (const QString& arg : context.args) {
            bool ok;
            double value = arg.toDouble(&ok);
            if (!ok) {
                setState(core::CommandState::Failed);
                return core::CommandResult::Failure(
                    QString("Invalid coordinate: %1").arg(arg));
            }
            coords.append(value);
        }
        
        result = executeWithCoordinates(coords);
    }
    
    if (result.success) {
        setState(core::CommandState::Completed);
    } else {
        setState(core::CommandState::Failed);
    }
    
    return result;
}

bool RectangleCommand::validateParameters(const core::CommandContext& context) const {
    // 無參數 = 互動模式，OK
    if (context.args.isEmpty()) {
        return true;
    }
    
    // 需要 4 個參數：x1 y1 x2 y2
    if (context.args.size() != 4) {
        return false;
    }
    
    // 檢查所有參數是否為數字
    for (const QString& arg : context.args) {
        bool ok;
        arg.toDouble(&ok);
        if (!ok) {
            return false;
        }
    }
    
    return true;
}

QString RectangleCommand::getUsage() const {
    return "Usage: rectangle [x1 y1 x2 y2]\n"
           "  Interactive mode: rectangle\n"
           "  Command mode: rectangle 0 0 100 50\n"
           "Creates a rectangle from two corner points.";
}

core::CommandResult RectangleCommand::executeInteractive() {
    // 互動模式的實作
    // 這裡應該與 UI 層互動來取得使用者點擊
    
    outputMessage("Click first corner point...");
    
    // TODO: 實際的互動邏輯需要與 MainWindow/CadView 整合
    // 這裡只是示範框架
    
    // 假設我們已經取得了兩個點
    // QVector2D point1 = ...; // 從使用者輸入取得
    // QVector2D point2 = ...; // 從使用者輸入取得
    
    outputMessage("Interactive mode requires UI integration");
    
    return core::CommandResult::Success("Rectangle created (interactive mode)");
}

core::CommandResult RectangleCommand::executeWithCoordinates(const QVector<double>& coords) {
    if (coords.size() != 4) {
        return core::CommandResult::Failure("Need exactly 4 coordinates");
    }
    
    QVector2D corner1(coords[0], coords[1]);
    QVector2D corner2(coords[2], coords[3]);
    
    outputMessage(QString("Creating rectangle: (%1, %2) to (%3, %4)")
        .arg(corner1.x(), 0, 'f', 2)
        .arg(corner1.y(), 0, 'f', 2)
        .arg(corner2.x(), 0, 'f', 2)
        .arg(corner2.y(), 0, 'f', 2));
    
    if (!createRectangle(corner1, corner2)) {
        return core::CommandResult::Failure("Failed to create rectangle");
    }
    
    // 計算尺寸
    double width = qAbs(corner2.x() - corner1.x());
    double height = qAbs(corner2.y() - corner1.y());
    
    QString msg = QString("Rectangle created: %.2f x %.2f").arg(width).arg(height);
    outputMessage(msg);
    
    // 透過 EventBus 發布事件
    if (core::Application* app = core::Application::instance()) {
        if (core::EventBus* bus = app->eventBus()) {
            QVariantMap data;
            data["corner1"] = QVariant::fromValue(corner1);
            data["corner2"] = QVariant::fromValue(corner2);
            data["width"] = width;
            data["height"] = height;
            
            bus->publish("rectangle.created", data);
        }
    }
    
    return core::CommandResult::Success(msg);
}

bool RectangleCommand::createRectangle(const QVector2D& corner1, 
                                       const QVector2D& corner2) 
{
    // TODO: 實際建立矩形的邏輯
    // 這裡應該透過 DocumentManager 來建立幾何
    
    qDebug() << "[RectangleCommand] Creating rectangle from"
             << corner1 << "to" << corner2;
    
    // 暫時假設成功
    return true;
}

} // namespace commands
} // namespace aicad