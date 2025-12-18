/**
 * @file Sketch.cpp
 * @brief Sketch 實作
 * @author Ben
 * @date 2024-12-04
 */

#include "Sketch.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Wire.hxx>
#include <BRep_Builder.hxx>
#include <Precision.hxx>

#include <QDebug>

namespace aicad {
namespace cad {

class Sketch::Private {
public:
    CustomPlane plane;
    QVector<QVector<QVector2D>> polylines;
};

Sketch::Sketch(const CustomPlane& plane, QObject* parent)
    : Feature(parent)
    , d(new Private())
{
    d->plane = plane;
    setName(QString("Sketch (%1)").arg(plane.getDisplayName()));
    qDebug() << "[Sketch] Created on" << plane.getDisplayName() << "plane";
}

Sketch::~Sketch() {
    qDebug() << "[Sketch] Destroyed:" << name();
    delete d;
}

TopoDS_Shape Sketch::compute() {
    if (d->polylines.isEmpty()) {
        qWarning() << "[Sketch] No geometry to compute";
        return TopoDS_Shape();
    }
    
    qDebug() << "[Sketch] Computing" << d->polylines.size() << "polylines";
    
    try {
        // 如果只有一條折線，回傳 Wire
        if (d->polylines.size() == 1) {
            return computeSingleWire(d->polylines[0]);
        }
        
        // 多條折線，建立 Compound
        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        
        for (const QVector<QVector2D>& points : d->polylines) {
            TopoDS_Shape wire = computeSingleWire(points);
            if (!wire.IsNull()) {
                builder.Add(compound, wire);
            }
        }
        
        if (compound.IsNull()) {
            qWarning() << "[Sketch] Failed to create compound";
            return TopoDS_Shape();
        }
        
        qDebug() << "[Sketch] Compute successful";
        return compound;
        
    } catch (const Standard_Failure& e) {
        qCritical() << "[Sketch] OCCT exception:" << e.GetMessageString();
        return TopoDS_Shape();
    } catch (...) {
        qCritical() << "[Sketch] Unknown exception during compute";
        return TopoDS_Shape();
    }
}

TopoDS_Shape Sketch::computeSingleWire(const QVector<QVector2D>& points) {
    if (points.size() < 2) {
        qWarning() << "[Sketch] Polyline has less than 2 points";
        return TopoDS_Shape();
    }
    
    BRepBuilderAPI_MakeWire wireBuilder;
    
    // 將 2D 點轉換為 3D 點並建立邊
    for (int i = 0; i < points.size() - 1; ++i) {
        // 平面座標轉世界座標
        QVector3D p1_3d = d->plane.origin + 
                         d->plane.uAxis * points[i].x() + 
                         d->plane.vAxis * points[i].y();
        
        QVector3D p2_3d = d->plane.origin + 
                         d->plane.uAxis * points[i+1].x() + 
                         d->plane.vAxis * points[i+1].y();
        
        gp_Pnt gp1(p1_3d.x(), p1_3d.y(), p1_3d.z());
        gp_Pnt gp2(p2_3d.x(), p2_3d.y(), p2_3d.z());
        
        // 檢查點是否太近
        if (gp1.Distance(gp2) < Precision::Confusion()) {
            qWarning() << "[Sketch] Points too close, skipping edge";
            continue;
        }
        
        // 建立邊
        BRepBuilderAPI_MakeEdge edgeBuilder(gp1, gp2);
        if (!edgeBuilder.IsDone()) {
            qWarning() << "[Sketch] Failed to create edge";
            continue;
        }
        
        wireBuilder.Add(edgeBuilder.Edge());
    }
    
    if (!wireBuilder.IsDone()) {
        qWarning() << "[Sketch] Failed to create wire";
        return TopoDS_Shape();
    }
    
    return wireBuilder.Wire();
}

CustomPlane Sketch::plane() const {
    return d->plane;
}

void Sketch::setPlane(const CustomPlane& plane) {
    d->plane = plane;
    invalidate();
    Q_EMIT planeChanged(plane);
    qDebug() << "[Sketch] Plane changed to:" << plane.getDisplayName();
}

void Sketch::addPolyline(const QVector<QVector2D>& points) {
    if (points.isEmpty()) {
        qWarning() << "[Sketch] Cannot add empty polyline";
        return;
    }
    
    d->polylines.append(points);
    invalidate();
    Q_EMIT geometryChanged();
    qDebug() << "[Sketch] Added polyline with" << points.size() << "points";
}

QVector<QVector<QVector2D>> Sketch::polylines() const {
    return d->polylines;
}

QVector<QVector2D> Sketch::polyline(int index) const {
    if (index >= 0 && index < d->polylines.size()) {
        return d->polylines[index];
    }
    return QVector<QVector2D>();
}

int Sketch::polylineCount() const {
    return d->polylines.size();
}

void Sketch::removePolyline(int index) {
    if (index >= 0 && index < d->polylines.size()) {
        d->polylines.removeAt(index);
        invalidate();
        Q_EMIT geometryChanged();
        qDebug() << "[Sketch] Removed polyline" << index;
    }
}

void Sketch::clear() {
    if (d->polylines.isEmpty()) {
        return;
    }
    
    d->polylines.clear();
    invalidate();
    Q_EMIT geometryChanged();
    qDebug() << "[Sketch] Cleared all geometry";
}

bool Sketch::isEmpty() const {
    return d->polylines.isEmpty();
}

} // namespace cad
} // namespace aicad