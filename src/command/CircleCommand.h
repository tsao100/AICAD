/**
 * @file CircleCommand.h
 * @brief CircleCommand 類別定義
 * @author TODO
 * @date 2026-01-07
 */

#ifndef AICAD_COMMAND_CIRCLECOMMAND_H
#define AICAD_COMMAND_CIRCLECOMMAND_H

#include <QObject>

namespace aicad {
namespace command {

/**
 * @brief CircleCommand 類別
 * 
 * TODO: 添加類別說明
 */
class CircleCommand : public QObject {
    Q_OBJECT
    
public:
    explicit CircleCommand(QObject* parent = nullptr);
    ~CircleCommand() override;
    
    // TODO: 添加公開方法
    
Q_SIGNALS:
    // TODO: 添加信號
    
private:
    // TODO: 添加私有成員
    
    class Private;
    Private* d;
};

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_CIRCLECOMMAND_H
