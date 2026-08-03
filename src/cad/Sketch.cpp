/**
 * @file Sketch.cpp
 * @brief 與增強版 Plane 整合的 Sketch 類別實作
 */

#include "Sketch.h"
#include "Document.h"
#include "PlaneManager.h"
#include "sketch/SketchLoopFinder.h"
#include "sketch/SketchPointAIS.h"
#include <gp_Ax3.hxx>

#include <QDebug>
#include <QtMath>
#include <QUuid>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Compound.hxx>
#include <Precision.hxx>
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <Geom_BSplineCurve.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <Standard_Failure.hxx>
#include <AIS_Shape.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_Circle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Prs3d_LineAspect.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <TopExp_Explorer.hxx>

#include <QJsonArray>
#include <QSet>
#include <cstring>

namespace aicad {
namespace cad {

Sketch::Sketch(Document* parent)
    : Feature(parent)
    , m_plane(nullptr)
{
    setName("Sketch");

    // 建立預設 XY 平面
    createDefaultPlane();

    qDebug() << "[Sketch]" << name() << "created with plane:"
             << (m_plane ? m_plane->displayName() : "None");
}

Sketch::~Sketch() {
    disconnectPlaneSignals();
    clearGeometry();
    qDebug() << "[Sketch]" << name() << "destroyed";
}

void Sketch::setPlane(Plane* plane) {
    if (!plane) {
        qWarning() << "[Sketch]" << name() << "Cannot set null plane";
        return;
    }

    if (m_plane == plane) {
        return;
    }

    disconnectPlaneSignals();

    Plane* oldPlane = m_plane;
    m_plane = plane;

    connectPlaneSignals();

    qDebug() << "[Sketch]" << name() << "plane changed from"
             << (oldPlane ? oldPlane->displayName() : "None")
             << "to" << m_plane->displayName();

    Q_EMIT planeChanged(m_plane);
    Q_EMIT rebuildRequested();
}

void Sketch::addGeometry(SketchGeometry* geom) {
    if (!geom) return;
    m_geometries.append(geom);
    Q_EMIT geometryChanged();
    Q_EMIT rebuildRequested();
}

void Sketch::removeGeometry(int index) {
    if (index >= 0 && index < m_geometries.size()) {
        QString uuid = m_geometries[index]->uuid;
        removeConstraintsOf(uuid);          // ← 新增
        removeAnnotationsOf(uuid);          // ← GDIM v2 Phase 1：連同標註一併清除
        delete m_geometries.takeAt(index);
        Q_EMIT geometryChanged();
        Q_EMIT rebuildRequested();
    }
}

bool Sketch::removeGeometry(const QString& uuid) {
    for (int i = 0; i < m_geometries.size(); ++i) {
        if (m_geometries[i]->uuid == uuid) {
            removeGeometry(i);
            return true;
        }
    }
    return false;
}


void Sketch::clearGeometry() {
    qDeleteAll(m_geometries);
    m_geometries.clear();
    m_uuidToGeomIndex.clear();
    Q_EMIT geometryChanged();
}

void Sketch::addLine(const QVector2D& p1, const QVector2D& p2) {
    // ✅ 改為呼叫 addLineGeom，確保 SketchPoint 被建立
    addLineGeom(p1, p2);
}

void Sketch::addPolyline(const QVector<QVector2D>& points, bool closed) {
    // ✅ 退化為多條獨立 SketchLine + 相鄰段落自動 Coincident 束制，
    //    不再建立單一 SketchPolyline 幾何。理由：
    //      - 每一段可個別被選取、標註尺寸、設定束制（水平/垂直/相切…）、
    //        轉為建構線、單獨刪除或倒圓角，與 Line 指令產生的結果完全一致；
    //      - 與 ConstraintSolver / grip 編輯 / 尺寸標註等既有管線共用同一套
    //        SketchLine 處理邏輯，不需要為 SketchPolyline 另外維護特例。
    //    （舊格式 SketchPolyline/addPolylineGeom 仍保留，供讀取舊檔案使用。）
    addLineChainGeom(points, closed);
}

void Sketch::addSpline(const QVector<QVector2D>& points) {
    if (points.size() < 3) {
        qWarning() << "[Sketch]" << name() << "spline needs at least 3 points";
        return;
    }
    addSplineGeom(points);
}

void Sketch::addCircle(const QVector2D& center, double radius) {
    // ✅ 改為呼叫 addCircleGeom，確保 SketchPoint 被建立
    addCircleGeom(center, radius);
}

void Sketch::addEllipse(const QVector2D& center, double majorRadius, double minorRadius, double angle) {
    if (majorRadius <= 0 || minorRadius <= 0) {
        qWarning() << "[Sketch]" << name() << "ellipse radii must be positive";
        return;
    }
    if (majorRadius < minorRadius) {
        qWarning() << "[Sketch]" << name() << "major radius must be >= minor radius";
        return;
    }
    addEllipseGeom(center, majorRadius, minorRadius, angle);
}

void Sketch::addRectangle(const QVector2D& corner1, const QVector2D& corner2) {
    QVector<QVector2D> points;
    points << corner1
           << QVector2D(corner2.x(), corner1.y())
           << corner2
           << QVector2D(corner1.x(), corner2.y());

    // 退化為 4 條獨立 SketchLine + 4 個角落的 Coincident 束制
    QStringList lineUuids = addLineChainGeom(points, true);

    // 補上 Horizontal/Vertical 束制，讓矩形在之後被拖曳/求解時仍維持「矩形」
    // （否則退化後只是一個沒有形狀限制的封閉四邊形）。
    // 邊的順序對應 points：corner1→(x2,y1) 水平、(x2,y1)→corner2 垂直、
    //                     corner2→(x1,y2) 水平、(x1,y2)→corner1 垂直。
    if (lineUuids.size() == 4) {
        constrainHorizontal(lineUuids[0]);
        constrainVertical(lineUuids[1]);
        constrainHorizontal(lineUuids[2]);
        constrainVertical(lineUuids[3]);
    } else {
        qWarning() << "[Sketch]" << name()
                   << "addRectangle: unexpected line count" << lineUuids.size();
    }
}

QVector3D Sketch::planeToWorld(const QVector2D& planePt) const {
    return m_plane->origin() + m_plane->xAxis() * planePt.x() + m_plane->yAxis() * planePt.y();
}

void Sketch::addArc(const QVector2D& startPoint,
                    const QVector2D& midPoint,
                    const QVector2D& endPoint)
{
    // ✅ 改為呼叫 addArcGeom，確保 SketchPoint 被建立
    addArcGeom(startPoint, midPoint, endPoint);
}

QString Sketch::addArcGeom(const QVector2D& startPoint,
                            const QVector2D& midPoint,
                            const QVector2D& endPoint,
                            const QString& reuseStartUuid,
                            const QString& reuseEndUuid,
                            const QString& reuseCenterUuid)
{
    try {
        QVector3D w1 = planeToWorld(startPoint);
        QVector3D w2 = planeToWorld(midPoint);
        QVector3D w3 = planeToWorld(endPoint);

        gp_Pnt gp1(w1.x(), w1.y(), w1.z());
        gp_Pnt gp2(w2.x(), w2.y(), w2.z());
        gp_Pnt gp3(w3.x(), w3.y(), w3.z());

        GC_MakeArcOfCircle arcMaker(gp1, gp2, gp3);
        if (!arcMaker.IsDone()) {
            qWarning() << "[Sketch]" << name() << "addArcGeom: failed (points may be collinear)";
            return QString();
        }

        Handle(Geom_TrimmedCurve) arc = arcMaker.Value();
        auto* arcGeom = new SketchArc(arc);

        // ✅ Phase 0B：建立或重用起點/終點/圓心 SketchPoint
        arcGeom->startUuid  = reuseStartUuid.isEmpty()
            ? addPoint(startPoint, SketchPoint::Origin::Endpoint)
            : reuseStartUuid;
        arcGeom->endUuid    = reuseEndUuid.isEmpty()
            ? addPoint(endPoint,   SketchPoint::Origin::Endpoint)
            : reuseEndUuid;

        if (reuseCenterUuid.isEmpty()) {
            // 計算圓心 2D 座標並建立 Center 點
            gp_Pnt gcCenter = Handle(Geom_Circle)::DownCast(arc->BasisCurve())->Location();
            QVector2D centerPos = m_plane->toPlane(
                QVector3D(gcCenter.X(), gcCenter.Y(), gcCenter.Z()));
            arcGeom->centerUuid = addPoint(centerPos, SketchPoint::Origin::Center);
        } else {
            arcGeom->centerUuid = reuseCenterUuid;
        }

        m_geometries.append(arcGeom);
        Q_EMIT geometryChanged();
        Q_EMIT rebuildRequested();
        qDebug() << "[Sketch]" << name() << "Arc added with SketchPoints";
        return arcGeom->uuid;
    } catch (Standard_Failure const& e) {
        qWarning() << "[Sketch] OCCT error:" << e.GetMessageString();
        return QString();
    }
}

// ── 便捷方法實作 ──────────────────────────────────────────────────────────

void Sketch::addConstructionLine(const QVector2D& p1, const QVector2D& p2) {
    addGeometry(new SketchLine(p1, p2, GeomRole::Construction));
}

void Sketch::addCenterline(const QVector2D& p1, const QVector2D& p2) {
    addGeometry(new SketchLine(p1, p2, GeomRole::Centerline));
}

void Sketch::addConstructionCircle(const QVector2D& center, double radius) {
    addGeometry(new SketchCircle(center, radius, GeomRole::Construction));
}

void Sketch::addConstructionArc(const QVector2D& start, const QVector2D& mid,
                                const QVector2D& end, GeomRole role) {
    // 複用現有 addArc 邏輯，但在建立後設定 role
    int countBefore = m_geometries.size();
    addArc(start, mid, end);
    if (m_geometries.size() > countBefore)
        m_geometries.last()->role = role;
}

QList<SketchGeometry*> Sketch::normalGeometries() const {
    QList<SketchGeometry*> result;
    for (auto* g : m_geometries)
        if (!g->isConstruction()) result.append(g);
    return result;
}

QList<SketchGeometry*> Sketch::constructionGeometries() const {
    QList<SketchGeometry*> result;
    for (auto* g : m_geometries)
        if (g->isConstruction()) result.append(g);
    return result;
}

SketchGeometry* Sketch::findGeometry(const QString& uuid) const {
    // O(1) cache lookup
    auto it = m_uuidToGeomIndex.find(uuid);
    if (it != m_uuidToGeomIndex.end()) {
        int idx = it.value();
        if (idx >= 0 && idx < m_geometries.size() && m_geometries[idx]->uuid == uuid)
            return m_geometries[idx];
        // cache stale — fall through to linear scan
    }
    // linear fallback（rebuild 後 index 可能改變）
    for (int i = 0; i < m_geometries.size(); ++i) {
        if (m_geometries[i]->uuid == uuid) {
            m_uuidToGeomIndex[uuid] = i;  // refresh cache
            return m_geometries[i];
        }
    }
    return nullptr;
}

// ============================================================================
//  geometryFingerprint() — cheap dirty-check for incremental AIS rebuild
// ============================================================================

namespace {
/// Combine a double into a running hash (bit-reinterpret to avoid NaN/round-off
/// surprises from qHash(double) on some Qt versions).
inline void hashCombine(quint64& seed, double v) {
    quint64 bits;
    static_assert(sizeof(bits) == sizeof(v), "double must be 64-bit");
    std::memcpy(&bits, &v, sizeof(v));
    // boost::hash_combine-style mix
    seed ^= bits + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}
inline void hashCombine(quint64& seed, int v) {
    hashCombine(seed, static_cast<double>(v));
}
} // anonymous namespace

quint64 Sketch::geometryFingerprint(const SketchGeometry* geom)
{
    if (!geom) return 0;

    quint64 h = 1469598103934665603ULL;  // FNV offset basis, arbitrary non-zero seed
    hashCombine(h, static_cast<int>(geom->type));
    hashCombine(h, static_cast<int>(geom->role));

    if (const auto* arc = dynamic_cast<const SketchArc*>(geom)) {
        // SketchArc keeps its geometry in an OCCT curve, not in `points`.
        if (!arc->curve.IsNull()) {
            const double t0 = arc->curve->FirstParameter();
            const double t1 = arc->curve->LastParameter();
            gp_Pnt pS = arc->curve->Value(t0);
            gp_Pnt pM = arc->curve->Value((t0 + t1) * 0.5);
            gp_Pnt pE = arc->curve->Value(t1);
            hashCombine(h, pS.X()); hashCombine(h, pS.Y()); hashCombine(h, pS.Z());
            hashCombine(h, pM.X()); hashCombine(h, pM.Y()); hashCombine(h, pM.Z());
            hashCombine(h, pE.X()); hashCombine(h, pE.Y()); hashCombine(h, pE.Z());
        }
        return h;
    }

    for (const QVector2D& p : geom->points) {
        hashCombine(h, static_cast<double>(p.x()));
        hashCombine(h, static_cast<double>(p.y()));
    }

    if (const auto* pt = dynamic_cast<const SketchPoint*>(geom)) {
        hashCombine(h, static_cast<double>(pt->pos.x()));
        hashCombine(h, static_cast<double>(pt->pos.y()));
    } else if (const auto* circle = dynamic_cast<const SketchCircle*>(geom)) {
        hashCombine(h, static_cast<double>(circle->center.x()));
        hashCombine(h, static_cast<double>(circle->center.y()));
        hashCombine(h, circle->radius);
    } else if (const auto* ellipse = dynamic_cast<const SketchEllipse*>(geom)) {
        hashCombine(h, static_cast<double>(ellipse->center.x()));
        hashCombine(h, static_cast<double>(ellipse->center.y()));
        hashCombine(h, ellipse->majorRadius);
        hashCombine(h, ellipse->minorRadius);
        hashCombine(h, ellipse->angle);
    } else if (const auto* pline = dynamic_cast<const SketchPolyline*>(geom)) {
        hashCombine(h, pline->closed ? 1 : 0);
    }

    return h;
}

bool Sketch::rebuild() {
    qDebug() << "[Sketch]" << name() << "rebuilding with"
             << m_geometries.size() << "geometries on plane"
             << (m_plane ? m_plane->displayName() : "NULL");

    // ✅ Guard: plane 必須有效
    if (!hasValidPlane()) {
        qWarning() << "[Sketch]" << name() << "rebuild skipped: no valid plane";
        return false;
    }

    try {
        m_wires.clear();
        m_aisShapes.clear();
        m_aisShapeUuids.clear();
        m_constructionShapes.clear();

        if (m_geometries.isEmpty()) {
            setShape(TopoDS_Shape());
            return true;
        }

        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);

        for (const SketchGeometry* geom : m_geometries) {
            if (!geom) continue;
            // ── Point：建立 SketchPointAIS 並納入 m_aisShapes ────────────────
            if (geom->type == SketchGeometryType::Point) {
                const auto* pt = static_cast<const SketchPoint*>(geom);
                gp_Pnt org(m_plane->origin().x(), m_plane->origin().y(), m_plane->origin().z());
                gp_Dir xd (m_plane->xAxis().x(),  m_plane->xAxis().y(),  m_plane->xAxis().z());
                gp_Dir nd (m_plane->normal().x(), m_plane->normal().y(), m_plane->normal().z());
                gp_Ax3 ax3(org, nd, xd);
                Handle(SketchPointAIS) ptAis = new SketchPointAIS(pt, ax3);
                if (!geom->isConstruction()) {
                    m_aisShapes.append(ptAis);
                    m_aisShapeUuids.append(pt->uuid);
                }
                continue;
            }

            TopoDS_Wire wire;
            bool wireCreated = false;

            // ── Line ───────────────────────────────────────────────────────
            if (geom->type == SketchGeometryType::Line && geom->points.size() >= 2) {
                QVector3D p1 = m_plane->toWorld(geom->points[0].x(), geom->points[0].y());
                QVector3D p2 = m_plane->toWorld(geom->points[1].x(), geom->points[1].y());

                BRepBuilderAPI_MakeEdge edgeBuilder(
                    gp_Pnt(p1.x(), p1.y(), p1.z()),
                    gp_Pnt(p2.x(), p2.y(), p2.z()));

                if (edgeBuilder.IsDone()) {
                    BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
                    if (wireBuilder.IsDone()) {
                        wire = wireBuilder.Wire();
                        wireCreated = true;
                    }
                }
            }

            // ── Polyline ───────────────────────────────────────────────────
            else if (geom->type == SketchGeometryType::Polyline) {
                const SketchPolyline* pline = static_cast<const SketchPolyline*>(geom);
                BRepBuilderAPI_MakeWire wireBuilder;

                int numSegments = pline->closed ? geom->points.size()
                                                : geom->points.size() - 1;

                for (int i = 0; i < numSegments; ++i) {
                    QVector3D p1 = m_plane->toWorld(geom->points[i].x(), geom->points[i].y());
                    int nextIdx  = (i + 1) % geom->points.size();
                    QVector3D p2 = m_plane->toWorld(geom->points[nextIdx].x(), geom->points[nextIdx].y());

                    BRepBuilderAPI_MakeEdge edgeBuilder(
                        gp_Pnt(p1.x(), p1.y(), p1.z()),
                        gp_Pnt(p2.x(), p2.y(), p2.z()));

                    if (edgeBuilder.IsDone()) {
                        wireBuilder.Add(edgeBuilder.Edge());
                    }
                }

                if (wireBuilder.IsDone()) {
                    wire = wireBuilder.Wire();
                    wireCreated = true;
                }
            }

            // ── Circle ────────────────────────────────────────────────────
            else if (geom->type == SketchGeometryType::Circle) {
                const SketchCircle* circle = static_cast<const SketchCircle*>(geom);
                QVector3D center3d = m_plane->toWorld(circle->center.x(), circle->center.y());

                gp_Ax2 ax2(
                    gp_Pnt(center3d.x(), center3d.y(), center3d.z()),
                    gp_Dir(m_plane->normal().x(), m_plane->normal().y(), m_plane->normal().z()));

                BRepBuilderAPI_MakeEdge edgeBuilder(gp_Circ(ax2, circle->radius));
                if (edgeBuilder.IsDone()) {
                    BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
                    if (wireBuilder.IsDone()) {
                        wire = wireBuilder.Wire();
                        wireCreated = true;
                    }
                }
            }

            // ── Spline ────────────────────────────────────────────────────
            else if (geom->type == SketchGeometryType::Spline) {
                if (geom->points.size() < 3) {
                    qWarning() << "[Sketch] Spline requires at least 3 points, got:"
                               << geom->points.size();
                    continue;
                }

                try {
                    int numPoints = geom->points.size();
                    Handle(TColgp_HArray1OfPnt) controlPoints =
                        new TColgp_HArray1OfPnt(1, numPoints);

                    for (int i = 0; i < numPoints; ++i) {
                        QVector3D p = m_plane->toWorld(geom->points[i].x(), geom->points[i].y());
                        controlPoints->SetValue(i + 1, gp_Pnt(p.x(), p.y(), p.z()));
                    }

                    GeomAPI_Interpolate interpolator(controlPoints, Standard_False, 1.0e-6);
                    interpolator.Perform();

                    if (!interpolator.IsDone()) {
                        qWarning() << "[Sketch] Failed to interpolate spline";
                        continue;
                    }

                    BRepBuilderAPI_MakeEdge edgeBuilder(interpolator.Curve());
                    if (!edgeBuilder.IsDone()) {
                        qWarning() << "[Sketch] Failed to create edge from spline curve";
                        continue;
                    }

                    BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
                    if (wireBuilder.IsDone()) {
                        wire = wireBuilder.Wire();
                        wireCreated = true;
                    }
                } catch (Standard_Failure& e) {
                    qWarning() << "[Sketch] Exception in spline creation:" << e.GetMessageString();
                }
            }

            // ── Arc ───────────────────────────────────────────────────────
            else if (geom->type == SketchGeometryType::Arc) {
                const SketchArc* arc = static_cast<const SketchArc*>(geom);
                if (arc->curve.IsNull()) {
                    qWarning() << "[Sketch] Arc curve is null";
                    continue;
                }

                BRepBuilderAPI_MakeEdge edgeBuilder(arc->curve);
                if (!edgeBuilder.IsDone()) {
                    qWarning() << "[Sketch] Arc edge build failed:" << edgeBuilder.Error();
                    continue;
                }

                BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
                if (wireBuilder.IsDone()) {
                    wire = wireBuilder.Wire();
                    wireCreated = true;
                } else {
                    qWarning() << "[Sketch] Arc wire build failed";
                }
            }

            // ── Ellipse ───────────────────────────────────────────────────
            else if (geom->type == SketchGeometryType::Ellipse) {
                const SketchEllipse* ellipse = static_cast<const SketchEllipse*>(geom);

                QVector3D center3d = m_plane->toWorld(ellipse->center.x(), ellipse->center.y());
                gp_Pnt centerPnt(center3d.x(), center3d.y(), center3d.z());
                gp_Dir normal(m_plane->normal().x(), m_plane->normal().y(), m_plane->normal().z());

                double cosAngle = qCos(ellipse->angle);
                double sinAngle = qSin(ellipse->angle);

                QVector3D majorAxisEnd3D = m_plane->toWorld(
                    ellipse->center.x() + cosAngle,
                    ellipse->center.y() + sinAngle);
                QVector3D majorAxisDir3D = (majorAxisEnd3D - center3d).normalized();
                gp_Dir xDir(majorAxisDir3D.x(), majorAxisDir3D.y(), majorAxisDir3D.z());

                gp_Ax2 ax2(centerPnt, normal, xDir);
                gp_Elips gpEllipse(ax2, ellipse->majorRadius, ellipse->minorRadius);

                BRepBuilderAPI_MakeEdge edgeBuilder(gpEllipse);
                if (edgeBuilder.IsDone()) {
                    BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
                    if (wireBuilder.IsDone()) {
                        wire = wireBuilder.Wire();
                        wireCreated = true;
                    }
                }
            }

            // ── Wire → AIS_Shape ──────────────────────────────────────────
            if (wireCreated) {
                Handle(AIS_Shape) aisShape = new AIS_Shape(wire);

                if (geom->isConstruction()) {
                    // ── 建構線樣式 ────────────────────────────────
                    applyConstructionStyle(aisShape, geom->role);  // ← 新增
                    m_constructionShapes.append(aisShape);
                    // 不加入 m_wires（不參與輪廓）
                } else {
                    // ── 正常幾何樣式 ──────────────────────────────
                    aisShape->SetColor(Quantity_NOC_WHITE);
                    aisShape->SetWidth(2.0);
                    aisShape->SetDisplayMode(AIS_WireFrame);
                    m_wires.append(wire);
                    m_aisShapes.append(aisShape);
                    m_aisShapeUuids.append(geom->uuid);
                }
            }
        }

        // compound 只由 normal wires 組成（供 Extrude 使用）
        builder.MakeCompound(compound);
        for (const TopoDS_Wire& w : m_wires)
            builder.Add(compound, w);

        setShape(compound);

        // SketchPointAIS 已在上方迴圈中建立並加入 m_aisShapes

        qDebug() << "[Sketch]" << name() << "rebuilt with"
                 << m_wires.size() << "wires and"
                 << m_aisShapes.size() << "AIS shapes";
        if (!m_suppressRebuiltSignal)
            Q_EMIT rebuilt();
        return true;

    } catch (const Standard_Failure& e) {
        QString error = QString("OCCT error: %1").arg(e.GetMessageString());
        qCritical() << "[Sketch]" << name() << error;
        setError(error);
        return false;
    }
}

bool Sketch::rebuildShapesOnly()
{
    if (!hasValidPlane()) return false;
    if (m_aisContext.IsNull()) return false;

    // ── Snapshot the previous AIS state before rebuild() overwrites it ───────
    // rebuild() is CPU-only (TopoDS + fresh AIS handle allocation, no OCCT
    // context calls), so it's cheap to call unconditionally. What's
    // expensive is Erase/Display/Activate/UpdateCurrentViewer on the AIS
    // context — so we diff old vs new by uuid+fingerprint and only touch
    // the context for geometries whose solved shape actually changed,
    // instead of erasing and redisplaying everything every solve.
    const QList<Handle(AIS_InteractiveObject)> oldAisShapes   = m_aisShapes;
    const QList<QString>                       oldAisUuids    = m_aisShapeUuids;
    const QHash<QString, quint64>              oldFingerprints = m_geomFingerprints;
    const QList<Handle(AIS_Shape)>             oldConstructionShapes = m_constructionShapes;

    QHash<QString, Handle(AIS_InteractiveObject)> oldByUuid;
    for (int i = 0; i < oldAisUuids.size(); ++i)
        oldByUuid.insert(oldAisUuids[i], oldAisShapes[i]);

    // ② 重建 TopoDS + 取得全新 AIS handle 候選列表（純 CPU，未碰 context）。
    //    rebuild() 會 emit shapeChanged()（Extrude 等下游 feature 需要這個信號
    //    才能即時更新，不可封鎖）以及 rebuilt()（此時 m_aisShapes 只是
    //    「候選」handle，未變動的幾何稍後會被下面的 diff 換回舊 handle；
    //    若讓 CadView::onSketchRebuilt() 在這個時間點處理，會把反查表
    //    登錄成 candidate（即將被丟棄）的指標，導致之後 pick 沿用舊
    //    handle 的幾何時反查失敗，例如 GDIM 第二個選取點不到）。
    //    因此只暫時阻斷 rebuilt()，shapeChanged() 仍正常發出。
    m_suppressRebuiltSignal = true;
    const bool ok = rebuild();
    m_suppressRebuiltSignal = false;
    if (!ok) return false;

    // ── ③ Diff: for each new geometry, decide reuse-old vs display-new ──────
    QHash<QString, quint64> newFingerprints;
    bool anyChanged = false;

    for (int i = 0; i < m_aisShapes.size(); ++i) {
        const QString& uuid = m_aisShapeUuids[i];
        SketchGeometry* geom = findGeometry(uuid);
        const quint64 fp = geometryFingerprint(geom);
        newFingerprints.insert(uuid, fp);

        auto oldIt = oldByUuid.find(uuid);
        const bool existedBefore = (oldIt != oldByUuid.end());
        const bool unchanged = existedBefore
                                && oldFingerprints.value(uuid, ~0ULL) == fp;

        if (unchanged) {
            // Geometry's solved shape is identical to last solve — keep the
            // AIS object already displayed in the context untouched
            // (preserves selection/highlight state too), discard the
            // freshly-built duplicate from rebuild().
            m_aisShapes[i] = oldIt.value();
            continue;
        }

        anyChanged = true;

        if (existedBefore) {
            // Try in-place update for SketchPointAIS (no Erase/Display
            // churn, preserves selection state); fall back to swap for
            // wire-based shapes where in-place geometry mutation isn't
            // exposed.
            Handle(SketchPointAIS) oldPtAis = Handle(SketchPointAIS)::DownCast(oldIt.value());
            const auto* pt = (geom && geom->type == SketchGeometryType::Point)
                                ? static_cast<const SketchPoint*>(geom) : nullptr;

            if (!oldPtAis.IsNull() && pt) {
                oldPtAis->updatePosition(pt->pos);
                m_aisContext->Redisplay(oldPtAis, Standard_False);
                m_aisShapes[i] = oldPtAis;   // keep the old, now-updated handle
            } else {
                m_aisContext->Erase(oldIt.value(), Standard_False);
                m_aisContext->Display(m_aisShapes[i], Standard_False);
                if (Handle(SketchPointAIS)::DownCast(m_aisShapes[i]))
                    m_aisContext->Activate(m_aisShapes[i], 0, Standard_False);
            }
        } else {
            // Brand-new geometry — just display it.
            m_aisContext->Display(m_aisShapes[i], Standard_False);
            if (Handle(SketchPointAIS)::DownCast(m_aisShapes[i]))
                m_aisContext->Activate(m_aisShapes[i], 0, Standard_False);
        }
    }

    // Geometries that existed before but are gone now (deleted) — erase them.
    // ⚠️ 例外：SketchPointAIS 一律不透過這個「uuid 已不在最新列表」的
    // 捷徑判斷來 Erase。這個判斷式只是拿「這次 rebuild() 產生的
    // m_aisShapeUuids 有沒有這個 uuid」來猜測「幾何是否被刪除」，但
    // rebuild() 對 Point 的收錄邏輯（見上方 addPoint 分支：
    // `if (!geom->isConstruction()) { append... }`）在某些求解路徑下
    // 可能與呼叫當下的暫時狀態不同步，導致「明明沒被使用者刪除的點」
    // 也被這裡誤判成「已刪除」而 Erase 掉——這正是「雙擊編輯尺寸約束數值
    // 完成後 SketchPoint 被隱藏」的成因之一。SketchPoint 依需求必須永遠
    // 顯示，真正刪除幾何（例如刪除一條線連帶其端點）另有專屬的刪除指令
    // 路徑（見 EraseCommand），不依賴這裡的捷徑判斷，因此排除 SketchPointAIS
    // 不會影響「真的刪除幾何」時的清理。
    QSet<QString> currentUuids;
    currentUuids.reserve(m_aisShapeUuids.size());
    for (const QString& u : m_aisShapeUuids)
        currentUuids.insert(u);
    for (auto it = oldByUuid.begin(); it != oldByUuid.end(); ++it) {
        if (!currentUuids.contains(it.key())) {
            if (!Handle(SketchPointAIS)::DownCast(it.value()).IsNull())
                continue;   // SketchPoint 永遠顯示，不因這裡的捷徑判斷被隱藏
            anyChanged = true;
            m_aisContext->Erase(it.value(), Standard_False);
        }
    }

    m_geomFingerprints = newFingerprints;

    // ── Construction shapes: no stable per-shape identity is tracked today
    //    (rebuild() always allocates fresh handles for these), so erase the
    //    previous batch and redisplay the new one. These are typically a
    //    small, mostly-static subset (reference geometry), so this remains
    //    cheap relative to the main geometry diff above. ───────────────────
    for (const Handle(AIS_Shape)& s : oldConstructionShapes) {
        if (!s.IsNull()) {
            anyChanged = true;
            m_aisContext->Erase(s, Standard_False);
        }
    }
    for (const Handle(AIS_Shape)& s : m_constructionShapes) {
        if (!s.IsNull()) {
            anyChanged = true;
            m_aisContext->Display(s, Standard_False);
            m_aisContext->Deactivate(s);
        }
    }

    if (anyChanged)
        m_aisContext->UpdateCurrentViewer();

    // 此時 m_aisShapes 已是「最終」狀態（未變動的沿用舊 handle，
    // 變動的是新 handle）。現在才發出 rebuilt()，讓
    // CadView::onSketchRebuilt() 用正確的 pointer 集合重建
    // aisToFeatureId / aisToGeomUuid / aisToGeomIndex 反查表，
    // 避免 pick（例如 GDIM 第二個選取）打到未登錄的 handle。
    Q_EMIT rebuilt();

    return true;
}

// 加在 Sketch.cpp 匿名 namespace 或 private 方法中
void Sketch::applyConstructionStyle(Handle(AIS_Shape)& shape, GeomRole role) {
    shape->SetDisplayMode(AIS_WireFrame);

    switch (role) {
    case GeomRole::Construction:
        // 青色虛線，細線
        shape->SetColor(Quantity_NOC_CYAN1);
        shape->SetWidth(1.0);
        shape->Attributes()->WireAspect()->SetTypeOfLine(Aspect_TOL_DASH);
        break;

    case GeomRole::Centerline:
        // 橙色點鏈線，區別於一般建構線
        shape->SetColor(Quantity_NOC_ORANGE);
        shape->SetWidth(1.0);
        shape->Attributes()->WireAspect()->SetTypeOfLine(Aspect_TOL_DOTDASH);
        break;

    default:
        break;
    }
}

// ============================================================================
// Serialization
// ============================================================================

QJsonObject Sketch::toJson() const {
    QJsonObject json = Feature::toJson();

    if (m_plane) {
        json["planeId"]   = m_plane->id();
        json["planeName"] = m_plane->displayName();

        // ✅ 儲存完整平面幾何，確保 load 後能正確重建
        json["planeData"] = m_plane->toJson();
    } else {
        qWarning() << "[Sketch]" << name() << "has no plane when serializing";
    }

    QJsonArray geomsArray;
    for (const SketchGeometry* geom : m_geometries) {
        QJsonObject geomJson;
        geomJson["type"] = static_cast<int>(geom->type);        
        geomJson["role"] = static_cast<int>(geom->role);
        geomJson["uuid"] = geom->uuid;

        QJsonArray pointsArray;
        for (const QVector2D& pt : geom->points) {
            QJsonObject ptJson;
            ptJson["x"] = pt.x();
            ptJson["y"] = pt.y();
            pointsArray.append(ptJson);
        }
        geomJson["points"] = pointsArray;

        // ✅ Phase 0B：儲存各幾何的點 UUID 引用
        if (const auto* line = dynamic_cast<const SketchLine*>(geom)) {
            geomJson["startUuid"]  = line->startUuid;
            geomJson["endUuid"]    = line->endUuid;
        } else if (const auto* circle = dynamic_cast<const SketchCircle*>(geom)) {
            geomJson["centerUuid"] = circle->centerUuid;
        } else if (const auto* arc = dynamic_cast<const SketchArc*>(geom)) {
            geomJson["startUuid"]  = arc->startUuid;
            geomJson["endUuid"]    = arc->endUuid;
            geomJson["centerUuid"] = arc->centerUuid;
        } else if (const auto* pline = dynamic_cast<const SketchPolyline*>(geom)) {
            // Phase 0B：序列化各頂點 UUID
            QJsonArray vertexUuidsArray;
            for (const QString& uuid : pline->vertexUuids)
                vertexUuidsArray.append(uuid);
            geomJson["vertexUuids"] = vertexUuidsArray;
        } else if (const auto* splineG = dynamic_cast<const SketchSpline*>(geom)) {
            // Phase 0B：序列化各控制點 UUID
            QJsonArray cpUuidsArray;
            for (const QString& uuid : splineG->controlPointUuids)
                cpUuidsArray.append(uuid);
            geomJson["controlPointUuids"] = cpUuidsArray;
        } else if (const auto* ellipseG = dynamic_cast<const SketchEllipse*>(geom)) {
            // Phase 0B：序列化橢圓圓心 UUID
            geomJson["centerUuid"] = ellipseG->centerUuid;
        }

        // 各幾何類型的額外屬性
        switch (geom->type) {
        case SketchGeometryType::Point: {
            // ✅ Step 15: Point 序列化（origin + pos 已在 points[] 中，額外存 origin 欄位）
            const auto* pt = static_cast<const SketchPoint*>(geom);
            geomJson["px"]     = pt->pos.x();
            geomJson["py"]     = pt->pos.y();
            geomJson["origin"] = static_cast<int>(pt->origin);
            break;
        }
        case SketchGeometryType::Circle: {
            const SketchCircle* c = static_cast<const SketchCircle*>(geom);
            geomJson["radius"]  = c->radius;
            geomJson["centerX"] = c->center.x();
            geomJson["centerY"] = c->center.y();
            break;
        }
        case SketchGeometryType::Arc: {
            const SketchArc* a = static_cast<const SketchArc*>(geom);
            if (!a->curve.IsNull()) {
                // Extract start / mid / end from the OCCT trimmed curve
                double t0  = a->curve->FirstParameter();
                double t1  = a->curve->LastParameter();
                gp_Pnt gpS = a->curve->Value(t0);
                gp_Pnt gpM = a->curve->Value((t0 + t1) * 0.5);
                gp_Pnt gpE = a->curve->Value(t1);

                // Convert 3-D world → 2-D plane coords
                auto worldToPlane2D = [&](const gp_Pnt& p) -> QJsonObject {
                    QVector2D uv = m_plane->toPlane(
                        QVector3D(p.X(), p.Y(), p.Z()));
                    QJsonObject o;
                    o["x"] = uv.x();
                    o["y"] = uv.y();
                    return o;
                };

                geomJson["startPt"] = worldToPlane2D(gpS);
                geomJson["midPt"]   = worldToPlane2D(gpM);
                geomJson["endPt"]   = worldToPlane2D(gpE);
            }
            break;
        }
        case SketchGeometryType::Polyline: {
            const SketchPolyline* p = static_cast<const SketchPolyline*>(geom);
            geomJson["closed"] = p->closed;
            break;
        }
        case SketchGeometryType::Ellipse: {
            const SketchEllipse* e = static_cast<const SketchEllipse*>(geom);
            geomJson["centerX"]     = e->center.x();
            geomJson["centerY"]     = e->center.y();
            geomJson["majorRadius"] = e->majorRadius;
            geomJson["minorRadius"] = e->minorRadius;
            geomJson["angle"]       = e->angle;
            break;
        }
        case SketchGeometryType::Spline:
            // points already saved above
            break;
        default:
            break;
        }

        geomsArray.append(geomJson);
    }
    json["geometries"] = geomsArray;

    // Phase 1 重構後：Point 已統一在 "geometries" 陣列中，不需要獨立儲存

    // GDIM v2 Phase 1：schemaVersion=2 起，"constraints" 只存「純約束」，
    // implicitOf 非空的隱含約束（由 SketchAnnotation 產生）排除在外，
    // 改由 "annotations" 陣列儲存、載入後由 addAnnotation() 重新生成，
    // 避免資料重複、也避免舊隱含約束值與標註值不同步。
    QJsonArray conArr;
    for (const auto& c : m_constraints) {
        if (!c.implicitOf.isEmpty()) continue;
        conArr.append(c.toJson());
    }
    json["constraints"] = conArr;

    QJsonArray annArr;
    for (const auto& a : m_annotations)
        annArr.append(a.toJson());
    json["annotations"]    = annArr;
    json["schemaVersion"]  = 2;

    // ✅ 儲存 Sketch 自身的參數（cant、H 等），否則重新載入後參數列表會消失
    if (m_parameterStore)
        json["parameters"] = m_parameterStore->toJson();

    return json;
}

bool Sketch::fromJson(const QJsonObject& json) {
    if (!Feature::fromJson(json)) {
        return false;
    }

    // ✅ blockSignals 涵蓋平面解析到幾何載入全程
    blockSignals(true);

    // ================================================================
    // ✅ 平面恢復：四階段 fallback
    //   1. 用 UUID 直接查 PlaneManager（同 session 重用時有效）
    //   2. 用 planeName 比對標準平面（XY / XZ / YZ / ZX）
    //   3. 用儲存的 planeData 幾何比對現有平面
    //   4. 用 planeData 重建新平面並向 PlaneManager 登記
    // ================================================================
    Plane* resolvedPlane = nullptr;

    const QString planeId   = json["planeId"].toString();
    const QString planeName = json["planeName"].toString();

    // ── 階段 1：UUID 查找 ────────────────────────────────────────────
    if (!planeId.isEmpty()) {
        resolvedPlane = PlaneManager::instance()->getPlane(planeId);
        if (resolvedPlane) {
            qDebug() << "[Sketch]" << name()
                     << "Plane resolved by UUID:" << resolvedPlane->displayName();
        }
    }

    // ── 階段 2：標準平面名稱比對 ────────────────────────────────────
    if (!resolvedPlane && !planeName.isEmpty()) {
        resolvedPlane = resolveStandardPlane(planeName);
        if (resolvedPlane) {
            qDebug() << "[Sketch]" << name()
                     << "Plane resolved by name '" << planeName
                     << "':" << resolvedPlane->displayName();
        }
    }

    // ── 階段 3 & 4：從儲存的幾何資料恢復 ───────────────────────────
    if (!resolvedPlane && json.contains("planeData")) {
        resolvedPlane = reconstructPlaneFromJson(json["planeData"].toObject(), planeName);
        if (resolvedPlane) {
            qDebug() << "[Sketch]" << name()
                     << "Plane reconstructed from planeData:" << resolvedPlane->displayName();
        }
    }


    // ── 最終 fallback ────────────────────────────────────────────────
    if (resolvedPlane) {
        setPlane(resolvedPlane);
    } else {
        qWarning() << "[Sketch]" << name()
                   << "All plane resolution strategies failed, using default XY plane."
                   << "planeId=" << planeId << "planeName=" << planeName;
        createDefaultPlane();
    }

    // ================================================================
    // 載入幾何元素（完整版：含 Line / Polyline / Circle /
    //                         Spline / Arc(跳過) / Ellipse）
    // ================================================================
    clearGeometry();

    // ✅ Phase 0B：先還原 SketchPoint（幾何需要 UUID 引用）
    if (json.contains("sketchPoints")) {
        // 舊格式：SketchPoints 在獨立陣列中
        for (const QJsonValue& v : json["sketchPoints"].toArray()) {
            QJsonObject ptJson = v.toObject();
            QString uuid = ptJson["uuid"].toString();
            QVector2D pos(ptJson["x"].toDouble(), ptJson["y"].toDouble());
            SketchPoint::Origin origin =
                static_cast<SketchPoint::Origin>(ptJson["origin"].toInt(
                    static_cast<int>(SketchPoint::Origin::Endpoint)));
            auto* pt = new SketchPoint(pos, origin);
            pt->uuid = uuid;
            m_geometries.append(pt);
            m_uuidToGeomIndex[uuid] = m_geometries.size() - 1;
        }
        qDebug() << "[Sketch]" << name() << "Loaded"
                 << points().size() << "SketchPoints (legacy format)";
    }

    if (json.contains("geometries")) {
        QJsonArray geomsArray = json["geometries"].toArray();

        // ── 第一遍：只載入 Point（曲線需要先有 UUID 才能引用）────────
        for (const QJsonValue& val : geomsArray) {
            QJsonObject geomJson = val.toObject();
            SketchGeometryType type =
                static_cast<SketchGeometryType>(geomJson["type"].toInt());
            if (type != SketchGeometryType::Point) continue;

            QString uuid = geomJson["uuid"].toString();
            if (uuid.isEmpty() || findGeometry(uuid)) continue;  // 已由 legacy 載入

            QVector2D pos(geomJson["px"].toDouble(), geomJson["py"].toDouble());
            SketchPoint::Origin origin =
                static_cast<SketchPoint::Origin>(geomJson["origin"].toInt(
                    static_cast<int>(SketchPoint::Origin::Endpoint)));
            auto* pt = new SketchPoint(pos, origin);
            pt->uuid = uuid;
            pt->role = static_cast<GeomRole>(geomJson["role"].toInt());
            m_geometries.append(pt);
            m_uuidToGeomIndex[uuid] = m_geometries.size() - 1;
        }

        // ── 第二遍：載入曲線（可安全引用已存在的 Point UUID）──────────
        for (const QJsonValue& val : geomsArray) {
            QJsonObject geomJson = val.toObject();
            SketchGeometryType type =
                static_cast<SketchGeometryType>(geomJson["type"].toInt());
            if (type == SketchGeometryType::Point) continue;  // 已在第一遍處理

            // 通用：讀取點陣列
            QVector<QVector2D> points;
            QJsonArray pointsArray = geomJson["points"].toArray();
            for (const QJsonValue& ptVal : pointsArray) {
                QJsonObject ptJson = ptVal.toObject();
                points.append(QVector2D(ptJson["x"].toDouble(),
                                        ptJson["y"].toDouble()));
            }

            switch (type) {
            case SketchGeometryType::Line:
                if (points.size() >= 2) {
                    // ✅ Phase 0B：傳遞儲存的 UUID，避免重建新 SketchPoint
                    QString startUuid = geomJson["startUuid"].toString();
                    QString endUuid   = geomJson["endUuid"].toString();
                    // 若舊格式無 uuid（legacy），addLineGeom 會自動建立點
                    addLineGeom(points[0], points[1], startUuid, endUuid);
                }
                // 通用：在每個 case 的 addXxx() 之後加：
                if (!m_geometries.isEmpty() && geomJson.contains("uuid"))
                    m_geometries.last()->uuid = geomJson["uuid"].toString();
                if (geomJson.contains("role") && !m_geometries.isEmpty())
                    m_geometries.last()->role = static_cast<GeomRole>(geomJson["role"].toInt());
                break;
            case SketchGeometryType::Polyline: {
                bool closed = geomJson["closed"].toBool(false);
                if (points.size() >= 2) {
                    // ✅ Phase 0B：傳入已儲存的 vertexUuids 重用已載入的 SketchPoint
                    QVector<QString> savedVertexUuids;
                    if (geomJson.contains("vertexUuids")) {
                        for (const auto& v : geomJson["vertexUuids"].toArray())
                            savedVertexUuids.append(v.toString());
                    }
                    addPolylineGeom(points, closed, savedVertexUuids);
                }
                if (!m_geometries.isEmpty() && geomJson.contains("uuid"))
                    m_geometries.last()->uuid = geomJson["uuid"].toString();
                if (geomJson.contains("role") && !m_geometries.isEmpty())
                    m_geometries.last()->role = static_cast<GeomRole>(geomJson["role"].toInt());
                break;
            }

            case SketchGeometryType::Circle: {
                QVector2D center(geomJson["centerX"].toDouble(),
                                 geomJson["centerY"].toDouble());
                double radius = geomJson["radius"].toDouble();
                if (radius > 0.0) {
                    // ✅ Phase 0B：傳入已儲存的 centerUuid 重用已載入的 SketchPoint
                    QString savedCenterUuid = geomJson["centerUuid"].toString();
                    addCircleGeom(center, radius, savedCenterUuid);
                }
                if (!m_geometries.isEmpty() && geomJson.contains("uuid"))
                    m_geometries.last()->uuid = geomJson["uuid"].toString();
                if (geomJson.contains("role") && !m_geometries.isEmpty())
                    m_geometries.last()->role = static_cast<GeomRole>(geomJson["role"].toInt());
                break;
            }

            case SketchGeometryType::Spline:
                if (points.size() >= 3) {
                    // ✅ Phase 0B：傳入已儲存的 controlPointUuids 重用已載入的 SketchPoint
                    QVector<QString> savedCpUuids;
                    if (geomJson.contains("controlPointUuids")) {
                        for (const auto& v : geomJson["controlPointUuids"].toArray())
                            savedCpUuids.append(v.toString());
                    }
                    addSplineGeom(points, savedCpUuids);
                } else {
                    qWarning() << "[Sketch]" << name()
                               << "Spline skipped: need >= 3 points, got" << points.size();
                }
                if (!m_geometries.isEmpty() && geomJson.contains("uuid"))
                    m_geometries.last()->uuid = geomJson["uuid"].toString();
                if (geomJson.contains("role") && !m_geometries.isEmpty())
                    m_geometries.last()->role = static_cast<GeomRole>(geomJson["role"].toInt());
                break;

            case SketchGeometryType::Arc: {
                auto readPt = [&](const QString& key) -> QVector2D {
                    QJsonObject o = geomJson[key].toObject();
                    return QVector2D(o["x"].toDouble(), o["y"].toDouble());
                };

                // ✅ Phase 0B：傳入已儲存的 UUID 重用已載入的 SketchPoint
                QString savedStartUuid  = geomJson["startUuid"].toString();
                QString savedEndUuid    = geomJson["endUuid"].toString();
                QString savedCenterUuid = geomJson["centerUuid"].toString();

                if (geomJson.contains("startPt") &&
                    geomJson.contains("midPt")   &&
                    geomJson.contains("endPt")) {
                    addArcGeom(readPt("startPt"), readPt("midPt"), readPt("endPt"),
                               savedStartUuid, savedEndUuid, savedCenterUuid);
                } else if (points.size() >= 3) {
                    // legacy fallback
                    addArcGeom(points[0], points[1], points[2],
                               savedStartUuid, savedEndUuid, savedCenterUuid);
                } else {
                    qWarning() << "[Sketch]" << name()
                               << "Arc skipped: no key points in JSON";
                }
                if (!m_geometries.isEmpty() && geomJson.contains("uuid"))
                    m_geometries.last()->uuid = geomJson["uuid"].toString();
                if (geomJson.contains("role") && !m_geometries.isEmpty())
                    m_geometries.last()->role = static_cast<GeomRole>(geomJson["role"].toInt());
                break;
            }
            case SketchGeometryType::Ellipse: {
                QVector2D center(geomJson["centerX"].toDouble(),
                                 geomJson["centerY"].toDouble());
                double major = geomJson["majorRadius"].toDouble();
                double minor = geomJson["minorRadius"].toDouble();
                double angle = geomJson["angle"].toDouble(0.0);
                if (major > 0.0 && minor > 0.0) {
                    // ✅ Phase 0B：傳入已儲存的 centerUuid 重用已載入的 SketchPoint
                    QString savedCenterUuid = geomJson["centerUuid"].toString();
                    addEllipseGeom(center, major, minor, angle, savedCenterUuid);
                }
                if (!m_geometries.isEmpty() && geomJson.contains("uuid"))
                    m_geometries.last()->uuid = geomJson["uuid"].toString();
                if (geomJson.contains("role") && !m_geometries.isEmpty())
                    m_geometries.last()->role = static_cast<GeomRole>(geomJson["role"].toInt());
                break;
            }

            default:
                qWarning() << "[Sketch]" << name()
                           << "Unknown geometry type:" << static_cast<int>(type);
                break;
            }
        }  // 第二遍 for loop
    }  // json.contains("geometries")


    qDebug() << "[Sketch]" << name() << "fromJson complete:"
             << m_geometries.size() << "geometries on plane"
             << (m_plane ? m_plane->displayName() : "NULL");

    m_constraints.clear();
    if (json.contains("constraints")) {
        for (const QJsonValue& v : json["constraints"].toArray())
            m_constraints.append(SketchConstraint::fromJson(v.toObject()));
    }

    // GDIM v2 Phase 1：讀取標註（schemaVersion >= 2）。
    // 舊檔（無 "annotations" 欄位、schemaVersion 缺省視為 1）目前尚無自動
    // 遷移路徑內嵌於此處——請改用隨附的一次性腳本
    // tools/migrate_gdim_schema_v2.py 離線轉檔（見該腳本說明），
    // 避免在 runtime 為每個舊欄位寫 fallback 判斷式。
    m_annotations.clear();
    if (json.contains("annotations")) {
        for (const QJsonValue& v : json["annotations"].toArray())
            m_annotations.append(SketchAnnotation::fromJson(v.toObject()));
        // 依標註內容重新生成隱含約束（存檔時已被排除，避免與標註值不同步）
        for (const auto& a : m_annotations)
            addImplicitConstraint(a);
    }

    // ✅ 還原 Sketch 自身的參數（cant、H 等），確保重新載入後參數列表正常顯示
    if (json.contains("parameters")) {
        parameterStore()->fromJson(json["parameters"].toObject());
    }

    blockSignals(false);
    // ✅ 載入完畢後只 emit 一次
//    Q_EMIT geometryChanged();

    // ✅ 新增：載入完後求解一次，使幾何符合約束
    if (!m_constraints.isEmpty())
        solveConstraints();

    // ✅ 載入幾何後必須 rebuild 一次，讓 m_wires / shape() 有效，
    //    否則後續 Extrude::rebuild() 的 hasValidShape() 檢查會失敗
    rebuild();

    return true;
}

// ── 加入約束 ─────────────────────────────────────────────────────────────
QString Sketch::addConstraint(const SketchConstraint& c) {
    // 驗證參考的幾何是否存在
    for (const GeomRef& ref : c.refs) {
        bool found = std::any_of(m_geometries.begin(), m_geometries.end(),
                                 [&](const SketchGeometry* g){ return g->uuid == ref.geomUuid; });
        if (!found) {
            qWarning() << "[Sketch] addConstraint: geom not found:" << ref.geomUuid;
            return {};
        }
    }
    m_constraints.append(c);
    Q_EMIT constraintAdded(c.uuid);
    // 加入約束後立即嘗試求解
    solveConstraints();
    return c.uuid;
}

bool Sketch::removeConstraint(const QString& uuid) {
    for (int i=0; i<m_constraints.size(); ++i) {
        if (m_constraints[i].uuid == uuid) {
            m_constraints.removeAt(i);
            Q_EMIT constraintRemoved(uuid);
            solveConstraints();
            return true;
        }
    }
    return false;
}

SketchConstraint* Sketch::findConstraint(const QString& uuid)
{
    for (SketchConstraint& c : m_constraints) {
        if (c.uuid == uuid)
            return &c;
    }
    return nullptr;
}

bool Sketch::updateConstraintDimOffset(const QString& uuid, double offsetX, double offsetY)
{
    SketchConstraint* c = findConstraint(uuid);
    if (!c) return false;
    c->dimLineOffsetX = offsetX;
    c->dimLineOffsetY = offsetY;
    // 不重新 solve，不觸發 rebuild — overlay 由呼叫端 (UIManager) 已即時更新
    return true;
}

void Sketch::removeConstraintsOf(const QString& geomUuid) {
    m_constraints.erase(
        std::remove_if(m_constraints.begin(), m_constraints.end(),
                       [&](const SketchConstraint& c){
                           return std::any_of(c.refs.begin(), c.refs.end(),
                                              [&](const GeomRef& r){ return r.geomUuid == geomUuid; });
                       }),
        m_constraints.end());
}

// ── 標註管理（GDIM v2 Phase 1）─────────────────────────────────────────────

void Sketch::addImplicitConstraint(const SketchAnnotation& a) {
    // 先移除舊的（若存在），再視需要重新加入，簡化「同步」邏輯
    removeImplicitConstraint(a.uuid);

    auto implicit = a.toImplicitConstraint();
    if (!implicit) return;   // driving==false 或 LeaderNote 等非尺寸型別

    // 驗證參考幾何存在，行為與 addConstraint 一致
    for (const GeomRef& ref : implicit->refs) {
        bool found = std::any_of(m_geometries.begin(), m_geometries.end(),
                                 [&](const SketchGeometry* g){ return g->uuid == ref.geomUuid; });
        if (!found) {
            qWarning() << "[Sketch] addImplicitConstraint: geom not found:" << ref.geomUuid;
            return;
        }
    }
    m_constraints.append(*implicit);
}

void Sketch::removeImplicitConstraint(const QString& annotationUuid) {
    m_constraints.erase(
        std::remove_if(m_constraints.begin(), m_constraints.end(),
                       [&](const SketchConstraint& c){ return c.implicitOf == annotationUuid; }),
        m_constraints.end());
}

QString Sketch::addAnnotation(const SketchAnnotation& a) {
    SketchAnnotation* existing = findAnnotation(a.uuid);
    if (existing) {
        *existing = a;
    } else {
        m_annotations.append(a);
    }
    addImplicitConstraint(a);
    if (a.driving)
        solveConstraints();   // 觸發 constraintSolved → ConstraintOverlayManager::rebuildAll()，
                               // 確保下面 annotationAdded 觸發輕量刷新時 AIS 已存在
    Q_EMIT annotationAdded(a.uuid);
    return a.uuid;
}

bool Sketch::removeAnnotation(const QString& uuid) {
    for (int i = 0; i < m_annotations.size(); ++i) {
        if (m_annotations[i].uuid == uuid) {
            m_annotations.removeAt(i);
            removeImplicitConstraint(uuid);
            Q_EMIT annotationRemoved(uuid);
            solveConstraints();
            return true;
        }
    }
    return false;
}

SketchAnnotation* Sketch::findAnnotation(const QString& uuid) {
    for (SketchAnnotation& a : m_annotations) {
        if (a.uuid == uuid) return &a;
    }
    return nullptr;
}

QList<SketchAnnotation*> Sketch::annotationsOf(const QString& geomUuid) {
    QList<SketchAnnotation*> result;
    for (SketchAnnotation& a : m_annotations) {
        bool refs_it = std::any_of(a.refs.begin(), a.refs.end(),
                                   [&](const GeomRef& r){ return r.geomUuid == geomUuid; });
        if (refs_it) result.append(&a);
    }
    return result;
}

void Sketch::removeAnnotationsOf(const QString& geomUuid) {
    QList<QString> toRemove;
    for (const SketchAnnotation& a : m_annotations) {
        bool hit = std::any_of(a.refs.begin(), a.refs.end(),
                               [&](const GeomRef& r){ return r.geomUuid == geomUuid; });
        if (hit) toRemove.append(a.uuid);
    }
    for (const QString& uuid : toRemove) {
        m_annotations.erase(
            std::remove_if(m_annotations.begin(), m_annotations.end(),
                           [&](const SketchAnnotation& a){ return a.uuid == uuid; }),
            m_annotations.end());
        removeImplicitConstraint(uuid);
    }
}

// GDIM v2 Phase 6：重複尺寸偵測
const SketchAnnotation* Sketch::findDuplicateAnnotation(
    const QList<GeomRef>& refs, AnnotationKind kind) const
{
    auto sameRefs = [](const QList<GeomRef>& a, const QList<GeomRef>& b) {
        if (a.size() != b.size()) return false;
        QList<GeomRef> remaining = b;   // 不分順序比對（例如 A→B 距離與 B→A 距離視為相同）
        for (const auto& ra : a) {
            int idx = -1;
            for (int i = 0; i < remaining.size(); ++i) {
                if (remaining[i].geomUuid == ra.geomUuid && remaining[i].handle == ra.handle) {
                    idx = i; break;
                }
            }
            if (idx < 0) return false;
            remaining.removeAt(idx);
        }
        return true;
    };

    for (const auto& a : m_annotations) {
        if (a.kind != kind) continue;
        if (sameRefs(a.refs, refs)) return &a;
    }
    return nullptr;
}

SolveResult Sketch::solveConstraints() {
    gp_Dir normal(0, 0, 1);
    if (m_plane) {
        QVector3D n = m_plane->normal();
        normal = gp_Dir(n.x(), n.y(), n.z());
    }
    // 統一從 normalGeometries() 取得幾何列表（含 SketchPoint）
    auto geoms = normalGeometries();
    auto result = m_solver.solve(geoms, m_constraints, normal);
    Q_EMIT constraintSolved(result);
    if (result.status != SolveStatus::SolverError) {
        // unpackVariables 已同時更新：
        //   - SketchLine.start/end（由 line 自身的 DOF 決定）
        //   - SketchPoint.pos（由 SketchPoint 自身的 DOF 決定）
        //
        // 若某條約束直接操作 SketchLine DOF（refs = lineUuid+Start/End），
        // Solver 更新了 SketchLine 但沒有更新對應的 SketchPoint。
        // 反之，若約束操作 SketchPoint UUID，Solver 更新了 SketchPoint
        // 但 SketchLine 需要跟著同步。
        //
        // 策略：先讓 line.start/end → SketchPoint（確保 Solver 對 line DOF
        // 的修改能傳播到 SketchPoint），再由 syncGeometryFromPoints 統一
        // 以 SketchPoint 為單一來源同步回所有曲線。
        for (auto* g : geoms) {
            if (auto* line = dynamic_cast<SketchLine*>(g)) {
                if (auto* ps = point(line->startUuid)) ps->pos = line->start;
                if (auto* pe = point(line->endUuid))   pe->pos = line->end;
            }
            // Arc/Circle 的 SketchPoint 由 unpackVariables 直接更新，不需覆蓋
        }
        // 以 SketchPoint 為單一來源，同步所有曲線座標
        syncGeometryFromPoints();
        markDirty();
        Q_EMIT geometryChanged();

        // ★ Solver 可能改變了幾何尺寸（如圓的 radius），需重建 AIS shapes。
        //   rebuildShapesOnly() 現在會逐一比對每個幾何的 fingerprint，
        //   只對「真的改變」的 shape 做 Erase/Display，未變動的沿用舊 handle。
        if (!m_aisContext.IsNull())
            rebuildShapesOnly();
    }

    // ✅ Step 13: 將全域 SolveStatus 傳遞給所有 SketchPointAIS，即時更新顏色
    //   只有狀態真的改變的點才呼叫 Redisplay／UpdateCurrentViewer，
    //   避免每次 solve 都對所有點觸發一次 context 更新。
    if (!m_aisContext.IsNull()) {
        bool anyStatusChanged = false;
        for (const auto& obj : m_aisShapes) {
            if (auto ptAis = Handle(SketchPointAIS)::DownCast(obj)) {
                if (ptAis->solveStatus() == result.status) continue;
                ptAis->updateSolveStatus(result.status);
                m_aisContext->Redisplay(ptAis, Standard_False);
                anyStatusChanged = true;
            }
        }
        if (anyStatusChanged)
            m_aisContext->UpdateCurrentViewer();
    }

    return result;
}

SolveResult Sketch::solveWithStore(const aicad::core::ParameterStore* store) {
    // 用指定 store 求值所有尺寸約束（instance store 內含父子 fallback）
    if (store) {
        for (auto& c : m_constraints) {
            if (c.isDimensional() && !c.paramExpr.isEmpty())
                c.evaluateValue(store);
        }
    }
    gp_Dir normal(0, 0, 1);
    if (m_plane) {
        QVector3D n = m_plane->normal();
        normal = gp_Dir(n.x(), n.y(), n.z());
    }
    return m_solver.solve(m_geometries, m_constraints, normal);
}

int Sketch::degreesOfFreedom() const {
    return ConstraintSolver::computeDOF(normalGeometries(), m_constraints);
}

// ── 便捷 API ──────────────────────────────────────────────────────────────
QString Sketch::constrainCoincident(const GeomRef& a, const GeomRef& b) {
    return addConstraint(SketchConstraint::makeCoincident(a, b));
}
QString Sketch::constrainHorizontal(const QString& uuid) {
    return addConstraint(SketchConstraint::makeHorizontal(uuid));
}

QString Sketch::constrainVertical(const QString& lineUuid) {
    return addConstraint(SketchConstraint::makeVertical(lineUuid));
}
QString Sketch::constrainParallel(const QString& lineA, const QString& lineB) {
    return addConstraint(SketchConstraint::makeParallel(lineA, lineB));
}
QString Sketch::constrainPerpendicular(const QString& lineA, const QString& lineB) {
    return addConstraint(SketchConstraint::makePerpendicular(lineA, lineB));
}
QString Sketch::constrainTangent(const QString& geomA, const QString& geomB) {
    return addConstraint(SketchConstraint::makeTangent(geomA, geomB));
}
QString Sketch::constrainEqualLength(const QString& lineA, const QString& lineB) {
    return addConstraint(SketchConstraint::makeEqualLength(lineA, lineB));
}
QString Sketch::constrainEqualRadius(const QString& circA, const QString& circB) {
    return addConstraint(SketchConstraint::makeEqualRadius(circA, circB));
}
QString Sketch::constrainConcentric(const QString& geomA, const QString& geomB) {
    return addConstraint(SketchConstraint::makeConcentric(geomA, geomB));
}
QString Sketch::constrainFixed(const QString& geomUuid) {
    return addConstraint(SketchConstraint::makeFixed(geomUuid));
}
QString Sketch::constrainDistance(const GeomRef& a, const GeomRef& b, double dist) {
    return addConstraint(SketchConstraint::makeFixedDistance(a, b, dist));
}
QString Sketch::constrainRadius(const QString& geomUuid, double radius) {
    return addConstraint(SketchConstraint::makeFixedRadius(geomUuid, radius));
}

// GeomRef overload：用於 ConstraintPickSession 選到的圓/弧 ref
QString Sketch::constrainRadius(const GeomRef& ref, double radius) {
    // FixedRadius 只需要幾何 UUID，handle 用 WholeGeom 即可
    SketchConstraint c;
    c.uuid    = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.type    = ConstraintType::FixedRadius;
    c.value   = radius;
    c.refs    = { GeomRef(ref.geomUuid, GeomHandle::WholeGeom) };
    c.driving = true;
    return addConstraint(c);
}

// ── 草圖平面參考幾何（X 軸 / Y 軸 / 原點）lazy-init ──────────────────────
// 這三個幾何在第一次被請求時建立，以「Fixed」約束固定位置。
// GeomRole::Construction 使它們不出現在普通幾何清單（未來可過濾）。

QString Sketch::xAxisGeomUuid()
{
    if (!m_xAxisGeomUuid.isEmpty()) return m_xAxisGeomUuid;

    const QVector2D p0(-200.0f, 0.0f);
    const QVector2D p1( 200.0f, 0.0f);

    auto* sp0  = new SketchPoint(p0, SketchPoint::Origin::Explicit, GeomRole::Construction);
    auto* sp1  = new SketchPoint(p1, SketchPoint::Origin::Explicit, GeomRole::Construction);
    auto* line = new SketchLine(p0, p1, GeomRole::Construction);
    line->startUuid = sp0->uuid;
    line->endUuid   = sp1->uuid;

    m_geometries.append(sp0);
    m_geometries.append(sp1);
    m_geometries.append(line);

    // Fixed 約束鎖住兩端點，使軸線不可移動
    for (auto* sp : { sp0, sp1 }) {
        SketchConstraint c;
        c.uuid  = QUuid::createUuid().toString(QUuid::WithoutBraces);
        c.type  = ConstraintType::Fixed;
        c.refs  = { GeomRef(sp->uuid, GeomHandle::WholeGeom) };
        m_constraints.append(c);
    }

    m_xAxisGeomUuid = line->uuid;
    return m_xAxisGeomUuid;
}

QString Sketch::yAxisGeomUuid()
{
    if (!m_yAxisGeomUuid.isEmpty()) return m_yAxisGeomUuid;

    const QVector2D p0(0.0f, -200.0f);
    const QVector2D p1(0.0f,  200.0f);

    auto* sp0  = new SketchPoint(p0, SketchPoint::Origin::Explicit, GeomRole::Construction);
    auto* sp1  = new SketchPoint(p1, SketchPoint::Origin::Explicit, GeomRole::Construction);
    auto* line = new SketchLine(p0, p1, GeomRole::Construction);
    line->startUuid = sp0->uuid;
    line->endUuid   = sp1->uuid;

    m_geometries.append(sp0);
    m_geometries.append(sp1);
    m_geometries.append(line);

    for (auto* sp : { sp0, sp1 }) {
        SketchConstraint c;
        c.uuid  = QUuid::createUuid().toString(QUuid::WithoutBraces);
        c.type  = ConstraintType::Fixed;
        c.refs  = { GeomRef(sp->uuid, GeomHandle::WholeGeom) };
        m_constraints.append(c);
    }

    m_yAxisGeomUuid = line->uuid;
    return m_yAxisGeomUuid;
}

QString Sketch::originPointUuid()
{
    if (!m_originPointUuid.isEmpty()) return m_originPointUuid;

    auto* sp = new SketchPoint(QVector2D(0.0f, 0.0f),
                               SketchPoint::Origin::Explicit,
                               GeomRole::Construction);
    m_geometries.append(sp);

    SketchConstraint c;
    c.uuid  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.type  = ConstraintType::Fixed;
    c.refs  = { GeomRef(sp->uuid, GeomHandle::WholeGeom) };
    m_constraints.append(c);

    m_originPointUuid = sp->uuid;
    return m_originPointUuid;
}

// 固定某個端點的 X 座標
QString Sketch::constrainFixedX(const GeomRef& point, double x) {
    SketchConstraint c;
    c.uuid    = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.type    = ConstraintType::FixedX;
    c.value   = x;
    c.refs    = { point };
    c.driving = true;
    return addConstraint(c);
}

// 固定某個端點的 Y 座標
QString Sketch::constrainFixedY(const GeomRef& point, double y) {
    SketchConstraint c;
    c.uuid    = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.type    = ConstraintType::FixedY;
    c.value   = y;
    c.refs    = { point };
    c.driving = true;
    return addConstraint(c);
}

// 兩條線（各取一個點）的夾角（弧度）
QString Sketch::constrainAngle(const GeomRef& a, const GeomRef& b, double angleRad) {
    SketchConstraint c;
    c.uuid    = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.type    = ConstraintType::FixedAngleDim;
    c.value   = angleRad;
    c.refs    = { a, b };
    c.driving = true;
    return addConstraint(c);
}

QString Sketch::constrainPointOnCurve(const GeomRef& point, const QString& curveUuid) {
    return addConstraint(SketchConstraint::makePointOnCurve(point, curveUuid));
}

// ============================================================================
// ✅ 新增：標準平面名稱解析
//    按 planeName 比對 PlaneManager 中已登記的標準平面
// ============================================================================
Plane* Sketch::resolveStandardPlane(const QString& planeName) {
    PlaneManager* manager = PlaneManager::instance();
    if (!manager) return nullptr;

    QList<Plane*> planes = manager->planes();

    // ── 優先：完全比對 displayName / name ──────────────────────────
    for (Plane* p : planes) {
        if (p->displayName() == planeName || p->name() == planeName) {
            return p;
        }
    }

    // ── 次要：依幾何類型比對 ────────────────────────────────────────
    //   planeName 可能是 "XY", "YZ", "XZ", "ZX" 等
    const QString upper = planeName.trimmed().toUpper();

    for (Plane* p : planes) {
        if ((upper == "XY" || upper == "XY PLANE") && p->isXY()) return p;
        if ((upper == "YZ" || upper == "YZ PLANE") && p->isYZ()) return p;
        if ((upper == "XZ" || upper == "XZ PLANE") && p->isXZ()) return p;
        if ((upper == "ZX" || upper == "ZX PLANE") && p->isZX()) return p;
    }

    // ── 次要：依 Plane::Type enum 比對 ──────────────────────────────
    for (Plane* p : planes) {
        if (upper == "XY" && p->type() == Plane::Type::XY) return p;
        if (upper == "YZ" && p->type() == Plane::Type::YZ) return p;
        if ((upper == "XZ" || upper == "ZX") &&
            (p->type() == Plane::Type::XZ || p->type() == Plane::Type::ZX)) {
            return p;
        }
    }

    return nullptr;
}

// ============================================================================
// ✅ 新增：從 planeData JSON 重建平面
//    先嘗試幾何比對現有平面，若無則建立新平面
// ============================================================================
Plane* Sketch::reconstructPlaneFromJson(const QJsonObject& planeJson,
                                        const QString& hint) {
    if (planeJson.isEmpty()) return nullptr;

    PlaneManager* manager = PlaneManager::instance();
    if (!manager) return nullptr;

    // ── 讀取幾何 ────────────────────────────────────────────────────
    QJsonObject geometry = planeJson["geometry"].toObject();
    if (geometry.isEmpty()) {
        // 舊格式：geometry 直接在頂層
        // 嘗試以 hint 名稱解析
        return resolveStandardPlane(hint);
    }

    auto readVec3 = [&](const QString& key) -> QVector3D {
        QJsonArray arr = geometry[key].toArray();
        if (arr.size() < 3) return QVector3D();
        return QVector3D(arr[0].toDouble(), arr[1].toDouble(), arr[2].toDouble());
    };

    QVector3D origin = readVec3("origin");
    QVector3D normal = readVec3("normal");
    QVector3D xAxis  = readVec3("xAxis");

    if (normal.length() < 1e-6) {
        qWarning() << "[Sketch] reconstructPlaneFromJson: invalid normal";
        return nullptr;
    }

    const double tol = 1e-4;

    // ── 嘗試與現有平面幾何比對（避免重複建立） ─────────────────────
    for (Plane* p : manager->planes()) {
        if ((p->origin() - origin).length() < tol &&
            (p->normal() - normal).length() < tol  &&
            (p->xAxis()  - xAxis ).length() < tol) {
            qDebug() << "[Sketch] reconstructPlaneFromJson: matched existing plane"
                     << p->displayName();
            return p;
        }
    }

    // ── 建立新平面並向 PlaneManager 登記 ────────────────────────────
    QString newName = hint.isEmpty()
                          ? planeJson["name"].toString("RestoredPlane")
                          : hint;

    Plane* newPlane = manager->createPlane(Plane::Type::Custom, newName);
    if (!newPlane) {
        qWarning() << "[Sketch] reconstructPlaneFromJson: createPlane failed";
        return nullptr;
    }

    newPlane->setCoordinateSystem(origin, normal, xAxis);

    qDebug() << "[Sketch] reconstructPlaneFromJson: created new plane"
             << newName
             << "normal(" << normal.x() << normal.y() << normal.z() << ")";

    return newPlane;
}

// ============================================================================
// Plane signals
// ============================================================================

void Sketch::onPlaneAboutToBeDeleted() {
    qWarning() << "[Sketch]" << name() << "associated plane is being deleted!";
    disconnectPlaneSignals();
    createDefaultPlane();
    Q_EMIT planeChanged(m_plane);
    Q_EMIT rebuildRequested();
}

void Sketch::onPlaneGeometryChanged() {
    qDebug() << "[Sketch]" << name() << "plane geometry changed";
    Q_EMIT rebuildRequested();
}

void Sketch::scheduleRebuild() {
    // 參數變更後重新求值尺寸約束並重建幾何
    if (!m_parameterStore) return;
    for (auto& c : m_constraints) {
        if (c.isDimensional() && !c.paramExpr.isEmpty())
            c.evaluateValue(m_parameterStore);
    }
    rebuild();
}

gp_Pnt Sketch::toWorld(const QVector2D& point) const {
    if (!hasValidPlane()) {
        qWarning() << "[Sketch]" << name() << "No valid plane for conversion";
        return gp_Pnt(point.x(), point.y(), 0);
    }

    QVector3D worldPt = m_plane->toWorld(point.x(), point.y());
    return gp_Pnt(worldPt.x(), worldPt.y(), worldPt.z());
}

void Sketch::createDefaultPlane() {
    PlaneManager* manager = PlaneManager::instance();

    for (Plane* p : manager->planes()) {
        if (p->type() == Plane::Type::XY && p->isXY()) {
            m_plane = p;
            connectPlaneSignals();
            qDebug() << "[Sketch]" << name() << "using existing XY plane";
            return;
        }
    }

    m_plane = manager->createPlane(
        Plane::Type::XY,
        QString("%1_DefaultPlane").arg(name()));

    connectPlaneSignals();
    qDebug() << "[Sketch]" << name() << "created new default XY plane";
}

void Sketch::connectPlaneSignals() {
    if (!m_plane) return;
    connect(m_plane, &Plane::aboutToBeDeleted, this, &Sketch::onPlaneAboutToBeDeleted);
    connect(m_plane, &Plane::geometryChanged,  this, &Sketch::onPlaneGeometryChanged);
}

void Sketch::disconnectPlaneSignals() {
    if (!m_plane) return;
    disconnect(m_plane, nullptr, this, nullptr);
}

// ============================================================================
// AIS display
// ============================================================================

QList<TopoDS_Wire> Sketch::wires() const {
    return m_wires;
}

QList<Handle(AIS_InteractiveObject)> Sketch::aisShapes() const {
    return m_aisShapes;
}

const QList<QString>& Sketch::aisShapeUuids() const {
    return m_aisShapeUuids;
}

QList<Handle(AIS_InteractiveObject)> Sketch::displayInContext(
    const Handle(AIS_InteractiveContext)& context) {
    m_aisContext = context;
    if (context.IsNull()) return {};

    // 顯示所有幾何 AIS（含 SketchPointAIS）
    for (const auto& obj : m_aisShapes) {
        if (obj.IsNull()) continue;
        context->Display(obj, Standard_False);
        // SketchPointAIS 需要明確啟用 selection mode 0
        if (Handle(SketchPointAIS)::DownCast(obj)) {
            context->Activate(obj, 0, Standard_False);
        }
    }

    // 建構線：顯示但不可選
    for (const Handle(AIS_Shape)& s : m_constructionShapes) {
        if (!s.IsNull()) {
            context->Display(s, Standard_False);
            context->Deactivate(s);
        }
    }

    context->UpdateCurrentViewer();
    return m_aisShapes;
}

void Sketch::eraseFromContext(const Handle(AIS_InteractiveContext)& context) {
    if (context.IsNull()) return;
    for (const auto& obj : m_aisShapes)
        if (!obj.IsNull()) context->Erase(obj, Standard_False);
    for (const Handle(AIS_Shape)& s : m_constructionShapes)
        if (!s.IsNull()) context->Erase(s, Standard_False);
    context->UpdateCurrentViewer();
}

TopoDS_Wire Sketch::mainWire() const {
    return m_wires.isEmpty() ? TopoDS_Wire() : m_wires.first();
}

bool Sketch::hasClosedProfile() const {
    for (const TopoDS_Wire& wire : m_wires) {
        if (!wire.IsNull() && wire.Closed()) return true;
    }
    return false;
}

QVector<SketchRegion> Sketch::detectRegions() const
{
    SketchLoopFinder finder;
    QVector<SketchRegion> regions = finder.findRegions(this);

    // 按 outer loop 頂點數量（近似面積）降序排列
    std::sort(regions.begin(), regions.end(),
              [](const SketchRegion& a,
                 const SketchRegion& b) {
                  return a.outerLoop.edgeUuids.size() >
                         b.outerLoop.edgeUuids.size();
              });
    return regions;
}

std::optional<SketchRegion>
Sketch::pickRegion(const QVector2D& sketchPt) const
{
    for (const auto& region : detectRegions()) {
        if (region.contains(sketchPt))
            return region;
    }
    return std::nullopt;
}

// Sketch.cpp 新增實作
bool Sketch::hasExtrudableProfile() const {
    if (m_wires.isEmpty()) return false;
    if (hasClosedProfile()) return true;

    // 嘗試連接所有 edge，看能否形成封閉輪廓
    Handle(TopTools_HSequenceOfShape) edges = new TopTools_HSequenceOfShape;
    for (const TopoDS_Wire& w : m_wires) {
        TopExp_Explorer exp(w, TopAbs_EDGE);
        for (; exp.More(); exp.Next())
            edges->Append(exp.Current());
    }
    if (edges->IsEmpty()) return false;

    Handle(TopTools_HSequenceOfShape) closed = new TopTools_HSequenceOfShape;
    ShapeAnalysis_FreeBounds::ConnectEdgesToWires(
        edges, Precision::Confusion(), Standard_False, closed);
    return !closed->IsEmpty();
}
// ═════════════════════════════════════════════════════════════════════════════
// Phase 0B：SketchPoint 一等公民 — 點管理 API 實作
// ═════════════════════════════════════════════════════════════════════════════

QString Sketch::addPoint(const QVector2D& pos, SketchPoint::Origin origin)
{
    auto* pt = new SketchPoint(pos, origin);
    m_geometries.append(pt);
    m_uuidToGeomIndex[pt->uuid] = m_geometries.size() - 1;
    return pt->uuid;
}

SketchPoint* Sketch::point(const QString& uuid) const
{
    auto* g = findGeometry(uuid);
    return (g && g->type == SketchGeometryType::Point)
           ? static_cast<SketchPoint*>(g) : nullptr;
}

QList<SketchPoint*> Sketch::points() const
{
    QList<SketchPoint*> result;
    for (auto* g : m_geometries)
        if (g->type == SketchGeometryType::Point)
            result.append(static_cast<SketchPoint*>(g));
    return result;
}

void Sketch::movePoint(const QString& uuid, const QVector2D& newPos)
{
    auto* pt = point(uuid);
    if (!pt) return;

    pt->pos = newPos;
    pt->points[0] = newPos;

    // 同步所有引用此點的曲線的 start/end 座標
    syncGeometryFromPoints();
    Q_EMIT geometryChanged();
}

void Sketch::mergePoints(const QString& fromUuid, const QString& toUuid)
{
    if (fromUuid == toUuid) return;
    auto* fromPt = point(fromUuid);
    auto* toPt   = point(toUuid);
    if (!fromPt || !toPt) return;

    // 更新所有引用 fromUuid 的曲線
    for (auto* g : m_geometries) {
        if (auto* line = dynamic_cast<SketchLine*>(g)) {
            if (line->startUuid == fromUuid) line->startUuid = toUuid;
            if (line->endUuid   == fromUuid) line->endUuid   = toUuid;
        }
        if (auto* arc = dynamic_cast<SketchArc*>(g)) {
            if (arc->startUuid  == fromUuid) arc->startUuid  = toUuid;
            if (arc->endUuid    == fromUuid) arc->endUuid    = toUuid;
            if (arc->centerUuid == fromUuid) arc->centerUuid = toUuid;
        }
        if (auto* circ = dynamic_cast<SketchCircle*>(g)) {
            if (circ->centerUuid == fromUuid) circ->centerUuid = toUuid;
        }
        if (auto* pline = dynamic_cast<SketchPolyline*>(g)) {
            for (auto& uuid : pline->vertexUuids)
                if (uuid == fromUuid) uuid = toUuid;
        }
        if (auto* spline = dynamic_cast<SketchSpline*>(g)) {
            for (auto& uuid : spline->controlPointUuids)
                if (uuid == fromUuid) uuid = toUuid;
        }
        if (auto* ellipse = dynamic_cast<SketchEllipse*>(g)) {
            if (ellipse->centerUuid == fromUuid) ellipse->centerUuid = toUuid;
        }
    }

    // 刪除 from 點（從 m_geometries 中移除並釋放）
    m_geometries.removeAll(fromPt);
    m_uuidToGeomIndex.remove(fromUuid);
    delete fromPt;

    syncGeometryFromPoints();
    Q_EMIT geometryChanged();
}

QList<SketchGeometry*> Sketch::curvesReferencingPoint(const QString& ptUuid) const
{
    QList<SketchGeometry*> result;
    for (auto* g : m_geometries) {
        if (auto* line = dynamic_cast<SketchLine*>(g)) {
            if (line->startUuid == ptUuid || line->endUuid == ptUuid)
                result.append(g);
        } else if (auto* arc = dynamic_cast<SketchArc*>(g)) {
            if (arc->startUuid == ptUuid || arc->endUuid == ptUuid || arc->centerUuid == ptUuid)
                result.append(g);
        } else if (auto* circ = dynamic_cast<SketchCircle*>(g)) {
            if (circ->centerUuid == ptUuid)
                result.append(g);
        } else if (auto* pline = dynamic_cast<SketchPolyline*>(g)) {
            if (pline->vertexUuids.contains(ptUuid))
                result.append(g);
        } else if (auto* spline = dynamic_cast<SketchSpline*>(g)) {
            if (spline->controlPointUuids.contains(ptUuid))
                result.append(g);
        } else if (auto* ellipse = dynamic_cast<SketchEllipse*>(g)) {
            if (ellipse->centerUuid == ptUuid)
                result.append(g);
        }
    }
    return result;
}

QString Sketch::addLineGeom(const QVector2D& p1, const QVector2D& p2,
                             const QString& reuseStart, const QString& reuseEnd,
                             GeomRole role)
{
    QString startUuid = reuseStart.isEmpty()
        ? addPoint(p1, SketchPoint::Origin::Endpoint)
        : reuseStart;
    QString endUuid = reuseEnd.isEmpty()
        ? addPoint(p2, SketchPoint::Origin::Endpoint)
        : reuseEnd;

    auto* line = new SketchLine(p1, p2);
    line->startUuid = startUuid;
    line->endUuid   = endUuid;
    line->role      = role;
    m_geometries.append(line);

    Q_EMIT geometryChanged();
    Q_EMIT rebuildRequested();
    return line->uuid;
}

QString Sketch::addCircleGeom(const QVector2D& center, double radius,
                               const QString& reuseCenterUuid)
{
    QString centerUuid = reuseCenterUuid.isEmpty()
        ? addPoint(center, SketchPoint::Origin::Center)
        : reuseCenterUuid;

    auto* circ = new SketchCircle(center, radius);
    circ->centerUuid = centerUuid;
    m_geometries.append(circ);

    Q_EMIT geometryChanged();
    Q_EMIT rebuildRequested();
    return circ->uuid;
}

QStringList Sketch::addLineChainGeom(const QVector<QVector2D>& pts, bool closed)
{
    QStringList lineUuids;
    if (pts.size() < 2) {
        qWarning() << "[Sketch]" << name() << "addLineChainGeom needs at least 2 points";
        return lineUuids;
    }

    // Step 1：逐段建立獨立的 SketchLine（每段各自擁有獨立端點，不共用同一個 SketchPoint）
    for (int i = 0; i < pts.size() - 1; ++i) {
        lineUuids.append(addLineGeom(pts[i], pts[i + 1]));
    }
    if (closed) {
        lineUuids.append(addLineGeom(pts.last(), pts.first()));
    }

    // Step 2：相鄰線段的接點自動加上 Coincident 束制（前一段終點 ≈ 下一段起點）
    for (int i = 0; i + 1 < lineUuids.size(); ++i) {
        constrainCoincident(GeomRef(lineUuids[i],     GeomHandle::End),
                             GeomRef(lineUuids[i + 1], GeomHandle::Start));
    }
    // Step 3：封閉迴路：最後一段終點 與 第一段起點 重合
    if (closed && lineUuids.size() >= 2) {
        constrainCoincident(GeomRef(lineUuids.last(),  GeomHandle::End),
                             GeomRef(lineUuids.first(), GeomHandle::Start));
    }

    return lineUuids;
}

QString Sketch::addPolylineGeom(const QVector<QVector2D>& pts, bool closed,
                                 const QVector<QString>& reuseVertexUuids)
{
    auto* pline = new SketchPolyline(pts, closed);

    for (int i = 0; i < pts.size(); ++i) {
        QString uuid;
        if (i < reuseVertexUuids.size() && !reuseVertexUuids[i].isEmpty()) {
            uuid = reuseVertexUuids[i];
        } else {
            // 首尾共點（closed）：最後一點複用第一點
            if (closed && i == pts.size() - 1 && !pline->vertexUuids.isEmpty()) {
                uuid = pline->vertexUuids.first();
            } else {
                uuid = addPoint(pts[i], SketchPoint::Origin::Endpoint);
            }
        }
        pline->vertexUuids.append(uuid);
    }

    m_geometries.append(pline);
    Q_EMIT geometryChanged();
    Q_EMIT rebuildRequested();
    return pline->uuid;
}

QString Sketch::addSplineGeom(const QVector<QVector2D>& pts,
                               const QVector<QString>& reuseControlPointUuids)
{
    if (pts.size() < 3) {
        qWarning() << "[Sketch]" << name() << "spline needs at least 3 points";
        return {};
    }

    auto* spline = new SketchSpline(pts);

    for (int i = 0; i < pts.size(); ++i) {
        QString uuid;
        if (i < reuseControlPointUuids.size() && !reuseControlPointUuids[i].isEmpty()) {
            uuid = reuseControlPointUuids[i];
        } else {
            uuid = addPoint(pts[i], SketchPoint::Origin::Endpoint);
        }
        spline->controlPointUuids.append(uuid);
    }

    m_geometries.append(spline);
    Q_EMIT geometryChanged();
    Q_EMIT rebuildRequested();
    return spline->uuid;
}

QString Sketch::addEllipseGeom(const QVector2D& center,
                                double majorRadius, double minorRadius, double angle,
                                const QString& reuseCenterUuid)
{
    if (majorRadius <= 0 || minorRadius <= 0) {
        qWarning() << "[Sketch]" << name() << "ellipse radii must be positive";
        return {};
    }
    if (majorRadius < minorRadius) {
        qWarning() << "[Sketch]" << name() << "major radius must be >= minor radius";
        return {};
    }

    QString centerUuid = reuseCenterUuid.isEmpty()
        ? addPoint(center, SketchPoint::Origin::Center)
        : reuseCenterUuid;

    auto* ellipse = new SketchEllipse(center, majorRadius, minorRadius, angle);
    ellipse->centerUuid = centerUuid;
    m_geometries.append(ellipse);

    Q_EMIT geometryChanged();
    Q_EMIT rebuildRequested();
    return ellipse->uuid;
}

void Sketch::syncGeometryFromPoints()
{
    for (auto* g : m_geometries) {
        if (auto* line = dynamic_cast<SketchLine*>(g)) {
            if (auto* ps = point(line->startUuid)) {
                line->start = ps->pos;
                if (!line->points.isEmpty()) line->points[0] = ps->pos;
            }
            if (auto* pe = point(line->endUuid)) {
                line->end = pe->pos;
                if (line->points.size() > 1) line->points[1] = pe->pos;
            }
        } else if (auto* circ = dynamic_cast<SketchCircle*>(g)) {
            if (auto* pc = point(circ->centerUuid)) {
                circ->center = pc->pos;
            }
        } else if (auto* arc = dynamic_cast<SketchArc*>(g)) {
            // ✅ GAP 1 Fix: Arc 的 SketchPoint 同步回 arc->points[]
            // Arc 以 OCCT curve 為主，points[] 只作顯示用
            // [0]=起點 [1]=中點 [2]=終點 (若存在)
            if (auto* ps = point(arc->startUuid)) {
                if (!arc->points.isEmpty()) arc->points[0] = ps->pos;
            }
            if (auto* pe = point(arc->endUuid)) {
                if (arc->points.size() > 2) arc->points[2] = pe->pos;
            }
        } else if (auto* pline = dynamic_cast<SketchPolyline*>(g)) {
            // Phase 0B：同步 Polyline 各頂點座標
            for (int i = 0; i < pline->vertexUuids.size(); ++i) {
                if (auto* pt = point(pline->vertexUuids[i])) {
                    if (i < pline->points.size())
                        pline->points[i] = pt->pos;
                }
            }
        } else if (auto* spline = dynamic_cast<SketchSpline*>(g)) {
            // Phase 0B：同步 Spline 各控制點座標
            for (int i = 0; i < spline->controlPointUuids.size(); ++i) {
                if (auto* pt = point(spline->controlPointUuids[i])) {
                    if (i < spline->points.size())
                        spline->points[i] = pt->pos;
                }
            }
        } else if (auto* ellipse = dynamic_cast<SketchEllipse*>(g)) {
            // Phase 0B：同步 Ellipse 圓心座標
            if (auto* pc = point(ellipse->centerUuid)) {
                ellipse->center = pc->pos;
            }
        }
    }
}

void Sketch::migrateFromLegacyFormat(const QJsonObject& json)
{
    // 舊格式沒有 "points" 陣列，從幾何座標重建
    // 相同座標的點視為同一點（模擬舊版 Coincident 語意）
    QHash<QString, QString> coordToPointUuid;  // "x,y" → pointUuid

    auto getOrCreate = [&](double x, double y) -> QString {
        QString key = QString("%1,%2").arg(x, 0, 'f', 6).arg(y, 0, 'f', 6);
        if (!coordToPointUuid.contains(key)) {
            coordToPointUuid[key] = addPoint(QVector2D(x, y), SketchPoint::Origin::Endpoint);
        }
        return coordToPointUuid[key];
    };

    for (const auto& gv : json["geometries"].toArray()) {
        QJsonObject g = gv.toObject();
        if (g["type"].toString() == "Line") {
            QString lineUuid; // will be set when line is added later by fromJson
            Q_UNUSED(getOrCreate(g["x1"].toDouble(), g["y1"].toDouble()));
            Q_UNUSED(getOrCreate(g["x2"].toDouble(), g["y2"].toDouble()));
        }
    }
    qDebug() << "[Sketch] migrateFromLegacyFormat: created" << points().size() << "points";
}

QList<Sketch::PointInfo> Sketch::listPoints() const
{
    QList<PointInfo> result;
    for (auto* g : m_geometries) {
        if (g->type != SketchGeometryType::Point) continue;
        auto* pt = static_cast<SketchPoint*>(g);
        PointInfo info;
        info.uuid   = pt->uuid;
        info.pos    = pt->pos;
        info.origin = pt->origin;

        for (auto* g : m_geometries) {
            if (auto* line = dynamic_cast<SketchLine*>(g)) {
                if (line->startUuid == pt->uuid)
                    info.referencedBy.append(line->uuid.left(8) + ".Start");
                if (line->endUuid == pt->uuid)
                    info.referencedBy.append(line->uuid.left(8) + ".End");
            } else if (auto* circ = dynamic_cast<SketchCircle*>(g)) {
                if (circ->centerUuid == pt->uuid)
                    info.referencedBy.append(circ->uuid.left(8) + ".Ctr");
            } else if (auto* pline = dynamic_cast<SketchPolyline*>(g)) {
                for (int i = 0; i < pline->vertexUuids.size(); ++i) {
                    if (pline->vertexUuids[i] == pt->uuid)
                        info.referencedBy.append(pline->uuid.left(8) + ".V" + QString::number(i));
                }
            } else if (auto* spline = dynamic_cast<SketchSpline*>(g)) {
                for (int i = 0; i < spline->controlPointUuids.size(); ++i) {
                    if (spline->controlPointUuids[i] == pt->uuid)
                        info.referencedBy.append(spline->uuid.left(8) + ".CP" + QString::number(i));
                }
            } else if (auto* ellipse = dynamic_cast<SketchEllipse*>(g)) {
                if (ellipse->centerUuid == pt->uuid)
                    info.referencedBy.append(ellipse->uuid.left(8) + ".Ctr");
            }
        }
        result.append(info);
    }
    return result;
}

// ── Phase 0B：新增 Constraint 便捷方法 ─────────────────────────────────────

QString Sketch::constrainMidpoint(const GeomRef& point, const QString& lineUuid) {
    return addConstraint(SketchConstraint::makeMidpoint(point, lineUuid));
}

QString Sketch::constrainSymmetric(const GeomRef& a, const GeomRef& b, const QString& axisUuid) {
    SketchConstraint c;
    c.uuid  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.type  = ConstraintType::Symmetric;
    c.refs  = { a, b, GeomRef(axisUuid, GeomHandle::Curve) };
    return addConstraint(c);
}

QString Sketch::constrainCollinear(const QString& lineA, const QString& lineB) {
    SketchConstraint c;
    c.uuid  = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.type  = ConstraintType::Collinear;
    c.refs  = { GeomRef(lineA, GeomHandle::Curve), GeomRef(lineB, GeomHandle::Curve) };
    return addConstraint(c);
}

} // namespace cad
} // namespace aicad