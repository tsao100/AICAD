/**
 * @file PropertyPanel.h
 * @brief 屬性面板，顯示和編輯選中物件的屬性
 * @author James
 * @date 2025-01-07
 */

#ifndef AICAD_UI_PROPERTYPANEL_H
#define AICAD_UI_PROPERTYPANEL_H

#include <QDockWidget>
#include <QTableWidget>
#include <QVariant>

namespace aicad {
namespace ui {

/**
 * @brief 屬性面板
 * 
 * PropertyPanel 顯示和編輯選中物件的屬性：
 * - 顯示屬性名稱和值
 * - 支援編輯
 * - 自動更新
 * 
 * 使用範例：
 * @code
 * PropertyPanel* panel = new PropertyPanel(parent);
 * panel->clear();
 * panel->addProperty("Name", "Sketch 1");
 * panel->addProperty("Type", "Sketch");
 * @endcode
 */
class PropertyPanel : public QDockWidget {
    Q_OBJECT
    
public:
    /**
     * @brief 建構子
     * @param parent 父視窗
     */
    explicit PropertyPanel(QWidget* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~PropertyPanel() override;
    
    /**
     * @brief 清空屬性
     */
    void clear();
    
    /**
     * @brief 新增屬性
     * @param name 屬性名稱
     * @param value 屬性值
     * @param editable 是否可編輯
     */
    void addProperty(const QString& name, const QVariant& value, bool editable = false);
    
    /**
     * @brief 設定屬性值
     * @param name 屬性名稱
     * @param value 屬性值
     */
    void setPropertyValue(const QString& name, const QVariant& value);
    
    /**
     * @brief 取得屬性值
     * @param name 屬性名稱
     * @return 屬性值
     */
    QVariant getPropertyValue(const QString& name) const;
    
Q_SIGNALS:
    /**
     * @brief 屬性值變更時發出
     * @param name 屬性名稱
     * @param value 新值
     */
    void propertyChanged(const QString& name, const QVariant& value);
    
private Q_SLOTS:
    void onCellChanged(int row, int column);
    
private:
    void setupUI();
    void connectSignals();
    int findPropertyRow(const QString& name) const;
    
    class Private;
    Private* d;
};

} // namespace ui
} // namespace aicad

#endif // AICAD_UI_PROPERTYPANEL_H