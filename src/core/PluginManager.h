/**
 * @file PluginManager.h
 * @brief 外掛管理器，負責載入和管理外掛模組
 * @author Jack
 * @date 2024-12-04
 */

#ifndef AICAD_CORE_PLUGINMANAGER_H
#define AICAD_CORE_PLUGINMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVariant>

namespace aicad {
namespace core {

/**
 * @brief 外掛介面
 *
 * 所有外掛必須實作此介面
 */
class IPlugin {
public:
    virtual ~IPlugin() = default;

    /**
     * @brief 取得外掛名稱
     */
    virtual QString name() const = 0;

    /**
     * @brief 取得外掛版本
     */
    virtual QString version() const = 0;

    /**
     * @brief 取得外掛描述
     */
    virtual QString description() const = 0;

    /**
     * @brief 取得外掛作者
     */
    virtual QString author() const = 0;

    /**
     * @brief 初始化外掛
     * @return 成功回傳 true
     */
    virtual bool initialize() = 0;

    /**
     * @brief 關閉外掛
     */
    virtual void shutdown() = 0;

    /**
     * @brief 檢查外掛是否已初始化
     */
    virtual bool isInitialized() const = 0;
};

/**
 * @brief 外掛資訊
 */
struct PluginInfo {
    QString name;           // 外掛名稱
    QString version;        // 版本
    QString description;    // 描述
    QString author;         // 作者
    QString filePath;       // 檔案路徑
    bool loaded;            // 是否已載入
    bool initialized;       // 是否已初始化
    IPlugin* instance;      // 外掛實例
};

/**
 * @brief 外掛管理器
 *
 * PluginManager 負責:
 * - 掃描外掛目錄
 * - 載入外掛
 * - 初始化外掛
 * - 管理外掛生命週期
 *
 * 使用範例:
 * @code
 * PluginManager* pluginMgr = app->pluginManager();
 *
 * // 載入所有外掛
 * pluginMgr->loadPluginsFromDirectory("./plugins");
 *
 * // 初始化外掛
 * pluginMgr->initializeAll();
 *
 * // 取得外掛
 * IPlugin* plugin = pluginMgr->getPlugin("MyPlugin");
 * @endcode
 */
class PluginManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(int pluginCount READ pluginCount NOTIFY pluginCountChanged)

public:
    /**
     * @brief 建構子
     * @param parent 父物件
     */
    explicit PluginManager(QObject* parent = nullptr);

    /**
     * @brief 解構子
     */
    ~PluginManager() override;

    /**
     * @brief 從目錄載入外掛
     * @param directory 外掛目錄路徑
     * @return 載入的外掛數量
     */
    int loadPluginsFromDirectory(const QString& directory);

    /**
     * @brief 載入單一外掛
     * @param filePath 外掛檔案路徑
     * @return 成功回傳 true
     */
    bool loadPlugin(const QString& filePath);

    /**
     * @brief 卸載外掛
     * @param name 外掛名稱
     * @return 成功回傳 true
     */
    bool unloadPlugin(const QString& name);

    /**
     * @brief 初始化所有外掛
     * @return 成功初始化的外掛數量
     */
    int initializeAll();

    /**
     * @brief 初始化指定外掛
     * @param name 外掛名稱
     * @return 成功回傳 true
     */
    bool initializePlugin(const QString& name);

    /**
     * @brief 關閉所有外掛
     */
    void shutdownAll();

    /**
     * @brief 關閉指定外掛
     * @param name 外掛名稱
     * @return 成功回傳 true
     */
    bool shutdownPlugin(const QString& name);

    /**
     * @brief 取得外掛實例
     * @param name 外掛名稱
     * @return 外掛實例，找不到則為 nullptr
     */
    IPlugin* getPlugin(const QString& name) const;

    /**
     * @brief 取得所有外掛資訊
     */
    QVector<PluginInfo> getAllPlugins() const;

    /**
     * @brief 取得外掛資訊
     * @param name 外掛名稱
     * @return 外掛資訊
     */
    PluginInfo getPluginInfo(const QString& name) const;

    /**
     * @brief 取得外掛數量
     */
    int pluginCount() const;

    /**
     * @brief 檢查外掛是否存在
     * @param name 外掛名稱
     */
    bool hasPlugin(const QString& name) const;

    /**
     * @brief 檢查外掛是否已載入
     * @param name 外掛名稱
     */
    bool isPluginLoaded(const QString& name) const;

    /**
     * @brief 檢查外掛是否已初始化
     * @param name 外掛名稱
     */
    bool isPluginInitialized(const QString& name) const;

    /**
     * @brief 設定外掛搜尋路徑
     * @param paths 路徑列表
     */
    void setPluginPaths(const QStringList& paths);

    /**
     * @brief 取得外掛搜尋路徑
     */
    QStringList pluginPaths() const;

    /**
     * @brief 新增外掛搜尋路徑
     * @param path 路徑
     */
    void addPluginPath(const QString& path);

Q_SIGNALS:
    /**
     * @brief 外掛被載入時發出
     * @param name 外掛名稱
     */
    void pluginLoaded(const QString& name);

    /**
     * @brief 外掛被卸載時發出
     * @param name 外掛名稱
     */
    void pluginUnloaded(const QString& name);

    /**
     * @brief 外掛被初始化時發出
     * @param name 外掛名稱
     */
    void pluginInitialized(const QString& name);

    /**
     * @brief 外掛被關閉時發出
     * @param name 外掛名稱
     */
    void pluginShutdown(const QString& name);

    /**
     * @brief 外掛數量改變時發出
     * @param count 新的外掛數量
     */
    void pluginCountChanged(int count);

    /**
     * @brief 外掛錯誤時發出
     * @param name 外掛名稱
     * @param error 錯誤訊息
     */
    void pluginError(const QString& name, const QString& error);

private:
    /**
     * @brief 驗證外掛介面
     * @param plugin 外掛實例
     * @return 有效回傳 true
     */
    bool validatePlugin(IPlugin* plugin) const;

    class Private;
    Private* d;
};

} // namespace core
} // namespace aicad

// Qt 外掛介面宏
#define IPlugin_iid "org.aicad.IPlugin/1.0"
Q_DECLARE_INTERFACE(aicad::core::IPlugin, IPlugin_iid)

#endif // AICAD_CORE_PLUGINMANAGER_H
