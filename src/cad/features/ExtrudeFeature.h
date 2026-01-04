#pragma once

#include "Feature.h"

#include <TopoDS_Shape.hxx>

namespace aicad::cad {

/**
 * @brief Extrude feature (Wire/Face → Solid)
 */
class ExtrudeFeature : public Feature
{
public:
    explicit ExtrudeFeature(double height);

    QString name() const override;

    /// Build extruded solid from input shape
    TopoDS_Shape build(const TopoDS_Shape& input) override;

private:
    double m_height;
};

} // namespace aicad::cad
