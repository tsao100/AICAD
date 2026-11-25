/**
 * @file SolidOps.cpp
 * @brief 見 SolidOps.h
 */
#include "SolidOps.h"

#include "Sketch.h"
#include "Plane.h"
#include "Document.h"
#include "sketch/SketchRegion.h"
#include "geometry/GeometryBuilder.h"
#include "core/ParameterStore.h"

#include <QDebug>
#include <QVector2D>
#include <QVector3D>
#include <QJsonValue>

#include <BRep_Builder.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <Precision.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <limits>

namespace aicad {
namespace cad {
namespace solidops {

// ═════════════════════════════════════════════════════════════════════════
//  運算類型
// ═════════════════════════════════════════════════════════════════════════

QString opToString(SolidOp op)
{
    switch (op) {
    case SolidOp::Fuse:   return QStringLiteral("Fuse");
    case SolidOp::Cut:    return QStringLiteral("Cut");
    case SolidOp::Common: return QStringLiteral("Common");
    case SolidOp::NewBody:
    default:              return QStringLiteral("NewBody");
    }
}

SolidOp opFromString(const QString& s)
{
    if (s == QLatin1String("Fuse"))   return SolidOp::Fuse;
    if (s == QLatin1String("Cut"))    return SolidOp::Cut;
    if (s == QLatin1String("Common")) return SolidOp::Common;
    return SolidOp::NewBody;
}

QString opLabel(SolidOp op)
{
    switch (op) {
    case SolidOp::Fuse:   return QStringLiteral("聯集（加到目標實體）");
    case SolidOp::Cut:    return QStringLiteral("挖除（從目標實體扣除）");
    case SolidOp::Common: return QStringLiteral("交集（只留重疊部分）");
    case SolidOp::NewBody:
    default:              return QStringLiteral("新實體");
    }
}

// ═════════════════════════════════════════════════════════════════════════
//  運算式
// ═════════════════════════════════════════════════════════════════════════

bool evalExpression(Document* doc, const QString& expr, double* out)
{
    const QString e = expr.trimmed();
    if (e.isEmpty()) return false;

    bool literal = false;
    const double v = e.toDouble(&literal);
    if (literal) {
        if (out) *out = v;
        return true;
    }
    if (!doc || !doc->parameterStore()) return false;

    const auto r = doc->parameterStore()->evaluate(e);
    if (!r.first) return false;
    if (out) *out = r.second;
    return true;
}

// ═════════════════════════════════════════════════════════════════════════
//  草圖 → 輪廓
// ═════════════════════════════════════════════════════════════════════════

namespace {

struct RegionFace {
    TopoDS_Face face;
    RegionInfo  info;
};

/// 草圖所有區域的面，依面積由大到小排序（面積相同再依質心 x、y）。
/// describeRegions() 與 profileFaces() 共用同一份排序，區域索引才會一致，且不受
/// SketchLoopFinder 輸出順序影響。
QList<RegionFace> collectRegionFaces(Sketch* sk)
{
    QList<RegionFace> out;
    if (!sk) return out;

    const QVector<SketchRegion> regions = sk->detectRegions();
    for (const SketchRegion& r : regions) {
        TopoDS_Face f = geometry::faceFromSketchRegion(sk, r);
        if (f.IsNull()) continue;

        RegionFace rf;
        rf.face = f;

        GProp_GProps gp;
        BRepGProp::SurfaceProperties(f, gp);
        rf.info.area = std::abs(gp.Mass());

        const gp_Pnt c = gp.CentreOfMass();
        if (Plane* pl = sk->plane()) {
            const QVector2D p = pl->toPlane(QVector3D(float(c.X()), float(c.Y()), float(c.Z())));
            rf.info.cx = p.x();
            rf.info.cy = p.y();
        }
        out.append(rf);
    }

    std::stable_sort(out.begin(), out.end(), [](const RegionFace& a, const RegionFace& b) {
        if (std::abs(a.info.area - b.info.area) > 1e-9) return a.info.area > b.info.area;
        if (std::abs(a.info.cx - b.info.cx) > 1e-9)     return a.info.cx < b.info.cx;
        return a.info.cy < b.info.cy;
    });
    for (int i = 0; i < out.size(); ++i) out[i].info.index = i;
    return out;
}

/// 沒有可偵測區域時（例如圓弧＋直線分屬不同 wire）：把所有邊接成封閉線圈再建面
QList<TopoDS_Face> fallbackFaces(Sketch* sk)
{
    QList<TopoDS_Face> faces;
    if (!sk) return faces;

    Handle(TopTools_HSequenceOfShape) edges = new TopTools_HSequenceOfShape;
    for (const TopoDS_Wire& w : sk->wires()) {
        if (w.IsNull()) continue;
        for (TopExp_Explorer ex(w, TopAbs_EDGE); ex.More(); ex.Next())
            edges->Append(ex.Current());
    }
    if (edges->IsEmpty()) return faces;

    Handle(TopTools_HSequenceOfShape) wires = new TopTools_HSequenceOfShape;
    ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges, 1.0e-3, Standard_False, wires);

