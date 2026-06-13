/**
 * @file RubberBand.h
 * @brief 橡皮筋輔助繪圖類別
 * @author Felicia
 * @date 2024-12-04
 */

#ifndef AICAD_VIEW_RUBBERBAND_H
#define AICAD_VIEW_RUBBERBAND_H

#include <QObject>
#include <QPointF>
#include <QVector2D>
#include <QVector3D>
#include <QVector>

#include <Prs3d_Presentation.hxx>
#include <AIS_InteractiveContext.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>

#include "railway/AlignmentDocument.h"   // SpiralType

namespace aicad {
// 前向宣告
namespace cad {
    class Plane;
}
namespace view {

/**
 * @brief 橡皮筋模式
 */
enum class RubberBandMode {
    None,       ///< 無橡皮筋
    Line,       ///< 線段模式
    Rectangle,  ///< 矩形模式
    Polyline,   ///< 多段線模式
    Polygon,    ///< 正多邊形模式
    Circle,     ///< 圓形模式
    Ellipse,    ///< 橢圓形模式
    Arc,        ///< 弧線模式
    Spline,     ///< 樣條線模式
    Spiral,     ///< 緩和曲線（單條 Clothoid）預覽
    SCS         ///< 螺旋–圓弧–螺旋完整預覽

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
 * rubber->addPoint(QPointF(0.0, 0.0));          // double，支援 TM2 大座標
 * rubber->setCurrentPoint(QPointF(10.0, 10.0));
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
    void setPlane(cad::Plane* plane);
    
    /**
     * @brief 取得工作平面
     */
    cad::Plane* plane() const;
    
    /**
     * @brief 加入點（Alignment 模式傳入 QPointF double；Sketch 模式可傳 QVector2D 由隱式轉換處理）
     * @param point 2D 平面座標（double 精度，支援 TM2 大座標）
     */
    void addPoint(const QPointF& point);

    /**
     * @brief 設定當前追蹤點（滑鼠游標位置）
     * @param point 2D 平面座標（double 精度）
     */
    void setCurrentPoint(const QPointF& point);

    /**
     * @brief 取得所有已加入的點（double 精度）
     */
    QVector<QPointF> points() const;
    
    /**
     * @brief 清除所有點
     */
    void clearPoints();

    /**
     * @brief 設定圓弧半徑（Spiral / SCS 模式使用）
     * @param r 半徑 [m]，正值 = 右彎，負值 = 左彎
     */
    void setRadius(double r);

    /**
     * @brief 取得圓弧半徑
     */
    double radius() const;

    /**
     * @brief 設定緩和曲線長度（Spiral / SCS 模式使用，同時設定 L1 與 L2）
     * @param ls 緩和曲線長度 [m]
     */
    void setSpiralLength(double ls);

    /**
     * @brief 取得緩和曲線長度（回傳 spiralLength1）
     */
    double spiralLength() const;

    /**
     * @brief 設定入螺旋長度 L1（SCS 模式）
     * @param ls 入螺旋長度 [m]；0 表示無入螺旋
     */
    void setSpiralLength1(double ls);

    /**
     * @brief 取得入螺旋長度 L1
     */
    double spiralLength1() const;

    /**
     * @brief 設定出螺旋長度 L2（SCS 模式）
     * @param ls 出螺旋長度 [m]；0 表示無出螺旋
     */
    void setSpiralLength2(double ls);

    /**
     * @brief 取得出螺旋長度 L2
     */
    double spiralLength2() const;

    /**
     * @brief 設定入螺旋類型（Spiral / SCS 模式）
     *
     * 控制 updateSpiral() 和 updateSCS() 入螺旋段使用的數學模型：
     * Clothoid（預設）、HalfSine、Parabola、CubicJPN、CubicECI。
     * 類型改變後需呼叫 update() 才會重新繪製。
     */
    void setSpiralType1(railway::SpiralType type);

    /**
     * @brief 取得入螺旋類型
     */
    railway::SpiralType spiralType1() const;

    /**
     * @brief 設定出螺旋類型（SCS 模式）
     *
     * 控制 updateSCS() 出螺旋段使用的數學模型。
     * 類型改變後需呼叫 update() 才會重新繪製。
     */
    void setSpiralType2(railway::SpiralType type);

    /**
     * @brief 取得出螺旋類型
     */
    railway::SpiralType spiralType2() const;

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
     * @brief 更新多段線模式
     */
    void updatePolygon();

    /**
     * @brief 更新圓形模式
     */
    void updateCircle();

    /**
     * @brief 更新雲形線模式
     */
    void updateSpline();

    void createAndDisplayPresentation(
        const Handle(Graphic3d_ArrayOfPolylines)& polyline);

    /**
     * @brief 更新橢圓形模式
     */
    void updateEllipse();
    
    /**
     * @brief 更新弧線模式
     */
    void updateArc();

    /**
     * @brief 更新緩和曲線（Clothoid）預覽 — 60 點虛線
     *
     * 點配置：
     *   points[0] = 切線起點（平面座標）
     *   points[1] = 切線方向點（可省略，省略時使用 currentPoint 作方向）
     *   currentPoint = 游標位置（僅在 points 只有 1 個時作方向參考）
     */
    void updateSpiral();

    /**
     * @brief 更新 SCS（入螺旋 / 圓弧 / 出螺旋）三段預覽
     *
     * 點配置：
     *   points[0] = 入切線起點
     *   points[1] = 交叉點 PI（入出切線交會點）
     *   currentPoint = 出切線方向點
     */
    void updateSCS();
    
    /**
     * @brief 平面座標轉世界座標（double 輸入，float QVector3D 輸出用於 OCCT 渲染）
     */
    QVector3D planeToWorld(const QPointF& planePt) const;
    
    class Private;
    Private* d;
};

} // namespace view
} // namespace aicad

#endif // AICAD_VIEW_RUBBERBAND_H