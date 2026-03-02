/**
 * @file Sketch_Enhanced.cpp
 * @brief 與增強版 Plane 整合的 Sketch 類別實作
 */

#include "Sketch.h"
#include "Document.h"
#include "PlaneManager.h"
#include <QDebug>
#include <QtMath>
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
#include <Geom_TrimmedCurve.hxx>
#include <QJsonArray>

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
    // 斷開平面信號
    disconnectPlaneSignals();

    // 清理幾何元素
    clearGeometry();

    // 注意：不刪除 m_plane，由 PlaneManager 管理

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

    // 斷開舊平面的連接
    disconnectPlaneSignals();

    Plane* oldPlane = m_plane;
    m_plane = plane;

    // 連接新平面的信號
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
        delete m_geometries.takeAt(index);
        Q_EMIT geometryChanged();
        Q_EMIT rebuildRequested();
    }
}

void Sketch::clearGeometry() {
    qDeleteAll(m_geometries);
    m_geometries.clear();
    Q_EMIT geometryChanged();
}

void Sketch::addLine(const QVector2D& p1, const QVector2D& p2) {
    SketchLine* line = new SketchLine(p1, p2);
    addGeometry(line);

    // 如果有有效平面，記錄 3D 座標
    if (hasValidPlane()) {
        QVector3D w1 = m_plane->toWorld(p1);
        QVector3D w2 = m_plane->toWorld(p2);
        qDebug() << "[Sketch]" << name() << "added line:"
                 << "Plane(" << p1 << "->" << p2 << ")"
                 << "World(" << w1 << "->" << w2 << ")";
    }
}

void Sketch::addPolyline(const QVector<QVector2D>& points, bool closed) {
    SketchPolyline* polyline = new SketchPolyline(points, closed);
    addGeometry(polyline);
}

void Sketch::addSpline(const QVector<QVector2D>& points) {
    if (points.size() < 3) {
        qWarning() << "[Sketch]" << name() << "spline needs at least 3 points";
        return;
    }
    addGeometry(new SketchSpline(points));
}


void Sketch::addCircle(const QVector2D& center, double radius) {
    SketchCircle* circle = new SketchCircle(center, radius);
    addGeometry(circle);
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

    addGeometry(new SketchEllipse(center, majorRadius, minorRadius, angle));
}


void Sketch::addRectangle(const QVector2D& corner1, const QVector2D& corner2) {
    QVector<QVector2D> points;
    points << corner1
           << QVector2D(corner2.x(), corner1.y())
           << corner2
           << QVector2D(corner1.x(), corner2.y());

    addPolyline(points, true);
}

QVector3D Sketch::planeToWorld(const QVector2D& planePt) const {
    return m_plane->origin() +  m_plane->xAxis() * planePt.x() + m_plane->yAxis() * planePt.y();
}


void Sketch::addArc(const QVector2D& startPoint,
                    const QVector2D& midPoint,
                    const QVector2D& endPoint)
{
    try
    {
        // ── 1️⃣ 轉成 3D 世界座標 ─────────────────────
        QVector3D w1 = planeToWorld(startPoint);
        QVector3D w2 = planeToWorld(midPoint);
        QVector3D w3 = planeToWorld(endPoint);

        gp_Pnt gp1(w1.x(), w1.y(), w1.z());
        gp_Pnt gp2(w2.x(), w2.y(), w2.z());
        gp_Pnt gp3(w3.x(), w3.y(), w3.z());

        // ── 2️⃣ 用 OCCT 建立三點弧 ────────────────────
        GC_MakeArcOfCircle arcMaker(gp1, gp2, gp3);

        if (!arcMaker.IsDone())
        {
            qWarning() << "[Sketch]" << name()
                       << "addArc: failed (points may be collinear)";
            return;
        }

        Handle(Geom_TrimmedCurve) arc = arcMaker.Value();

        // ── 3️⃣ 加入 Sketch ──────────────────────────
        addGeometry(new SketchArc(arc));

        qDebug() << "[Sketch]" << name()
                 << "Arc added (OCCT 3-point arc)";
    }
    catch (Standard_Failure const& e)
    {
        qWarning() << "[Sketch] OCCT error:"
                   << e.GetMessageString();
    }
}

