/**
 * @file Sketch.cpp
 * @brief 草圖特徵類別實作
 * @author Ben
 * @date 2025-01-06
 */

#include "Sketch.h"
#include "Document.h"

#include <QDebug>
#include <QtMath>

// OCCT includes
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <TDataStd_RealArray.hxx>
#include <TDataStd_IntegerArray.hxx>
#include <TDF_ChildIterator.hxx>

namespace aicad {
namespace cad {

// SketchPlane implementation
SketchPlane SketchPlane::XY() {
    SketchPlane p;
    p.origin = QVector3D(0, 0, 0);
    p.normal = QVector3D(0, 0, 1);
    p.xAxis = QVector3D(1, 0, 0);
    p.yAxis = QVector3D(0, 1, 0);
    return p;
}

SketchPlane SketchPlane::XZ() {
    SketchPlane p;
    p.origin = QVector3D(0, 0, 0);
    p.normal = QVector3D(0, 1, 0);
    p.xAxis = QVector3D(1, 0, 0);
    p.yAxis = QVector3D(0, 0, 1);
    return p;
}

SketchPlane SketchPlane::YZ() {
    SketchPlane p;
    p.origin = QVector3D(0, 0, 0);
    p.normal = QVector3D(1, 0, 0);
    p.xAxis = QVector3D(0, 1, 0);
    p.yAxis = QVector3D(0, 0, 1);
    return p;
}

gp_Pln SketchPlane::toGpPln() const {
    gp_Pnt origin_pnt(origin.x(), origin.y(), origin.z());
    gp_Dir normal_dir(normal.x(), normal.y(), normal.z());
    return gp_Pln(origin_pnt, normal_dir);
}

gp_Ax2 SketchPlane::toGpAx2() const {
    gp_Pnt origin_pnt(origin.x(), origin.y(), origin.z());
    gp_Dir normal_dir(normal.x(), normal.y(), normal.z());
    gp_Dir xaxis_dir(xAxis.x(), xAxis.y(), xAxis.z());
    return gp_Ax2(origin_pnt, normal_dir, xaxis_dir);
}

QVector3D SketchPlane::toWorld(const QVector2D& point2D) const {
    return origin + xAxis * point2D.x() + yAxis * point2D.y();
}

QVector2D SketchPlane::toLocal(const QVector3D& point3D) const {
    QVector3D localVec = point3D - origin;
    float x = QVector3D::dotProduct(localVec, xAxis);
    float y = QVector3D::dotProduct(localVec, yAxis);
    return QVector2D(x, y);
}

// Private implementation
class Sketch::Private {
public:
    Private(const SketchPlane& p) : plane(p) {}
    
