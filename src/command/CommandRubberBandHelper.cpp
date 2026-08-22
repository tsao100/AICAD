/**
 * @file CommandRubberBandHelper.cpp
 * @brief 見 CommandRubberBandHelper.h 檔頭說明。
 */
#include "CommandRubberBandHelper.h"

#include "../core/Application.h"
#include "../cad/Sketch.h"
#include "../ui/UIManager.h"
#include "../view/CadView.h"
#include "../view/RubberBand.h"

namespace aicad {
namespace command {
namespace rb {

namespace {
view::RubberBand* getRubberBand()
{
    auto* app = core::Application::instance();
    auto* uiMgr = app ? app->uiManager() : nullptr;
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    return cadView ? cadView->rubberBand() : nullptr;
}
} // namespace

void armLinePreview(cad::Sketch* sketch, const QVector2D& anchor)
{
    view::RubberBand* band = getRubberBand();
    if (!band || !sketch) return;

    band->setPlane(sketch->plane());
    band->setMode(view::RubberBandMode::Line);
    band->clearPoints();
    band->addPoint(QPointF(anchor.x(), anchor.y()));
}

void armRectPreview(cad::Sketch* sketch, const QVector2D& corner1)
{
    view::RubberBand* band = getRubberBand();
    if (!band || !sketch) return;

    band->setPlane(sketch->plane());
    band->setMode(view::RubberBandMode::Rectangle);
    band->clearPoints();
    band->addPoint(QPointF(corner1.x(), corner1.y()));
}

void showPolylinePreview(cad::Sketch* sketch, const QVector<QVector2D>& points)
{
    view::RubberBand* band = getRubberBand();
    if (!band || !sketch || points.size() < 2) return;

    band->setPlane(sketch->plane());
    band->setMode(view::RubberBandMode::Polyline);
    band->clearPoints();
    // RubberBand::updatePolyline() 固定把 currentPoint 當最後一段的終點
    // （見 RubberBand.cpp），其餘點依序用 addPoint()——這裡沒有「跟隨
    // 游標的下一點」，所以直接把最後一個算好的座標當 currentPoint。
    for (int i = 0; i + 1 < points.size(); ++i)
        band->addPoint(QPointF(points[i].x(), points[i].y()));
    band->setCurrentPoint(QPointF(points.last().x(), points.last().y()));
    band->update();
}

void disarm()
{
    view::RubberBand* band = getRubberBand();
    if (!band) return;

    band->clearPoints();
    band->setMode(view::RubberBandMode::None);
    band->clear();
}

} // namespace rb
} // namespace command
} // namespace aicad
