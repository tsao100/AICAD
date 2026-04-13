/**
 * @file FeatureBrowser.h
 * @brief 特徵瀏覽器，顯示文件中的所有特徵
 * @author James
 * @date 2025-01-07
 */

#ifndef AICAD_UI_FEATUREBROWSER_H
#define AICAD_UI_FEATUREBROWSER_H

#include <ui/FeatureTreeItem.h>

#include <QDockWidget>
#include <QTreeWidget>

namespace aicad {

namespace cad {
    class Document;
}

namespace ui {

/**
 * @brief 特徵瀏覽器
 * 
 * FeatureBrowser 顯示當前文件的特徵樹狀結構：
 * - 顯示所有特徵
 * - 支援選擇和高亮
 * - 顯示特徵類型圖示
 * - 支援右鍵選單
 * 
 * 使用範例：
 * @code
 * FeatureBrowser* browser = new FeatureBrowser(parent);
 * browser->setCurrentDocument(document);
 * browser->refresh();
 * @endcode
 */
class FeatureBrowser : public QDockWidget {
    Q_OBJECT
    
public:
    /**
     * @brief 建構子
     * @param parent 父視窗
     */
    explicit FeatureBrowser(QWidget* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~FeatureBrowser() override;
    
    /**
     * @brief 設定當前文件
     * @param document 文件指標
     */
    void setCurrentDocument(cad::Document* document);
    
    /**
     * @brief 刷新特徵樹
     */
    void refresh();
    
    /**
     * @brief 清空特徵樹
     */
    void clear();
    
    /**
     * @brief 選擇特徵
     * @param featureId 特徵 ID
     */
    void selectFeature(int featureId);

    void selectFeature(const QString& featureId);
    
Q_SIGNALS:
    /**
     * @brief 特徵被選中時發出
     * @param featureId 特徵 ID
     */
    void featureSelected(int featureId);

    void featureSelectedById(QString itemId);
    
    /**
     * @brief 特徵被雙擊時發出
     * @param featureId 特徵 ID
     */
    void featureDoubleClicked(int featureId);

    void featureDoubleClickedById(QString itemId);
    
    /**
     * @brief 特徵需要顯示右鍵選單時發出
     * @param featureId 特徵 ID
     * @param globalPos 滑鼠全域位置
     */
    void featureContextMenu(int featureId, const QPoint& globalPos);
    void featureContextMenuById(QString itemId,  QPoint& globalPos);

    // Context menu actions
    void editSketchRequested(const QString& featureId);
    void deleteFeatureRequested(const QString& featureId);
    void sectionViewRequested(const QString& featureId);
    
private Q_SLOTS:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onCustomContextMenu(const QPoint& pos);

    /**
     * @brief 處理 tree 結構改變
     */
    void onTreeStructureChanged();

    /**
     * @brief 處理項目可見性切換
     */
    void onItemVisibilityToggled(QTreeWidgetItem* item, int column);

private:
    void setupUI();
    void connectSignals();

    /**
     * @brief 根據資料建立 tree items
     */
    void buildTreeFromData(const QVector<FeatureTreeItem>& items);

    /**
     * @brief 建立單個 tree widget item
     */
    QTreeWidgetItem* createTreeWidgetItem(const FeatureTreeItem& itemData);

    /**
     * @brief 取得項目圖示
     */
    QIcon getIconForType(ItemType type);

    // 儲存 item 到 ID 的映射
    QHash<QString, QTreeWidgetItem*> m_itemMap;

    class Private;
    Private* d;
};

} // namespace ui
} // namespace aicad

#endif // AICAD_UI_FEATUREBROWSER_H