bool Sketch::rebuild() {
    qDebug() << "[Sketch]" << name() << "rebuilding with"
             << m_geometries.size() << "geometries";

    try {
        m_wires.clear();
        m_aisShapes.clear();  // ✅ 清除 QList

        if (m_geometries.isEmpty()) {
            setShape(TopoDS_Shape());
            return true;
        }

        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);

        for (const SketchGeometry* geom : m_geometries) {
            if (!geom) continue;

            TopoDS_Wire wire;
            bool wireCreated = false;

            // ✅ 處理 Line
            if (geom->type == SketchGeometryType::Line && geom->points.size() >= 2) {
                QVector3D p1 = m_plane->toWorld(geom->points[0].x(), geom->points[0].y());
                QVector3D p2 = m_plane->toWorld(geom->points[1].x(), geom->points[1].y());

                gp_Pnt gp1(p1.x(), p1.y(), p1.z());
                gp_Pnt gp2(p2.x(), p2.y(), p2.z());

                BRepBuilderAPI_MakeEdge edgeBuilder(gp1, gp2);
                if (edgeBuilder.IsDone()) {
                    BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
                    if (wireBuilder.IsDone()) {
                        wire = wireBuilder.Wire();
                        wireCreated = true;
                    }
                }
            }

            // ✅ 處理 Polyline
            else if (geom->type == SketchGeometryType::Polyline) {
                const SketchPolyline* pline = static_cast<const SketchPolyline*>(geom);
                BRepBuilderAPI_MakeWire wireBuilder;

                // ✅ Determine number of segments based on closed status
                // Open: N-1 segments (connect consecutive points)
                // Closed: N segments (last point connects back to first)
                int numSegments = pline->closed ? geom->points.size() : geom->points.size() - 1;

                for (int i = 0; i < numSegments; ++i) {
                    QVector3D p1 = m_plane->toWorld(geom->points[i].x(), geom->points[i].y());

                    // ✅ For closed polyline, wrap around to first point
                    int nextIdx = (i + 1) % geom->points.size();
                    QVector3D p2 = m_plane->toWorld(geom->points[nextIdx].x(), geom->points[nextIdx].y());

                    gp_Pnt gp1(p1.x(), p1.y(), p1.z());
                    gp_Pnt gp2(p2.x(), p2.y(), p2.z());

                    BRepBuilderAPI_MakeEdge edgeBuilder(gp1, gp2);
                    if (edgeBuilder.IsDone()) {
                        wireBuilder.Add(edgeBuilder.Edge());
                    }
                }

                if (wireBuilder.IsDone()) {
                    wire = wireBuilder.Wire();
                    wireCreated = true;
                }
            }


            // ✅ 處理 Circle
            else if (geom->type == SketchGeometryType::Circle) {
                const SketchCircle* circle = static_cast<const SketchCircle*>(geom);
                QVector3D center3d = m_plane->toWorld(circle->center.x(), circle->center.y());

                gp_Pnt centerPnt(center3d.x(), center3d.y(), center3d.z());
                gp_Dir normal(m_plane->normal().x(), m_plane->normal().y(), m_plane->normal().z());
                gp_Ax2 ax2(centerPnt, normal);

                gp_Circ gpCircle(ax2, circle->radius);
                BRepBuilderAPI_MakeEdge edgeBuilder(gpCircle);

                if (edgeBuilder.IsDone()) {
                    BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
                    if (wireBuilder.IsDone()) {
                        wire = wireBuilder.Wire();
                        wireCreated = true;
                    }
                }
            }
            // ✅ 處理 Spline
            else if (geom->type == SketchGeometryType::Spline) {
                // 檢查至少有 3 個控制點
                if (geom->points.size() < 3) {
                    qWarning() << "[Sketch] Spline requires at least 3 points, got:"
                               << geom->points.size();
                    continue;
                }

                try {
                    // 1. 收集控制點並轉換為 3D 世界座標
                    int numPoints = geom->points.size();
                    Handle(TColgp_HArray1OfPnt) controlPoints =
                        new TColgp_HArray1OfPnt(1, numPoints);

                    for (int i = 0; i < numPoints; ++i) {
                        QVector3D p = m_plane->toWorld(geom->points[i].x(), geom->points[i].y());
                        controlPoints->SetValue(i + 1, gp_Pnt(p.x(), p.y(), p.z()));
                    }

                    // 2. 創建插值樣條曲線
                    GeomAPI_Interpolate interpolator(
                        controlPoints,     // 控制點
                        Standard_False,    // 不閉合
                        1.0e-6            // 容差
                        );
                    interpolator.Perform();

                    if (!interpolator.IsDone()) {
                        qWarning() << "[Sketch] Failed to interpolate spline";
                        continue;
                    }

                    Handle(Geom_BSplineCurve) splineCurve = interpolator.Curve();

                    // 3. 從樣條曲線創建 Edge
                    BRepBuilderAPI_MakeEdge edgeBuilder(splineCurve);

                    if (!edgeBuilder.IsDone()) {
                        qWarning() << "[Sketch] Failed to create edge from spline curve";
                        continue;
                    }

                    // 4. 創建 Wire
                    BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());

                    if (wireBuilder.IsDone()) {
                        wire = wireBuilder.Wire();
                        wireCreated = true;
                    } else {
                        qWarning() << "[Sketch] Failed to create wire from spline edge";
                    }

                } catch (Standard_Failure& e) {
                    qWarning() << "[Sketch] Exception in spline creation:"
                               << e.GetMessageString();
                }
            }
            // ✅ 處理 Arc
            else if (geom->type == SketchGeometryType::Arc)
            {
                const SketchArc* arc =
                    static_cast<const SketchArc*>(geom);

                if (arc->curve.IsNull())
                {
                    qWarning() << "[Sketch] Arc curve is null";
                    continue;
                }

                // ── 直接用 OCCT curve 建立 Edge ─────────────────────
                BRepBuilderAPI_MakeEdge edgeBuilder(arc->curve);

                if (!edgeBuilder.IsDone())
                {
                    qWarning() << "[Sketch] Arc edge build failed:"
                               << edgeBuilder.Error();
                    continue;
                }

                BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());

                if (wireBuilder.IsDone())
                {
                    wire        = wireBuilder.Wire();
                    wireCreated = true;
                }
                else
                {
                    qWarning() << "[Sketch] Arc wire build failed";
                }
            }
            else if (geom->type == SketchGeometryType::Ellipse) {
                const SketchEllipse* ellipse = static_cast<const SketchEllipse*>(geom);

                // Transform center from 2D sketch plane to 3D world coordinates
                QVector3D center3d = m_plane->toWorld(ellipse->center.x(), ellipse->center.y());
                gp_Pnt centerPnt(center3d.x(), center3d.y(), center3d.z());

                // Get sketch plane normal (Z direction)
                gp_Dir normal(m_plane->normal().x(), m_plane->normal().y(), m_plane->normal().z());

                // Calculate major axis direction in world coordinates
                // The ellipse angle is in the sketch plane, so we need to rotate in that plane
                double cosAngle = qCos(ellipse->angle);
                double sinAngle = qSin(ellipse->angle);

                // Major axis direction in sketch plane coordinates
                QVector2D majorAxisDir2D(cosAngle, sinAngle);

                // Transform major axis direction to 3D world coordinates
                QVector3D majorAxisEnd3D = m_plane->toWorld(
                    ellipse->center.x() + majorAxisDir2D.x(),
                    ellipse->center.y() + majorAxisDir2D.y()
                    );
                QVector3D majorAxisDir3D = (majorAxisEnd3D - center3d).normalized();
                gp_Dir xDir(majorAxisDir3D.x(), majorAxisDir3D.y(), majorAxisDir3D.z());

                // Create coordinate system for ellipse
                gp_Ax2 ax2(centerPnt, normal, xDir);

                // Create ellipse (major radius, minor radius)
                gp_Elips gpEllipse(ax2, ellipse->majorRadius, ellipse->minorRadius);

                // Build edge and wire
                BRepBuilderAPI_MakeEdge edgeBuilder(gpEllipse);
                if (edgeBuilder.IsDone()) {
                    BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
                    if (wireBuilder.IsDone()) {
                        wire = wireBuilder.Wire();
                        wireCreated = true;
                    }
                }
            }

            // ✅ 創建 Wire 和對應的 AIS_Shape
            if (wireCreated) {
                m_wires.append(wire);
                builder.Add(compound, wire);

                // ✅ 為每條線創建獨立的 AIS_Shape
                Handle(AIS_Shape) aisShape = new AIS_Shape(wire);
                aisShape->SetColor(Quantity_NOC_WHITE);
                aisShape->SetWidth(2.0);
                aisShape->SetDisplayMode(AIS_WireFrame);

                m_aisShapes.append(aisShape);  // ✅ 加入 QList
            }
        }

        setShape(compound);

        qDebug() << "[Sketch]" << name() << "rebuilt with"
                 << m_wires.size() << "wires and"
                 << m_aisShapes.size() << "AIS shapes";
        return true;

    } catch (const Standard_Failure& e) {
        QString error = QString("OCCT error: %1").arg(e.GetMessageString());
        qCritical() << "[Sketch]" << name() << error;
        setError(error);
        return false;
    }
}

