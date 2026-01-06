#pragma once

#include <QString>
#include <TopoDS_Shape.hxx>

namespace aicad::cad {

/**
 * @brief CAD Feature base class
 *        Represents one modeling history step.
 */
class Feature
{
public:
    virtual ~Feature() = default;

    /// Feature display name
    virtual QString name() const = 0;

    /**
     * @brief Build shape from input shape
     * @param input Previous feature result (may be null)
     * @return Resulting shape
     */
    virtual TopoDS_Shape build(const TopoDS_Shape& input) = 0;
};

} // namespace aicad::cad
