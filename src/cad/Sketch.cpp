/**
 * @file Sketch.cpp
 * @brief 草圖特徵實作
 * @author AICAD Team
 * @date 2025-01-08
 */

#include "Sketch.h"
#include "Document.h"
#include "geometry/GeometryBuilder.h"

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
#include <QJsonArray>
#include <QtMath>
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
    // ✅ ADD: Remove from context
    if (document()) {
        Handle(AIS_InteractiveContext) context = document()->aisContext();
        if (!context.IsNull()) {
            eraseFromContext(context);
        }
    }
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
                QVector3D p1 = m_plane.toWorld(geom->points[0].x(), geom->points[0].y());
                QVector3D p2 = m_plane.toWorld(geom->points[1].x(), geom->points[1].y());

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
                    QVector3D p1 = m_plane.toWorld(geom->points[i].x(), geom->points[i].y());

                    // ✅ For closed polyline, wrap around to first point
                    int nextIdx = (i + 1) % geom->points.size();
                    QVector3D p2 = m_plane.toWorld(geom->points[nextIdx].x(), geom->points[nextIdx].y());

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
                QVector3D center3d = m_plane.toWorld(circle->center.x(), circle->center.y());

                gp_Pnt centerPnt(center3d.x(), center3d.y(), center3d.z());
                gp_Dir normal(m_plane.normal().x(), m_plane.normal().y(), m_plane.normal().z());
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
                        QVector3D p = m_plane.toWorld(geom->points[i].x(), geom->points[i].y());
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
            else if (geom->type == SketchGeometryType::Arc) {
                const SketchArc* arc = static_cast<const SketchArc*>(geom);

                // ── 將 2D 圓心映射到世界座標 ─────────────────────────────────────────
                QVector3D center3d = m_plane.toWorld(arc->center.x(), arc->center.y());
                gp_Pnt centerPnt(center3d.x(), center3d.y(), center3d.z());
                gp_Dir normal(m_plane.normal().x(), m_plane.normal().y(), m_plane.normal().z());
                gp_Ax2 ax2(centerPnt, normal);

                // ── 建立 OCC 完整圓（弧是圓的一段）──────────────────────────────────
                gp_Circ gpCircle(ax2, arc->radius);

                // ── 將角度（度數）轉為弧度，並映射到平面上的 3D 點 ──────────────────
                double startRad = qDegreesToRadians(arc->startAngle);
                double endRad   = qDegreesToRadians(arc->endAngle);

                // OCC 以 ax2 的 X 軸方向為 0°，在平面上計算起、終點
                QVector3D startPt3d = m_plane.toWorld(
                    arc->center.x() + arc->radius * std::cos(startRad),
                    arc->center.y() + arc->radius * std::sin(startRad));
                QVector3D endPt3d   = m_plane.toWorld(
                    arc->center.x() + arc->radius * std::cos(endRad),
                    arc->center.y() + arc->radius * std::sin(endRad));

                gp_Pnt startPnt(startPt3d.x(), startPt3d.y(), startPt3d.z());
                gp_Pnt endPnt  (endPt3d.x(),   endPt3d.y(),   endPt3d.z());

                // ── 建立弧形 Edge（從 startPnt 逆時針到 endPnt）─────────────────────
                BRepBuilderAPI_MakeEdge edgeBuilder(gpCircle, startRad, endRad);
                if (!edgeBuilder.IsDone()) {
                    qWarning() << "[Sketch] Arc edge build failed:"
                               << edgeBuilder.Error();
                } else {
                    BRepBuilderAPI_MakeWire wireBuilder(edgeBuilder.Edge());
                    if (wireBuilder.IsDone()) {
                        wire        = wireBuilder.Wire();
                        wireCreated = true;
                    } else {
                        qWarning() << "[Sketch] Arc wire build failed";
                    }
                }
            }
            else if (geom->type == SketchGeometryType::Ellipse) {
                const SketchEllipse* ellipse = static_cast<const SketchEllipse*>(geom);

                // Transform center from 2D sketch plane to 3D world coordinates
                QVector3D center3d = m_plane.toWorld(ellipse->center.x(), ellipse->center.y());
                gp_Pnt centerPnt(center3d.x(), center3d.y(), center3d.z());

                // Get sketch plane normal (Z direction)
                gp_Dir normal(m_plane.normal().x(), m_plane.normal().y(), m_plane.normal().z());

                // Calculate major axis direction in world coordinates
                // The ellipse angle is in the sketch plane, so we need to rotate in that plane
                double cosAngle = qCos(ellipse->angle);
                double sinAngle = qSin(ellipse->angle);

                // Major axis direction in sketch plane coordinates
                QVector2D majorAxisDir2D(cosAngle, sinAngle);

                // Transform major axis direction to 3D world coordinates
                QVector3D majorAxisEnd3D = m_plane.toWorld(
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

void Sketch::addGeometry(SketchGeometry* geom) {
    if (!geom) {
        qWarning() << "[Sketch]" << name() << "cannot add null geometry";
        return;
    }
    
    m_geometries.append(geom);

    // ✅ Debug: 列出所有幾何
    qDebug() << "[Sketch]" << name() << "geometry added, total:" << m_geometries.size();
    for (int i = 0; i < m_geometries.size(); ++i) {
        const SketchGeometry* g = m_geometries[i];
        if (g->type == SketchGeometryType::Line) {
            qDebug() << "  [" << i << "] Line:"
                     << g->points[0] << "→" << g->points[1];
        }
    }

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

    // ✅ Debug: 顯示當前有多少個幾何
    qDebug() << "[Sketch]" << name() << "addLine:"
             << p1 << "to" << p2
             << "| Total geometries:" << m_geometries.size();
}

void Sketch::addPolyline(const QVector<QVector2D>& points, bool closed) {
    if (points.size() < 2) {
        qWarning() << "[Sketch]" << name() << "polyline needs at least 2 points";
        return;
    }
    addGeometry(new SketchPolyline(points, closed));
}

void Sketch::addSpline(const QVector<QVector2D>& points) {
    if (points.size() < 3) {
        qWarning() << "[Sketch]" << name() << "spline needs at least 3 points";
        return;
    }
    addGeometry(new SketchSpline(points));
}


void Sketch::addCircle(const QVector2D& center, double radius) {
    if (radius <= 0) {
        qWarning() << "[Sketch]" << name() << "circle radius must be positive";
        return;
    }
    addGeometry(new SketchCircle(center, radius));
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
    points << QVector2D(corner1.x(), corner1.y());
    points << QVector2D(corner2.x(), corner1.y());
    points << QVector2D(corner2.x(), corner2.y());
    points << QVector2D(corner1.x(), corner2.y());
    points << QVector2D(corner1.x(), corner1.y()); // 封閉
    
    addPolyline(points, true);
}

void Sketch::addArc(const QVector2D& startPoint,
                    const QVector2D& midPoint,
                    const QVector2D& endPoint)
{
    // ── 驗證三點不共線（行列式 = 0 → 無法構成弧）─────────────────────────
    const float ax  = midPoint.x()  - startPoint.x();
    const float ay  = midPoint.y()  - startPoint.y();
    const float bx  = endPoint.x()  - startPoint.x();
    const float by  = endPoint.y()  - startPoint.y();
    const float det = ax * by - ay * bx;

    if (qAbs(det) < 1e-6f) {
        qWarning() << "[Sketch]" << name()
                   << "addArc: the three points are collinear, cannot form an arc";
        return;
    }

    // ── 三點求圓心（垂直平分線交點）───────────────────────────────────────
    const float ux = (by * (ax * ax + ay * ay) - ay * (bx * bx + by * by))
                     / (2.f * det);
    const float uy = (ax * (bx * bx + by * by) - bx * (ax * ax + ay * ay))
                     / (2.f * det);

    const QVector2D center(startPoint.x() + ux, startPoint.y() + uy);
    const double    radius = static_cast<double>(
        QVector2D(center - startPoint).length());

    if (radius <= 0) {
        qWarning() << "[Sketch]" << name()
                   << "addArc: computed radius is zero";
        return;
    }

    // ── 計算起、終角度（度數，以圓心為基準）──────────────────────────────
    double startAngle = qRadiansToDegrees(
        qAtan2(startPoint.y() - center.y(),
               startPoint.x() - center.x()));
    double endAngle   = qRadiansToDegrees(
        qAtan2(endPoint.y() - center.y(),
               endPoint.x() - center.x()));

    // ── 確認弧的掃掠方向（中點必須落在 start → end 的掃掠範圍內）──────────
    // 將中點角度也算出來，再比對方向
    double midAngle = qRadiansToDegrees(
        qAtan2(midPoint.y() - center.y(),
               midPoint.x() - center.x()));

    // 將三個角度統一正規化到 [0, 360)
    auto normalize360 = [](double a) -> double {
        a = std::fmod(a, 360.0);
        return a < 0 ? a + 360.0 : a;
    };

    double a0 = normalize360(startAngle);
    double am = normalize360(midAngle);
    double a1 = normalize360(endAngle);

    // 判斷 midAngle 是否落在逆時針 a0 → a1 區間
    // 若不在 → 改用順時針（即 endAngle 與 startAngle 對調掃掠）
    bool midInCCW = (a0 <= a1) ? (am >= a0 && am <= a1)
                               : (am >= a0 || am <= a1);
    if (!midInCCW) {
        // 順時針：交換 start / end 使 SketchArc 內部統一以逆時針儲存
        std::swap(startAngle, endAngle);
    }

    // ── 建立幾何物件並加入 Sketch ─────────────────────────────────────────
    addGeometry(new SketchArc(center, radius, startAngle, endAngle));

    qDebug() << "[Sketch]" << name()
             << "Arc added: center(" << center.x() << "," << center.y() << ")"
             << "r=" << radius
             << "angles:" << startAngle << "->" << endAngle;
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

    // ✅ 顯示每條獨立的線
    for (const Handle(AIS_Shape)& aisShape : m_aisShapes) {
        if (!aisShape.IsNull()) {
            context->Display(aisShape, Standard_False);
        }
    }

    context->UpdateCurrentViewer();

    qDebug() << "[Sketch]" << name() << "displayed"
             << m_aisShapes.size() << "shapes";
}

void Sketch::eraseFromContext(const Handle(AIS_InteractiveContext)& context) {
    if (context.IsNull()) {
        return;
    }

    // ✅ 移除每條線
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
        // 收集所有點並使用 GeometryBuilder
        QVector<QVector2D> allPoints;
        
        for (const SketchGeometry* geom : geoms) {
            if (!geom) continue;
            
            switch (geom->type) {
            case SketchGeometryType::Line:
            case SketchGeometryType::Polyline:
                for (const QVector2D& pt : geom->points) {
                    allPoints.append(pt);
                }
                break;
            
            case SketchGeometryType::Circle:
                // 圓形需要特殊處理
                // 暫時跳過，未來改進
                break;
            
            default:
                qWarning() << "[Sketch] Unsupported geometry type:" 
                          << static_cast<int>(geom->type);
                break;
            }
        }
        
        if (!allPoints.isEmpty()) {
            auto result = geometry::GeometryBuilder::makeWire(allPoints, m_plane, false);
            if (result) {
                return TopoDS::Wire(result.shape);
            }
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
