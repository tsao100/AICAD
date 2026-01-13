// src/command/CommandFactory.h
/**
 * @file CommandFactory.h
 * @brief 命令工廠，根據 ID 建立命令物件
 */

#ifndef AICAD_COMMAND_COMMANDFACTORY_H
#define AICAD_COMMAND_COMMANDFACTORY_H

#include "Command.h"
#include <QString>
#include <QMap>
#include <QDebug>
#include <functional>

namespace aicad {
namespace command {

/**
 * @brief 命令工廠
 * 
 * 負責根據命令 ID 建立對應的 Command 物件
 */
class CommandFactory {
public:
    using CreatorFunc = std::function<Command*()>;
    
    /**
     * @brief 註冊命令建立器
     */
    static void registerCreator(const QString& commandId, CreatorFunc creator) {
        creators()[commandId] = creator;
    }
    
    /**
     * @brief 根據 ID 建立命令
     */
    static Command* create(const QString& commandId) {
        auto& map = creators();
        if (map.contains(commandId)) {
            return map[commandId]();
        }
        
        qWarning() << "[CommandFactory] Unknown command:" << commandId;
        return nullptr;
    }
    
    /**
     * @brief 檢查命令是否已註冊
     */
    static bool hasCommand(const QString& commandId) {
        return creators().contains(commandId);
    }
    
private:
    static QMap<QString, CreatorFunc>& creators() {
        static QMap<QString, CreatorFunc> map;
        return map;
    }
};

// 輔助巨集，簡化命令註冊
#define REGISTER_COMMAND(ID, CLASS) \
    static bool _reg_##CLASS = []() { \
        CommandFactory::registerCreator(ID, []() -> Command* { \
            return new CLASS(); \
        }); \
        return true; \
    }()

} // namespace command
} // namespace aicad

#endif
