// src/cad/grips/SketchGripProvider.cpp
#include "SketchGripProvider.h"
#include "../Plane.h"
#include "../sketch/ConstraintOverlayManager.h"
#include "../sketch/DimensionLineAIS.h"
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
                    auto* g = m_sketch->normalGeometries()[gi];
                    QVector2D newPos = m_sketch->plane()->toPlane(
                        QVector3D(np.X(), np.Y(), np.Z()));
                    // ✅ Task A.1: 透過 movePoint 更新 SketchPoint，避免 Solver 覆蓋
                    QString ptUuid;
                    if (auto* line = dynamic_cast<SketchLine*>(g)) {
                        ptUuid = (pi == 0) ? line->startUuid : line->endUuid;
                    }
                    if (!ptUuid.isEmpty()) {
                        m_sketch->movePoint(ptUuid, newPos);
                    } else {
                        // Polyline 或無 UUID：維持舊行為
                        g->points[pi] = newPos;
                    }
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
                    // ✅ Task A.2: Line 中點拖曳：透過 movePoint 同時更新兩端 SketchPoint
                    if (auto* line = dynamic_cast<SketchLine*>(g)) {
                        if (!line->startUuid.isEmpty())
                            m_sketch->movePoint(line->startUuid, g->points[si] + delta);
                        if (!line->endUuid.isEmpty())
                            m_sketch->movePoint(line->endUuid, g->points[ni] + delta);
                    } else {
                        g->points[si] += delta;
                        g->points[ni] += delta;
                    }
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
                QVector2D newCenter = m_sketch->plane()->toPlane(
                    QVector3D(np.X(), np.Y(), np.Z()));
                // ✅ Task A.3: 透過 movePoint 更新 SketchPoint，避免 Solver 覆蓋
                if (!c->centerUuid.isEmpty()) {
                    m_sketch->movePoint(c->centerUuid, newCenter);
                } else {
                    c->center = newCenter;
                }
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

            const Handle(Geom_Circle) circle =
                Handle(Geom_Circle)::DownCast(arc->curve->BasisCurve());
            if (circle.IsNull()) continue;

            double t0 = arc->curve->FirstParameter();
            double t1 = arc->curve->LastParameter();
            double tM = (t0 + t1) * 0.5;

            // ✅ 在 computeGrips() 時就固定三點座標，capture by value
            gp_Pnt fixedStart  = arc->curve->Value(t0);
            gp_Pnt fixedMid    = arc->curve->Value(tM);
            gp_Pnt fixedEnd    = arc->curve->Value(t1);
            gp_Pnt fixedCenter = circle->Location();

            // 起點 grip：移動起點，midPoint / endPoint 固定
            {
                GripPoint gp;
                gp.id       = QString("g%1_arcStart").arg(gi);
                gp.position = fixedStart;
                gp.type     = GripType::Vertex;
                gp.onDrag   = [this, gi, fixedMid, fixedEnd](const gp_Pnt& np, bool) {
                    auto* a = static_cast<SketchArc*>(m_sketch->normalGeometries()[gi]);
                    GC_MakeArcOfCircle maker(np, fixedMid, fixedEnd);
                    if (maker.IsDone()) {
                        a->curve = maker.Value();
                        // ✅ GAP 2: 同步起點 SketchPoint
                        if (!a->startUuid.isEmpty()) {
                            QVector2D p2d = m_sketch->plane()->toPlane(
                                QVector3D(np.X(), np.Y(), np.Z()));
                            m_sketch->movePoint(a->startUuid, p2d);
                        }
                    }
                    m_sketch->rebuildShapesOnly();
                };
                grips.append(gp);
            }

            // 中點 grip：移動弧上中點，startPoint / endPoint 固定
            {
                GripPoint gp;
                gp.id       = QString("g%1_arcMid").arg(gi);
                gp.position = fixedMid;
                gp.type     = GripType::Midpoint;
                gp.onDrag   = [this, gi, fixedStart, fixedEnd](const gp_Pnt& np, bool) {
                    auto* a = static_cast<SketchArc*>(m_sketch->normalGeometries()[gi]);
                    GC_MakeArcOfCircle maker(fixedStart, np, fixedEnd);
                    if (maker.IsDone()) a->curve = maker.Value();
                    // 弧中點無對應 SketchPoint UUID，不需 movePoint
                    m_sketch->rebuildShapesOnly();
                };
                grips.append(gp);
            }

            // 終點 grip：移動終點，startPoint / midPoint 固定
            {
                GripPoint gp;
                gp.id       = QString("g%1_arcEnd").arg(gi);
                gp.position = fixedEnd;
                gp.type     = GripType::Vertex;
                gp.onDrag   = [this, gi, fixedStart, fixedMid](const gp_Pnt& np, bool) {
                    auto* a = static_cast<SketchArc*>(m_sketch->normalGeometries()[gi]);
                    GC_MakeArcOfCircle maker(fixedStart, fixedMid, np);
                    if (maker.IsDone()) {
                        a->curve = maker.Value();
                        // ✅ GAP 2: 同步終點 SketchPoint
                        if (!a->endUuid.isEmpty()) {
                            QVector2D p2d = m_sketch->plane()->toPlane(
                                QVector3D(np.X(), np.Y(), np.Z()));
                            m_sketch->movePoint(a->endUuid, p2d);
                        }
                    }
                    m_sketch->rebuildShapesOnly();
                };
                grips.append(gp);
            }

            // 圓心 grip：平移整段弧（三點同步加上 delta）
            {
                GripPoint gp;
                gp.id       = QString("g%1_arcCenter").arg(gi);
                gp.position = fixedCenter;
                gp.type     = GripType::Center;
                gp.onDrag   = [this, gi, fixedCenter, fixedStart, fixedMid, fixedEnd](const gp_Pnt& np, bool) {
                    auto* a = static_cast<SketchArc*>(m_sketch->normalGeometries()[gi]);
                    gp_Vec delta(fixedCenter, np);
                    gp_Pnt newStart = fixedStart.Translated(delta);
                    gp_Pnt newMid   = fixedMid.Translated(delta);
                    gp_Pnt newEnd   = fixedEnd.Translated(delta);
                    GC_MakeArcOfCircle maker(newStart, newMid, newEnd);
                    if (maker.IsDone()) {
                        a->curve = maker.Value();
                        // ✅ GAP 2: 同步起/終點 SketchPoint（圓心 UUID 無 points[] 對應）
                        auto toPlane = [&](const gp_Pnt& p) {
                            return m_sketch->plane()->toPlane(QVector3D(p.X(), p.Y(), p.Z()));
                        };
                        if (!a->startUuid.isEmpty())
                            m_sketch->movePoint(a->startUuid, toPlane(newStart));
                        if (!a->endUuid.isEmpty())
                            m_sketch->movePoint(a->endUuid, toPlane(newEnd));
                    }
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

// ── Task F: 尺寸線 Grip ──────────────────────────────────────────────────────
QVector<GripPoint> SketchGripProvider::gripsForConstraint(
    const QString& constraintUuid,
    ConstraintOverlayManager* overlay) const
{
    QVector<GripPoint> grips;
    if (!m_sketch || !overlay) return grips;

    auto* con = m_sketch->findConstraint(constraintUuid);
    if (!con) return grips;

    // 僅尺寸型約束提供可拖曳 Grip
    const bool isDim =
        con->type == ConstraintType::FixedDistance ||
        con->type == ConstraintType::FixedRadius   ||
        con->type == ConstraintType::FixedX        ||
        con->type == ConstraintType::FixedY        ||
        con->type == ConstraintType::FixedAngleDim;
    if (!isDim) return grips;

    // 取得尺寸線 AIS 物件，從中取錨點位置
    Handle(AIS_DimensionLine) dimAIS = overlay->dimLineAISForConstraint(constraintUuid);
    if (dimAIS.IsNull()) return grips;

    GripPoint grip;
    grip.id       = constraintUuid + QLatin1String("_dim");
    grip.position = dimAIS->dimLineAnchorPoint3D();
    grip.type     = GripType::DimLine;  // ✅ Task F: 菱形 Grip

    grip.onDrag = [this, constraintUuid, overlay](const gp_Pnt& np, bool /*snapped*/) {
        SketchConstraint* c = m_sketch->findConstraint(constraintUuid);
        if (!c || !m_sketch->plane()) return;

        QVector2D newPlanePt = m_sketch->plane()->toPlane(
            QVector3D(np.X(), np.Y(), np.Z()));

        // 計算中心點（兩 ref 的中點，用於計算偏移量）
        QVector2D p1, p2;
        if (!c->refs.isEmpty())
            p1 = c->refs[0].resolvePosition(m_sketch);
        p2 = (c->refs.size() > 1) ? c->refs[1].resolvePosition(m_sketch) : p1;
        QVector2D center = (p1 + p2) * 0.5f;

        // 將新位置轉換為相對偏移量並儲存
        c->dimLineOffsetX = static_cast<double>(newPlanePt.x() - center.x());
        c->dimLineOffsetY = static_cast<double>(newPlanePt.y() - center.y());

        // 即時更新 AIS 顯示
        overlay->updateDimLine(constraintUuid,
                               c->dimLineOffsetX,
                               c->dimLineOffsetY);
    };

    grips.append(grip);
    return grips;
}

} // namespace aicad::cad
