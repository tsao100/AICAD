/**
 * @file PropertyPanel.h
 * @brief PropertyPanel 類別定義
 * @author TODO
 * @date 2026-01-07
 */

#ifndef AICAD_UI_PROPERTYPANEL_H
#define AICAD_UI_PROPERTYPANEL_H

#include <QObject>

namespace aicad {
namespace ui {

/**
 * @brief PropertyPanel 類別
 * 
 * TODO: 添加類別說明
 */
class PropertyPanel : public QObject {
    Q_OBJECT
    
public:
    explicit PropertyPanel(QObject* parent = nullptr);
    ~PropertyPanel() override;
    
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

#endif // AICAD_UI_PROPERTYPANEL_H
