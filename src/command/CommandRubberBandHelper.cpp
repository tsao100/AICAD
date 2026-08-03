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
