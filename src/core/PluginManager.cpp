/**
 * @file PluginManager.cpp
 * @brief 外掛管理器實作
 * @author Jack
 * @date 2024-12-04
 */

#include "PluginManager.h"
#include "Application.h"
#include "EventBus.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QPluginLoader>
#include <QHash>

namespace aicad {
namespace core {

// Private implementation
class PluginManager::Private {
public:
    Private() {}

    ~Private() {
        // 清理所有外掛
        for (auto& info : plugins.values()) {
            if (info.instance && info.initialized) {
                info.instance->shutdown();
            }
            delete info.instance;
        }
        plugins.clear();
    }

    // 外掛名稱 -> 外掛資訊
    QHash<QString, PluginInfo> plugins;

    // 外掛搜尋路徑
    QStringList pluginPaths;
};

PluginManager::PluginManager(QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    qDebug() << "[PluginManager] Created";

    // 設定預設外掛路徑
    d->pluginPaths << "./plugins"
                   << "./lib/plugins"
                   << QCoreApplication::applicationDirPath() + "/plugins";
}

PluginManager::~PluginManager() {
    qDebug() << "[PluginManager] Destroying...";
    shutdownAll();
    delete d;
}

int PluginManager::loadPluginsFromDirectory(const QString& directory) {
    qDebug() << "[PluginManager] Loading plugins from:" << directory;

    QDir dir(directory);
    if (!dir.exists()) {
        qWarning() << "[PluginManager] Directory does not exist:" << directory;
        return 0;
    }

    // 設定過濾器
    QStringList filters;
#ifdef Q_OS_WIN
    filters << "*.dll";
#elif defined(Q_OS_MAC)
    filters << "*.dylib";
#else
    filters << "*.so";
#endif

    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files);

    QFileInfoList files = dir.entryInfoList();
    int loadedCount = 0;

    for (const QFileInfo& fileInfo : files) {
        if (loadPlugin(fileInfo.absoluteFilePath())) {
            loadedCount++;
        }
    }

    qDebug() << "[PluginManager] Loaded" << loadedCount
             << "plugins from" << directory;

    return loadedCount;
}

bool PluginManager::loadPlugin(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        qWarning() << "[PluginManager] Plugin file does not exist:" << filePath;
        return false;
    }

    qDebug() << "[PluginManager] Loading plugin:" << fileInfo.fileName();

    // 使用 QPluginLoader 載入
    QPluginLoader loader(filePath);
    QObject* pluginObject = loader.instance();

    if (!pluginObject) {
        QString error = loader.errorString();
        qWarning() << "[PluginManager] Failed to load plugin:" << error;
        Q_EMIT pluginError(fileInfo.fileName(), error);
        return false;
    }

    // 轉換為 IPlugin 介面
    IPlugin* plugin = qobject_cast<IPlugin*>(pluginObject);
    if (!plugin) {
        qWarning() << "[PluginManager] Plugin does not implement IPlugin interface";
        loader.unload();
        Q_EMIT pluginError(fileInfo.fileName(), "Invalid plugin interface");
        return false;
    }

    // 驗證外掛
    if (!validatePlugin(plugin)) {
        qWarning() << "[PluginManager] Plugin validation failed";
        loader.unload();
        Q_EMIT pluginError(fileInfo.fileName(), "Plugin validation failed");
        return false;
    }

    QString pluginName = plugin->name();

    // 檢查是否已載入
    if (d->plugins.contains(pluginName)) {
        qWarning() << "[PluginManager] Plugin already loaded:" << pluginName;
        loader.unload();
        return false;
    }

    // 儲存外掛資訊
    PluginInfo info;
    info.name = pluginName;
    info.version = plugin->version();
    info.description = plugin->description();
    info.author = plugin->author();
    info.filePath = filePath;
    info.loaded = true;
    info.initialized = false;
    info.instance = plugin;

    d->plugins[pluginName] = info;

    qDebug() << "[PluginManager] Plugin loaded:" << pluginName
             << "Version:" << info.version;

    Q_EMIT pluginLoaded(pluginName);
    Q_EMIT pluginCountChanged(d->plugins.size());

    return true;
}

bool PluginManager::unloadPlugin(const QString& name) {
    if (!d->plugins.contains(name)) {
        qWarning() << "[PluginManager] Plugin not found:" << name;
        return false;
    }

    qDebug() << "[PluginManager] Unloading plugin:" << name;

    PluginInfo& info = d->plugins[name];

    // 先關閉外掛
    if (info.initialized) {
        shutdownPlugin(name);
    }

    // 刪除實例
    delete info.instance;
    info.instance = nullptr;

    // 從列表中移除
    d->plugins.remove(name);

    qDebug() << "[PluginManager] Plugin unloaded:" << name;

    Q_EMIT pluginUnloaded(name);
    Q_EMIT pluginCountChanged(d->plugins.size());

    return true;
}

int PluginManager::initializeAll() {
    qDebug() << "[PluginManager] Initializing all plugins...";

    int successCount = 0;

    for (auto it = d->plugins.begin(); it != d->plugins.end(); ++it) {
        if (initializePlugin(it.key())) {
            successCount++;
        }
    }

    qDebug() << "[PluginManager] Initialized" << successCount
             << "out of" << d->plugins.size() << "plugins";

    return successCount;
}

