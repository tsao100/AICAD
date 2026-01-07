/**
 * @file Extrude.cpp
 * @brief 擠出特徵類別實作
 * @author Ben
 * @date 2025-01-06
 */

#include "Extrude.h"
#include "Sketch.h"
#include "Document.h"

#include <QDebug>

// OCCT includes
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
#include <TDataStd_Real.hxx>

namespace aicad {
namespace cad {

// Private implementation
class Extrude::Private {
public:
    Private(Sketch* sk, double dist)
        : sketch(sk)
        , distance(dist)
        , direction(ExtrudeDirection::Normal)
    {
    }
    
    Sketch* sketch;
    double distance;
    ExtrudeDirection direction;
};

Extrude::Extrude(Document* doc, TDF_Label label, 
                 Sketch* sketch, double distance)
    : Feature(doc, label)
    , d(new Private(sketch, distance))
{
    qDebug() << "[Extrude] Constructor called";
    
    // 儲存參數到 OCCT 標籤
    TDataStd_Real::Set(label, distance);
}

Extrude::~Extrude() {
    qDebug() << "[Extrude] Destructor called";
    delete d;
}

Sketch* Extrude::sketch() const {
    return d->sketch;
}

double Extrude::distance() const {
    return d->distance;
}

void Extrude::setDistance(double distance) {
    if (qAbs(d->distance - distance) > 1e-6) {
        qDebug() << "[Extrude] Distance changed from" 
                 << d->distance << "to" << distance;
        
        d->distance = distance;
        
        // 儲存到 OCCT 標籤
        TDataStd_Real::Set(label(), distance);
        
        // 重建
        rebuild();
        
        Q_EMIT distanceChanged(d->distance);
        notifyModified();
    }
}

ExtrudeDirection Extrude::direction() const {
    return d->direction;
}

void Extrude::setDirection(ExtrudeDirection direction) {
    if (d->direction != direction) {
        qDebug() << "[Extrude] Direction changed";
        
        d->direction = direction;
        
        // 重建
        rebuild();
        
        Q_EMIT directionChanged(d->direction);
        notifyModified();
    }
}

bool Extrude::rebuild() {
    if (!d->sketch) {
        qWarning() << "[Extrude] Cannot rebuild: sketch is null";
        setValid(false);
        return false;
    }
    
    qDebug() << "[Extrude] Rebuilding extrude:" << name()
             << "distance:" << d->distance;
    
    try {
        // 確保草圖已重建
        if (!d->sketch->isValid()) {
            d->sketch->rebuild();
        }
        
        // 建立擠出形狀
        TopoDS_Shape extrusionShape = createExtrusionShape();
        
        if (!extrusionShape.IsNull()) {
            setShape(extrusionShape);
            setValid(true);
            qDebug() << "[Extrude] Rebuild successful";
            return true;
        } else {
            qWarning() << "[Extrude] Failed to create extrusion shape";
        }
    } catch (const Standard_Failure& e) {
        qWarning() << "[Extrude] Rebuild failed:" << e.GetMessageString();
    } catch (...) {
        qWarning() << "[Extrude] Rebuild failed with unknown exception";
    }
    
    setValid(false);
    return false;
}

double Extrude::volume() const {
    TopoDS_Shape sh = shape();
    if (sh.IsNull()) {
        return 0.0;
    }
    
    GProp_GProps props;
    BRepGProp::VolumeProperties(sh, props);
    return props.Mass();
}

double Extrude::surfaceArea() const {
    TopoDS_Shape sh = shape();
    if (sh.IsNull()) {
        return 0.0;
    }
    
    GProp_GProps props;
    BRepGProp::SurfaceProperties(sh, props);
    return props.Mass();
}

TopoDS_Shape Extrude::createExtrusionShape() {
    if (!d->sketch) {
        return TopoDS_Shape();
    }
    
    // 取得草圖元素
    QVector<SketchElement> elements = d->sketch->elements();
    if (elements.isEmpty()) {
        qWarning() << "[Extrude] No elements in sketch";
        return TopoDS_Shape();
    }
    
    // 取得草圖平面
    SketchPlane plane = d->sketch->plane();
    
    try {
        // 建立 wire
        BRepBuilderAPI_MakeWire wireBuilder;
        
        for (const SketchElement& elem : elements) {
            if (elem.type == SketchElementType::Polyline) {
                // 建立多段線的所有邊
                for (int i = 0; i < elem.points.size() - 1; ++i) {
                    QVector3D p1 = plane.toWorld(elem.points[i]);
                    QVector3D p2 = plane.toWorld(elem.points[i + 1]);
                    
                    gp_Pnt gp1(p1.x(), p1.y(), p1.z());
                    gp_Pnt gp2(p2.x(), p2.y(), p2.z());
                    
                    BRepBuilderAPI_MakeEdge edgeBuilder(gp1, gp2);
                    if (edgeBuilder.IsDone()) {
                        wireBuilder.Add(edgeBuilder.Edge());
                    }
                }
            }
        }
        
        if (!wireBuilder.IsDone()) {
            qWarning() << "[Extrude] Failed to create wire";
            return TopoDS_Shape();
        }
        
        TopoDS_Wire wire = wireBuilder.Wire();
        
        // 建立面
        gp_Pln gpPlane = plane.toGpPln();
        BRepBuilderAPI_MakeFace faceBuilder(gpPlane, wire);
        
        if (!faceBuilder.IsDone()) {
            qWarning() << "[Extrude] Failed to create face";
            return TopoDS_Shape();
        }
        
        TopoDS_Face face = faceBuilder.Face();
        
        // 計算擠出向量
        double actualDistance = d->distance;
        if (d->direction == ExtrudeDirection::Reversed) {
            actualDistance = -actualDistance;
        } else if (d->direction == ExtrudeDirection::Symmetric) {
            // 對稱擠出: 待實作
            // 目前先用單向
        }
        
        gp_Vec extrudeVec(
            plane.normal.x() * actualDistance,
            plane.normal.y() * actualDistance,
            plane.normal.z() * actualDistance
        );
        
        // 建立擠出體
        BRepPrimAPI_MakePrism prismBuilder(face, extrudeVec);
        
        if (!prismBuilder.IsDone()) {
            qWarning() << "[Extrude] Failed to create prism";
            return TopoDS_Shape();
        }
        
        return prismBuilder.Shape();
        
    } catch (const Standard_Failure& e) {
        qWarning() << "[Extrude] Exception:" << e.GetMessageString();
    } catch (...) {
        qWarning() << "[Extrude] Unknown exception";
    }
    
    return TopoDS_Shape();
}

// Helper functions
QString extrudeDirectionToString(ExtrudeDirection direction) {
    switch (direction) {
        case ExtrudeDirection::Normal:
            return "Normal";
        case ExtrudeDirection::Reversed:
            return "Reversed";
        case ExtrudeDirection::Symmetric:
            return "Symmetric";
        default:
            return "Unknown";
    }
}

ExtrudeDirection stringToExtrudeDirection(const QString& str) {
    QString lower = str.toLower();
    if (lower == "normal") return ExtrudeDirection::Normal;
    if (lower == "reversed") return ExtrudeDirection::Reversed;
    if (lower == "symmetric") return ExtrudeDirection::Symmetric;
    return ExtrudeDirection::Normal;
}

} // namespace cad
} // namespace aicad