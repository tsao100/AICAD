-// src/view/RubberBand.h

#ifndef AICAD_VIEW_RUBBERBAND_H
#define AICAD_VIEW_RUBBERBAND_H

#include <QObject>
#include <QVector2D>
#include <QVector>
#include <V3d_View.hxx>
#include <Prs3d_Presentation.hxx>

namespace aicad {
namespace view {

/**
 * @brief 橡皮筋模式
 */
enum class RubberBandMode {
    None,
    Line,           // 直線
    Rectangle,      // 矩形
    Polyline,       // 折線
    Circle,         // 圓
    Arc             // 圓弧
};

/**
 * @file RubberBand.h
 * @brief 橡皮筋視覺回饋類別
 * @author Felicia
 * @date 2024-12-06
 */

/**
 * @brief 橡皮筋視覺回饋
 * 
 * RubberBand 提供互動式繪圖的即時視覺回饋。
 * 
 * 使用範例:
 * @code
 * RubberBand* rubber = new RubberBand(view);
 * rubber->setMode(RubberBandMode::Rectangle);
 * rubber->setBasePoint(QVector2D(0, 0));
 * rubber->updateCurrentPoint(QVector2D(100, 50));
 * rubber->render();
 * @endcode
 */
class RubberBand : public QObject {
    Q_OBJECT
    
public:
    explicit RubberBand(Handle(V3d_View) view, QObject* parent = nullptr);
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
     * @brief 設定基準點
     */
    void setBasePoint(const QVector2D& point);
    
    /**
     * @brief 取得基準點
     */
    QVector2D basePoint() const;
    
    /**
     * @brief 更新當前點
     */
    void updateCurrentPoint(const QVector2D& point);
    
    /**
     * @brief 取得當前點
     */
    QVector2D currentPoint() const;
    
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
     */
    void render();
    
    /**
     * @brief 設定線條顏色
     */
    void setColor(const QColor& color);
    
    /**
     * @brief 設定線條寬度
     */
    void setLineWidth(double width);
    
    /**
     * @brief 設定線條樣式
     */
    void setLineStyle(Qt::PenStyle style);
    
Q_SIGNALS:
    void pointAdded(const QVector2D& point);
    void cleared();
    
private:
    void renderLine();
    void renderRectangle();
    void renderPolyline();
    void renderCircle();
    void renderArc();
    
    void clearPresentation();
    
    class Private;
    Private* d;
};

} // namespace view
} // namespace aicad

#endif // AICAD_VIEW_RUBBERBAND_H
