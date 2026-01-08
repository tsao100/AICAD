/**
 * @file GeometryBuilder.cpp
 * @brief 幾何建構輔助類別實作
 * @author AICAD Team
 * @date 2025-01-08
 */

#include "GeometryBuilder.h"
#include "cad/Plane.h"
#include "cad/Sketch.h"

#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <Geom_Plane.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>

#include <GC_MakeSegment.hxx>
#include <GC_MakeCircle.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_Circle.hxx>
#include <Geom_TrimmedCurve.hxx>

#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <Precision.hxx>
#include <ShapeAnalysis_Wire.hxx>

#include <QDebug>
#include <cmath>

namespace aicad {
namespace geometry {

// === Wire 建構 ===

BuildResult GeometryBuilder::makeWire(const QVector<QVector2D>& points,
                                      const cad::Plane& plane,
                                      bool closed) {
    if (points.size() < 2) {
        return BuildResult::error("At least 2 points required");
    }
    
    try {
        BRepBuilderAPI_MakeWire wireBuilder;
        
        // 將 2D 點轉為 3D
        QVector<QVector3D> points3D;
        for (const QVector2D& pt : points) {
            points3D.append(plane.toWorld(pt.x(), pt.y()));
        }
        
        // 如果需要封閉但最後一點不等於第一點，加入最後一段
        if (closed && points3D.size() > 2) {
            QVector3D first = points3D.first();
            QVector3D last = points3D.last();
            if ((first - last).length() > Precision::Confusion()) {
                points3D.append(first);
            }
        }
        
        // 建立邊
        for (int i = 0; i < points3D.size() - 1; ++i) {
            gp_Pnt p1 = toGpPnt(points3D[i]);
            gp_Pnt p2 = toGpPnt(points3D[i + 1]);
            
            if (p1.Distance(p2) > Precision::Confusion()) {
                BRepBuilderAPI_MakeEdge edgeBuilder(p1, p2);
                if (edgeBuilder.IsDone()) {
                    wireBuilder.Add(edgeBuilder.Edge());
                }
            }
        }
        
        if (wireBuilder.IsDone()) {
            return BuildResult(wireBuilder.Wire());
        } else {
            return BuildResult::error("Failed to build wire");
        }
        
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

BuildResult GeometryBuilder::makeWire(const QVector<QVector3D>& points,
                                      bool closed) {
    if (points.size() < 2) {
        return BuildResult::error("At least 2 points required");
    }
    
    try {
        BRepBuilderAPI_MakeWire wireBuilder;
        
        QVector<QVector3D> pts = points;
        if (closed && pts.size() > 2) {
            QVector3D first = pts.first();
            QVector3D last = pts.last();
            if ((first - last).length() > Precision::Confusion()) {
                pts.append(first);
            }
        }
        
        for (int i = 0; i < pts.size() - 1; ++i) {
            gp_Pnt p1 = toGpPnt(pts[i]);
            gp_Pnt p2 = toGpPnt(pts[i + 1]);
            
            if (p1.Distance(p2) > Precision::Confusion()) {
                BRepBuilderAPI_MakeEdge edgeBuilder(p1, p2);
                if (edgeBuilder.IsDone()) {
                    wireBuilder.Add(edgeBuilder.Edge());
                }
            }
        }
        
        if (wireBuilder.IsDone()) {
            return BuildResult(wireBuilder.Wire());
        } else {
            return BuildResult::error("Failed to build wire");
        }
        
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

BuildResult GeometryBuilder::makeWire(const QVector<TopoDS_Edge>& edges) {
    if (edges.isEmpty()) {
        return BuildResult::error("No edges provided");
    }
    
    try {
        BRepBuilderAPI_MakeWire wireBuilder;
        
        for (const TopoDS_Edge& edge : edges) {
            if (!edge.IsNull()) {
                wireBuilder.Add(edge);
            }
        }
        
        if (wireBuilder.IsDone()) {
            return BuildResult(wireBuilder.Wire());
        } else {
            return BuildResult::error("Failed to build wire from edges");
        }
        
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

// === Edge 建構 ===

BuildResult GeometryBuilder::makeLine(const gp_Pnt& p1, const gp_Pnt& p2) {
    if (p1.Distance(p2) < Precision::Confusion()) {
        return BuildResult::error("Points are too close");
    }
    
    try {
        BRepBuilderAPI_MakeEdge edgeBuilder(p1, p2);
        if (edgeBuilder.IsDone()) {
            return BuildResult(edgeBuilder.Edge());
        } else {
            return BuildResult::error("Failed to create line");
        }
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

BuildResult GeometryBuilder::makeArc(const gp_Pnt& center,
                                     double radius,
                                     const gp_Dir& normal,
                                     double startAngle,
                                     double endAngle) {
    if (radius <= Precision::Confusion()) {
        return BuildResult::error("Radius must be positive");
    }
    
    try {
        gp_Ax2 ax2(center, normal);
        Handle(Geom_Circle) circle = new Geom_Circle(ax2, radius);
        
        Handle(Geom_TrimmedCurve) arc = new Geom_TrimmedCurve(
            circle, startAngle, endAngle, Standard_True);
        
        BRepBuilderAPI_MakeEdge edgeBuilder(arc);
        if (edgeBuilder.IsDone()) {
            return BuildResult(edgeBuilder.Edge());
        } else {
            return BuildResult::error("Failed to create arc");
        }
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

BuildResult GeometryBuilder::makeCircle(const gp_Pnt& center,
                                        double radius,
                                        const gp_Dir& normal) {
    if (radius <= Precision::Confusion()) {
        return BuildResult::error("Radius must be positive");
    }
    
    try {
        gp_Ax2 ax2(center, normal);
        Handle(Geom_Circle) circle = new Geom_Circle(ax2, radius);
        
        BRepBuilderAPI_MakeEdge edgeBuilder(circle);
        if (edgeBuilder.IsDone()) {
            return BuildResult(edgeBuilder.Edge());
        } else {
            return BuildResult::error("Failed to create circle");
        }
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

// === Face 建構 ===

BuildResult GeometryBuilder::makeFace(const TopoDS_Wire& wire,
                                      const gp_Pln& plane) {
    if (wire.IsNull()) {
        return BuildResult::error("Wire is null");
    }
    
    try {
        BRepBuilderAPI_MakeFace faceBuilder(plane, wire);
        if (faceBuilder.IsDone()) {
            return BuildResult(faceBuilder.Face());
        } else {
            return BuildResult::error("Failed to create face");
        }
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

BuildResult GeometryBuilder::makeFace(const TopoDS_Wire& wire) {
    if (wire.IsNull()) {
        return BuildResult::error("Wire is null");
    }
    
    try {
        // 嘗試自動偵測平面
        gp_Pln plane;
        if (isPlanar(wire, &plane)) {
            return makeFace(wire, plane);
        }
        
        // 如果不是平面，嘗試直接建立
        BRepBuilderAPI_MakeFace faceBuilder(wire);
        if (faceBuilder.IsDone()) {
            return BuildResult(faceBuilder.Face());
        } else {
            return BuildResult::error("Wire is not planar or face creation failed");
        }
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

BuildResult GeometryBuilder::makeFace(const TopoDS_Wire& outerWire,
                                      const QVector<TopoDS_Wire>& innerWires) {
    if (outerWire.IsNull()) {
        return BuildResult::error("Outer wire is null");
    }
    
    try {
        BRepBuilderAPI_MakeFace faceBuilder(outerWire);
        
        for (const TopoDS_Wire& innerWire : innerWires) {
            if (!innerWire.IsNull()) {
                faceBuilder.Add(innerWire);
            }
        }
        
        if (faceBuilder.IsDone()) {
            return BuildResult(faceBuilder.Face());
        } else {
            return BuildResult::error("Failed to create face with holes");
        }
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

// === Solid 建構 ===

BuildResult GeometryBuilder::extrude(const TopoDS_Face& face,
                                     const gp_Vec& direction) {
    if (face.IsNull()) {
        return BuildResult::error("Face is null");
    }
    
    if (direction.Magnitude() < Precision::Confusion()) {
        return BuildResult::error("Direction vector is too small");
    }
    
    try {
        BRepPrimAPI_MakePrism prismBuilder(face, direction);
        if (prismBuilder.IsDone()) {
            return BuildResult(prismBuilder.Shape());
        } else {
            return BuildResult::error("Failed to extrude face");
        }
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

BuildResult GeometryBuilder::extrude(const TopoDS_Wire& wire,
                                     const gp_Vec& direction,
                                     bool makeSolid) {
    if (wire.IsNull()) {
        return BuildResult::error("Wire is null");
    }
    
    if (makeSolid && !isClosed(wire)) {
        return BuildResult::error("Wire must be closed to create solid");
    }
    
    try {
        // 先從 Wire 建立 Face
        auto faceResult = makeFace(wire);
        if (!faceResult) {
            return faceResult;
        }
        
        // 擠出 Face
        return extrude(TopoDS::Face(faceResult.shape), direction);
        
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

BuildResult GeometryBuilder::extrudeSketch(cad::Sketch* sketch, double height) {
    if (!sketch) {
        return BuildResult::error("Sketch is null");
    }
    
    if (std::abs(height) < Precision::Confusion()) {
        return BuildResult::error("Height is too small");
    }
    
    try {
        // 取得主要輪廓
        TopoDS_Wire wire = sketch->mainWire();
        if (wire.IsNull()) {
            return BuildResult::error("Sketch has no valid wire");
        }
        
        // 計算擠出方向
        cad::Plane plane = sketch->plane();
        QVector3D normal = plane.normal();
        gp_Vec direction = toGpVec(normal * height);
        
        // 執行擠出
        return extrude(wire, direction, true);
        
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

BuildResult GeometryBuilder::revolve(const TopoDS_Shape& profile,
                                     const gp_Pnt& axisPoint,
                                     const gp_Dir& axisDir,
                                     double angle) {
    if (profile.IsNull()) {
        return BuildResult::error("Profile is null");
    }
    
    try {
        gp_Ax1 axis(axisPoint, axisDir);
        BRepPrimAPI_MakeRevol revolBuilder(profile, axis, angle);
        
        if (revolBuilder.IsDone()) {
            return BuildResult(revolBuilder.Shape());
        } else {
            return BuildResult::error("Failed to revolve profile");
        }
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

// === 布林運算 ===

BuildResult GeometryBuilder::unite(const TopoDS_Shape& shape1,
                                   const TopoDS_Shape& shape2) {
    if (shape1.IsNull() || shape2.IsNull()) {
        return BuildResult::error("Input shape is null");
    }
    
    try {
        BRepAlgoAPI_Fuse fuseOp(shape1, shape2);
        if (fuseOp.IsDone()) {
            return BuildResult(fuseOp.Shape());
        } else {
            return BuildResult::error("Boolean union failed");
        }
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

BuildResult GeometryBuilder::cut(const TopoDS_Shape& base,
                                 const TopoDS_Shape& tool) {
    if (base.IsNull() || tool.IsNull()) {
        return BuildResult::error("Input shape is null");
    }
    
    try {
        BRepAlgoAPI_Cut cutOp(base, tool);
        if (cutOp.IsDone()) {
            return BuildResult(cutOp.Shape());
        } else {
            return BuildResult::error("Boolean cut failed");
        }
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

BuildResult GeometryBuilder::common(const TopoDS_Shape& shape1,
                                    const TopoDS_Shape& shape2) {
    if (shape1.IsNull() || shape2.IsNull()) {
        return BuildResult::error("Input shape is null");
    }
    
    try {
        BRepAlgoAPI_Common commonOp(shape1, shape2);
        if (commonOp.IsDone()) {
            return BuildResult(commonOp.Shape());
        } else {
            return BuildResult::error("Boolean common failed");
        }
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

// === 修改操作 ===

BuildResult GeometryBuilder::fillet(const TopoDS_Shape& shape,
                                    const QVector<TopoDS_Edge>& edges,
                                    double radius) {
    if (shape.IsNull()) {
        return BuildResult::error("Shape is null");
    }
    
    if (radius <= Precision::Confusion()) {
        return BuildResult::error("Radius must be positive");
    }
    
    try {
        BRepFilletAPI_MakeFillet filletBuilder(shape);
        
        for (const TopoDS_Edge& edge : edges) {
            if (!edge.IsNull()) {
                filletBuilder.Add(radius, edge);
            }
        }
        
        filletBuilder.Build();
        if (filletBuilder.IsDone()) {
            return BuildResult(filletBuilder.Shape());
        } else {
            return BuildResult::error("Fillet operation failed");
        }
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

BuildResult GeometryBuilder::chamfer(const TopoDS_Shape& shape,
                                     const QVector<TopoDS_Edge>& edges,
                                     double distance) {
    if (shape.IsNull()) {
        return BuildResult::error("Shape is null");
    }
    
    if (distance <= Precision::Confusion()) {
        return BuildResult::error("Distance must be positive");
    }
    
    try {
        BRepFilletAPI_MakeChamfer chamferBuilder(shape);
        
        for (const TopoDS_Edge& edge : edges) {
            if (!edge.IsNull()) {
                // 簡化版本：使用對稱倒角
                TopoDS_Face face;  // 需要相鄰的面，這裡簡化處理
                chamferBuilder.Add(distance, edge);
            }
        }
        
        chamferBuilder.Build();
        if (chamferBuilder.IsDone()) {
            return BuildResult(chamferBuilder.Shape());
        } else {
            return BuildResult::error("Chamfer operation failed");
        }
    } catch (const Standard_Failure& e) {
        return BuildResult::error(QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}

// === 工具函式 ===

bool GeometryBuilder::isClosed(const TopoDS_Wire& wire) {
    if (wire.IsNull()) return false;
    return wire.Closed();
}

bool GeometryBuilder::isPlanar(const TopoDS_Wire& wire, gp_Pln* plane) {
    if (wire.IsNull()) return false;
    
    try {
        BRepBuilderAPI_MakeFace faceBuilder(wire, Standard_True);
        if (faceBuilder.IsDone()) {
            if (plane) {
                TopoDS_Face face = faceBuilder.Face();
                Handle(Geom_Surface) surface = BRep_Tool::Surface(face);
                Handle(Geom_Plane) geomPlane = Handle(Geom_Plane)::DownCast(surface);
                if (!geomPlane.IsNull()) {
                    *plane = geomPlane->Pln();
                }
            }
            return true;
        }
    } catch (...) {
    }
    
    return false;
}

void GeometryBuilder::getBoundingBox(const TopoDS_Shape& shape,
                                     double& xmin, double& ymin, double& zmin,
                                     double& xmax, double& ymax, double& zmax) {
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
}

gp_Pnt GeometryBuilder::toGpPnt(const QVector3D& v) {
    return gp_Pnt(v.x(), v.y(), v.z());
}

gp_Vec GeometryBuilder::toGpVec(const QVector3D& v) {
    return gp_Vec(v.x(), v.y(), v.z());
}

gp_Dir GeometryBuilder::toGpDir(const QVector3D& v) {
    QVector3D normalized = v.normalized();
    return gp_Dir(normalized.x(), normalized.y(), normalized.z());
}

QVector3D GeometryBuilder::fromGpPnt(const gp_Pnt& p) {
    return QVector3D(p.X(), p.Y(), p.Z());
}

} // namespace geometry
} // namespace aicad
