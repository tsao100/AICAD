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
#include <ShapeFix_Shape.hxx>
#include <BRepLib.hxx>

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
#include "cad/Sketch.h"
#include "cad/sketch/SketchRegion.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_HSequenceOfShape.hxx>

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
        if (!prismBuilder.IsDone())
            return BuildResult::error("Failed to extrude face");

        TopoDS_Shape result = prismBuilder.Shape();

        // ✅ 修正面法向量 / 拓撲一致性，避免重繪後某些面消失
        ShapeFix_Shape fixer(result);
        fixer.Perform();
        result = fixer.Shape();

        // ✅ 確保所有曲線精度一致（避免 B-rep 邊界不吻合）
        BRepLib::BuildCurves3d(result);

        return BuildResult(result);
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
    if (!sketch)
        return BuildResult::error("Sketch is null");
    if (std::abs(height) < Precision::Confusion())
        return BuildResult::error("Height is too small");
    if (!sketch->hasValidPlane())
        return BuildResult::error("Sketch has no valid plane");

    try {
        cad::Plane* plane = sketch->plane();
        gp_Vec direction  = toGpVec(plane->normal() * height);
        gp_Pln gpPlane    = plane->toGpPln();

        QList<TopoDS_Wire> wires = sketch->wires();
        if (wires.isEmpty())
            return BuildResult::error("Sketch has no geometry");

        // ── 情況 1：單一封閉 wire（圓、封閉 polyline、矩形）──────────
        if (wires.size() == 1 && wires.first().Closed()) {
            auto faceResult = makeFace(wires.first(), gpPlane);
            if (!faceResult) return faceResult;
            return extrude(TopoDS::Face(faceResult.shape), direction);
        }

        // ── 情況 2：多條或非封閉 wire → 嘗試連接成封閉輪廓 ──────────
        Handle(TopTools_HSequenceOfShape) edges = new TopTools_HSequenceOfShape;
        for (const TopoDS_Wire& w : wires) {
            TopExp_Explorer exp(w, TopAbs_EDGE);
            for (; exp.More(); exp.Next())
                edges->Append(exp.Current());
        }

        Handle(TopTools_HSequenceOfShape) closedWires = new TopTools_HSequenceOfShape;
        Handle(TopTools_HSequenceOfShape) openWires   = new TopTools_HSequenceOfShape;
        ShapeAnalysis_FreeBounds::ConnectEdgesToWires(
            edges, Precision::Confusion(), Standard_False, closedWires);

        if (closedWires->IsEmpty())
            return BuildResult::error(
                "Sketch edges do not form a closed contour. "
                "Please ensure the profile is fully closed.");

        // 多輪廓（含孔洞）：第一條為外輪廓，其餘為孔
        TopoDS_Wire outerWire = TopoDS::Wire(closedWires->Value(1));
        BRepBuilderAPI_MakeFace faceBuilder(gpPlane, outerWire);
        if (!faceBuilder.IsDone())
            return BuildResult::error("Failed to build face from closed contour");

        for (int i = 2; i <= closedWires->Length(); ++i) {
            TopoDS_Wire inner = TopoDS::Wire(closedWires->Value(i));
            faceBuilder.Add(inner);
        }

        if (!faceBuilder.IsDone())
            return BuildResult::error("Failed to build face with holes");

        return extrude(faceBuilder.Face(), direction);

    } catch (const Standard_Failure& e) {
        return BuildResult::error(
            QString("OCCT error: %1").arg(e.GetMessageString()));
    }
}


