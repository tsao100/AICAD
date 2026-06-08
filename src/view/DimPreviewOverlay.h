#pragma once
#include <QObject>
#include <QVector2D>
#include <QList>

#include <AIS_InteractiveContext.hxx>
#include <Prs3d_Presentation.hxx>

#include "cad/sketch/SketchConstraint.h"   // GeomRef, ConstraintType, DistanceMode

namespace aicad {
namespace cad { class Sketch; }
namespace view {

/**
 * @brief 尺寸預覽：仿 RubberBand，用 OCCT Prs3d_Presentation 在 3D view 中
 *        即時顯示跟隨滑鼠的尺寸線預覽。完全不使用 Qt widget overlay，
 *        避免 WA_PaintOnScreen native window 的 child widget 繪製問題。
 */
class DimPreviewOverlay : public QObject
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

    explicit DimPreviewOverlay(QObject* parent = nullptr);
    ~DimPreviewOverlay();

    /// 設定 OCCT context（由 CadView 在建構時呼叫）
    void setContext(const Handle(AIS_InteractiveContext)& ctx);

    /// 設定草圖（由 CadView 在 setDimPreview 前呼叫）
    void setSketch(cad::Sketch* sk);

    void setPreview(const PreviewInfo& info);
    void clearPreview();

    /// 滑鼠在草圖平面的座標（由 CadView::mouseMoveEvent 每幀更新）
    void setMousePlanePt(const QVector2D& pt);

    // 兼容舊介面（不再需要，保留空實作避免編譯錯誤）
    void setMouseScreenPt(const QPoint&) {}
    using PlaneToPxFn = std::function<QPoint(const QVector2D&)>;
    void setPlaneToPxFn(PlaneToPxFn) {}
    void show()  {}
    void hide()  {}
    void raise() {}
    void setGeometry(int,int,int,int) {}
    bool isVisible() const { return m_info.valid; }

private:
    void rebuild();   ///< 重新計算並顯示 presentation
    void clearPrs();  ///< 清除 OCCT presentation

    // 草圖平面座標 → world gp_Pnt
    gp_Pnt toWorld(const QVector2D& pt) const;

    // 在 presentation 的 group 中畫線段（batch，一次加入多條）
    void addLine(const Handle(Graphic3d_Group)& grp,
                 const gp_Pnt& a, const gp_Pnt& b);
    void addArrow3D(const Handle(Graphic3d_Group)& grp,
                    const gp_Pnt& tip, const gp_Vec& dir, double size = 4.0);

    Handle(AIS_InteractiveContext) m_context;
    Handle(Prs3d_Presentation)     m_prs;

    cad::Sketch*  m_sketch = nullptr;
    PreviewInfo   m_info;
    QVector2D     m_mouse;   // 草圖平面座標，決定尺寸線偏移方向與距離
    // 暫存 FixedDiameter/Radius 計算結果，供 dA/dB 段使用
    QVector2D     m_dimDir;
    QVector2D     m_dimPerp;
    float         m_dimR      = 0.f;
    QVector2D     m_dimCenter;
};

} // namespace view
} // namespace aicad