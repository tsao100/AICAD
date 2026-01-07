/**
 * @file Command.h
 * @brief 命令基礎類別，所有命令都必須繼承此類別
 * @author Kaufen
 * @date 2024-12-04
 */

#ifndef AICAD_CORE_COMMAND_H
#define AICAD_CORE_COMMAND_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace aicad {
namespace core {

// 前向宣告
struct CommandContext;
struct CommandResult;

/**
 * @brief 命令執行狀態
 */
enum class CommandState {
    Ready,      // 準備執行
    Running,    // 執行中
    Completed,  // 已完成
    Cancelled,  // 已取消
    Failed      // 執行失敗
};

/**
 * @brief 命令基礎類別
 * 
 * Command 是所有 CAD 命令的基礎類別。
 * 每個具體命令都必須繼承此類別並實作 execute() 方法。
 * 
 * 命令生命週期:
 * 1. 建立命令物件
 * 2. 呼叫 initialize() 初始化
 * 3. 呼叫 execute() 執行
 * 4. 呼叫 cleanup() 清理
 * 5. 刪除命令物件
 * 
 * 使用範例:
 * @code
 * class LineCommand : public Command {
 * public:
 *     LineCommand() : Command("line", "Draw a line") {}
 *     
 *     CommandResult execute(const CommandContext& context) override {
 *         // 實作繪製直線的邏輯
 *         return CommandResult::Success("Line created");
 *     }
 * };
 * @endcode
 */
class Command : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString description READ description CONSTANT)
    Q_PROPERTY(CommandState state READ state NOTIFY stateChanged)
    
public:
    /**
     * @brief 建構子
     * @param name 命令名稱
     * @param description 命令描述
     * @param parent 父物件
     */
    explicit Command(const QString& name,
                    const QString& description = QString(),
                    QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    virtual ~Command();
    
    /**
     * @brief 取得命令名稱
     */
    QString name() const;
    
    /**
     * @brief 取得命令描述
     */
    QString description() const;
    
    /**
     * @brief 取得命令狀態
     */
    CommandState state() const;
    
    /**
     * @brief 初始化命令
     * 
     * 在執行命令前呼叫，用於準備必要資源
     * 子類別可以覆寫此方法
     * 
     * @return 成功回傳 true
     */
    virtual bool initialize();
    
    /**
     * @brief 執行命令（純虛擬函數，必須實作）
     * @param context 命令上下文
     * @return 執行結果
     */
    virtual CommandResult execute(const CommandContext& context) = 0;
    
    /**
     * @brief 清理命令
     * 
     * 在命令執行完畢後呼叫，用於釋放資源
     * 子類別可以覆寫此方法
     */
    virtual void cleanup();
    
    /**
     * @brief 取消命令執行
     * 
     * 子類別可以覆寫此方法來實作取消邏輯
     */
    virtual void cancel();
    
    /**
     * @brief 檢查命令是否可以取消
     */
    virtual bool canCancel() const;
    
    /**
     * @brief 取得命令使用說明
     */
    virtual QString getUsage() const;
    
    /**
     * @brief 驗證命令參數
     * @param context 命令上下文
     * @return 參數有效回傳 true
     */
    virtual bool validateParameters(const CommandContext& context) const;
    
Q_SIGNALS:
    /**
     * @brief 命令狀態改變時發出
     * @param state 新的狀態
     */
    void stateChanged(CommandState state);
    
    /**
     * @brief 命令進度更新時發出
     * @param current 當前進度
     * @param total 總進度
     * @param message 進度訊息
     */
    void progressUpdated(int current, int total, const QString& message);
    
    /**
     * @brief 命令輸出訊息時發出
     * @param message 訊息內容
     */
    void messageOutput(const QString& message);
    
protected:
    /**
     * @brief 設定命令狀態
     * @param state 新的狀態
     */
    void setState(CommandState state);
    
    /**
     * @brief 輸出訊息
     * @param message 訊息內容
     */
    void outputMessage(const QString& message);
    
    /**
     * @brief 更新進度
     * @param current 當前進度
     * @param total 總進度
     * @param message 進度訊息
     */
    void updateProgress(int current, int total, const QString& message = QString());
    
private:
    class Private;
    Private* d;
};

} // namespace core
} // namespace aicad

#endif // AICAD_CORE_COMMAND_H