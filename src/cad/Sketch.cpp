/**
 * @file Sketch.cpp
 * @brief 草圖特徵實作
 * @author AICAD Team
 * @date 2025-01-08
 */

#include "Sketch.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <GC_MakeSegment.hxx>
#include <GC_MakeCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Precision.hxx>
#include <QJsonArray>
#include <QDebug>

namespace aicad {
namespace cad {

Sketch::Sketch(Document* parent)
    : Feature(parent)
    , m_plane(Plane::xy())
{
    setName("Sketch");
    qDebug() << "[Sketch] Created on" << m_plane.displayName() << "plane";
}

Sketch::~Sketch() {
    clearGeometry();
    qDebug() << "[Sketch]" << name() << "destroyed";
}

void Sketch::setPlane(const Plane& plane) {
    m_plane = plane;
    qDebug() << "[Sketch]" << name() << "plane changed to" << plane.displayName();
    Q_EMIT planeChanged(plane);
    Q_EMIT rebuildRequested();
}

bool Sketch::rebuild() {
    qDebug() << "[Sketch]" << name() << "rebuilding with"
             << m_geometries.size() << "geometries";

    try {
        m_wires.clear();

        if (m_geometries.isEmpty()) {
            qDebug() << "[Sketch]" << name() << "no geometries to build";
            setShape(TopoDS_Shape());
            return true;
        }

        // 暫時將所有幾何元素建成一個 Wire
        // 未來可以支援多個獨立的 Wire
        TopoDS_Wire wire = buildWire(m_geometries);

        if (!wire.IsNull()) {
            m_wires.append(wire);
            setShape(wire);
            qDebug() << "[Sketch]" << name() << "rebuilt successfully";
            return true;
        } else {
            qWarning() << "[Sketch]" << name() << "failed to build wire";
            setError("Failed to build wire from geometries");
            return false;
        }

    } catch (const Standard_Failure& e) {
        QString error = QString("OCCT error: %1").arg(e.GetMessageString());
        qCritical() << "[Sketch]" << name() << error;
        setError(error);
        return false;
    } catch (...) {
        QString error = "Unknown error during rebuild";
        qCritical() << "[Sketch]" << name() << error;
        setError(error);
        return false;
    }
}

void Sketch::addGeometry(SketchGeometry* geom) {
    if (!geom) {
        qWarning() << "[Sketch]" << name() << "cannot add null geometry";
        return;
    }

    m_geometries.append(geom);
    qDebug() << "[Sketch]" << name() << "geometry added, total:" << m_geometries.size();
    Q_EMIT geometryChanged();
    Q_EMIT rebuildRequested();
}

void Sketch::removeGeometry(int index) {
    if (index < 0 || index >= m_geometries.size()) {
        qWarning() << "[Sketch]" << name() << "invalid geometry index:" << index;
        return;
    }

    delete m_geometries.takeAt(index);
    qDebug() << "[Sketch]" << name() << "geometry removed at" << index;
    Q_EMIT geometryChanged();
    Q_EMIT rebuildRequested();
}

void Sketch::clearGeometry() {
    qDeleteAll(m_geometries);
    m_geometries.clear();
    m_wires.clear();
    qDebug() << "[Sketch]" << name() << "all geometry cleared";
    Q_EMIT geometryChanged();
}

void Sketch::addLine(const QVector2D& p1, const QVector2D& p2) {
    addGeometry(new SketchLine(p1, p2));
}

void Sketch::addPolyline(const QVector<QVector2D>& points, bool closed) {
    if (points.size() < 2) {
        qWarning() << "[Sketch]" << name() << "polyline needs at least 2 points";
        return;
    }
    addGeometry(new SketchPolyline(points, closed));
}

void Sketch::addCircle(const QVector2D& center, double radius) {
    if (radius <= 0) {
        qWarning() << "[Sketch]" << name() << "circle radius must be positive";
        return;
    }
    addGeometry(new SketchCircle(center, radius));
}

void Sketch::addRectangle(const QVector2D& corner1, const QVector2D& corner2) {
    QVector<QVector2D> points;
    points << QVector2D(corner1.x(), corner1.y());
    points << QVector2D(corner2.x(), corner1.y());
    points << QVector2D(corner2.x(), corner2.y());
    points << QVector2D(corner1.x(), corner2.y());
    points << QVector2D(corner1.x(), corner1.y()); // 封閉

    addPolyline(points, true);
}

QList<TopoDS_Wire> Sketch::wires() const {
    return m_wires;
}

TopoDS_Wire Sketch::mainWire() const {
    if (m_wires.isEmpty()) {
        return TopoDS_Wire();
    }

    // TODO: 尋找封閉的 Wire
    // 目前只返回第一個
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

gp_Pnt Sketch::toWorld(const QVector2D& point) const {
    QVector3D worldPt = m_plane.toWorld(point.x(), point.y());
    return gp_Pnt(worldPt.x(), worldPt.y(), worldPt.z());
}

TopoDS_Wire Sketch::buildWire(const QList<SketchGeometry*>& geoms) {
    if (geoms.isEmpty()) {
        return TopoDS_Wire();
    }

    try {
        BRepBuilderAPI_MakeWire wireBuilder;

        for (const SketchGeometry* geom : geoms) {
            if (!geom) continue;

            switch (geom->type) {
            case SketchGeometryType::Line: {
                if (geom->points.size() >= 2) {
                    gp_Pnt p1 = toWorld(geom->points[0]);
                    gp_Pnt p2 = toWorld(geom->points[1]);

                    if (p1.Distance(p2) > Precision::Confusion()) {
                        BRepBuilderAPI_MakeEdge edgeBuilder(p1, p2);
                        if (edgeBuilder.IsDone()) {
                            wireBuilder.Add(edgeBuilder.Edge());
                        }
                    }
                }
                break;
            }

            case SketchGeometryType::Polyline: {
                for (int i = 0; i < geom->points.size() - 1; ++i) {
                    gp_Pnt p1 = toWorld(geom->points[i]);
                    gp_Pnt p2 = toWorld(geom->points[i + 1]);

                    if (p1.Distance(p2) > Precision::Confusion()) {
                        BRepBuilderAPI_MakeEdge edgeBuilder(p1, p2);
                        if (edgeBuilder.IsDone()) {
                            wireBuilder.Add(edgeBuilder.Edge());
                        }
                    }
                }
                break;
            }

            case SketchGeometryType::Circle: {
                const SketchCircle* circle = static_cast<const SketchCircle*>(geom);
                gp_Pnt center = toWorld(circle->center);
                gp_Ax2 ax2 = m_plane.toGpAx2();
                ax2.SetLocation(center);

                Handle(Geom_Circle) geomCircle = new Geom_Circle(ax2, circle->radius);
                BRepBuilderAPI_MakeEdge edgeBuilder(geomCircle);

                if (edgeBuilder.IsDone()) {
                    wireBuilder.Add(edgeBuilder.Edge());
                }
                break;
            }

            default:
                qWarning() << "[Sketch] Unsupported geometry type:"
                           << static_cast<int>(geom->type);
                break;
            }
        }

        if (wireBuilder.IsDone()) {
            return wireBuilder.Wire();
        }

    } catch (const Standard_Failure& e) {
        qWarning() << "[Sketch] Failed to build wire:" << e.GetMessageString();
    } catch (...) {
        qWarning() << "[Sketch] Unknown error building wire";
    }

    return TopoDS_Wire();
}

QJsonObject Sketch::toJson() const {
    QJsonObject json = Feature::toJson();

    // 儲存平面資訊
    QJsonObject planeJson;
    planeJson["originX"] = m_plane.origin().x();
    planeJson["originY"] = m_plane.origin().y();
    planeJson["originZ"] = m_plane.origin().z();
    planeJson["normalX"] = m_plane.normal().x();
    planeJson["normalY"] = m_plane.normal().y();
    planeJson["normalZ"] = m_plane.normal().z();
    planeJson["xAxisX"] = m_plane.xAxis().x();
    planeJson["xAxisY"] = m_plane.xAxis().y();
    planeJson["xAxisZ"] = m_plane.xAxis().z();
    json["plane"] = planeJson;

    // 儲存幾何元素
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

    // 載入平面
    if (json.contains("plane")) {
        QJsonObject planeJson = json["plane"].toObject();
        QVector3D origin(
            planeJson["originX"].toDouble(),
            planeJson["originY"].toDouble(),
            planeJson["originZ"].toDouble()
            );
        QVector3D normal(
            planeJson["normalX"].toDouble(),
            planeJson["normalY"].toDouble(),
            planeJson["normalZ"].toDouble()
            );
        QVector3D xAxis(
            planeJson["xAxisX"].toDouble(),
            planeJson["xAxisY"].toDouble(),
            planeJson["xAxisZ"].toDouble()
            );
        m_plane = Plane(origin, normal, xAxis);
    }

    // 載入幾何元素
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

} // namespace cad
} // namespace aicad