bool PluginManager::initializePlugin(const QString& name) {
    if (!d->plugins.contains(name)) {
        qWarning() << "[PluginManager] Plugin not found:" << name;
        return false;
    }

    PluginInfo& info = d->plugins[name];

    if (info.initialized) {
        qDebug() << "[PluginManager] Plugin already initialized:" << name;
        return true;
    }

    qDebug() << "[PluginManager] Initializing plugin:" << name;

    try {
        if (!info.instance->initialize()) {
            qWarning() << "[PluginManager] Plugin initialization failed:" << name;
            Q_EMIT pluginError(name, "Initialization failed");
            return false;
        }

        info.initialized = true;

        qDebug() << "[PluginManager] Plugin initialized:" << name;

        Q_EMIT pluginInitialized(name);

        // 透過 EventBus 發布事件
        if (auto app = Application::instance()) {
            if (auto bus = app->eventBus()) {
                QVariantMap data;
                data["name"] = name;
                data["version"] = info.version;
                bus->publish("plugin.initialized", data);
            }
        }

        return true;

    } catch (const std::exception& e) {
        QString error = QString("Exception during initialization: %1").arg(e.what());
        qCritical() << "[PluginManager]" << error;
        Q_EMIT pluginError(name, error);
        return false;
    } catch (...) {
        QString error = "Unknown exception during initialization";
        qCritical() << "[PluginManager]" << error;
        Q_EMIT pluginError(name, error);
        return false;
    }
}

void PluginManager::shutdownAll() {
    qDebug() << "[PluginManager] Shutting down all plugins...";

    for (auto it = d->plugins.begin(); it != d->plugins.end(); ++it) {
        if (it.value().initialized) {
            shutdownPlugin(it.key());
        }
    }

    qDebug() << "[PluginManager] All plugins shut down";
}

bool PluginManager::shutdownPlugin(const QString& name) {
    if (!d->plugins.contains(name)) {
        qWarning() << "[PluginManager] Plugin not found:" << name;
        return false;
    }

    PluginInfo& info = d->plugins[name];

    if (!info.initialized) {
        qDebug() << "[PluginManager] Plugin not initialized:" << name;
        return true;
    }

    qDebug() << "[PluginManager] Shutting down plugin:" << name;

    try {
        info.instance->shutdown();
        info.initialized = false;

        qDebug() << "[PluginManager] Plugin shut down:" << name;

        Q_EMIT pluginShutdown(name);

        return true;

    } catch (const std::exception& e) {
        QString error = QString("Exception during shutdown: %1").arg(e.what());
        qCritical() << "[PluginManager]" << error;
        Q_EMIT pluginError(name, error);
        return false;
    } catch (...) {
        QString error = "Unknown exception during shutdown";
        qCritical() << "[PluginManager]" << error;
        Q_EMIT pluginError(name, error);
        return false;
    }
}

IPlugin* PluginManager::getPlugin(const QString& name) const {
    if (!d->plugins.contains(name)) {
        return nullptr;
    }

    return d->plugins[name].instance;
}

QVector<PluginInfo> PluginManager::getAllPlugins() const {
    QVector<PluginInfo> result;
    for (const PluginInfo& info : d->plugins.values()) {
        result.append(info);
    }
    return result;
}

PluginInfo PluginManager::getPluginInfo(const QString& name) const {
    if (d->plugins.contains(name)) {
        return d->plugins[name];
    }
    return PluginInfo();
}

int PluginManager::pluginCount() const {
    return d->plugins.size();
}

bool PluginManager::hasPlugin(const QString& name) const {
    return d->plugins.contains(name);
}

bool PluginManager::isPluginLoaded(const QString& name) const {
    if (!d->plugins.contains(name)) {
        return false;
    }
    return d->plugins[name].loaded;
}

bool PluginManager::isPluginInitialized(const QString& name) const {
    if (!d->plugins.contains(name)) {
        return false;
    }
    return d->plugins[name].initialized;
}

void PluginManager::setPluginPaths(const QStringList& paths) {
    d->pluginPaths = paths;
    qDebug() << "[PluginManager] Plugin paths set:" << paths;
}

QStringList PluginManager::pluginPaths() const {
    return d->pluginPaths;
}

void PluginManager::addPluginPath(const QString& path) {
    if (!d->pluginPaths.contains(path)) {
        d->pluginPaths.append(path);
        qDebug() << "[PluginManager] Added plugin path:" << path;
    }
}

bool PluginManager::validatePlugin(IPlugin* plugin) const {
    if (!plugin) {
        return false;
    }

    // 檢查基本資訊
    if (plugin->name().isEmpty()) {
        qWarning() << "[PluginManager] Plugin has no name";
        return false;
    }

    if (plugin->version().isEmpty()) {
        qWarning() << "[PluginManager] Plugin has no version";
        return false;
    }

    return true;
}

} // namespace core
} // namespace aicad
