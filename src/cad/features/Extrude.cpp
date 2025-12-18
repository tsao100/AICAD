/**
 * @file Extrude.cpp
 * @brief Extrude 實作
 * @author Ben
 * @date 2024-12-04
 */

#include "Extrude.h"
#include "Sketch.h"

#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp_Explorer.hxx>
#include <Precision.hxx>

#include <QDebug>
#include <QtMath>

namespace aicad {
namespace cad {

class Extrude::Private {
public:
    Sketch* sketch = nullptr;
    double height = 0.0;
};

Extrude::Extrude(Sketch* sketch, double height, QObject* parent)
    : Feature(parent)
    , d(new Private())
{
    d->sketch = sketch;
    d->height = height;
    
    setName(QString("Extrude (%.2f)").arg(height));
    
    if (d->sketch) {
        d->sketch->addDependent(this);
        
        // 連接草圖更新信號
        connect(d->sketch, &Feature::updated, this, [this]() {
            qDebug() << "[Extrude] Sketch updated, invalidating";
            invalidate();
        });
    }
    
    qDebug() << "[Extrude] Created with height:" << height;
}

Extrude::~Extrude() {
    qDebug() << "[Extrude] Destroyed:" << name();
    
    if (d->sketch) {
        d->sketch->removeDependent(this);
    }
    
    delete d;
}

TopoDS_Shape Extrude::compute() {
    if (!d->sketch) {
        qWarning() << "[Extrude] No sketch set";
        return TopoDS_Shape();
    }
    
    if (qAbs(d->height) < Precision::Confusion()) {
        qWarning() << "[Extrude] Height too small:" << d->height;
        return TopoDS_Shape();
    }
    
    // 確保草圖是最新的
    if (d->sketch->needsUpdate()) {
        qDebug() << "[Extrude] Updating sketch first";
        d->sketch->update();
    }
    
    TopoDS_Shape sketchShape = d->sketch->shape();
    if (sketchShape.IsNull()) {
        qWarning() << "[Extrude] Sketch shape is null";
        return TopoDS_Shape();
    }
    
    qDebug() << "[Extrude] Computing with height:" << d->height;
    
    try {
        // 嘗試將草圖形狀轉換為 Wire
        TopoDS_Wire wire;
        
        if (sketchShape.ShapeType() == TopAbs_WIRE) {
            wire = TopoDS::Wire(sketchShape);
        } else if (sketchShape.ShapeType() == TopAbs_COMPOUND) {
            // 如果是 Compound，嘗試提取第一條 Wire
            TopExp_Explorer explorer(sketchShape, TopAbs_WIRE);
            if (explorer.More()) {
                wire = TopoDS::Wire(explorer.Current());
            } else {
                qWarning() << "[Extrude] No wire found in compound";
                return TopoDS_Shape();
            }
        } else {
            qWarning() << "[Extrude] Unexpected sketch shape type:" << sketchShape.ShapeType();
            return TopoDS_Shape();
        }
        
        if (wire.IsNull()) {
            qWarning() << "[Extrude] Wire is null";
            return TopoDS_Shape();
        }
        
        // 建立面
        gp_Pln plane = d->sketch->plane().toGpPln();
        BRepBuilderAPI_MakeFace faceBuilder(plane, wire);
        
        if (!faceBuilder.IsDone()) {
            qWarning() << "[Extrude] Failed to create face";
            return TopoDS_Shape();
        }
        
        TopoDS_Face face = faceBuilder.Face();
        
        // 計算擠出向量
        gp_Vec extrudeVec(
            d->sketch->plane().normal.x() * d->height,
            d->sketch->plane().normal.y() * d->height,
            d->sketch->plane().normal.z() * d->height
        );
        
        qDebug() << "[Extrude] Extrude vector:" 
                 << extrudeVec.X() << extrudeVec.Y() << extrudeVec.Z();
        
        // 建立擠出實體
        BRepPrimAPI_MakePrism prismBuilder(face, extrudeVec);
        
        if (!prismBuilder.IsDone()) {
            qWarning() << "[Extrude] Failed to create prism";
            return TopoDS_Shape();
        }
        
        TopoDS_Shape result = prismBuilder.Shape();
        
        if (result.IsNull()) {
            qWarning() << "[Extrude] Result shape is null";
            return TopoDS_Shape();
        }
        
        qDebug() << "[Extrude] Compute successful, shape type:" << result.ShapeType();
        return result;
        
    } catch (const Standard_Failure& e) {
        qCritical() << "[Extrude] OCCT exception:" << e.GetMessageString();
        return TopoDS_Shape();
    } catch (const std::exception& e) {
        qCritical() << "[Extrude] Standard exception:" << e.what();
        return TopoDS_Shape();
    } catch (...) {
        qCritical() << "[Extrude] Unknown exception during compute";
        return TopoDS_Shape();
    }
}

QVector<Feature*> Extrude::dependencies() const {
    QVector<Feature*> deps;
    if (d->sketch) {
        deps.append(d->sketch);
    }
    return deps;
}

Sketch* Extrude::sketch() const {
    return d->sketch;
}

void Extrude::setSketch(Sketch* sketch) {
    if (d->sketch == sketch) {
        return;
    }
    
    // 移除舊草圖的依賴
    if (d->sketch) {
        d->sketch->removeDependent(this);
        disconnect(d->sketch, nullptr, this, nullptr);
    }
    
    d->sketch = sketch;
    
    // 新增新草圖的依賴
    if (d->sketch) {
        d->sketch->addDependent(this);
        connect(d->sketch, &Feature::updated, this, [this]() {
            invalidate();
        });
    }
    
    invalidate();
    Q_EMIT sketchChanged(sketch);
    
    qDebug() << "[Extrude] Sketch changed";
}

double Extrude::height() const {
    return d->height;
}

void Extrude::setHeight(double height) {
    if (qFuzzyCompare(d->height, height)) {
        return;
    }
    
    d->height = height;
    setName(QString("Extrude (%.2f)").arg(height));
    
    invalidate();
    Q_EMIT heightChanged(height);
    
    qDebug() << "[Extrude] Height changed to:" << height;
}

} // namespace cad
} // namespace aicad