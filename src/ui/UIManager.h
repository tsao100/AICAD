/**
 * @file UIManager.h
 * @brief UI 管理器，管理所有使用者介面元件
 * @author James
 * @date 2025-01-07
 */

#ifndef AICAD_UI_UIMANAGER_H
#define AICAD_UI_UIMANAGER_H

#include <QObject>
#include <QString>
#include <QMainWindow>

namespace aicad {

// 前向宣告
namespace cad {
    class Document;
    class Sketch;
}

namespace view {
    class CadView;
}

namespace core {
    class MenuParser;
    class CommandLineManager;  // ✅ 新增
}

namespace command {
    class CommandAlias;  // ✅ 新增
}

namespace ui {

class MainWindow;
class FeatureBrowser;
class PropertyPanel;
class ToolManager;
class CommandOverlayWidget;  // ✅ 新增
class AutoCompleteModel;     // ✅ 新增

/**
 * @brief UI 管理器
 * 
 * UIManager 管理應用程式的所有 UI 元件：
 * - 主視窗
 * - 特徵瀏覽器
 * - 屬性面板
 * - 工具列管理
 * 
 * 使用範例：
 * @code
 * UIManager* uiMgr = app->uiManager();
 * uiMgr->showMainWindow();
 * uiMgr->updateFeatureTree();
 * @endcode
 */
class UIManager : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief 建構子
     * @param parent 父物件
     */
    explicit UIManager(QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~UIManager() override;
    
    /**
     * @brief 初始化 UI 系統
     * @return 成功回傳 true
     */
    bool initialize(core::MenuParser* menuParser);
    void initGripSystem();

    /**
     * @brief 顯示主視窗
     */
    void showMainWindow();
    
    /**
     * @brief 取得主視窗
     * @return 主視窗指標
     */
    MainWindow* mainWindow() const;
    
    /**
     * @brief 取得特徵瀏覽器
     * @return 特徵瀏覽器指標
     */
    FeatureBrowser* featureBrowser() const;
    
    /**
     * @brief 取得屬性面板
     * @return 屬性面板指標
     */
    PropertyPanel* propertyPanel() const;
    
    /**
     * @brief 取得工具管理器
     * @return 工具管理器指標
     */
    ToolManager* toolManager() const;
    
    /**
     * @brief 更新特徵樹
     */
    void updateFeatureTree();
    
    /**
     * @brief 設定狀態列訊息
     * @param message 訊息內容
     * @param timeout 顯示時間（毫秒），0 表示永久顯示
     */
    void setStatusMessage(const QString& message, int timeout = 0);
    view::CadView* cadView() const;

    void setupMenusFromParser();
    void setupToolbarsFromParser();
    void executeCommand(const QString& commandId);
    void setupDefaultUI();
    void onNewDocument();
    void onOpenDocument();
    void onSaveDocument();

    void onViewReady();

    /**
     * @brief 取得命令列覆蓋層
     * @return CommandOverlayWidget 指標
     */
    CommandOverlayWidget* commandLine() const;

    /**
     * @brief 取得命令列管理器
     * @return CommandLineManager 指標
     */
    core::CommandLineManager* commandLineManager() const;

    /**
     * @brief 取得命令別名管理器
     * @return CommandAlias 指標
     */
    command::CommandAlias* commandAlias() const;

    /**
     * @brief 在命令列顯示訊息
     * @param message 訊息內容
     * @param color 文字顏色 (預設白色)
     */
    void showCommandMessage(const QString& message, const QString& color = "white");

    /**
     * @brief 在命令列顯示錯誤
     * @param error 錯誤訊息
     */
    void showCommandError(const QString& error);

    /**
     * @brief 在命令列顯示警告
     * @param warning 警告訊息
     */
    void showCommandWarning(const QString& warning);

Q_SIGNALS:
    /**
     * @brief UI 初始化完成時發出
     */
    void initialized();
    
    /**
     * @brief 主視窗關閉時發出
     */
    void mainWindowClosed();

public slots:
    void onSketchEditStarted(cad::Sketch* sketch);
    void onSketchEditEnded();

    
private:

    void initializeReferenceGeometry();
    void onDocumentCreated();
    void onCurrentDocumentChanged(cad::Document* doc);

    void setupCommandLine();
    void connectCommandLineEvents();

    class Private;
    Private* d;
};

} // namespace ui
} // namespace aicad

#endif // AICAD_UI_UIMANAGER_H
