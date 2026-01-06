/**
 * @file GridOverlay.h
 * @brief 網格覆蓋層系統
 * @author Felicia
 * @date 2024-12-04
 */

#ifndef AICAD_VIEW_GRIDOVERLAY_H
#define AICAD_VIEW_GRIDOVERLAY_H

#include <QObject>
#include <QColor>
#include <V3d_View.hxx>
#include <Prs3d_Presentation.hxx>
#include <AIS_InteractiveContext.hxx>

namespace aicad {

// 前向宣告
namespace cad {
    struct CustomPlane;
}

namespace view {

/**
 * @brief 網格覆蓋層類別
 * 
 * 在草圖平面上顯示輔助網格，包括:
 * - 主網格線
 * - 副網格線
 * - X/Y 軸線 (不同顏色)
 * - 原點標記
 * 
 * 使用範例:
 * @code
 * GridOverlay* grid = new GridOverlay(view, context);
 * grid->setPlane(CustomPlane::XY());
 * grid->setGridSize(20);
 * grid->setGridSpacing(10.0f);
 * grid->show();
 * @endcode
 */
class GridOverlay : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY visibilityChanged)
    Q_PROPERTY(int gridSize READ gridSize WRITE setGridSize)
    Q_PROPERTY(float gridSpacing READ gridSpacing WRITE setGridSpacing)
    
public:
    /**
     * @brief 建構子
     * @param view OCCT 視圖
     * @param context AIS 上下文
     * @param parent 父物件
     */
    explicit GridOverlay(Handle(V3d_View) view,
                        Handle(AIS_InteractiveContext) context,
                        QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~GridOverlay() override;
    
    /**
     * @brief 設定網格平面
     */
    void setPlane(const cad::CustomPlane& plane);
    
    /**
     * @brief 取得網格平面
     */
    cad::CustomPlane plane() const;
    
    /**
     * @brief 設定網格大小 (線數)
     * @param size 網格線數 (預設 20)
     */
    void setGridSize(int size);
    
    /**
     * @brief 取得網格大小
     */
    int gridSize() const;
    
    /**
     * @brief 設定網格間距
     * @param spacing 間距 (預設 10.0)
     */
    void setGridSpacing(float spacing);
    
    /**
     * @brief 取得網格間距
     */
    float gridSpacing() const;
    
    /**
     * @brief 設定主網格顏色
     */
    void setMajorGridColor(const QColor& color);
    
    /**
     * @brief 設定副網格顏色
     */
    void setMinorGridColor(const QColor& color);
    
    /**
     * @brief 設定 X 軸顏色
     */
    void setXAxisColor(const QColor& color);
    
    /**
     * @brief 設定 Y 軸顏色
     */
    void setYAxisColor(const QColor& color);
    
    /**
     * @brief 顯示網格
     */
    void show();
    
    /**
     * @brief 隱藏網格
     */
    void hide();
    
    /**
     * @brief 清除網格
     */
    void clear();
    
    /**
     * @brief 更新網格顯示
     */
    void update();
    
    /**
     * @brief 檢查是否可見
     */
    bool isVisible() const;
    
    /**
     * @brief 設定可見性
     */
    void setVisible(bool visible);
    
    /**
     * @brief 啟用/停用主網格
     */
    void setMajorGridEnabled(bool enabled);
    
    /**
     * @brief 啟用/停用副網格
     */
    void setMinorGridEnabled(bool enabled);
    
    /**
     * @brief 啟用/停用軸線
     */
    void setAxesEnabled(bool enabled);
    
Q_SIGNALS:
    /**
     * @brief 可見性改變時發出
     */
    void visibilityChanged(bool visible);
    
    /**
     * @brief 網格更新時發出
     */
    void updated();
    
private:
    /**
     * @brief 建立網格顯示
     */
    void createGrid();
    
    /**
     * @brief 清除顯示
     */
    void clearPresentation();
    
    class Private;
    Private* d;
};

} // namespace view
} // namespace aicad

#endif // AICAD_VIEW_GRIDOVERLAY_H