    for (int i = 1; i <= wires->Length(); ++i) {
        const TopoDS_Wire w = TopoDS::Wire(wires->Value(i));
        if (w.IsNull() || !w.Closed()) continue;
        BRepBuilderAPI_MakeFace mk(w, Standard_True);
        if (mk.IsDone()) faces.append(mk.Face());
    }
    return faces;
}

} // namespace

QList<RegionInfo> describeRegions(Sketch* sketch)
{
    QList<RegionInfo> out;
    for (const RegionFace& rf : collectRegionFaces(sketch))
        out.append(rf.info);
    return out;
}

QList<TopoDS_Face> profileFaces(Sketch* sketch, int regionIndex, QString* err)
{
    QList<TopoDS_Face> faces;
    if (!sketch) {
        if (err) *err = QStringLiteral("尚未指定草圖");
        return faces;
    }
    if (!sketch->hasValidShape()) {
        if (err) *err = QStringLiteral("草圖「%1」沒有有效的形狀").arg(sketch->name());
        return faces;
    }

    try {
        const QList<RegionFace> regions = collectRegionFaces(sketch);
        if (!regions.isEmpty()) {
            if (regionIndex >= 0 && regionIndex < regions.size()) {
                faces.append(regions[regionIndex].face);
            } else {
                for (const RegionFace& rf : regions) faces.append(rf.face);
            }
            return faces;
        }

        faces = fallbackFaces(sketch);
    } catch (const Standard_Failure& ex) {
        if (err) *err = QStringLiteral("取得草圖輪廓失敗：%1")
                            .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown");
        return QList<TopoDS_Face>();
    }

    if (faces.isEmpty() && err)
        *err = QStringLiteral("草圖「%1」沒有封閉輪廓").arg(sketch->name());
    return faces;
}

bool sectionWireFromSketch(Sketch* sketch, TopoDS_Wire& out, QString* err)
{
    const QList<TopoDS_Face> faces = profileFaces(sketch, 0, err);
    if (faces.isEmpty()) return false;

    const TopoDS_Wire w = BRepTools::OuterWire(faces.first());
    if (w.IsNull()) {
        if (err) *err = QStringLiteral("草圖「%1」的輪廓沒有外環").arg(sketch->name());
        return false;
    }
    out = w;
    return true;
}

bool pathWireFromSketch(Sketch* sketch, TopoDS_Wire& out, QString* err)
{
    if (!sketch) {
        if (err) *err = QStringLiteral("尚未指定路徑草圖");
        return false;
    }

    try {
        Handle(TopTools_HSequenceOfShape) edges = new TopTools_HSequenceOfShape;
        for (const TopoDS_Wire& w : sketch->wires()) {
            if (w.IsNull()) continue;
            for (TopExp_Explorer ex(w, TopAbs_EDGE); ex.More(); ex.Next())
                edges->Append(ex.Current());
        }
        if (edges->IsEmpty()) {
            if (err) *err = QStringLiteral("路徑草圖「%1」沒有任何曲線").arg(sketch->name());
            return false;
        }

        Handle(TopTools_HSequenceOfShape) wires = new TopTools_HSequenceOfShape;
        ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges, 1.0e-3, Standard_False, wires);
        if (wires->IsEmpty()) {
            if (err) *err = QStringLiteral("路徑草圖「%1」的曲線無法接成連續路徑").arg(sketch->name());
            return false;
        }

        // 多條不相連的線圈時取邊數最多者
        TopoDS_Wire best;
        int bestCount = -1;
        for (int i = 1; i <= wires->Length(); ++i) {
            const TopoDS_Wire w = TopoDS::Wire(wires->Value(i));
            int count = 0;
            for (TopExp_Explorer ex(w, TopAbs_EDGE); ex.More(); ex.Next()) ++count;
            if (count > bestCount) { best = w; bestCount = count; }
        }
        if (best.IsNull()) {
            if (err) *err = QStringLiteral("路徑草圖「%1」沒有可用的路徑").arg(sketch->name());
            return false;
        }
        if (wires->Length() > 1)
            qWarning() << "[SolidOps] path sketch" << sketch->name()
                       << "has" << wires->Length() << "disconnected chains; using the longest one";
        out = best;
        return true;
    } catch (const Standard_Failure& ex) {
        if (err) *err = QStringLiteral("建立路徑失敗：%1")
                            .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown");
        return false;
    }
}

// ═════════════════════════════════════════════════════════════════════════
//  形狀處理
// ═════════════════════════════════════════════════════════════════════════

bool containsSolid(const TopoDS_Shape& s)
{
    if (s.IsNull()) return false;
    TopExp_Explorer ex(s, TopAbs_SOLID);
    return ex.More();
}

TopoDS_Shape finalizeSolid(const TopoDS_Shape& in, QString* err)
{
    if (in.IsNull()) {
        if (err) *err = QStringLiteral("結果形狀為空");
        return TopoDS_Shape();
    }

    try {
        BRep_Builder bb;
        TopoDS_Compound comp;
        bb.MakeCompound(comp);

        int n = 0;
        TopoDS_Solid lastSolid;

        auto addSolid = [&](TopoDS_Solid s) {
            GProp_GProps props;
            BRepGProp::VolumeProperties(s, props);
            if (props.Mass() < 0.0) s.Reverse();      // 體積為負 = 方向反了
            bb.Add(comp, s);
            lastSolid = s;
            ++n;
        };

        for (TopExp_Explorer ex(in, TopAbs_SOLID); ex.More(); ex.Next())
            addSolid(TopoDS::Solid(ex.Current()));

        if (n == 0) {
            // 只有殼：嘗試縫成實體
            for (TopExp_Explorer ex(in, TopAbs_SHELL); ex.More(); ex.Next()) {
                BRepBuilderAPI_MakeSolid ms(TopoDS::Shell(ex.Current()));
                if (ms.IsDone()) addSolid(ms.Solid());
            }
        }

        if (n == 0) {
            if (err) *err = QStringLiteral("運算結果沒有任何實體（輪廓或參數可能不合理）");
            return TopoDS_Shape();
        }

        TopoDS_Shape result = (n == 1) ? TopoDS_Shape(lastSolid) : TopoDS_Shape(comp);

        BRepCheck_Analyzer ana(result);
        if (!ana.IsValid()) {
            ShapeFix_Shape fix(result);
            fix.Perform();
            const TopoDS_Shape fixed = fix.Shape();
            if (!fixed.IsNull() && containsSolid(fixed)) {
                BRepCheck_Analyzer ana2(fixed);
                if (ana2.IsValid()) result = fixed;
                else qWarning() << "[SolidOps] result solid is still not valid after ShapeFix";
            }
        }
        return result;

    } catch (const Standard_Failure& ex) {
        if (err) *err = QStringLiteral("整理實體失敗：%1")
                            .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown");
        return TopoDS_Shape();
    }
}

TopoDS_Shape applyOp(const TopoDS_Shape& target, const TopoDS_Shape& tool,
                     SolidOp op, QString* err)
{
    if (op == SolidOp::NewBody) return tool;

    if (target.IsNull()) {
        if (err) *err = QStringLiteral("目標實體沒有有效的形狀");
        return TopoDS_Shape();
    }
    if (tool.IsNull()) {
        if (err) *err = QStringLiteral("形狀為空，無法進行布林運算");
        return TopoDS_Shape();
    }

    TopoDS_Shape result;
    try {
        switch (op) {
        case SolidOp::Fuse: {
            BRepAlgoAPI_Fuse mk(target, tool);
            if (!mk.IsDone() || mk.HasErrors()) {
                if (err) *err = QStringLiteral("聯集運算失敗");
                return TopoDS_Shape();
            }
            result = mk.Shape();
            break;
        }
        case SolidOp::Cut: {
            BRepAlgoAPI_Cut mk(target, tool);
            if (!mk.IsDone() || mk.HasErrors()) {
                if (err) *err = QStringLiteral("挖除運算失敗（挖除的形狀可能沒有與目標實體相交）");
                return TopoDS_Shape();
            }
            result = mk.Shape();
            break;
        }
        case SolidOp::Common: {
            BRepAlgoAPI_Common mk(target, tool);
            if (!mk.IsDone() || mk.HasErrors()) {
                if (err) *err = QStringLiteral("交集運算失敗");
                return TopoDS_Shape();
            }
            result = mk.Shape();
            break;
        }
        default:
            return tool;
        }
    } catch (const Standard_Failure& ex) {
        if (err) *err = QStringLiteral("布林運算發生例外：%1")
                            .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown");
        return TopoDS_Shape();
    }

    if (result.IsNull()) {
        if (err) *err = QStringLiteral("布林運算沒有產生結果");
        return TopoDS_Shape();
    }

    // 合併共平面／共曲面的面，讓結果乾淨（失敗就維持原結果）
    try {
        ShapeUpgrade_UnifySameDomain unify(result, Standard_True, Standard_True, Standard_True);
        unify.Build();
        const TopoDS_Shape unified = unify.Shape();
        if (!unified.IsNull() && containsSolid(unified)) result = unified;
    } catch (const Standard_Failure&) {
        // 忽略：保留未合併的結果
    }
    return result;
}

TopoDS_Shape fuseAll(const QList<TopoDS_Shape>& shapes, QString* err)
{
    if (shapes.isEmpty()) {
        if (err) *err = QStringLiteral("沒有任何形狀可以聯集");
        return TopoDS_Shape();
    }
    if (shapes.size() == 1) return shapes.first();

    try {
        TopTools_ListOfShape args, tools;
        args.Append(shapes.first());
        for (int i = 1; i < shapes.size(); ++i) tools.Append(shapes.at(i));

        BRepAlgoAPI_Fuse mk;
        mk.SetArguments(args);
        mk.SetTools(tools);
        mk.Build();
        if (!mk.IsDone() || mk.HasErrors()) {
            if (err) *err = QStringLiteral("多個形狀聯集失敗");
            return TopoDS_Shape();
        }
        return mk.Shape();
    } catch (const Standard_Failure& ex) {
        if (err) *err = QStringLiteral("聯集發生例外：%1")
                            .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown");
        return TopoDS_Shape();
    }
}

TopoDS_Shape applyOpMulti(const TopoDS_Shape& target, const QList<TopoDS_Shape>& tools,
                          SolidOp op, QString* err)
{
    if (tools.isEmpty()) return target;
    if (tools.size() == 1) return applyOp(target, tools.first(), op, err);
    if (op == SolidOp::NewBody) return fuseAll(tools, err);

    if (target.IsNull()) {
        if (err) *err = QStringLiteral("目標實體沒有有效的形狀");
        return TopoDS_Shape();
    }
    if (op == SolidOp::Common) {
        const TopoDS_Shape u = fuseAll(tools, err);
        if (u.IsNull()) return TopoDS_Shape();
        return applyOp(target, u, op, err);
    }

    TopoDS_Shape result;
    try {
        TopTools_ListOfShape args, tl;
        args.Append(target);
        for (const TopoDS_Shape& t : tools) tl.Append(t);

        if (op == SolidOp::Fuse) {
            BRepAlgoAPI_Fuse mk;
            mk.SetArguments(args);
            mk.SetTools(tl);
            mk.Build();
            if (!mk.IsDone() || mk.HasErrors()) {
                if (err) *err = QStringLiteral("聯集運算失敗");
                return TopoDS_Shape();
            }
            result = mk.Shape();
        } else {
            BRepAlgoAPI_Cut mk;
            mk.SetArguments(args);
            mk.SetTools(tl);
            mk.Build();
            if (!mk.IsDone() || mk.HasErrors()) {
                if (err) *err = QStringLiteral("挖除運算失敗（挖除的形狀可能沒有與目標實體相交）");
                return TopoDS_Shape();
            }
            result = mk.Shape();
        }
    } catch (const Standard_Failure& ex) {
        if (err) *err = QStringLiteral("布林運算發生例外：%1")
                            .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown");
        return TopoDS_Shape();
    }
    if (result.IsNull()) {
        if (err) *err = QStringLiteral("布林運算沒有產生結果");
        return TopoDS_Shape();
    }
    try {
        ShapeUpgrade_UnifySameDomain unify(result, Standard_True, Standard_True, Standard_True);
        unify.Build();
        const TopoDS_Shape unified = unify.Shape();
        if (!unified.IsNull() && containsSolid(unified)) result = unified;
    } catch (const Standard_Failure&) {
    }
    return result;
}

TopoDS_Shape taperedPrism(const TopoDS_Face& face, const gp_Vec& dir,
                          double angleDeg, QString* err)
{
    if (face.IsNull()) {
        if (err) *err = QStringLiteral("沒有有效的輪廓");
        return TopoDS_Shape();
    }
    try {
        int wireCount = 0;
        for (TopExp_Explorer ex(face, TopAbs_WIRE); ex.More(); ex.Next()) ++wireCount;
        if (wireCount != 1) {
            if (err) *err = QStringLiteral("拔模擠出目前只支援沒有內孔的輪廓");
            return TopoDS_Shape();
        }

        const double len = dir.Magnitude();
        if (len < 1.0e-9) {
            if (err) *err = QStringLiteral("擠出長度太小");
            return TopoDS_Shape();
        }
        if (std::abs(angleDeg) >= 89.0) {
            if (err) *err = QStringLiteral("拔模角度必須介於 -89° ~ 89°");
            return TopoDS_Shape();
        }

        const double inset = len * std::tan(angleDeg * M_PI / 180.0);   // 頂面相對底面向內縮的距離

        const TopoDS_Wire baseWire = BRepTools::OuterWire(face);
        if (baseWire.IsNull()) {
            if (err) *err = QStringLiteral("輪廓沒有外環");
            return TopoDS_Shape();
        }

        // 以面為基準等距：正值 = 向外（遠離材料），所以向內縮用 -inset
        BRepOffsetAPI_MakeOffset offset(face, GeomAbs_Intersection);
        offset.Perform(-inset);
        if (!offset.IsDone() || offset.Shape().IsNull()) {
            if (err) *err = QStringLiteral("拔模角度太大：輪廓向內收縮後消失了");
            return TopoDS_Shape();
        }

        const TopoDS_Shape offShape = offset.Shape();
        TopoDS_Wire topWire;
        if (offShape.ShapeType() == TopAbs_WIRE) {
            topWire = TopoDS::Wire(offShape);
        } else {
            int found = 0;
            for (TopExp_Explorer ex(offShape, TopAbs_WIRE); ex.More(); ex.Next()) {
                if (found++ == 0) topWire = TopoDS::Wire(ex.Current());
            }
            if (found > 1) {
                if (err) *err = QStringLiteral("拔模後輪廓分裂成多個區域，無法建立");
                return TopoDS_Shape();
            }
        }
        if (topWire.IsNull()) {
            if (err) *err = QStringLiteral("拔模角度太大：輪廓向內收縮後消失了");
            return TopoDS_Shape();
        }

        // 平移到頂面位置
        gp_Trsf tr;
        tr.SetTranslation(dir);
        BRepBuilderAPI_Transform mover(topWire, tr, Standard_True);
        if (!mover.IsDone()) {
            if (err) *err = QStringLiteral("移動頂面輪廓失敗");
            return TopoDS_Shape();
        }
        topWire = TopoDS::Wire(mover.Shape());

        // 兩個輪廓的繞向要一致，否則放樣會扭轉
        auto windingNormal = [](const TopoDS_Wire& w, gp_Dir& n) -> bool {
            BRepBuilderAPI_MakeFace mk(w, Standard_True);
            if (!mk.IsDone()) return false;
            BRepAdaptor_Surface s(mk.Face());
            if (s.GetType() != GeomAbs_Plane) return false;
            n = s.Plane().Axis().Direction();
            if (mk.Face().Orientation() == TopAbs_REVERSED) n.Reverse();
            return true;
        };
        gp_Dir n0, n1;
        if (windingNormal(baseWire, n0) && windingNormal(topWire, n1) && n0.Dot(n1) < 0.0)
            topWire.Reverse();

        BRepOffsetAPI_ThruSections gen(Standard_True, Standard_True);   // 實體、直紋
        gen.CheckCompatibility(Standard_False);
        gen.AddWire(baseWire);
        gen.AddWire(topWire);
        gen.Build();
        if (!gen.IsDone()) {
            if (err) *err = QStringLiteral("建立拔模擠出失敗");
            return TopoDS_Shape();
        }
        return gen.Shape();

    } catch (const Standard_Failure& ex) {
        if (err) *err = QStringLiteral("拔模擠出失敗：%1")
                            .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown");
        return TopoDS_Shape();
    }
}

// ═════════════════════════════════════════════════════════════════════════
//  簽章
// ═════════════════════════════════════════════════════════════════════════

FaceSignature makeFaceSignature(const TopoDS_Face& face)
{
    FaceSignature sig;
    if (face.IsNull()) return sig;

    GProp_GProps gp;
    BRepGProp::SurfaceProperties(face, gp);
    sig.center = gp.CentreOfMass();
    sig.area   = std::abs(gp.Mass());

    BRepAdaptor_Surface surf(face);
    if (surf.GetType() == GeomAbs_Plane) {
        gp_Dir n = surf.Plane().Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED) n.Reverse();
        sig.normal = n;
        sig.planar = true;
    }
    return sig;
}

