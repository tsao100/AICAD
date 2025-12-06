// src/core/PluginManager.h

#ifndef AICAD_CORE_PLUGINMANAGER_H
#define AICAD_CORE_PLUGINMANAGER_H

#include <QObject>
#include <QString>
#include <QVector>

namespace aicad {
namespace core {

/**
 * @brief 外掛管理器 (未來擴充用)
 */
class PluginManager : public QObject {
    Q_OBJECT
    
public:
    explicit PluginManager(QObject* parent = nullptr);
    ~PluginManager() override;
    
    /**
     * @brief 從目錄載入外掛
     */
    void loadPluginsFromDirectory(const QString& directory);
    
    /**
     * @brief 卸載所有外掛
     */
    void unloadAll();
    
private:
    class Private;
    Private* d;
};

} // namespace core
} // namespace aicad

#endif // AICAD_CORE_PLUGINMANAGER_H