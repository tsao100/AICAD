/**
 * @file CommandManager.h
 * @brief 命令管理器，統一管理所有 CAD 命令
 * @author Kaufen
 * @date 2024-12-04
 */

#ifndef AICAD_CORE_COMMANDMANAGER_H
#define AICAD_CORE_COMMANDMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QMap>      // 提供 QMap 類型
#include <QVariant>  // 提供 QVariant 類型
#include <functional>

#include "CommandTypes.h"

namespace aicad {
namespace command {

// 前向宣告
class Command;

/**
 * @brief 命令管理器
 * 
 * CommandManager 負責:
 * - 註冊和管理所有命令
 * - 執行命令並處理結果
 * - 維護命令歷史記錄
 * - 支援命令別名
 * - 提供命令自動完成
 * 
 * 使用範例:
 * @code
 * CommandManager* cmdMgr = app->commandManager();
 * 
 * // 註冊命令
 * cmdMgr->registerCommand("rectangle", {"rect"}, 
 *     []() { return new RectangleCommand(); });
 * 
 * // 執行命令
 * cmdMgr->executeCommand("rectangle", args);
 * @endcode
 */
class CommandManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(int commandCount READ commandCount NOTIFY commandCountChanged)
    
public:
    /**
     * @brief 命令工廠函數類型
     */
    using CommandFactory = std::function<Command*()>;
    
    /**
     * @brief 建構子
     * @param parent 父物件
     */
    explicit CommandManager(QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~CommandManager() override;
    
    /**
     * @brief 註冊命令
     * @param name 命令名稱
     * @param aliases 命令別名列表
     * @param factory 命令工廠函數
     * @return 成功回傳 true
     * 
     * @code
     * cmdMgr->registerCommand("line", {"l"}, 
     *     []() { return new LineCommand(); });
     * @endcode
     */
    bool registerCommand(const QString& name,
                        const QStringList& aliases,
                        CommandFactory factory);
    
    /**
     * @brief 取消註冊命令
     * @param name 命令名稱
     */
    void unregisterCommand(const QString& name);
    
    /**
     * @brief 執行命令
     * @param commandName 命令名稱
     * @param context 命令上下文
     * @return 執行結果
     */
    CommandResult executeCommand(const QString& commandName,
                                const CommandContext& context = CommandContext());
    
    void onCommandFinished(const CommandResult& result);
    /**
     * @brief 簡化的執行命令
     * @param commandName 命令名稱
     * @param args 參數列表
     * @return 執行結果
     */
    CommandResult executeCommand(const QString& commandName,
                                const QStringList& args);
    
    /**
     * @brief 取消當前執行的命令
     */
    void cancelCurrentCommand();
    
    /**
     * @brief 檢查命令是否存在
     * @param name 命令名稱（可以是別名）
     */
    bool hasCommand(const QString& name) const;
    
    /**
     * @brief 取得命令的標準名稱
     * @param nameOrAlias 命令名稱或別名
     * @return 標準命令名稱，不存在則回傳空字串
     */
    QString getCanonicalName(const QString& nameOrAlias) const;
    
    /**
     * @brief 取得所有已註冊的命令名稱
     */
    QStringList getAllCommandNames() const;
    
    /**
     * @brief 取得命令的別名列表
     * @param name 命令名稱
     */
    QStringList getCommandAliases(const QString& name) const;
    
    /**
     * @brief 取得命令描述
     * @param name 命令名稱
     */
    QString getCommandDescription(const QString& name) const;
    
    /**
     * @brief 取得命令數量
     */
    int commandCount() const;
    
    /**
     * @brief 取得命令歷史
     * @param maxCount 最大數量（-1 表示全部）
     */
    QStringList getCommandHistory(int maxCount = -1) const;
    
    /**
     * @brief 清除命令歷史
     */
    void clearHistory();
    
    /**
     * @brief 自動完成命令名稱
     * @param prefix 輸入的前綴
     * @return 匹配的命令名稱列表
     */
    QStringList autoCompleteCommand(const QString& prefix) const;
    
    /**
     * @brief 取得當前執行的命令
     * @return 當前命令指標，無則為 nullptr
     */
    Command* currentCommand() const;
    
    /**
     * @brief 檢查是否有命令正在執行
     */
    bool isCommandRunning() const;
    
Q_SIGNALS:
    /**
     * @brief 命令開始執行時發出
     * @param commandName 命令名稱
     */
    void commandStarted(const QString& commandName);
    
    /**
     * @brief 命令執行完成時發出
     * @param commandName 命令名稱
     * @param result 執行結果
     */
    void commandFinished(const QString& commandName, const CommandResult& result);
    
    /**
     * @brief 命令被取消時發出
     * @param commandName 命令名稱
     */
    void commandCancelled(const QString& commandName);
    
    /**
     * @brief 命令數量改變時發出
     * @param count 新的數量
     */
    void commandCountChanged(int count);
    
    /**
     * @brief 命令輸出訊息時發出
     * @param message 訊息內容
     */
    void commandMessage(const QString& message);
    
private:
    /**
     * @brief 添加到歷史記錄
     */
    void addToHistory(const QString& commandName, const QStringList& args);
    
    class Private;
    Private* d;
};

} // namespace core
} // namespace aicad

#endif // AICAD_CORE_COMMANDMANAGER_H
