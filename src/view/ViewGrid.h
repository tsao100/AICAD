/**
 * @file ViewGrid.h
 * @brief 視圖網格類別
 * @author Felicia
 * @date 2024-12-04
 */

#ifndef AICAD_VIEW_VIEWGRID_H
#define AICAD_VIEW_VIEWGRID_H

#include <QObject>
#include <QVector3D>

#include <Prs3d_Presentation.hxx>
#include <AIS_InteractiveContext.hxx>

namespace aicad {

// 前向宣告
namespace cad {
    class Plane;
}

namespace view {

struct CustomPlane;

/**
 * @brief 網格樣式
 */
enum class GridStyle {
    Lines,      ///< 線條網格
    Dots,       ///< 點狀網格
    Crosses     ///< 十字網格
};

/**
 * @brief 視圖網格類別
 * 
 * ViewGrid 提供網格顯示功能:
 * - 顯示工作平面網格
 * - 顯示座標軸
 * - 可自訂網格大小和間距
 * 
 * 使用範例:
 * @code
 * ViewGrid* grid = new ViewGrid(context);
 * grid->setPlane(CustomPlane::XY());
 * grid->setSize(20);
 * grid->setSpacing(10.0f);
 * grid->show();
 * @endcode
 */
class ViewGrid : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief 建構子
     * @param context OCCT 互動上下文
     * @param parent 父物件
     */
    explicit ViewGrid(const Handle(AIS_InteractiveContext)& context,
                     QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~ViewGrid() override;
    
    /**
     * @brief 設定工作平面
     * @param plane 平面定義
     */
    void setPlane(cad::Plane* plane);
    
    /**
     * @brief 取得工作平面
     */
    cad::Plane* plane() const;
    
    /**
     * @brief 設定網格大小 (格線數量)
     * @param size 格線數量
     */
    void setSize(int size);
    
    /**
     * @brief 取得網格大小
     */
    int size() const;
    
    /**
     * @brief 設定網格間距
     * @param spacing 間距值
     */
    void setSpacing(float spacing);
    
    /**
     * @brief 取得網格間距
     */
    float spacing() const;
    
    /**
     * @brief 設定網格樣式
     * @param style 網格樣式
     */
    void setStyle(GridStyle style);
    
    /**
     * @brief 取得網格樣式
     */
    GridStyle style() const;
    
    /**
     * @brief 設定是否顯示座標軸
     * @param show 是否顯示
     */
    void setShowAxes(bool show);
    
    /**
     * @brief 檢查是否顯示座標軸
     */
    bool showAxes() const;
    
    /**
     * @brief 設定網格顏色
     * @param r 紅色分量 (0-1)
     * @param g 綠色分量 (0-1)
     * @param b 藍色分量 (0-1)
     */
    void setGridColor(float r, float g, float b);
    
    /**
     * @brief 顯示網格
     */
    void show();
    
    /**
     * @brief 隱藏網格
     */
    void hide();
    
    /**
     * @brief 檢查是否可見
     */
    bool isVisible() const;
    
    /**
     * @brief 更新網格顯示
     */
    void update();
    
Q_SIGNALS:
    /**
     * @brief 可見性改變時發出
     * @param visible 是否可見
     */
    void visibilityChanged(bool visible);
    
private:
    /**
     * @brief 建立網格幾何
     */
    void createGridGeometry();
    
    /**
     * @brief 建立座標軸
     */
    void createAxes();
    
    /**
     * @brief 清除顯示
     */
    void clear();
    
    class Private;
    Private* d;
};

} // namespace view
} // namespace aicad

#endif // AICAD_VIEW_VIEWGRID_H
