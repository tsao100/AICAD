#include "SketchRegion.h"

namespace aicad { namespace cad {

bool SketchRegion::raycast(const QVector2D& pt,
                           const QVector<QVector2D>& poly)
{
    int n = poly.size(), cnt = 0;
    for (int i = 0; i < n; ++i) {
        const QVector2D& a = poly[i];
        const QVector2D& b = poly[(i + 1) % n];
        if (((a.y() <= pt.y()) && (b.y() > pt.y())) ||
            ((b.y() <= pt.y()) && (a.y() > pt.y())))
        {
            float t = (pt.y() - a.y()) / (b.y() - a.y());
            if (pt.x() < a.x() + t * (b.x() - a.x()))
                ++cnt;
        }
    }
    return (cnt % 2) != 0;
}

bool SketchRegion::contains(const QVector2D& pt) const
{
    // 必須在外輪廓內
    if (!raycast(pt, outerLoop.vertices))
        return false;
    // 但不能在任何洞內
    for (const SketchLoop& hole : holes) {
        if (raycast(pt, hole.vertices))
            return false;
    }
    return true;
}

}} // namespace aicad::cad
