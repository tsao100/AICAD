#include "RectangleCommand.h"
#include <QDebug>

using namespace aicad::cad;

void RectangleCommand::begin()
{
    m_points.clear();
    qDebug() << "Rectangle command started";
}

void RectangleCommand::mousePress(const QPoint& pos)
{
    m_points.push_back(pos);

    if (m_points.size() == 2)
    {
        qDebug() << "Create rectangle:"
                 << m_points[0] << m_points[1];

        // TODO:
        // 1. 建立 Sketch / Edge
        // 2. 發送 FEATURE_CREATED 事件

        // 結束命令（之後由 CommandManager 呼叫）
    }
}
