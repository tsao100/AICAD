/**
 * @file Sketch.cpp
 * @brief 與增強版 Plane 整合的 Sketch 類別實作
 */

#include "Sketch.h"
#include "Document.h"
#include "PlaneManager.h"
#include "sketch/SketchLoopFinder.h"

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
#include <Geom_TrimmedCurve.hxx>
#include <Prs3d_LineAspect.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <TopExp_Explorer.hxx>

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

    if (hasValidPlane()) {
        QVector3D w1 = m_plane->toWorld(p1);
        QVector3D w2 = m_plane->toWorld(p2);
        qDebug() << "[Sketch]" << name() << "added line:"
                 << "Plane(" << p1 << "->" << p2 << ")"
                 << "World(" << w1 << "->" << w2 << ")";
    }
}

void Sketch::addPolyline(const QVector<QVector2D>& points, bool closed) {
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
    points << corner1
           << QVector2D(corner2.x(), corner1.y())
           << corner2
           << QVector2D(corner1.x(), corner2.y());
    addPolyline(points, true);
}

QVector3D Sketch::planeToWorld(const QVector2D& planePt) const {
    return m_plane->origin() + m_plane->xAxis() * planePt.x() + m_plane->yAxis() * planePt.y();
}

void Sketch::addArc(const QVector2D& startPoint,
                    const QVector2D& midPoint,
                    const QVector2D& endPoint)
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
            qWarning() << "[Sketch]" << name() << "addArc: failed (points may be collinear)";
            return;
        }

        Handle(Geom_TrimmedCurve) arc = arcMaker.Value();
        addGeometry(new SketchArc(arc));
        qDebug() << "[Sketch]" << name() << "Arc added";
    } catch (Standard_Failure const& e) {
        qWarning() << "[Sketch] OCCT error:" << e.GetMessageString();
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
    for (auto* g : m_geometries)
        if (g->uuid == uuid) return g;
    return nullptr;
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

        qDebug() << "[Sketch]" << name() << "rebuilt with"
                 << m_wires.size() << "wires and"
                 << m_aisShapes.size() << "AIS shapes";
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

    // ① 把舊 AIS_Shape 從 context 先 Erase（保留 handle 本身）
    for (const Handle(AIS_Shape)& s : m_aisShapes)
        if (!s.IsNull()) m_aisContext->Erase(s, Standard_False);
    for (const Handle(AIS_Shape)& s : m_constructionShapes)
        if (!s.IsNull()) m_aisContext->Erase(s, Standard_False);

    // ② 重建 TopoDS + 更新 m_aisShapes（建立全新 handle）
    if (!rebuild()) return false;

    // ③ 把新 AIS_Shape Display 回 context
    for (const Handle(AIS_Shape)& s : m_aisShapes)
        if (!s.IsNull()) m_aisContext->Display(s, Standard_False);
    for (const Handle(AIS_Shape)& s : m_constructionShapes) {
        if (!s.IsNull()) {
            m_aisContext->Display(s, Standard_False);
            m_aisContext->Deactivate(s);
        }
    }

    m_aisContext->UpdateCurrentViewer();
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

        // 各幾何類型的額外屬性
        switch (geom->type) {
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

    QJsonArray conArr;
    for (const auto& c : m_constraints)
        conArr.append(c.toJson());
    json["constraints"] = conArr;

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
    if (json.contains("geometries")) {
        QJsonArray geomsArray = json["geometries"].toArray();

        for (const QJsonValue& val : geomsArray) {
            QJsonObject geomJson = val.toObject();
            SketchGeometryType type =
                static_cast<SketchGeometryType>(geomJson["type"].toInt());

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
                    addLine(points[0], points[1]);
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
                    addPolyline(points, closed);
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
                    addCircle(center, radius);
                }
                if (!m_geometries.isEmpty() && geomJson.contains("uuid"))
                    m_geometries.last()->uuid = geomJson["uuid"].toString();
                if (geomJson.contains("role") && !m_geometries.isEmpty())
                    m_geometries.last()->role = static_cast<GeomRole>(geomJson["role"].toInt());
                break;
            }



            case SketchGeometryType::Spline:
                if (points.size() >= 3) {
                    addSpline(points);
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

                if (geomJson.contains("startPt") &&
                    geomJson.contains("midPt")   &&
                    geomJson.contains("endPt")) {
                    addArc(readPt("startPt"), readPt("midPt"), readPt("endPt"));
                } else if (points.size() >= 3) {
                    // legacy fallback
                    addArc(points[0], points[1], points[2]);
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
                    addEllipse(center, major, minor, angle);
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
        }
    }


    qDebug() << "[Sketch]" << name() << "fromJson complete:"
             << m_geometries.size() << "geometries on plane"
             << (m_plane ? m_plane->displayName() : "NULL");

    m_constraints.clear();
    if (json.contains("constraints")) {
        for (const QJsonValue& v : json["constraints"].toArray())
            m_constraints.append(SketchConstraint::fromJson(v.toObject()));
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

void Sketch::removeConstraintsOf(const QString& geomUuid) {
    m_constraints.erase(
        std::remove_if(m_constraints.begin(), m_constraints.end(),
                       [&](const SketchConstraint& c){
                           return std::any_of(c.refs.begin(), c.refs.end(),
                                              [&](const GeomRef& r){ return r.geomUuid == geomUuid; });
                       }),
        m_constraints.end());
}

SolveResult Sketch::solveConstraints() {
    gp_Dir normal(0, 0, 1);
    if (m_plane) {
        QVector3D n = m_plane->normal();
        normal = gp_Dir(n.x(), n.y(), n.z());
    }
    auto result = m_solver.solve(m_geometries, m_constraints, normal);
    Q_EMIT constraintSolved(result);
    if (result.status != SolveStatus::Conflict &&
        result.status != SolveStatus::SolverError) {
        // 求解後幾何已被修改，觸發重建
        markDirty();
        Q_EMIT geometryChanged();
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
    return ConstraintSolver::computeDOF(m_geometries, m_constraints);
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

QList<Handle(AIS_Shape)> Sketch::aisShapes() const {
    return m_aisShapes;
}

const QList<QString>& Sketch::aisShapeUuids() const {
    return m_aisShapeUuids;
}

QList<Handle(AIS_Shape)> Sketch::displayInContext(
    const Handle(AIS_InteractiveContext)& context) {
    m_aisContext = context;
    if (context.IsNull()) return {};

    for (const Handle(AIS_Shape)& s : m_aisShapes)
        if (!s.IsNull()) context->Display(s, Standard_False);

    // ← 新增：建構線也顯示，但不可選（避免誤選）
    for (const Handle(AIS_Shape)& s : m_constructionShapes) {
        if (!s.IsNull()) {
            context->Display(s, Standard_False);
            context->Deactivate(s);    // 不參與選擇
        }
    }

    context->UpdateCurrentViewer();
    return m_aisShapes;   // 只回傳 normal shapes
}

void Sketch::eraseFromContext(const Handle(AIS_InteractiveContext)& context) {
    if (context.IsNull()) return;
    for (const Handle(AIS_Shape)& s : m_aisShapes)
        if (!s.IsNull()) context->Erase(s, Standard_False);
    for (const Handle(AIS_Shape)& s : m_constructionShapes)   // ← 新增
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
    m_points.insert(pt->uuid, pt);
    // 同時加入 m_geometries 以便 Solver 處理
    m_geometries.append(pt);
    return pt->uuid;
}

SketchPoint* Sketch::point(const QString& uuid) const
{
    return m_points.value(uuid, nullptr);
}

QList<SketchPoint*> Sketch::points() const
{
    return m_points.values();
}

void Sketch::movePoint(const QString& uuid, const QVector2D& newPos)
{
    auto* pt = m_points.value(uuid, nullptr);
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
    if (!m_points.contains(fromUuid) || !m_points.contains(toUuid)) return;

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
    }

    // 刪除 from 點
    SketchPoint* fromPt = m_points.take(fromUuid);
    m_geometries.removeAll(fromPt);
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
        }
    }
    return result;
}

QString Sketch::addLineGeom(const QVector2D& p1, const QVector2D& p2,
                             const QString& reuseStart, const QString& reuseEnd)
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
    m_geometries.append(line);

    Q_EMIT geometryChanged();
    Q_EMIT rebuildRequested();
    return line->uuid;
}

QString Sketch::addCircleGeom(const QVector2D& center, double radius)
{
    QString centerUuid = addPoint(center, SketchPoint::Origin::Center);

    auto* circ = new SketchCircle(center, radius);
    circ->centerUuid = centerUuid;
    m_geometries.append(circ);

    Q_EMIT geometryChanged();
    Q_EMIT rebuildRequested();
    return circ->uuid;
}

void Sketch::syncGeometryFromPoints()
{
    for (auto* g : m_geometries) {
        if (auto* line = dynamic_cast<SketchLine*>(g)) {
            if (auto* ps = m_points.value(line->startUuid)) {
                line->start = ps->pos;
                if (!line->points.isEmpty()) line->points[0] = ps->pos;
            }
            if (auto* pe = m_points.value(line->endUuid)) {
                line->end = pe->pos;
                if (line->points.size() > 1) line->points[1] = pe->pos;
            }
        } else if (auto* circ = dynamic_cast<SketchCircle*>(g)) {
            if (auto* pc = m_points.value(circ->centerUuid)) {
                circ->center = pc->pos;
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
    qDebug() << "[Sketch] migrateFromLegacyFormat: created" << m_points.size() << "points";
}

QList<Sketch::PointInfo> Sketch::listPoints() const
{
    QList<PointInfo> result;
    for (auto* pt : m_points) {
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