    SketchPlane plane;
    QVector<SketchElement> elements;
};

Sketch::Sketch(Document* doc, TDF_Label label, const SketchPlane& plane)
    : Feature(doc, label)
    , d(new Private(plane))
{
    qDebug() << "[Sketch] Constructor called";
    loadElements();
}

Sketch::~Sketch() {
    qDebug() << "[Sketch] Destructor called";
    delete d;
}

SketchPlane Sketch::plane() const {
    return d->plane;
}

QString Sketch::planeName() const {
    // 判斷平面類型
    if (d->plane.normal == QVector3D(0, 0, 1)) {
        return "XY";
    } else if (d->plane.normal == QVector3D(0, 1, 0)) {
        return "XZ";
    } else if (d->plane.normal == QVector3D(1, 0, 0)) {
        return "YZ";
    } else {
        return QString("Custom(%1,%2,%3)")
            .arg(d->plane.normal.x(), 0, 'f', 2)
            .arg(d->plane.normal.y(), 0, 'f', 2)
            .arg(d->plane.normal.z(), 0, 'f', 2);
    }
}

int Sketch::addLine(const QVector2D& start, const QVector2D& end) {
    qDebug() << "[Sketch] Adding line from" << start << "to" << end;
    
    SketchElement element;
    element.type = SketchElementType::Line;
    element.points.append(start);
    element.points.append(end);
    
    d->elements.append(element);
    int index = d->elements.size() - 1;
    
    saveElements();
    notifyModified();
    
    Q_EMIT elementAdded(index);
    Q_EMIT elementCountChanged(d->elements.size());
    
    return index;
}

int Sketch::addRectangle(const QVector2D& corner1, const QVector2D& corner2) {
    qDebug() << "[Sketch] Adding rectangle" << corner1 << corner2;
    
    // 建立矩形四個角點
    QVector<QVector2D> points;
    points.append(QVector2D(corner1.x(), corner1.y()));
    points.append(QVector2D(corner2.x(), corner1.y()));
    points.append(QVector2D(corner2.x(), corner2.y()));
    points.append(QVector2D(corner1.x(), corner2.y()));
    points.append(QVector2D(corner1.x(), corner1.y())); // 閉合
    
    return addPolyline(points, true);
}

int Sketch::addCircle(const QVector2D& center, double radius) {
    qDebug() << "[Sketch] Adding circle at" << center << "radius" << radius;
    
    SketchElement element;
    element.type = SketchElementType::Circle;
    element.points.append(center);
    element.parameters["radius"] = radius;
    
    d->elements.append(element);
    int index = d->elements.size() - 1;
    
    saveElements();
    notifyModified();
    
    Q_EMIT elementAdded(index);
    Q_EMIT elementCountChanged(d->elements.size());
    
    return index;
}

int Sketch::addArc(const QVector2D& center, double radius, 
                   double startAngle, double endAngle) {
    qDebug() << "[Sketch] Adding arc at" << center 
             << "radius" << radius 
             << "angles" << startAngle << "to" << endAngle;
    
    SketchElement element;
    element.type = SketchElementType::Arc;
    element.points.append(center);
    element.parameters["radius"] = radius;
    element.parameters["startAngle"] = startAngle;
    element.parameters["endAngle"] = endAngle;
    
    d->elements.append(element);
    int index = d->elements.size() - 1;
    
    saveElements();
    notifyModified();
    
    Q_EMIT elementAdded(index);
    Q_EMIT elementCountChanged(d->elements.size());
    
    return index;
}

int Sketch::addPolyline(const QVector<QVector2D>& points, bool closed) {
    if (points.size() < 2) {
        qWarning() << "[Sketch] Cannot add polyline: need at least 2 points";
        return -1;
    }
    
    qDebug() << "[Sketch] Adding polyline with" << points.size() 
             << "points, closed:" << closed;
    
    SketchElement element;
    element.type = SketchElementType::Polyline;
    element.points = points;
    element.parameters["closed"] = closed;
    
    d->elements.append(element);
    int index = d->elements.size() - 1;
    
    saveElements();
    notifyModified();
    
    Q_EMIT elementAdded(index);
    Q_EMIT elementCountChanged(d->elements.size());
    
    return index;
}

bool Sketch::removeElement(int index) {
    if (index < 0 || index >= d->elements.size()) {
        qWarning() << "[Sketch] Invalid element index:" << index;
        return false;
    }
    
    qDebug() << "[Sketch] Removing element at index:" << index;
    
    d->elements.removeAt(index);
    
    saveElements();
    notifyModified();
    
    Q_EMIT elementRemoved(index);
    Q_EMIT elementCountChanged(d->elements.size());
    
    return true;
}

void Sketch::clear() {
    qDebug() << "[Sketch] Clearing all elements";
    
    d->elements.clear();
    
    saveElements();
    notifyModified();
    
    Q_EMIT elementCountChanged(0);
}

int Sketch::elementCount() const {
    return d->elements.size();
}

QVector<SketchElement> Sketch::elements() const {
    return d->elements;
}

SketchElement Sketch::element(int index) const {
    if (index >= 0 && index < d->elements.size()) {
        return d->elements[index];
    }
    return SketchElement();
}

bool Sketch::rebuild() {
    qDebug() << "[Sketch] Rebuilding sketch:" << name();
    
    try {
        // 建立 wire 來表示草圖
        BRepBuilderAPI_MakeWire wireBuilder;
        
        for (const SketchElement& elem : d->elements) {
            if (elem.type == SketchElementType::Line) {
                if (elem.points.size() >= 2) {
                    QVector3D p1 = d->plane.toWorld(elem.points[0]);
                    QVector3D p2 = d->plane.toWorld(elem.points[1]);
                    
                    gp_Pnt gp1(p1.x(), p1.y(), p1.z());
                    gp_Pnt gp2(p2.x(), p2.y(), p2.z());
                    
                    BRepBuilderAPI_MakeEdge edgeBuilder(gp1, gp2);
                    if (edgeBuilder.IsDone()) {
                        wireBuilder.Add(edgeBuilder.Edge());
                    }
                }
            } else if (elem.type == SketchElementType::Polyline) {
                for (int i = 0; i < elem.points.size() - 1; ++i) {
                    QVector3D p1 = d->plane.toWorld(elem.points[i]);
                    QVector3D p2 = d->plane.toWorld(elem.points[i + 1]);
                    
                    gp_Pnt gp1(p1.x(), p1.y(), p1.z());
                    gp_Pnt gp2(p2.x(), p2.y(), p2.z());
                    
                    BRepBuilderAPI_MakeEdge edgeBuilder(gp1, gp2);
                    if (edgeBuilder.IsDone()) {
                        wireBuilder.Add(edgeBuilder.Edge());
                    }
                }
            }
            // TODO: 實作 Circle 和 Arc
        }
        
        if (wireBuilder.IsDone()) {
            setShape(wireBuilder.Wire());
            setValid(true);
            qDebug() << "[Sketch] Rebuild successful";
            return true;
        }
    } catch (...) {
        qWarning() << "[Sketch] Rebuild failed with exception";
    }
    
    setValid(false);
    return false;
}

bool Sketch::isClosed() const {
    // 簡單檢查：如果有任何閉合的 polyline，認為是閉合的
    for (const SketchElement& elem : d->elements) {
        if (elem.type == SketchElementType::Polyline) {
            if (elem.parameters.value("closed", false).toBool()) {
                return true;
            }
        } else if (elem.type == SketchElementType::Circle) {
            return true;
        }
    }
    return false;
}

void Sketch::loadElements() {
    // TODO: 從 OCCT 標籤載入元素
    // 這需要定義元素的序列化格式
    qDebug() << "[Sketch] Loading elements from label";
}

void Sketch::saveElements() {
    // TODO: 儲存元素到 OCCT 標籤
    // 這需要定義元素的序列化格式
    qDebug() << "[Sketch] Saving elements to label";
}

} // namespace cad
} // namespace aicad