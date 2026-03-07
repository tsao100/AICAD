/**
 * @file ViewGrid.h
 * @brief 視圖網格類別 (OCCT built-in grid)
 * @author Felicia
 * @date 2024-12-04
 */

#ifndef AICAD_VIEW_VIEWGRID_H
#define AICAD_VIEW_VIEWGRID_H

#include <QObject>

#include <V3d_Viewer.hxx>
#include <Aspect_GridType.hxx>
#include <Aspect_GridDrawMode.hxx>

namespace aicad {
namespace cad { class Plane; }

namespace view {

enum class GridStyle {
    Lines,      ///< 線條網格
    Dots,       ///< 點狀網格
};

class ViewGrid : public QObject {
    Q_OBJECT

public:
    explicit ViewGrid(const Handle(V3d_Viewer)& viewer, QObject* parent = nullptr);
    ~ViewGrid() override;

    void setPlane(cad::Plane* plane);
    cad::Plane* plane() const;

    void setSpacing(float spacing);
    float spacing() const;

    void setStyle(GridStyle style);
    GridStyle style() const;

    void setGridColor(float r, float g, float b);
    void setTenthColor(float r, float g, float b);  ///< 每10格的強調色

    void show();
    void hide();
    bool isVisible() const;

    void update();

Q_SIGNALS:
    void visibilityChanged(bool visible);

private:
    void applyPrivilegedPlane();

    class Private;
    Private* d;
};

} // namespace view
} // namespace aicad

#endif // AICAD_VIEW_VIEWGRID_H
