#pragma once
#include "core/command/Command.h"
#include <vector>

namespace aicad::cad {

class RectangleCommand : public core::Command
{
public:
    QString name() const override { return "rectangle"; }

    void begin() override;
    void mousePress(const QPoint& pos) override;

private:
    std::vector<QPoint> m_points;
};

}