QJsonObject Sketch::toJson() const {
    QJsonObject json = Feature::toJson();

    // ✅ 只儲存平面 ID（參考），而非完整平面資料
    if (m_plane) {
        json["planeId"] = m_plane->id();
        json["planeName"] = m_plane->displayName();  // 輔助資訊，用於除錯
    } else {
        qWarning() << "[Sketch]" << name() << "has no plane when serializing";
    }

    // 儲存幾何元素（與原版相同）
    QJsonArray geomsArray;
    for (const SketchGeometry* geom : m_geometries) {
        QJsonObject geomJson;
        geomJson["type"] = static_cast<int>(geom->type);

        QJsonArray pointsArray;
        for (const QVector2D& pt : geom->points) {
            QJsonObject ptJson;
            ptJson["x"] = pt.x();
            ptJson["y"] = pt.y();
            pointsArray.append(ptJson);
        }
        geomJson["points"] = pointsArray;

        // 額外屬性
        if (geom->type == SketchGeometryType::Circle) {
            const SketchCircle* circle = static_cast<const SketchCircle*>(geom);
            geomJson["radius"] = circle->radius;
            geomJson["centerX"] = circle->center.x();
            geomJson["centerY"] = circle->center.y();
        } else if (geom->type == SketchGeometryType::Polyline) {
            const SketchPolyline* polyline = static_cast<const SketchPolyline*>(geom);
            geomJson["closed"] = polyline->closed;
        }

        geomsArray.append(geomJson);
    }
    json["geometries"] = geomsArray;

    return json;
}

