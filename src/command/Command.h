/**
 * @file Command.h
 * @brief 命令基類定義
 * @author Jack
 * @date 2024-12-04
 * @version Fixed - 包含完整的類型定義
 */

#ifndef AICAD_CORE_COMMAND_H
#define AICAD_CORE_COMMAND_H

#include "CommandTypes.h"  // 包含完整的 CommandContext 和 CommandResult 定義
#include <QObject>
#include <QString>

namespace aicad {
namespace command {

// 前向聲明
struct CommandContext;
struct CommandResult;

/**
 * @brief 命令狀態
 */
enum class CommandState {
    Ready,
    Running,
    Completed,
    Failed,
    Cancelled,
    WaitingInput
};

/**
 * @brief 命令基類
 * 
 * 所有 CAD 命令都應該繼承這個基類
 * 
 * 使用範例:
 * @code
 * class LineCommand : public Command {
 * public:
 *     LineCommand() : Command("line", "繪製直線") {}
 *     
 *     CommandResult execute(const CommandContext& context) override {
 *         // 實作繪製直線邏輯
 *         return CommandResult::Success("Line created");
 *     }
 * };
 * @endcode
 */
class Command : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief 構造函數
     * @param name 命令名稱
     * @param description 命令描述
     * @param parent 父對象
     */
    explicit Command(const QString& name, 
                    const QString& description = QString(),
                    QObject* parent = nullptr);
    
    /**
     * @brief 析構函數
     */
    virtual ~Command();
    
    /**
     * @brief 執行命令
     * @param context 命令上下文
     * @return 執行結果
     */
    virtual CommandResult execute(const CommandContext& context) = 0;
    
    /**
     * @brief 取消命令執行
     * 
     * 對於交互式命令,可以實作此方法來處理取消操作
     */
    virtual bool initialize();
    virtual void cleanup();
    virtual void cancel();
    virtual bool canCancel() const;

    /**
     * @brief 驗證參數
     * @param context 命令上下文
     * @return 參數是否有效
     */
    virtual bool validateParameters(const CommandContext& context) const;
    virtual QString getUsage() const;

    /**
     * @brief 獲取命令名稱
     */
    QString name() const;
    
    /**
     * @brief 獲取命令描述
     */
    QString description() const;
    CommandState state() const;
    
    /**
     * @brief 設置命令描述
     */
    void setDescription(const QString& desc);
    
    /**
     * @brief 檢查命令是否為交互式命令
     */
    virtual bool isInteractive() const;
    
    /**
     * @brief 獲取命令的幫助文本
     */
    virtual QString helpText() const;

    /**
     * @brief Mark command as waiting for async operation
     *
     * Call this in execute() if the command needs to wait
     * for user input or other async operation.
     *
     * The command will stay alive until finished() is emitted.
     */
    void setWaitingForInput() {
        setState(CommandState::Running);
    }

    /**
     * @brief Complete the command with result
     *
     * Call this when async operation completes
     */
    void complete(const CommandResult& result) {
        if (result.success) {
            setState(CommandState::Completed);
        } else {
            setState(CommandState::Failed);
        }
        Q_EMIT finished(result);
    }

    
Q_SIGNALS:
    /**
     * @brief 命令開始執行時發出
     */
    void started();
    
    /**
     * @brief 命令執行完成時發出
     * @param result 執行結果
     */
    void finished(const CommandResult& result);
    
    /**
     * @brief 命令被取消時發出
     */
    void cancelled();
    
    /**
     * @brief 命令執行進度更新
     * @param progress 進度百分比 (0-100)
     * @param message 進度消息
     */
    void progressChanged(int progress, const QString& message);

    void stateChanged(CommandState state);
    void messageOutput(const QString& message);
    void progressUpdated(int current, int total, const QString& message);

    
protected:
    void setState(CommandState state);
    void outputMessage(const QString& message);

    /**
     * @brief 發出進度更新信號
     */
    void updateProgress(int current, int total, const QString& message);
    
private:
    class Private;
    Private* d;
};

} // namespace core
} // namespace aicad

#endif // AICAD_CORE_COMMAND_H
