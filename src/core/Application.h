/**
 * @file Application.h
 * @brief AICAD 應用程式核心單例
 * @author Jack
 * @date 2024-12-04
 */

#ifndef AICAD_CORE_APPLICATION_H
#define AICAD_CORE_APPLICATION_H

#include <QObject>
#include <QString>
#include "ui/UIManager.h"
#include "scripting/LispEngine.h"
#include "scripting/LispBindings.h"

namespace aicad {
namespace core {

class EventBus;
class DocumentManager;
class CommandManager;
class LispEngine;

/**
 * @brief 應用程式單例，管理全域資源和模組生命週期
 * 
 * Application 是整個應用程式的核心，負責:
 * - 初始化和關閉各個模組
 * - 提供全域資源的存取點
 * - 管理應用程式生命週期
 * 
 * 使用範例:
 * @code
 * Application* app = Application::instance();
 * if (app->initialize()) {
 *     DocumentManager* docMgr = app->documentManager();
 *     CommandManager* cmdMgr = app->commandManager();
 * }
 * @endcode
 */
class Application : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief 取得應用程式單例實例
     * @return Application 指標 (永不為 nullptr)
     */
    static Application* instance();
    
    /**
     * @brief 初始化應用程式
     * @return 成功回傳 true，失敗回傳 false
     * 
     * 此方法執行以下初始化:
     * - 建立事件總線
     * - 建立文件管理器
     * - 建立命令管理器
     * - 初始化 OCCT 環境
     * - 註冊預設模組
     */
    bool initialize();
    
    /**
     * @brief 關閉應用程式並釋放資源
     * 
     * 此方法執行清理工作:
     * - 關閉所有開啟的文件
     * - 釋放模組資源
     * - 清理暫存檔案
     */
    void shutdown();
    
    /**
     * @brief 取得文件管理器
     * @return DocumentManager 指標，未初始化則為 nullptr
     */
    DocumentManager* documentManager() const;
    
    /**
     * @brief 取得事件總線
     * @return EventBus 指標，未初始化則為 nullptr
     */
    EventBus* eventBus() const;
    
    /**
     * @brief 取得命令管理器
     * @return CommandManager 指標，未初始化則為 nullptr
     */
    CommandManager* commandManager() const;
    
    /**
     * @brief 檢查應用程式是否已初始化
     */
    bool isInitialized() const;
    
    /**
     * @brief 取得應用程式版本
     */
    QString version() const;
    
    /**
     * @brief 取得應用程式名稱
     */
    QString applicationName() const;
    
    /**
     * @brief 取得 UI 管理器
     * @return UIManager 指標，未初始化則為 nullptr
     */
    ui::UIManager* uiManager() const;

    /**
     * @brief 取得 lispEngine
     * @return LispEngine 指標，未初始化則為 nullptr
     */
    scripting::LispEngine* lispEngine() const;

Q_SIGNALS:
    /**
     * @brief 初始化完成時發出
     */
    void initialized();
    
    /**
     * @brief 即將關閉時發出
     */
    void aboutToQuit();
    
    /**
     * @brief 錯誤發生時發出
     * @param message 錯誤訊息
     */
    void errorOccurred(const QString& message);
    
private:
    /**
     * @brief 私有建構子 (單例模式)
     */
    Application();
    
    /**
     * @brief 解構子
     */
    ~Application() override;
    
    // 禁用複製
    Q_DISABLE_COPY(Application)
    
    // Private implementation (PIMPL idiom)
    class Private;
    Private* d;
};

} // namespace core
} // namespace aicad

#endif // AICAD_CORE_APPLICATION_H
