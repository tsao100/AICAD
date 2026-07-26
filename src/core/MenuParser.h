// src/core/MenuParser.h
/**
 * @file MenuParser.h
 * @brief 解析 menu.txt 的統一配置檔
 * @author Integration Team
 * @date 2025-01-12
 */

#ifndef AICAD_CORE_MENUPARSER_H
#define AICAD_CORE_MENUPARSER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QVector>

namespace aicad {
namespace core {

/**
 * @brief 選單項目類型
 */
enum class MenuItemType {
    Menu,
    Toolbar,
    Separator,
    Command
};

/**
 * @brief 選單項目結構
 */
struct MenuItem {
    MenuItemType type;
    QString parent;        // 父選單或工具列名稱
    QString id;           // 命令 ID
    QString label;        // 顯示標籤
    QString icon;         // 圖示路徑
    QString shortcut;     // 快捷鍵
    
    MenuItem() : type(MenuItemType::Menu) {}
};

/**
 * @brief 命令定義結構
 */
struct CommandDef {
    QString id;              // 命令 ID
    QStringList aliases;     // 別名列表
    int expectedArgs;        // 預期參數數量 (-1 = 可變)
    
    CommandDef() : expectedArgs(0) {}
};

/**
 * @brief Menu.txt 解析器
 * 
 * 負責解析 menu.txt 並提供統一的配置資料給:
 * - UIManager: 建立選單和工具列
 * - CommandManager: 註冊命令和別名
 * - LispBindings: 綁定腳本命令
 * 
 * 使用範例:
 * @code
 * MenuParser* parser = new MenuParser();
 * if (parser->load("menu.txt")) {
 *     // 取得選單項目
 *     auto menus = parser->getMenuItems("File");
 *     
 *     // 取得工具列項目
 *     auto toolbars = parser->getToolbarItems("main");
 *     
 *     // 取得命令定義
 *     auto cmd = parser->getCommand("rectangle");
 * }
 * @endcode
 */
class MenuParser : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief 建構子
     */
    explicit MenuParser(QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~MenuParser() override;
    
    /**
     * @brief 載入並解析 menu.txt
     * @param filePath 檔案路徑
     * @return 成功回傳 true
     */
    bool load(const QString& filePath);
    
    /**
     * @brief 取得指定選單的項目
     * @param menuName 選單名稱
     * @return 選單項目列表
     */
    QVector<MenuItem> getMenuItems(const QString& menuName) const;
    
    /**
     * @brief 取得指定工具列的項目
     * @param toolbarName 工具列名稱
     * @return 工具列項目列表
     */
    QVector<MenuItem> getToolbarItems(const QString& toolbarName) const;
    
    /**
     * @brief 取得所有選單名稱
     */
    QStringList getAllMenuNames() const;
    
    /**
     * @brief 取得所有工具列名稱
     */
    QStringList getAllToolbarNames() const;
    
    /**
     * @brief 取得命令定義
     * @param commandId 命令 ID
     * @return 命令定義，不存在則回傳空結構
     */
    CommandDef getCommand(const QString& commandId) const;
    
    /**
     * @brief 取得所有命令定義
     */
    QVector<CommandDef> getAllCommands() const;
    
    /**
     * @brief 根據別名尋找命令 ID
     * @param alias 別名
     * @return 命令 ID，找不到則回傳空字串
     */
    QString findCommandByAlias(const QString& alias) const;
    
    /**
     * @brief 檢查是否已載入
     */
    bool isLoaded() const;
    
    /**
     * @brief 清空所有資料
     */
    void clear();
    
Q_SIGNALS:
    /**
     * @brief 載入完成時發出
     */
    void loaded();
    
    /**
     * @brief 載入錯誤時發出
     */
    void errorOccurred(const QString& error);
    
private:
    /**
     * @brief 解析單行
     */
    bool parseLine(const QString& line, int lineNumber);
    
    /**
     * @brief 解析選單項目
     */
    bool parseMenuItem(const QStringList& parts, int lineNumber);
    
    /**
     * @brief 解析工具列項目
     */
    bool parseToolbarItem(const QStringList& parts, int lineNumber);
    
    /**
     * @brief 解析命令定義
     */
    bool parseCommand(const QStringList& parts, int lineNumber);
    
    class Private;
    Private* d;
};

} // namespace core
} // namespace aicad

#endif // AICAD_CORE_MENUPARSER_H