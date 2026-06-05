#pragma once
#include <QWidget>
#include <QVector2D>
#include <QList>
#include "cad/sketch/SketchConstraint.h"   // GeomRef, ConstraintType, DistanceMode

namespace aicad {
namespace cad { class Sketch; }
namespace view {

/**
 * @brief 透明 overlay widget，疊在 CadView 上方，負責 QPainter 尺寸線預覽。
 *
 * CadView 本身設定了 WA_PaintOnScreen + paintEngine()→nullptr，
 * 導致 QPainter 無法直接在 CadView 上工作。
 * 此 widget 是 CadView 的 child，完全覆蓋其大小，
 * setAttribute(WA_TransparentForMouseEvents) 讓滑鼠事件穿透到 CadView。
 */
class DimPreviewOverlay : public QWidget
{
    Q_OBJECT
public:
    struct PreviewInfo {
        QList<cad::GeomRef>  refs;
        cad::ConstraintType  type     = cad::ConstraintType::FixedDistance;
        cad::DistanceMode    distMode = cad::DistanceMode::PointToPoint;
        double               value    = 0.0;
        bool                 valid    = false;
    };

    explicit DimPreviewOverlay(QWidget* parent = nullptr);

    void setPreview(const PreviewInfo& info);
    void clearPreview();

    /// 目前滑鼠在草圖平面的座標（由 CadView 在 mouseMoveEvent 更新）
    void setMousePlanePt(const QVector2D& pt);

    /// 草圖平面座標 → 本 widget 像素座標（由 CadView 回呼）
    using PlaneToPxFn = std::function<QPoint(const QVector2D&)>;
    void setPlaneToPxFn(PlaneToPxFn fn);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void drawFixedLength   (QPainter& p);
    void drawFixedDiameter (QPainter& p);
    void drawFixedRadius   (QPainter& p);
    void drawDistance      (QPainter& p);
    void drawAngle         (QPainter& p);
    void drawCoordinate    (QPainter& p);

    QPointF toScr(const QVector2D& pt) const;
    void    drawArrow(QPainter& p, QPointF tip, QPointF dir);
    QVector2D getPos(int i) const;

    PreviewInfo   m_info;
    QVector2D     m_mouse;
    PlaneToPxFn   m_planeToPx;
    cad::Sketch*  m_sketch = nullptr;   // 由 CadView 在 setPreview 前設定

public:
    void setSketch(cad::Sketch* sk) { m_sketch = sk; }
};

} // namespace view
} // namespace aicad
