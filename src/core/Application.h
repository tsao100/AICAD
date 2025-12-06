// src/core/Application.h

#ifndef AICAD_CORE_APPLICATION_H
#define AICAD_CORE_APPLICATION_H

#include <QObject>
#include <QString>
#include <QCoreApplication>

namespace aicad {

// Forward declarations
namespace cad {
    class DocumentManager;
}

namespace commands {
    class CommandManager;
}

namespace view {
    class ViewManager;
}

namespace scripting {
    class LispEngine;
}

namespace core {

class EventBus;
class PluginManager;

/**
 * @file Application.h
 * @brief AICAD 應用程式單例類別
 * @author Jack
 * @date 2024-12-06
 */

/**
 * @brief AICAD 應用程式單例
 * 
 * Application 是整個系統的核心，管理所有全域資源和管理器。
 * 採用單例模式確保只有一個實例存在。
 * 
 * 主要職責:
 * - 初始化和關閉應用程式
 * - 管理各個子系統 (文件、命令、視圖、腳本)
 * - 提供事件總線進行模組間通訊
 * - 管理應用程式設定
 * 
 * 使用範例:
 * @code
 * Application* app = Application::instance();
 * if (app->initialize()) {
 *     DocumentManager* docMgr = app->documentManager();
 *     // 使用文件管理器...
 * }
 * @endcode
 * 
 * @see DocumentManager
 * @see CommandManager
 * @see EventBus
 */
class Application : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief 取得應用程式單例實例
     * @return Application 單例指標
     * 
     * @note 第一次呼叫時會建立實例
     */
    static Application* instance();
    
    /**
     * @brief 初始化應用程式
     * @return 成功回傳 true，失敗回傳 false
     * 
     * 初始化流程:
     * 1. 載入應用程式設定
     * 2. 初始化事件總線
     * 3. 建立各個管理器
     * 4. 初始化 Lisp 引擎 (如果啟用)
     * 5. 載入外掛程式
     * 
     * @note 必須在使用任何功能前呼叫此函式
     * @warning 此函式只能呼叫一次
     */
    bool initialize();
    
    /**
     * @brief 關閉應用程式
     * 
     * 清理流程:
     * 1. 儲存應用程式設定
     * 2. 關閉所有文件
     * 3. 清理各個管理器
     * 4. 關閉 Lisp 引擎
     * 5. 卸載外掛程式
     * 
     * @note 會自動儲存未儲存的文件
     */
    void shutdown();
    
    /**
     * @brief 應用程式是否已初始化
     */
    bool isInitialized() const;
    
    // === 管理器存取 ===
    
    /**
     * @brief 取得文件管理器
     * @return DocumentManager 指標，未初始化則回傳 nullptr
     */
    cad::DocumentManager* documentManager() const;
    
    /**
     * @brief 取得命令管理器
     * @return CommandManager 指標，未初始化則回傳 nullptr
     */
    commands::CommandManager* commandManager() const;
    
    /**
     * @brief 取得視圖管理器
     * @return ViewManager 指標，未初始化則回傳 nullptr
     */
    view::ViewManager* viewManager() const;
    
    /**
     * @brief 取得 Lisp 引擎
     * @return LispEngine 指標，未啟用則回傳 nullptr
     */
    scripting::LispEngine* lispEngine() const;
    
    /**
     * @brief 取得事件總線
     * @return EventBus 指標
     */
    EventBus* eventBus() const;
    
    /**
     * @brief 取得外掛管理器
     * @return PluginManager 指標
     */
    PluginManager* pluginManager() const;
    
    // === 設定管理 ===
    
    /**
     * @brief 取得應用程式設定值
     * @param key 設定鍵值
     * @param defaultValue 預設值
     * @return 設定值
     */
    QVariant setting(const QString& key, const QVariant& defaultValue = QVariant()) const;
    
    /**
     * @brief 設定應用程式設定值
     * @param key 設定鍵值
     * @param value 設定值
     */
    void setSetting(const QString& key, const QVariant& value);
    
    /**
     * @brief 儲存所有設定
     */
    void saveSettings();
    
    // === 版本資訊 ===
    
    /**
     * @brief 取得應用程式版本
     * @return 版本字串 (如 "1.0.0")
     */
    static QString version();
    
    /**
     * @brief 取得建置日期
     * @return 建置日期字串
     */
    static QString buildDate();
    
    /**
     * @brief 取得建置資訊
     * @return 包含編譯器、Qt 版本等資訊的字串
     */
    static QString buildInfo();
    
Q_SIGNALS:
    /**
     * @brief 應用程式初始化完成
     */
    void initialized();
    
    /**
     * @brief 應用程式即將關閉
     */
    void aboutToQuit();
    
    /**
     * @brief 設定值改變
     * @param key 設定鍵值
     * @param value 新值
     */
    void settingChanged(const QString& key, const QVariant& value);
    
    /**
     * @brief 錯誤發生
     * @param message 錯誤訊息
     */
    void errorOccurred(const QString& message);
    
private:
    /**
     * @brief 私有建構子 (單例模式)
     */
    Application();
    
    /**
     * @brief 私有解構子
     */
    ~Application() override;
    
    // 禁止複製
    Q_DISABLE_COPY(Application)
    
    /**
     * @brief 初始化事件總線
     */
    bool initializeEventBus();
    
    /**
     * @brief 初始化文件管理器
     */
    bool initializeDocumentManager();
    
    /**
     * @brief 初始化命令管理器
     */
    bool initializeCommandManager();
    
    /**
     * @brief 初始化視圖管理器
     */
    bool initializeViewManager();
    
    /**
     * @brief 初始化 Lisp 引擎
     */
    bool initializeLispEngine();
    
    /**
     * @brief 初始化外掛系統
     */
    bool initializePluginManager();
    
    /**
     * @brief 載入應用程式設定
     */
    void loadSettings();
    
    /**
     * @brief 連接子系統信號
     */
    void connectSubsystems();
    
    // PIMPL 模式隱藏實作細節
    class Private;
    Private* d;
    
    static Application* s_instance;
};

} // namespace core
} // namespace aicad

// 便捷巨集
#define aicadApp (aicad::core::Application::instance())

#endif // AICAD_CORE_APPLICATION_H