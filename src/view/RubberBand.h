/**
 * @file RubberBand.h
 * @brief 橡皮筋視覺回饋系統
 * @author Felicia
 * @date 2024-12-04
 */

#ifndef AICAD_VIEW_RUBBERBAND_H
#define AICAD_VIEW_RUBBERBAND_H

#include <QObject>
#include <QVector2D>
#include <QVector>
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
 * @brief 橡皮筋模式
 */
enum class RubberBandMode {
    None,           ///< 無橡皮筋
    Line,           ///< 直線
    Rectangle,      ///< 矩形
    Polyline,       ///< 折線
    Circle,         ///< 圓
    Arc             ///< 圓弧
};

/**
 * @brief 橡皮筋視覺回饋類別
 * 
 * 在使用者繪圖時提供即時的視覺回饋，顯示當前正在繪製的幾何圖形。
 * 
 * 使用範例:
 * @code
 * RubberBand* rubber = new RubberBand(view, context, plane);
 * rubber->setMode(RubberBandMode::Rectangle);
 * rubber->setBasePoint(QVector2D(0, 0));
 * rubber->updateCurrentPoint(QVector2D(100, 50));
 * rubber->render();
 * @endcode
 */
class RubberBand : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief 建構子
     * @param view OCCT 視圖
     * @param context AIS 上下文
     * @param plane 繪圖平面
     * @param parent 父物件
     */
    explicit RubberBand(Handle(V3d_View) view,
                       Handle(AIS_InteractiveContext) context,
                       const cad::CustomPlane& plane,
                       QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~RubberBand() override;
    
    /**
     * @brief 設定橡皮筋模式
     */
    void setMode(RubberBandMode mode);
    
    /**
     * @brief 取得當前模式
     */
    RubberBandMode mode() const;
    
    /**
     * @brief 設定繪圖平面
     */
    void setPlane(const cad::CustomPlane& plane);
    
    /**
     * @brief 設定基準點 (第一個點)
     */
    void setBasePoint(const QVector2D& point);
    
    /**
     * @brief 取得基準點
     */
    QVector2D basePoint() const;
    
    /**
     * @brief 更新當前滑鼠位置
     */
    void updateCurrentPoint(const QVector2D& point);
    
    /**
     * @brief 新增點 (用於折線模式)
     */
    void addPoint(const QVector2D& point);
    
    /**
     * @brief 取得所有點
     */
    QVector<QVector2D> points() const;
    
    /**
     * @brief 清除橡皮筋
     */
    void clear();
    
    /**
     * @brief 渲染橡皮筋
     * 
     * 根據當前模式和點座標繪製橡皮筋圖形
     */
    void render();
    
    /**
     * @brief 設定橡皮筋顏色
     */
    void setColor(const QColor& color);
    
    /**
     * @brief 設定線寬
     */
    void setLineWidth(double width);
    
    /**
     * @brief 設定線型
     */
    void setLineStyle(int style);  // 0=實線, 1=虛線, 2=點線
    
    /**
     * @brief 是否顯示
     */
    bool isVisible() const;
    
    /**
     * @brief 設定可見性
     */
    void setVisible(bool visible);
    
Q_SIGNALS:
    /**
     * @brief 橡皮筋更新時發出
     */
    void updated();
    
private:
    /**
     * @brief 清除當前顯示
     */
    void clearPresentation();
    
    /**
     * @brief 渲染直線
     */
    void renderLine();
    
    /**
     * @brief 渲染矩形
     */
    void renderRectangle();
    
    /**
     * @brief 渲染折線
     */
    void renderPolyline();
    
    /**
     * @brief 渲染圓
     */
    void renderCircle();
    
    /**
     * @brief 平面座標轉世界座標
     */
    gp_Pnt planeToWorld(const QVector2D& pt) const;
    
    class Private;
    Private* d;
};

} // namespace view
} // namespace aicad

#endif // AICAD_VIEW_RUBBERBAND_H