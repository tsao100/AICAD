/**
 * @file SelectionManager.h
 * @brief SelectionManager 類別定義
 * @author TODO
 * @date 2026-01-07
 */

#ifndef AICAD_VIEW_SELECTIONMANAGER_H
#define AICAD_VIEW_SELECTIONMANAGER_H

#include <QObject>

namespace aicad {
namespace view {

/**
 * @brief SelectionManager 類別
 * 
 * TODO: 添加類別說明
 */
class SelectionManager : public QObject {
    Q_OBJECT
    
public:
    explicit SelectionManager(QObject* parent = nullptr);
    ~SelectionManager() override;
    
    // TODO: 添加公開方法
    
Q_SIGNALS:
    // TODO: 添加信號
    
private:
    // TODO: 添加私有成員
    
    class Private;
    Private* d;
};

} // namespace view
} // namespace aicad

#endif // AICAD_VIEW_SELECTIONMANAGER_H
