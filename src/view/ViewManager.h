// src/view/ViewManager.h

#ifndef AICAD_VIEW_VIEWMANAGER_H
#define AICAD_VIEW_VIEWMANAGER_H

#include <QObject>
#include <QVector>
#include "../cad/Document.h"
#include "CadView.h"

namespace aicad {
namespace view {

/**
 * @file ViewManager.h
 * @brief 視圖管理器
 * @author Felicia
 * @date 2024-12-06
 */

/**
 * @brief 管理所有 CAD 視圖的管理器
 * 
 * ViewManager 負責:
 * - 建立和銷毀視圖
 * - 管理活動視圖
 * - 同步視圖與文件
 * - 協調多視圖顯示
 * 
 * 使用範例:
 * @code
 * ViewManager* viewMgr = aicadApp->viewManager();
 * CadView* view = viewMgr->createView();
 * viewMgr->setActiveView(view);
 * @endcode
 */
class ViewManager : public QObject {
    Q_OBJECT
    
public:
    explicit ViewManager(QObject* parent = nullptr);
    ~ViewManager() override;
    
    /**
     * @brief 建立新視圖
     * @param parent 父視窗
     * @return 新建立的視圖指標
     */
    CadView* createView(QWidget* parent = nullptr);
    
    /**
     * @brief 關閉視圖
     * @param view 要關閉的視圖
     */
    void closeView(CadView* view);
    
    /**
     * @brief 關閉所有視圖
     */
    void closeAllViews();
    
    /**
     * @brief 取得所有視圖
     */
    QVector<CadView*> views() const;
    
    /**
     * @brief 取得活動視圖
     */
    CadView* activeView() const;
    
    /**
     * @brief 設定活動視圖
     */
    void setActiveView(CadView* view);
    
    /**
     * @brief 設定活動文件 (所有視圖會顯示此文件)
     */
    void setActiveDocument(cad::Document* document);
    
    /**
     * @brief 取得活動文件
     */
    cad::Document* activeDocument() const;
    
    /**
     * @brief 更新所有視圖
     */
    void updateAllViews();
    
    /**
     * @brief 所有視圖適應顯示
     */
    void fitAllViews();
    
Q_SIGNALS:
    void viewCreated(CadView* view);
    void viewClosed(CadView* view);
    void activeViewChanged(CadView* view);
    void activeDocumentChanged(cad::Document* document);
    
private Q_SLOTS:
    void onViewDestroyed(QObject* obj);
    
private:
    class Private;
    Private* d;
};

} // namespace view
} // namespace aicad

#endif // AICAD_VIEW_VIEWMANAGER_H