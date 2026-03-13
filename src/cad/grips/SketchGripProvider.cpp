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
    const auto& geoms = m_sketch->geometries();


    for (int gi = 0; gi < geoms.size(); ++gi) {

        // ✅ Skip if not in selection (empty set = include all)
        if (!m_geomIndices.isEmpty() && !m_geomIndices.contains(gi))
            continue;

        const SketchGeometry* geom = geoms[gi];
        if (!geom) continue;

        // ── Polyline / Line: Vertex grips + Midpoint grips ─────
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

                // 閉合拖拉 lambda
                gp.onDrag = [this, gi, pi](const gp_Pnt& np, bool /*snapped*/) {
                    auto& pts = m_sketch->geometries()[gi]->points;
                    QVector2D local = m_sketch->plane()->toPlane(
                        QVector3D(np.X(), np.Y(), np.Z()));
                    pts[pi] = local;
                    m_sketch->rebuild();
                };

                grips.append(gp);
            }

            // 中點 grip（移動整條邊）
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

                grips.append(gp);
            }
        }

        // ── Circle: center + 4 quadrant grips ──────────────────
        else if (geom->type == SketchGeometryType::Circle) {
            const auto* circle = static_cast<const SketchCircle*>(geom);

            // Center grip → Move
            QVector3D cW = plane->toWorld(circle->center.x(),
                                          circle->center.y());
            GripPoint cGrip;
            cGrip.id       = QString("g%1_center").arg(gi);
            cGrip.position = gp_Pnt(cW.x(), cW.y(), cW.z());
            cGrip.type     = GripType::Center;
            cGrip.onDrag   = [this, gi](const gp_Pnt& np, bool) {
                auto* c = static_cast<SketchCircle*>(
                    m_sketch->geometries()[gi]);
                // c->center = m_sketch->plane()->toPlane(
                //     QVector3D(np.X(), np.Y(), np.Z()));
                m_sketch->rebuild();
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
                        m_sketch->geometries()[gi]);
                    QVector2D local = m_sketch->plane()->toPlane(
                        QVector3D(np.X(), np.Y(), np.Z()));
                    c->radius = (local - c->center).length();
                    m_sketch->rebuild();
                };
                grips.append(qGrip);
            }
        }
    }

    return grips;
}

void SketchGripProvider::onGripDragBegin(const QString& /*gripId*/)
{
    // 快照：記錄所有 geometry 點以供 Undo
    m_snapshots.clear();
    for (const SketchGeometry* g : m_sketch->geometries()) {
        for (int i = 0; i < g->points.size(); ++i) {
            m_snapshots[QString("pt_%1_%2").arg((quintptr)g).arg(i)] = g->points[i];
        }
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
    qDebug() << "[SketchGripProvider] Grip committed:"
             << gripId
             << "from" << startPos.X() << startPos.Y() << startPos.Z()
             << "to"   << endPos.X()   << endPos.Y()   << endPos.Z();
    // TODO: 推入 UndoStack（GripMoveCommand）
}

} // namespace aicad::cad
