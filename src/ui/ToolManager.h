//  src/ui/ToolbarManager.h
/**
 * @file ToolbarManager.h
 * @brief ToolbarManager 類別定義
 * @author TODO
 * @date 2026-01-07
 */

#ifndef AICAD_UI_TOOLBARMANAGER_H
#define AICAD_UI_TOOLBARMANAGER_H

#include <QObject>

namespace aicad {
namespace ui {

/**
 * @brief ToolbarManager 類別
 * 
 * TODO: 添加類別說明
 */
class ToolbarManager : public QObject {
    Q_OBJECT
    
public:
    explicit ToolbarManager(QObject* parent = nullptr);
    ~ToolbarManager() override;
    
    // TODO: 添加公開方法
    
Q_SIGNALS:
    // TODO: 添加信號
    
private:
    // TODO: 添加私有成員
    
    class Private;
    Private* d;
};

} // namespace ui
} // namespace aicad

#endif // AICAD_UI_TOOLBARMANAGER_H