BuildResult GeometryBuilder::extrudeShape(const TopoDS_Shape& profile,
                                          double height)
{
    if (profile.IsNull())
        return BuildResult::error("Profile is null");

    gp_Vec direction(0.0, 0.0, height);
    try {
        BRepPrimAPI_MakePrism prism(profile, direction);
        if (prism.IsDone())
            return BuildResult(prism.Shape());          // ← 用既有建構子
        return BuildResult::error("BRepPrimAPI_MakePrism failed"); // ← 用 static error()
    } catch (const Standard_Failure& e) {
        return BuildResult::error(
            QString("OCCT error: %1").arg(e.GetMessageString()));
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

TopoDS_Edge edgeFromSketchGeometry(const cad::Sketch* sketch,
                                   const cad::SketchGeometry* g)
{
    using namespace aicad::cad;
    if (!g) return {};

    switch (g->type) {
    case SketchGeometryType::Line: {
        auto* l = static_cast<const SketchLine*>(g);
        QVector3D p1 = sketch->planeToWorld(l->start);
        QVector3D p2 = sketch->planeToWorld(l->end);
        return BRepBuilderAPI_MakeEdge(
            gp_Pnt(p1.x(), p1.y(), p1.z()),
            gp_Pnt(p2.x(), p2.y(), p2.z()));
    }
    case SketchGeometryType::Arc: {
        const auto* a = static_cast<const cad::SketchArc*>(g);
        if (a->curve.IsNull()) return {};
        BRepBuilderAPI_MakeEdge e(a->curve);          // ← 修正
        return e.IsDone() ? e.Edge() : TopoDS_Edge();
    }

    case SketchGeometryType::Circle: {
        const auto* c = static_cast<const cad::SketchCircle*>(g);
        QVector3D ctr = sketch->planeToWorld(c->center);
        QVector3D n   = sketch->plane()->normal();
        gp_Ax2 ax2(gp_Pnt(ctr.x(), ctr.y(), ctr.z()),
                   gp_Dir(n.x(),   n.y(),   n.z()));
        BRepBuilderAPI_MakeEdge e(gp_Circ(ax2, c->radius));
        return e.IsDone() ? e.Edge() : TopoDS_Edge();
    }
    case SketchGeometryType::Polyline: {
        // Polyline 不能只回傳單一 edge；此 case 僅供 fallback 偵錯，
        // 正常路徑應從 faceFromSketchRegion 展開
        qWarning() << "[edgeFromSketchGeometry] Polyline should be handled by faceFromSketchRegion";
        return {};
    }
    default:
        return {};
    }
}


TopoDS_Face faceFromSketchRegion(const cad::Sketch* sketch,
                                 const cad::SketchRegion& region)
{
    BRepBuilderAPI_MakeWire outerWire;
    QSet<QString> processedUuids; // ← 防止同一 Polyline uuid 處理 N 次

    for (const QString& uuid : region.outerLoop.edgeUuids) {
        for (const cad::SketchGeometry* g : sketch->geometries()) {
            if (g->uuid != uuid) continue;

            if (g->type == cad::SketchGeometryType::Polyline) {
                if (processedUuids.contains(uuid)) break; // ← 只展開一次
                processedUuids.insert(uuid);
                const auto* pl = static_cast<const cad::SketchPolyline*>(g);
                int nSeg = pl->closed ? pl->points.size() : pl->points.size() - 1;
                for (int i = 0; i < nSeg; ++i) {
                    QVector3D p1 = sketch->planeToWorld(pl->points[i]);
                    QVector3D p2 = sketch->planeToWorld(pl->points[(i + 1) % pl->points.size()]);
                    BRepBuilderAPI_MakeEdge e(
                        gp_Pnt(p1.x(), p1.y(), p1.z()),
                        gp_Pnt(p2.x(), p2.y(), p2.z()));
                    if (e.IsDone()) outerWire.Add(e.Edge());
                }
            } else {
                TopoDS_Edge e = edgeFromSketchGeometry(sketch, g);
                if (!e.IsNull()) outerWire.Add(e);
            }
            break;
        }
    }
    if (!outerWire.IsDone()) return {};

    BRepBuilderAPI_MakeFace faceMaker(outerWire.Wire(), Standard_True);

    for (const auto& hole : region.holes) {
        BRepBuilderAPI_MakeWire holeWire;
        for (const QString& uuid : hole.edgeUuids) {
            for (const cad::SketchGeometry* g : sketch->geometries()) {
                if (g->uuid != uuid) continue;
                TopoDS_Edge e = edgeFromSketchGeometry(sketch, g);  // ← 正確函式名
                if (!e.IsNull()) holeWire.Add(e);
            }
        }
        if (holeWire.IsDone()) faceMaker.Add(holeWire.Wire());
    }

    return faceMaker.IsDone() ? faceMaker.Face() : TopoDS_Face();
}

} // namespace geometry
} // namespace aicad