TopoDS_Face matchFace(const TopoDS_Shape& shape, const FaceSignature& sig)
{
    if (shape.IsNull()) return TopoDS_Face();

    TopTools_IndexedMapOfShape faceMap;
    TopExp::MapShapes(shape, TopAbs_FACE, faceMap);

    const double scale   = std::max(std::sqrt(std::max(sig.area, 0.0)), 1.0);
    const double maxDist = scale * 3.0;

    TopoDS_Face best;
    double bestScore = std::numeric_limits<double>::max();

    for (int i = 1; i <= faceMap.Extent(); ++i) {
        const TopoDS_Face f = TopoDS::Face(faceMap(i));
        const FaceSignature c = makeFaceSignature(f);

        const double dist = sig.center.Distance(c.center);
        if (dist > maxDist) continue;

        double score = dist;
        if (sig.planar) {
            if (!c.planar) continue;
            const double dot = sig.normal.Dot(c.normal);
            if (dot < 0.5) continue;                      // 法向差太多 = 不是同一個面
            score += (1.0 - dot) * scale;
        }
        const double areaDiff = std::abs(c.area - sig.area) / std::max(sig.area, 1.0);
        score += areaDiff * scale * 0.1;

        if (score < bestScore) { bestScore = score; best = f; }
    }
    return best;
}

