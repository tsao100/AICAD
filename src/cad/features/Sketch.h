#pragma once

#include "Feature.h"

#include <QVector>
#include <gp_Pnt2d.hxx>

namespace aicad::cad {

/**
 * @brief 2D line primitive inside a sketch
 */
struct SketchLine
{
    gp_Pnt2d p1;
    gp_Pnt2d p2;
};

/**
 * @brief Sketch feature (2D profile)
 */
class Sketch : public Feature
{
public:
    Sketch();

    QString name() const override;

    /// Add a 2D line to sketch
    void addLine(const gp_Pnt2d& p1, const gp_Pnt2d& p2);

    /// Build sketch shape (wire)
    TopoDS_Shape build(const TopoDS_Shape& input) override;

private:
    QVector<SketchLine> m_lines;
};

} // namespace aicad::cad
