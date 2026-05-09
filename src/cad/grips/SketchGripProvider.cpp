// src/cad/grips/SketchGripProvider.cpp
#include "SketchGripProvider.h"
#include "../Plane.h"
#include <QDebug>

namespace aicad::cad {

SketchGripProvider::SketchGripProvider(Sketch* sketch,
                                       const QSet<int>& geomIndices)
    : m_sketch(sketch)
    , m_geomIndices(geomIndices)
{}

QVector<GripPoint> SketchGripProvider::computeGrips() const
{
    QVector<GripPoint> grips;
    if (!m_sketch || !m_sketch->plane()) return grips;

    Plane* plane = m_sketch->plane();

    // ✅ 用 normalGeometries() 讓 gi 與 aisToGeomIndex 對齊
    const auto geoms = m_sketch->normalGeometries();

    for (int gi = 0; gi < geoms.size(); ++gi) {

        if (!m_geomIndices.isEmpty() && !m_geomIndices.contains(gi))
            continue;

        const SketchGeometry* geom = geoms[gi];
        if (!geom) continue;

        // ── Line ───────────────────────────────────────────────────────
        if (geom->type == SketchGeometryType::Line ||
            geom->type == SketchGeometryType::Polyline)
        {
            for (int pi = 0; pi < geom->points.size(); ++pi) {
                QVector3D w = plane->toWorld(geom->points[pi].x(),
                                             geom->points[pi].y());
                GripPoint gp;
                gp.id       = QString("g%1_v%2").arg(gi).arg(pi);
                gp.position = gp_Pnt(w.x(), w.y(), w.z());
                gp.type     = GripType::Vertex;
                gp.onDrag   = [this, gi, pi](const gp_Pnt& np, bool) {
                    // ✅ 用 normalGeometries() 保持索引一致
                    auto* g = m_sketch->normalGeometries()[gi];
                    g->points[pi] = m_sketch->plane()->toPlane(
                        QVector3D(np.X(), np.Y(), np.Z()));
                    // ✅ 輕量更新：僅重建 shape，不走 Document 路徑
                    m_sketch->rebuildShapesOnly();
                };
                grips.append(gp);
            }

            // 中點 grip
            int segments = (geom->type == SketchGeometryType::Polyline &&
                            static_cast<const SketchPolyline*>(geom)->closed)
                               ? geom->points.size()
                               : geom->points.size() - 1;

            for (int si = 0; si < segments; ++si) {
                int ni = (si + 1) % geom->points.size();
                QVector2D mid2d = (geom->points[si] + geom->points[ni]) * 0.5f;
                QVector3D midW  = plane->toWorld(mid2d.x(), mid2d.y());

                GripPoint gp;
                gp.id       = QString("g%1_mid%2").arg(gi).arg(si);
                gp.position = gp_Pnt(midW.x(), midW.y(), midW.z());
                gp.type     = GripType::Midpoint;
                gp.onDrag   = [this, gi, si](const gp_Pnt& np, bool) {
                    auto* g = m_sketch->normalGeometries()[gi];
                    int   ni = (si + 1) % g->points.size();
                    QVector2D newMid = m_sketch->plane()->toPlane(
                        QVector3D(np.X(), np.Y(), np.Z()));
                    QVector2D delta = newMid -
                                      (g->points[si] + g->points[ni]) * 0.5f;
                    g->points[si] += delta;
                    g->points[ni] += delta;
                    m_sketch->rebuildShapesOnly();
                };
                grips.append(gp);
            }
        }

        // ── Circle ────────────────────────────────────────────────────
        else if (geom->type == SketchGeometryType::Circle) {
            const auto* circle = static_cast<const SketchCircle*>(geom);

            // Center grip → Move
            QVector3D cW = plane->toWorld(circle->center.x(), circle->center.y());
            GripPoint cGrip;
            cGrip.id       = QString("g%1_center").arg(gi);
            cGrip.position = gp_Pnt(cW.x(), cW.y(), cW.z());
            cGrip.type     = GripType::Center;
            cGrip.onDrag   = [this, gi](const gp_Pnt& np, bool) {
                auto* c = static_cast<SketchCircle*>(
                    m_sketch->normalGeometries()[gi]);
                // ✅ 取消 comment，實際更新 center
                c->center = m_sketch->plane()->toPlane(
                    QVector3D(np.X(), np.Y(), np.Z()));
                m_sketch->rebuildShapesOnly();
            };
            grips.append(cGrip);

            // Quadrant grips → Radius resize
            const double r = circle->radius;
            for (int q = 0; q < 4; ++q) {
                double angle = q * M_PI * 0.5;
                QVector2D qPt = circle->center +
                                QVector2D(r * std::cos(angle), r * std::sin(angle));
                QVector3D qW = plane->toWorld(qPt.x(), qPt.y());

                GripPoint qGrip;
                qGrip.id       = QString("g%1_quad%2").arg(gi).arg(q);
                qGrip.position = gp_Pnt(qW.x(), qW.y(), qW.z());
                qGrip.type     = GripType::Quadrant;
                qGrip.onDrag   = [this, gi](const gp_Pnt& np, bool) {
                    auto* c = static_cast<SketchCircle*>(
                        m_sketch->normalGeometries()[gi]);
                    QVector2D local = m_sketch->plane()->toPlane(
                        QVector3D(np.X(), np.Y(), np.Z()));
                    c->radius = (local - c->center).length();
                    m_sketch->rebuildShapesOnly();
                };
                grips.append(qGrip);
            }
        }
    }

    return grips;
}

void SketchGripProvider::onGripDragBegin(const QString& /*gripId*/)
{
    m_snapshots.clear();
    const auto geoms = m_sketch->normalGeometries();
    for (int i = 0; i < geoms.size(); ++i) {
        if (!m_geomIndices.isEmpty() && !m_geomIndices.contains(i)) continue;
        GeomSnapshot snap;
        snap.geomIndex = i;
        snap.points    = geoms[i]->points;   // deep copy
        m_snapshots.append(snap);
    }
}

void SketchGripProvider::onGripDrag(const QString& gripId, const gp_Pnt& newPos)
{
    // 對應 grip 的 onDrag 已在 computeGrips() 內綁定
    Q_UNUSED(gripId); Q_UNUSED(newPos);
}

void SketchGripProvider::onGripDragEnd(const QString& gripId,
                                       const gp_Pnt& startPos,
                                       const gp_Pnt& endPos)
{
    Q_UNUSED(startPos)
    // cancel 時 startPos == endPos，不做任何事
    if (startPos.Distance(endPos) < Precision::Confusion()) {
        qDebug() << "[SketchGripProvider] Cancelled (no movement):" << gripId;
        return;
    }

    qDebug() << "[SketchGripProvider] Grip committed:" << gripId
             << "to" << endPos.X() << endPos.Y() << endPos.Z();

    // ✅ 拖動結束：重新求解約束，然後走完整 Document 路徑更新
    if (m_sketch) {
        m_sketch->solveConstraints();  // 套用現有約束
        Q_EMIT m_sketch->rebuildRequested();  // 通知 Document 更新
    }
}

void SketchGripProvider::restoreSnapshot()
{
    const auto geoms = m_sketch->normalGeometries();
    for (const GeomSnapshot& snap : m_snapshots) {
        if (snap.geomIndex < geoms.size())
            geoms[snap.geomIndex]->points = snap.points;
    }
    m_sketch->rebuildShapesOnly();
}

} // namespace aicad::cad
