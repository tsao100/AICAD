/**
 * @file RubberBand.h
 * @brief 橡皮筋輔助繪圖類別
 * @author Felicia
 * @date 2024-12-04
 */

#ifndef AICAD_VIEW_RUBBERBAND_H
#define AICAD_VIEW_RUBBERBAND_H

#include <QObject>
#include <QVector2D>
#include <QVector>

#include <Prs3d_Presentation.hxx>
#include <AIS_InteractiveContext.hxx>

namespace aicad {
namespace view {

/**
 * @brief 橡皮筋模式
 */
enum class RubberBandMode {
    None,       ///< 無橡皮筋
    Line,       ///< 線段模式
    Rectangle,  ///< 矩形模式
    Polyline,   ///< 多段線模式
    Circle,     ///< 圓形模式
    Arc         ///< 弧線模式
};

/**
 * @brief 自訂平面結構
 */
struct CustomPlane {
    QVector3D origin;
    QVector3D normal;
    QVector3D uAxis;
    QVector3D vAxis;
    
    static CustomPlane XY();
    static CustomPlane XZ();
    static CustomPlane YZ();
};

/**
 * @brief 橡皮筋類別
 * 
 * RubberBand 提供互動式繪圖輔助:
 * - 顯示動態預覽線條
 * - 支援多種繪圖模式
 * - 管理臨時幾何圖形
 * 
 * 使用範例:
 * @code
 * RubberBand* rubber = new RubberBand(context, view);
 * rubber->setMode(RubberBandMode::Line);
 * rubber->setPlane(CustomPlane::XY());
 * rubber->addPoint(QVector2D(0, 0));
 * rubber->setCurrentPoint(QVector2D(10, 10));
 * rubber->update();
 * @endcode
 */
class RubberBand : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief 建構子
     * @param context OCCT 互動上下文
     * @param parent 父物件
     */
    explicit RubberBand(const Handle(AIS_InteractiveContext)& context, 
                       QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~RubberBand() override;
    
    /**
     * @brief 設定橡皮筋模式
     * @param mode 新的模式
     */
    void setMode(RubberBandMode mode);
    
    /**
     * @brief 取得當前模式
     */
    RubberBandMode mode() const;
    
    /**
     * @brief 設定工作平面
     * @param plane 平面定義
     */
    void setPlane(const CustomPlane& plane);
    
    /**
     * @brief 取得工作平面
     */
    CustomPlane plane() const;
    
    /**
     * @brief 加入點
     * @param point 2D 平面座標
     */
    void addPoint(const QVector2D& point);
    
    /**
     * @brief 設定當前追蹤點
     * @param point 2D 平面座標
     */
    void setCurrentPoint(const QVector2D& point);
    
    /**
     * @brief 取得所有已加入的點
     */
    QVector<QVector2D> points() const;
    
    /**
     * @brief 清除所有點
     */
    void clearPoints();
    
    /**
     * @brief 更新橡皮筋顯示
     */
    void update();
    
    /**
     * @brief 清除橡皮筋顯示
     */
    void clear();
    
    /**
     * @brief 是否有當前點
     */
    bool hasCurrentPoint() const;
    
Q_SIGNALS:
    /**
     * @brief 橡皮筋更新時發出
     */
    void updated();
    
    /**
     * @brief 橡皮筋清除時發出
     */
    void cleared();
    
private:
    /**
     * @brief 更新線段模式
     */
    void updateLine();
    
    /**
     * @brief 更新矩形模式
     */
    void updateRectangle();
    
    /**
     * @brief 更新多段線模式
     */
    void updatePolyline();
    
    /**
     * @brief 更新圓形模式
     */
    void updateCircle();
    
    /**
     * @brief 更新弧線模式
     */
    void updateArc();
    
    /**
     * @brief 平面座標轉世界座標
     */
    QVector3D planeToWorld(const QVector2D& planePt) const;
    
    class Private;
    Private* d;
};

} // namespace view
} // namespace aicad

#endif // AICAD_VIEW_RUBBERBAND_H