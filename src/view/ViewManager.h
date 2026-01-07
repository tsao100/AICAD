/**
 * @file ViewManager.h
 * @brief 視圖管理器，管理多個 CAD 視圖
 * @author Felicia
 * @date 2024-12-04
 */

#ifndef AICAD_VIEW_VIEWMANAGER_H
#define AICAD_VIEW_VIEWMANAGER_H

#include <QObject>
#include <QVector>

namespace aicad {

// 前向宣告
namespace cad {
    class Document;
}

namespace view {

class CadView;

/**
 * @brief 視圖管理器
 * 
 * ViewManager 管理應用程式中的所有視圖:
 * - 建立/關閉視圖
 * - 追蹤活動視圖
 * - 管理視圖與文件的關聯
 * - 處理視圖間的同步
 * 
 * 使用範例:
 * @code
 * ViewManager* viewMgr = app->viewManager();
 * 
 * // 建立視圖
 * CadView* view = viewMgr->createView(document);
 * 
 * // 取得活動視圖
 * CadView* active = viewMgr->activeView();
 * @endcode
 */
class ViewManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(int viewCount READ viewCount NOTIFY viewCountChanged)
    
public:
    /**
     * @brief 建構子
     * @param parent 父物件
     */
    explicit ViewManager(QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~ViewManager() override;
    
    /**
     * @brief 建立新視圖
     * @param document 關聯的文件
     * @param parent 視圖的父 Widget
     * @return 新建立的視圖指標
     */
    CadView* createView(cad::Document* document, QWidget* parent = nullptr);
    
    /**
     * @brief 關閉視圖
     * @param view 要關閉的視圖
     * @return 成功回傳 true
     */
    bool closeView(CadView* view);
    
    /**
     * @brief 關閉所有視圖
     * @return 成功回傳 true
     */
    bool closeAll();
    
    /**
     * @brief 取得所有視圖
     */
    QVector<CadView*> views() const;
    
    /**
     * @brief 取得視圖數量
     */
    int viewCount() const;
    
    /**
     * @brief 設定活動視圖
     * @param view 要設為活動的視圖
     */
    void setActiveView(CadView* view);
    
    /**
     * @brief 取得活動視圖
     * @return 當前活動視圖指標，無視圖時為 nullptr
     */
    CadView* activeView() const;
    
    /**
     * @brief 根據文件尋找視圖
     * @param document 文件指標
     * @return 找到的視圖列表
     */
    QVector<CadView*> findViewsByDocument(cad::Document* document) const;
    
    /**
     * @brief 更新所有視圖的顯示
     */
    void refreshAllViews();
    
    /**
     * @brief 同步所有視圖到指定文件
     * @param document 文件指標
     */
    void syncViewsToDocument(cad::Document* document);
    
Q_SIGNALS:
    /**
     * @brief 視圖被建立時發出
     * @param view 視圖指標
     */
    void viewCreated(CadView* view);
    
    /**
     * @brief 視圖被關閉時發出
     * @param view 視圖指標
     */
    void viewClosed(CadView* view);
    
    /**
     * @brief 活動視圖改變時發出
     * @param view 新的活動視圖
     */
    void activeViewChanged(CadView* view);
    
    /**
     * @brief 視圖數量改變時發出
     * @param count 新的視圖數量
     */
    void viewCountChanged(int count);
    
private:
    class Private;
    Private* d;
};

} // namespace view
} // namespace aicad

#endif // AICAD_VIEW_VIEWMANAGER_H