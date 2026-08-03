/**
 * @file SketchGeom2DMath.cpp
 * @brief 見 SketchGeom2DMath.h 檔頭說明。
 */
#include "SketchGeom2DMath.h"

#include <cmath>
#include <algorithm>

namespace aicad {
namespace cad {
namespace geom2d {

namespace {
constexpr double kEps = 1e-9;
constexpr double kTwoPi = 2.0 * M_PI;
}

std::optional<QVector2D> lineLineIntersect(const QVector2D& p1, const QVector2D& p2,
                                           const QVector2D& p3, const QVector2D& p4)
{
    const double d1x = double(p2.x()) - double(p1.x());
    const double d1y = double(p2.y()) - double(p1.y());
    const double d2x = double(p4.x()) - double(p3.x());
    const double d2y = double(p4.y()) - double(p3.y());

    const double denom = d1x * d2y - d1y * d2x;
    if (std::abs(denom) < kEps) {
        return std::nullopt;  // 平行或其中一條退化為點
    }

    const double dx = double(p3.x()) - double(p1.x());
    const double dy = double(p3.y()) - double(p1.y());
    const double t = (dx * d2y - dy * d2x) / denom;

    return QVector2D(float(double(p1.x()) + t * d1x), float(double(p1.y()) + t * d1y));
}

QVector<QVector2D> lineCircleIntersect(const QVector2D& lp1, const QVector2D& lp2,
                                       const QVector2D& center, double radius)
{
    QVector<QVector2D> result;

    const double dx = double(lp2.x()) - double(lp1.x());
    const double dy = double(lp2.y()) - double(lp1.y());
    const double a = dx * dx + dy * dy;
    if (a < kEps) return result;  // lp1 == lp2，無法定義直線

    const double fx = double(lp1.x()) - double(center.x());
    const double fy = double(lp1.y()) - double(center.y());

    const double b = 2.0 * (fx * dx + fy * dy);
    const double c = (fx * fx + fy * fy) - radius * radius;

    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < -kEps) return result;  // 不相交

    const double disc = std::max(discriminant, 0.0);
    const double sqrtDisc = std::sqrt(disc);

    const double t1 = (-b - sqrtDisc) / (2.0 * a);
    result.append(QVector2D(float(double(lp1.x()) + t1 * dx), float(double(lp1.y()) + t1 * dy)));

    if (sqrtDisc > kEps) {
        const double t2 = (-b + sqrtDisc) / (2.0 * a);
        result.append(QVector2D(float(double(lp1.x()) + t2 * dx), float(double(lp1.y()) + t2 * dy)));
    }

    return result;
}

QVector<QVector2D> circleCircleIntersect(const QVector2D& c1, double r1,
                                         const QVector2D& c2, double r2)
{
    QVector<QVector2D> result;

    const double dx = double(c2.x()) - double(c1.x());
    const double dy = double(c2.y()) - double(c1.y());
    const double d = std::sqrt(dx * dx + dy * dy);

    if (d < kEps) return result;                      // 同心圓：無交點或無限多交點，皆不處理
    if (d > r1 + r2 + kEps) return result;             // 太遠，不相交
    if (d < std::abs(r1 - r2) - kEps) return result;   // 一圓完全包在另一圓內，不相交

    double a = (r1 * r1 - r2 * r2 + d * d) / (2.0 * d);
    double h2 = r1 * r1 - a * a;
    if (h2 < 0.0) h2 = 0.0;  // 相切邊界情形的浮點誤差防呆
    const double h = std::sqrt(h2);

    const double midX = double(c1.x()) + a * dx / d;
    const double midY = double(c1.y()) + a * dy / d;

    // 垂直於兩圓心連線的單位方向
    const double perpX = -dy / d;
    const double perpY =  dx / d;

    result.append(QVector2D(float(midX + h * perpX), float(midY + h * perpY)));
    if (h > kEps) {
        result.append(QVector2D(float(midX - h * perpX), float(midY - h * perpY)));
    }

    return result;
}

double paramOnLine(const QVector2D& p, const QVector2D& a, const QVector2D& b)
{
    const double dx = double(b.x()) - double(a.x());
    const double dy = double(b.y()) - double(a.y());
    const double len2 = dx * dx + dy * dy;
    if (len2 < kEps) return 0.0;

    const double px = double(p.x()) - double(a.x());
    const double py = double(p.y()) - double(a.y());
    return (px * dx + py * dy) / len2;
}

QVector<QVector2D> lineEllipseIntersect(const QVector2D& lp1, const QVector2D& lp2,
                                        const QVector2D& center,
                                        double majorRadius, double minorRadius,
                                        double angleRad)
{
    QVector<QVector2D> result;
    if (majorRadius < kEps || minorRadius < kEps) return result;

    const double c = std::cos(-angleRad);
    const double s = std::sin(-angleRad);

    // 世界/平面座標 → 橢圓單位圓局部座標：平移到原點 → 旋轉 -angleRad
    // （抵銷橢圓長軸方向）→ 依半長短軸縮放成單位圓。
    auto toLocal = [&](const QVector2D& p) -> QVector2D {
        const double dx = double(p.x()) - double(center.x());
        const double dy = double(p.y()) - double(center.y());
        const double rx = dx * c - dy * s;
        const double ry = dx * s + dy * c;
        return QVector2D(float(rx / majorRadius), float(ry / minorRadius));
    };
    // 局部單位圓座標 → 世界/平面座標（toLocal 的反變換）。
    auto toWorld = [&](const QVector2D& p) -> QVector2D {
        const double sx = double(p.x()) * majorRadius;
        const double sy = double(p.y()) * minorRadius;
        const double cc = std::cos(angleRad);
        const double ss = std::sin(angleRad);
        const double rx = sx * cc - sy * ss;
        const double ry = sx * ss + sy * cc;
        return QVector2D(float(rx + center.x()), float(ry + center.y()));
    };

    const QVector2D localP1 = toLocal(lp1);
    const QVector2D localP2 = toLocal(lp2);

    const QVector<QVector2D> localHits =
        lineCircleIntersect(localP1, localP2, QVector2D(0.0f, 0.0f), 1.0);

    for (const QVector2D& hit : localHits)
        result.append(toWorld(hit));

    return result;
}

double normalizeAngle(double angleRad)
{
    double a = std::fmod(angleRad, kTwoPi);
    if (a < 0.0) a += kTwoPi;
    return a;
}

bool angleInSweep(double angleRad, double startRad, double endRad)
{
    const double a = normalizeAngle(angleRad);
    const double s = normalizeAngle(startRad);
    const double e = normalizeAngle(endRad);

    constexpr double kAngleEps = 1e-7;

    if (s <= e) {
        return a >= s - kAngleEps && a <= e + kAngleEps;
    }
    // 掃描範圍跨越 0/2π（例如 start=350°, end=10°）
    return a >= s - kAngleEps || a <= e + kAngleEps;
}

} // namespace geom2d
} // namespace cad
} // namespace aicad
