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
#include <functional>
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

    /**
     * @brief 在指定螢幕座標彈出「等同 Feature Browser 該項目右鍵」的選單。
     *        供 View 中點選圖形（例如 Railway 線形）右鍵時共用，選單內容與樹狀圖完全一致。
     * @return false = 樹狀圖中找不到該項目（未彈出選單）
     */
    bool showContextMenuForItem(const QString& itemId, const QPoint& globalPos);

    /**
     * @brief 設定「項目目前實際是否顯示」的查詢函式（回傳 1=顯示 0=隱藏 -1=未知，改用樹狀圖眼睛狀態）。
     *        用於 Railway 3D 彙總顯示時，單一線路的實際顯示狀態與樹狀圖眼睛不同。
     */
    void setVisibilityResolver(std::function<int(const QString&)> resolver) { m_visibilityResolver = std::move(resolver); }
    
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
    /// ProfileArray 測站（SketchInstance）的「查看草圖」：唯讀，顯示本站實際套用值
    void viewSketchInstanceRequested(const QString& featureId);
    /// 複製草圖到剪貼簿（可貼到其他檔案）
    void copySketchRequested(const QString& featureId);
    /// 重新命名草圖（右鍵「重新命名」或 F2）
    void renameSketchRequested(const QString& featureId);
    /// 把剪貼簿中的草圖貼進目前文件
    void pasteSketchRequested();
    void deleteFeatureRequested(const QString& featureId);
    void sectionViewRequested(const QString& featureId);
    /// 右鍵「聚焦 Focus」：把該項目的內容 Fit 聚焦顯示在 View 中
    /// itemId 可為 Feature id 或 TrackCenterLine id
    void focusRequested(const QString& itemId);
    /// 右鍵「隱藏 Hide (eyeClose)」／「顯示 Show (eyeOpen)」
    void visibilityRequested(const QString& itemId, bool visible);
    /// Railway 資料夾右鍵「所有線形 eyeOpen / eyeClose」
    void allAlignmentsVisibilityRequested(bool visible);
    /// 沿線斷面陣列（Pattern）右鍵「所有測站草圖 eyeOpen / eyeClose」
    void stationSketchesVisibilityRequested(const QString& arrayId, bool visible);
    /// 道版 E,N 平面圖右鍵「匯出 DXF…」
    void exportSlabPlanDxfRequested(const QString& itemId);
    /// 3D 組件安裝右鍵「編輯安裝位置…」
    void editComponentPlacementsRequested(const QString& itemId);
    /// 建立／重設組件座標線（與斷面草圖建立約束關係）
    void createComponentFrameRequested(const QString& itemId);
    /// 立即與來源 *.aicad 同步
    void syncComponentRequested(const QString& itemId);
    /// 切換「來源檔被存檔時自動同步」
    void toggleComponentAutoSyncRequested(const QString& itemId);
    /// 雙擊組件 → 在另一個 CADView（另開文件視圖）開啟來源 *.aicad 編輯
    void openComponentSourceRequested(const QString& itemId);
    /// 「組件安裝位置」資料夾：新增／編輯各式組件沿線形的安裝位置
    void editComponentLayoutRequested();
    /// 「組件安裝位置」資料夾：插入新的 3D 組件（*.aicad）
    void insertComponentRequested();
    /// Extrude 右鍵「編輯擠出（方向／尺寸）…」
    void editExtrudeRequested(const QString& itemId);

    /// 旋轉／掃掠／放樣／圓角／抽殼／布林 右鍵「編輯特徵參數…」
    void editSolidFeatureRequested(const QString& itemId);
    /// 定義平面：以 baseRef（"XY"/"XZ"/"YZ" 或 DefinedPlane 的 id）為基準新增
    void defineNewPlaneRequested(const QString& baseRef);
    /// 定義平面：編輯基準／偏移
    void editDefinedPlaneRequested(const QString& itemId);
    /// 定義平面：在此平面建立草圖
    void createSketchOnDefinedPlaneRequested(const QString& itemId);
    /// 零件組合（Assembly）與 Void／Assembly 根：action = insertPart／insertPartVoid／moveToPart／moveToAssembly／moveToVoid／setActivePart／setActiveAssembly／setActiveVoid／addMate／solve／editPart／toggleFixed／syncPart／openSource／
    ///                              addMateForPart／editMate／toggleSuppress；itemId 為零件或配合的 id（資料夾動作為空或資料夾 id）
    void assemblyActionRequested(const QString& action, const QString& itemId);

    // TrackCenterLine context menu actions
    void editAlignmentRequested(const QString& tclId);
    void renameTrackRequested(const QString& tclId);
    void deleteTrackRequested(const QString& tclId);
    void showAlignmentDataTableRequested(const QString& tclId);

    /** 右鍵 TrackCenterLine 節點 →「以 AQT 編輯」，開啟
     *  ALIGNMENTQUICKTABLE 對話框並預先載入此線路現有的關鍵點資料。 */
    void editWithQuickTableRequested(const QString& tclId);

    /**
     * 圖冊／圖紙節點的動作。action：
     *   newbook | open | bookSettings | regen | index | rename | deleteBook | exportPdf |
     *   sheetSettings | clearOverride | regenSheet | deleteSheet
     * itemId：樹節點 id（"book_<id>" / "dsheet_<id>" / "__drawings_folder__"）
     */
    void drawingActionRequested(const QString& action, const QString& itemId);

    /** Step 8：右鍵 AlignedProfileArray 節點 → 顯示唯讀站位資料表。 */
    void showProfileArrayTableRequested(const QString& featureId);
    
private Q_SLOTS:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onCustomContextMenu(const QPoint& pos);
    void showItemContextMenu(QTreeWidgetItem* item, const QPoint& globalPos);
    void collapseRailwayFolder();

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
    void applyDragDropFlags();
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
    std::function<int(const QString&)> m_visibilityResolver;

    class Private;
    Private* d;
};

} // namespace ui
} // namespace aicad

#endif // AICAD_UI_FEATUREBROWSER_H