bool Sketch::fromJson(const QJsonObject& json) {
    if (!Feature::fromJson(json)) {
        return false;
    }

    // ✅ 從 planeId 恢復平面參考
    if (json.contains("planeId")) {
        QString planeId = json["planeId"].toString();
        Plane* plane = PlaneManager::instance()->getPlane(planeId);

        if (plane) {
            setPlane(plane);
            qDebug() << "[Sketch]" << name() << "restored plane reference:"
                     << plane->displayName();
        } else {
            qWarning() << "[Sketch]" << name()
                       << "Plane not found:" << planeId
                       << "Creating default plane instead";
            createDefaultPlane();
        }
    } else {
        qWarning() << "[Sketch]" << name() << "No planeId in JSON";
        createDefaultPlane();
    }

    // 載入幾何元素（與原版相同）
    clearGeometry();
    if (json.contains("geometries")) {
        QJsonArray geomsArray = json["geometries"].toArray();
        for (const QJsonValue& val : geomsArray) {
            QJsonObject geomJson = val.toObject();
            SketchGeometryType type = static_cast<SketchGeometryType>(
                geomJson["type"].toInt()
                );

            // 載入點
            QVector<QVector2D> points;
            QJsonArray pointsArray = geomJson["points"].toArray();
            for (const QJsonValue& ptVal : pointsArray) {
                QJsonObject ptJson = ptVal.toObject();
                points.append(QVector2D(
                    ptJson["x"].toDouble(),
                    ptJson["y"].toDouble()
                    ));
            }

            // 建立幾何元素
            if (type == SketchGeometryType::Line && points.size() >= 2) {
                addLine(points[0], points[1]);
            } else if (type == SketchGeometryType::Polyline) {
                bool closed = geomJson["closed"].toBool(false);
                addPolyline(points, closed);
            } else if (type == SketchGeometryType::Circle) {
                QVector2D center(
                    geomJson["centerX"].toDouble(),
                    geomJson["centerY"].toDouble()
                    );
                double radius = geomJson["radius"].toDouble();
                addCircle(center, radius);
            }
        }
    }

    return true;
}