TopoDS_Edge matchEdge(const TopoDS_Shape& shape, const EdgeSignature& sig)
{
    if (shape.IsNull()) return TopoDS_Edge();

    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);

    const double maxDist = std::max(sig.length, 1.0) * 5.0;

    TopoDS_Edge best;
    double bestScore = std::numeric_limits<double>::max();

    for (int i = 1; i <= edgeMap.Extent(); ++i) {
        const TopoDS_Edge e = TopoDS::Edge(edgeMap(i));
        const EdgeSignature c = ChamferSolid::makeSignature(e);

        const double dist = sig.midPoint.Distance(c.midPoint);
        if (dist > maxDist) continue;

        const double score = dist + std::abs(c.length - sig.length) * 0.1;
        if (score < bestScore) { bestScore = score; best = e; }
    }
    return best;
}

QJsonObject toJson(const FaceSignature& sig)
{
    QJsonObject o;
    o["cx"] = sig.center.X();
    o["cy"] = sig.center.Y();
    o["cz"] = sig.center.Z();
    o["area"] = sig.area;
    o["nx"] = sig.normal.X();
    o["ny"] = sig.normal.Y();
    o["nz"] = sig.normal.Z();
    o["planar"] = sig.planar;
    return o;
}

FaceSignature faceSignatureFromJson(const QJsonObject& o)
{
    FaceSignature sig;
    sig.center = gp_Pnt(o["cx"].toDouble(), o["cy"].toDouble(), o["cz"].toDouble());
    sig.area   = o["area"].toDouble();
    sig.planar = o["planar"].toBool(false);

    gp_Vec nv(o["nx"].toDouble(), o["ny"].toDouble(), o["nz"].toDouble(1.0));
    if (nv.Magnitude() > 1e-12) sig.normal = gp_Dir(nv);
    return sig;
}

QJsonObject toJson(const EdgeSignature& sig)
{
    QJsonObject o;
    o["mx"] = sig.midPoint.X();
    o["my"] = sig.midPoint.Y();
    o["mz"] = sig.midPoint.Z();
    o["length"] = sig.length;
    o["loopIndex"] = sig.loopIndex;
    o["cornerIndex"] = sig.cornerIndex;
    return o;
}

EdgeSignature edgeSignatureFromJson(const QJsonObject& o)
{
    EdgeSignature sig;
    sig.midPoint    = gp_Pnt(o["mx"].toDouble(), o["my"].toDouble(), o["mz"].toDouble());
    sig.length      = o["length"].toDouble();
    sig.loopIndex   = o["loopIndex"].toInt(-1);
    sig.cornerIndex = o["cornerIndex"].toInt(-1);
    return sig;
}

} // namespace solidops
} // namespace cad
} // namespace aicad
