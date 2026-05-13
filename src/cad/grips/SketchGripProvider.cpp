// src/cad/grips/SketchGripProvider.cpp
#include "SketchGripProvider.h"
#include "../Plane.h"
#include <QtMath>
#include <QDebug>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_Circle.hxx>

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
        // ── Arc ────────────────────────────────────────────────────────
        else if (geom->type == SketchGeometryType::Arc) {
            const auto* arc = static_cast<const SketchArc*>(geom);
            if (arc->curve.IsNull()) continue;

            // 取得弧的首尾參數以及圓心/半徑
            const Handle(Geom_Circle) circle =
                Handle(Geom_Circle)::DownCast(arc->curve->BasisCurve());
            if (circle.IsNull()) continue;

            double t0 = arc->curve->FirstParameter();
            double t1 = arc->curve->LastParameter();
            double tM = (t0 + t1) * 0.5;

            gp_Pnt gpStart  = arc->curve->Value(t0);
            gp_Pnt gpMid    = arc->curve->Value(tM);
            gp_Pnt gpEnd    = arc->curve->Value(t1);
            gp_Pnt gpCenter = circle->Location();

            // 起點 grip
            {
                GripPoint gp;
                gp.id       = QString("g%1_arcStart").arg(gi);
                gp.position = gpStart;
                gp.type     = GripType::Vertex;
                gp.onDrag   = [this, gi, t0, t1](const gp_Pnt& np, bool) {
                    auto* a = static_cast<SketchArc*>(m_sketch->normalGeometries()[gi]);
                    if (a->curve.IsNull()) return;
                    const Handle(Geom_Circle) c =
                        Handle(Geom_Circle)::DownCast(a->curve->BasisCurve());
                    if (c.IsNull()) return;
                    // 以新點重新計算起始角度，保持終點不變
                    gp_Pnt gpEnd = a->curve->Value(t1);
                    GC_MakeArcOfCircle maker(np, a->curve->Value((t0+t1)*0.5), gpEnd);
                    if (maker.IsDone()) a->curve = maker.Value();
                    m_sketch->rebuildShapesOnly();
                };
                grips.append(gp);
            }

            // 中點 grip（移動整段弧）
            {
                GripPoint gp;
                gp.id       = QString("g%1_arcMid").arg(gi);
                gp.position = gpMid;
                gp.type     = GripType::Midpoint;
                gp.onDrag   = [this, gi, t0, t1](const gp_Pnt& np, bool) {
                    auto* a = static_cast<SketchArc*>(m_sketch->normalGeometries()[gi]);
                    if (a->curve.IsNull()) return;
                    gp_Pnt gpS = a->curve->Value(t0);
                    gp_Pnt gpE = a->curve->Value(t1);
                    GC_MakeArcOfCircle maker(gpS, np, gpE);
                    if (maker.IsDone()) a->curve = maker.Value();
                    m_sketch->rebuildShapesOnly();
                };
                grips.append(gp);
            }

            // 終點 grip
            {
                GripPoint gp;
                gp.id       = QString("g%1_arcEnd").arg(gi);
                gp.position = gpEnd;
                gp.type     = GripType::Vertex;
                gp.onDrag   = [this, gi, t0, t1](const gp_Pnt& np, bool) {
                    auto* a = static_cast<SketchArc*>(m_sketch->normalGeometries()[gi]);
                    if (a->curve.IsNull()) return;
                    gp_Pnt gpS = a->curve->Value(t0);
                    GC_MakeArcOfCircle maker(gpS, a->curve->Value((t0+t1)*0.5), np);
                    if (maker.IsDone()) a->curve = maker.Value();
                    m_sketch->rebuildShapesOnly();
                };
                grips.append(gp);
            }

            // 圓心 grip（平移整段弧）
            {
                GripPoint gp;
                gp.id       = QString("g%1_arcCenter").arg(gi);
                gp.position = gpCenter;
                gp.type     = GripType::Center;
                gp.onDrag   = [this, gi, t0, t1, gpCenter](const gp_Pnt& np, bool) {
                    auto* a = static_cast<SketchArc*>(m_sketch->normalGeometries()[gi]);
                    if (a->curve.IsNull()) return;
                    gp_Vec delta(gpCenter, np);
                    gp_Pnt gpS = a->curve->Value(t0).Translated(delta);
                    gp_Pnt gpM = a->curve->Value((t0+t1)*0.5).Translated(delta);
                    gp_Pnt gpE = a->curve->Value(t1).Translated(delta);
                    GC_MakeArcOfCircle maker(gpS, gpM, gpE);
                    if (maker.IsDone()) a->curve = maker.Value();
                    m_sketch->rebuildShapesOnly();
                };
                grips.append(gp);
            }
        }

        // ── Ellipse ────────────────────────────────────────────────────
        else if (geom->type == SketchGeometryType::Ellipse) {
            const auto* ellipse = static_cast<const SketchEllipse*>(geom);

            // 中心 grip（平移）
            {
                QVector3D cW = plane->toWorld(ellipse->center.x(), ellipse->center.y());
                GripPoint gp;
                gp.id       = QString("g%1_ellCenter").arg(gi);
                gp.position = gp_Pnt(cW.x(), cW.y(), cW.z());
                gp.type     = GripType::Center;
                gp.onDrag   = [this, gi](const gp_Pnt& np, bool) {
                    auto* e = static_cast<SketchEllipse*>(m_sketch->normalGeometries()[gi]);
                    e->center = m_sketch->plane()->toPlane(QVector3D(np.X(), np.Y(), np.Z()));
                    m_sketch->rebuildShapesOnly();
                };
                grips.append(gp);
            }

            // 長軸端點 grip（調整 majorRadius）
            {
                double cosA = qCos(ellipse->angle), sinA = qSin(ellipse->angle);
                QVector2D majPt = ellipse->center +
                                  QVector2D(cosA, sinA) * (float)ellipse->majorRadius;
                QVector3D majW = plane->toWorld(majPt.x(), majPt.y());
                GripPoint gp;
                gp.id       = QString("g%1_ellMajor").arg(gi);
                gp.position = gp_Pnt(majW.x(), majW.y(), majW.z());
                gp.type     = GripType::Quadrant;
                gp.onDrag   = [this, gi](const gp_Pnt& np, bool) {
                    auto* e = static_cast<SketchEllipse*>(m_sketch->normalGeometries()[gi]);
                    QVector2D local = m_sketch->plane()->toPlane(QVector3D(np.X(), np.Y(), np.Z()));
                    e->majorRadius = (local - e->center).length();
                    m_sketch->rebuildShapesOnly();
                };
                grips.append(gp);
            }

            // 短軸端點 grip（調整 minorRadius）
            {
                double cosB = qCos(ellipse->angle + M_PI_2), sinB = qSin(ellipse->angle + M_PI_2);
                QVector2D minPt = ellipse->center +
                                  QVector2D(cosB, sinB) * (float)ellipse->minorRadius;
                QVector3D minW = plane->toWorld(minPt.x(), minPt.y());
                GripPoint gp;
                gp.id       = QString("g%1_ellMinor").arg(gi);
                gp.position = gp_Pnt(minW.x(), minW.y(), minW.z());
                gp.type     = GripType::Quadrant;
                gp.onDrag   = [this, gi](const gp_Pnt& np, bool) {
                    auto* e = static_cast<SketchEllipse*>(m_sketch->normalGeometries()[gi]);
                    QVector2D local = m_sketch->plane()->toPlane(QVector3D(np.X(), np.Y(), np.Z()));
                    e->minorRadius = (local - e->center).length();
                    m_sketch->rebuildShapesOnly();
                };
                grips.append(gp);
            }
        }

        // ── Spline ─────────────────────────────────────────────────────
        else if (geom->type == SketchGeometryType::Spline) {
            // Spline 的 points 就是控制點，直接沿用 Vertex grip 邏輯
            for (int pi = 0; pi < geom->points.size(); ++pi) {
                QVector3D w = plane->toWorld(geom->points[pi].x(), geom->points[pi].y());
                GripPoint gp;
                gp.id       = QString("g%1_sp%2").arg(gi).arg(pi);
                gp.position = gp_Pnt(w.x(), w.y(), w.z());
                gp.type     = GripType::Vertex;
                gp.onDrag   = [this, gi, pi](const gp_Pnt& np, bool) {
                    auto* s = m_sketch->normalGeometries()[gi];
                    s->points[pi] = m_sketch->plane()->toPlane(
                        QVector3D(np.X(), np.Y(), np.Z()));
                    m_sketch->rebuildShapesOnly();
                };
                grips.append(gp);
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
        // ← 新增：備份 Arc 的 curve
        if (geoms[i]->type == SketchGeometryType::Arc) {
            snap.arcCurve = static_cast<SketchArc*>(geoms[i])->curve;
        }
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
        if (snap.geomIndex >= geoms.size()) continue;
        geoms[snap.geomIndex]->points = snap.points;
        // ← 新增：還原 Arc 的 curve
        if (geoms[snap.geomIndex]->type == SketchGeometryType::Arc &&
            !snap.arcCurve.IsNull()) {
            static_cast<SketchArc*>(geoms[snap.geomIndex])->curve = snap.arcCurve;
        }
    }
    m_sketch->rebuildShapesOnly();
}

} // namespace aicad::cad
