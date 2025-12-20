// src/view/CadView.h

#ifndef AICAD_VIEW_CADVIEW_H
#define AICAD_VIEW_CADVIEW_H

#include <QWidget>
#include <QPoint>
#include <QVector2D>
#include <QVector3D>

// OCCT headers
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <AIS_InteractiveContext.hxx>
#include <AIS_ViewCube.hxx>

namespace aicad {

namespace cad {
    class Document;
    class Feature;
}

namespace view {

class RubberBand;
class GridOverlay;

/**
 * @brief 視圖方向
 */
enum class ViewOrientation {
    Top,
    Bottom,
    Front,
    Back,
    Left,
    Right,
    Isometric
};

/**
 * @brief 互動模式
 */
enum class InteractionMode {
    None,           // 無互動
    Pan,            // 平移
    Rotate,         // 旋轉
    Zoom,           // 縮放
    SelectPoint,    // 選取點
    SelectEdge,     // 選取邊
    SelectFace,     // 選取面
    Sketching       // 草圖模式
};

/**
 * @file CadView.h
 * @brief 重構後的 3D CAD 視圖
 * @author Felicia
 * @date 2024-12-06
 */

/**
 * @brief 3D CAD 視圖類別
 * 
 * CadView 是顯示 CAD 模型的主要視圖類別。
 * 
 * 主要改進:
 * - 分離 RubberBand 為獨立類別
 * - 分離 GridOverlay 為獨立類別
 * - 簡化事件處理邏輯
 * - 改進座標轉換系統
 * 
 * 使用範例:
 * @code
 * CadView* view = new CadView(parent);
 * view->setDocument(document);
 * view->setOrientation(ViewOrientation::Isometric);
 * view->fitAll();
 * @endcode
 */
class CadView : public QWidget {
    Q_OBJECT
    
public:
    explicit CadView(QWidget* parent = nullptr);
    ~CadView() override;
    
    // === 文件與顯示 ===
    
    /**
     * @brief 設定要顯示的文件
     */
    void setDocument(cad::Document* document);
    
    /**
     * @brief 取得當前文件
     */
    cad::Document* document() const;
    
    /**
     * @brief 顯示特徵
     */
    void displayFeature(cad::Feature* feature);
    
    /**
     * @brief 移除特徵顯示
     */
    void eraseFeature(cad::Feature* feature);
    
    /**
     * @brief 更新特徵顯示
     */
    void updateFeature(cad::Feature* feature);
    
    /**
     * @brief 顯示所有特徵
     */
    void displayAll();
    
    /**
     * @brief 清除所有顯示
     */
    void eraseAll();
    
    // === 視圖控制 ===
    
    /**
     * @brief 適應所有物件到視圖
     */
    void fitAll();
    
    /**
     * @brief 重繪視圖
     */
    void redraw();
    
    /**
     * @brief 設定視圖方向
     */
    void setOrientation(ViewOrientation orientation);
    
    /**
     * @brief 取得當前視圖方向
     */
    ViewOrientation orientation() const;
    
    // === 互動模式 ===
    
    /**
     * @brief 設定互動模式
     */
    void setInteractionMode(InteractionMode mode);
    
    /**
     * @brief 取得當前互動模式
     */
    InteractionMode interactionMode() const;
    
    // === 座標轉換 ===
    
    /**
     * @brief 螢幕座標轉換到工作平面 2D 座標
     * @param screenPos 螢幕座標
     * @return 平面 2D 座標
     */
    QVector2D screenToPlane(const QPoint& screenPos) const;
    
    /**
     * @brief 螢幕座標轉換到 3D 世界座標
     * @param screenPos 螢幕座標
     * @param depth 深度值 (0-1)
     * @return 3D 世界座標
     */
    QVector3D screenToWorld(const QPoint& screenPos, double depth = 0.5) const;
    
    /**
     * @brief 世界座標轉換到螢幕座標
     */
    QPoint worldToScreen(const QVector3D& worldPos) const;
    
    // === OCCT 存取 ===
    
    /**
     * @brief 取得 OCCT 視圖
     */
    Handle(V3d_View) occView() const;
    
    /**
     * @brief 取得 AIS 上下文
     */
    Handle(AIS_InteractiveContext) context() const;
    
    // === 輔助工具 ===
    
    /**
     * @brief 取得橡皮筋
     */
    RubberBand* rubberBand() const;
    
    /**
     * @brief 取得網格覆蓋層
     */
    GridOverlay* gridOverlay() const;
    
    /**
     * @brief 設定是否顯示 ViewCube
     */
    void setViewCubeVisible(bool visible);
    
    /**
     * @brief 設定背景顏色
     */
    void setBackgroundColor(const QColor& color);
    
Q_SIGNALS:
    void pointSelected(const QVector2D& point);
    void selectionChanged();
    void viewChanged();
    void orientationChanged(ViewOrientation orientation);
    void interactionModeChanged(InteractionMode mode);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    
    QPaintEngine* paintEngine() const override { return nullptr; }
    
private:
    void initializeViewer();
    void updateRubberBand(const QPoint& pos);
    void handleViewCubeClick(const QPoint& pos);
    
    // Qt 座標轉 OCCT 座標
    void qtToOcct(const QPoint& qtPos, Standard_Integer& occX, Standard_Integer& occY) const;
    
    class Private;
    Private* d;
};

} // namespace view
} // namespace aicad

#endif // AICAD_VIEW_CADVIEW_H