void Sketch::onPlaneAboutToBeDeleted() {
    qWarning() << "[Sketch]" << name() << "associated plane is being deleted!";

    // 平面即將被刪除，切換到新的預設平面
    disconnectPlaneSignals();
    createDefaultPlane();

    Q_EMIT planeChanged(m_plane);
    Q_EMIT rebuildRequested();
}

void Sketch::onPlaneGeometryChanged() {
    qDebug() << "[Sketch]" << name() << "plane geometry changed";

    // 平面幾何改變，需要重建
    Q_EMIT rebuildRequested();
}

gp_Pnt Sketch::toWorld(const QVector2D& point) const {
    if (!hasValidPlane()) {
        qWarning() << "[Sketch]" << name() << "No valid plane for conversion";
        return gp_Pnt(point.x(), point.y(), 0);  // 退化到 XY 平面
    }

    QVector3D worldPt = m_plane->toWorld(point.x(), point.y());
    return gp_Pnt(worldPt.x(), worldPt.y(), worldPt.z());
}

void Sketch::createDefaultPlane() {
    // 建立或取得預設 XY 平面
    PlaneManager* manager = PlaneManager::instance();

    // 嘗試使用已存在的 XY 平面
    QList<Plane*> planes = manager->planes();
    for (Plane* p : planes) {
        if (p->type() == Plane::Type::XY && p->isXY()) {
            m_plane = p;
            connectPlaneSignals();
            qDebug() << "[Sketch]" << name() << "using existing XY plane";
            return;
        }
    }

    // 沒有找到，建立新的
    m_plane = manager->createPlane(
        Plane::Type::XY,
        QString("%1_DefaultPlane").arg(name())
        );

    connectPlaneSignals();
    qDebug() << "[Sketch]" << name() << "created new default plane";
}

void Sketch::connectPlaneSignals() {
    if (!m_plane) return;

    connect(m_plane, &Plane::aboutToBeDeleted,
            this, &Sketch::onPlaneAboutToBeDeleted);

    connect(m_plane, &Plane::geometryChanged,
            this, &Sketch::onPlaneGeometryChanged);
}

void Sketch::disconnectPlaneSignals() {
    if (!m_plane) return;

    disconnect(m_plane, nullptr, this, nullptr);
}

QList<TopoDS_Wire> Sketch::wires() const {
    return m_wires;
}

QList<Handle(AIS_Shape)> Sketch::aisShapes() const {
    return m_aisShapes;
}

void Sketch::displayInContext(const Handle(AIS_InteractiveContext)& context) {
    if (context.IsNull()) {
        qWarning() << "[Sketch]" << name() << "Cannot display: context is null";
        return;
    }

    for (const Handle(AIS_Shape)& aisShape : m_aisShapes) {
        if (!aisShape.IsNull()) {
            context->Display(aisShape, Standard_False);
        }
    }

    context->UpdateCurrentViewer();
    qDebug() << "[Sketch]" << name() << "displayed" << m_aisShapes.size() << "shapes";
}

void Sketch::eraseFromContext(const Handle(AIS_InteractiveContext)& context) {
    if (context.IsNull()) {
        return;
    }

    for (const Handle(AIS_Shape)& aisShape : m_aisShapes) {
        if (!aisShape.IsNull()) {
            context->Erase(aisShape, Standard_False);
        }
    }

    context->UpdateCurrentViewer();
}

TopoDS_Wire Sketch::mainWire() const {
    if (m_wires.isEmpty()) {
        return TopoDS_Wire();
    }
    return m_wires.first();
}

bool Sketch::hasClosedProfile() const {
    for (const TopoDS_Wire& wire : m_wires) {
        if (!wire.IsNull() && wire.Closed()) {
            return true;
        }
    }
    return false;
}

} // namespace cad
} // namespace aicad
