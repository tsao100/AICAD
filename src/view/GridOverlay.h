// src/view/GridOverlay.h

#ifndef AICAD_VIEW_GRIDOVERLAY_H
#define AICAD_VIEW_GRIDOVERLAY_H

#include <QObject>
#include <QVector3D>
#include <V3d_View.hxx>
#include <Prs3d_Presentation.hxx>

namespace aicad {
namespace view {

/**
 * @file GridOverlay.h
 * @brief 網格覆蓋層類別
 * @author Felicia
 * @date 2024-12-06
 */

/**
 * @brief 網格覆蓋層
 * 
 * GridOverlay 在視圖中顯示工作平面網格。
 * 
 * 使用範例:
 * @code
 * GridOverlay* grid = new GridOverlay(view);
 * grid->setOrigin(QVector3D(0, 0, 0));
 * grid->setSize(20, 10.0);
 * grid->show();
 * @endcode
 */
class GridOverlay : public QObject {
    Q_OBJECT
    
public:
    explicit GridOverlay(Handle(V3d_View) view, QObject* parent = nullptr);
    ~GridOverlay() override;
    
    /**
     * @brief 設定網格原點
     */
    void setOrigin(const QVector3D& origin);
    
    /**
     * @brief 設定網格大小
     * @param gridCount 網格線數量
     * @param gridSpacing 網格間距
     */
    void setSize(int gridCount, double gridSpacing);
    
    /**
     * @brief 設定網格平面
     * @param normal 平面法向量
     * @param uAxis U 軸方向
     * @param vAxis V 軸方向
     */
    void setPlane(const QVector3D& normal, 
                  const QVector3D& uAxis,
                  const QVector3D& vAxis);
    
    /**
     * @brief 顯示網格
     */
    void show();
    
    /**
     * @brief 隱藏網格
     */
    void hide();
    
    /**
     * @brief 網格是否可見
     */
    bool isVisible() const;
    
    /**
     * @brief 設定網格顏色
     */
    void setColor(const QColor& color);
    
    /**
     * @brief 設定軸線顏色
     */
    void setAxisColors(const QColor& xColor, const QColor& yColor);
    
private:
    void render();
    void clearPresentation();
    
    class Private;
    Private* d;
};

} // namespace view
} // namespace aicad

#endif // AICAD_VIEW_GRIDOVERLAY_H
