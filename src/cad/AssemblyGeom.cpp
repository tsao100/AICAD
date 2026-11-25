/**
 * @file AssemblyGeom.cpp
 */
#include "AssemblyGeom.h"

#include <QByteArray>
#include <QJsonArray>
#include <QObject>
#include <cmath>
#include <sstream>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax1.hxx>
#include <gp_Circ.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>

namespace aicad {
namespace cad {

using asmsolve::RefKind;

QJsonObject MateRefData::toJson() const
{
    QJsonObject o;
    o["owner"]  = ownerId;
    o["kind"]   = static_cast<int>(kind);
    o["source"] = static_cast<int>(source);
    o["index"]  = topoIndex;
    o["datum"]  = datum;
    o["label"]  = label;
    o["p"] = QJsonArray{ p[0], p[1], p[2] };
    o["d"] = QJsonArray{ d[0], d[1], d[2] };
    return o;
}

MateRefData MateRefData::fromJson(const QJsonObject& o)
{
    MateRefData r;
    r.ownerId   = o["owner"].toString();
    r.kind      = static_cast<RefKind>(o["kind"].toInt());
    r.source    = static_cast<RefSource>(o["source"].toInt());
    r.topoIndex = o["index"].toInt(-1);
    r.datum     = o["datum"].toString();
    r.label     = o["label"].toString();
    const QJsonArray pa = o["p"].toArray(), da = o["d"].toArray();
    for (int i = 0; i < 3; ++i) {
        r.p[i] = pa.size() > i ? pa[i].toDouble() : 0.0;
        r.d[i] = da.size() > i ? da[i].toDouble() : (i == 2 ? 1.0 : 0.0);
    }
    return r;
}

namespace assemblygeom {

namespace {

void setP(MateRefData& r, double x, double y, double z) { r.p[0] = x; r.p[1] = y; r.p[2] = z; }
void setD(MateRefData& r, double x, double y, double z)
{
    const double n = std::sqrt(x * x + y * y + z * z);
    const double k = n > 1e-300 ? 1.0 / n : 1.0;
    r.d[0] = x * k; r.d[1] = y * k; r.d[2] = z * k;
    if (n <= 1e-300) { r.d[0] = 0; r.d[1] = 0; r.d[2] = 1; }
}

TopAbs_ShapeEnum typeOf(RefSource s)
{
    return s == RefSource::Face ? TopAbs_FACE : s == RefSource::Edge ? TopAbs_EDGE : TopAbs_VERTEX;
}

} // namespace

QString datumLabel(const QString& datum)
{
    if (datum == QLatin1String("origin")) return QObject::tr("原點");
    if (datum == QLatin1String("x"))  return QObject::tr("X 軸");
    if (datum == QLatin1String("y"))  return QObject::tr("Y 軸");
    if (datum == QLatin1String("z"))  return QObject::tr("Z 軸");
    if (datum == QLatin1String("xy")) return QObject::tr("XY 平面（法向 +Z）");
    if (datum == QLatin1String("yz")) return QObject::tr("YZ 平面（法向 +X）");
    if (datum == QLatin1String("xz")) return QObject::tr("XZ 平面（法向 -Y）");
    return datum;
}

MateRefData datumRef(const QString& datum, const QString& ownerId)
{
    MateRefData r;
    r.ownerId = ownerId;
    r.source = RefSource::Datum;
    r.datum = datum;
    r.label = datumLabel(datum);
    setP(r, 0, 0, 0);
    if (datum == QLatin1String("origin")) { r.kind = RefKind::Point; setD(r, 0, 0, 1); }
    else if (datum == QLatin1String("x"))  { r.kind = RefKind::Axis;  setD(r, 1, 0, 0); }
    else if (datum == QLatin1String("y"))  { r.kind = RefKind::Axis;  setD(r, 0, 1, 0); }
    else if (datum == QLatin1String("z"))  { r.kind = RefKind::Axis;  setD(r, 0, 0, 1); }
    else if (datum == QLatin1String("xy")) { r.kind = RefKind::Plane; setD(r, 0, 0, 1); }
    else if (datum == QLatin1String("yz")) { r.kind = RefKind::Plane; setD(r, 1, 0, 0); }
    else if (datum == QLatin1String("xz")) { r.kind = RefKind::Plane; setD(r, 0, -1, 0); }
    return r;
}

QVector<MateRefData> allDatumRefs(const QString& ownerId)
{
    QVector<MateRefData> v;
    for (const char* n : { "origin", "x", "y", "z", "xy", "yz", "xz" })
        v.append(datumRef(QLatin1String(n), ownerId));
    return v;
}

bool fromSubShape(const TopoDS_Shape& sub, MateRefData& out, QString* error)
{
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    if (sub.IsNull()) return fail(QObject::tr("沒有選到幾何"));
    try {
        switch (sub.ShapeType()) {
        case TopAbs_FACE: {
            const TopoDS_Face face = TopoDS::Face(sub);
            BRepAdaptor_Surface s(face);                // 含面的 Location → 座標即為目前所在座標
            out.source = RefSource::Face;
            switch (s.GetType()) {
            case GeomAbs_Plane: {
                const gp_Pln pl = s.Plane();
                gp_Dir n = pl.Axis().Direction();
                if (face.Orientation() == TopAbs_REVERSED) n.Reverse();      // 外向法向量
                const gp_Pnt o = pl.Location();
                out.kind = RefKind::Plane; setP(out, o.X(), o.Y(), o.Z()); setD(out, n.X(), n.Y(), n.Z());
                out.label = QObject::tr("平面");
                return true;
            }
            case GeomAbs_Cylinder: {
                const gp_Cylinder c = s.Cylinder();
                const gp_Ax1 ax = c.Axis();
                out.kind = RefKind::Axis; setP(out, ax.Location().X(), ax.Location().Y(), ax.Location().Z());
                setD(out, ax.Direction().X(), ax.Direction().Y(), ax.Direction().Z());
                out.label = QObject::tr("圓柱軸（R=%1）").arg(c.Radius(), 0, 'g', 6);
                return true;
            }
            case GeomAbs_Cone: {
                const gp_Ax1 ax = s.Cone().Axis();
                out.kind = RefKind::Axis; setP(out, ax.Location().X(), ax.Location().Y(), ax.Location().Z());
                setD(out, ax.Direction().X(), ax.Direction().Y(), ax.Direction().Z());
                out.label = QObject::tr("圓錐軸");
                return true;
            }
            case GeomAbs_Torus: {
                const gp_Ax1 ax = s.Torus().Axis();
                out.kind = RefKind::Axis; setP(out, ax.Location().X(), ax.Location().Y(), ax.Location().Z());
                setD(out, ax.Direction().X(), ax.Direction().Y(), ax.Direction().Z());
                out.label = QObject::tr("環面軸");
                return true;
            }
            case GeomAbs_Sphere: {
                const gp_Pnt c = s.Sphere().Location();
                out.kind = RefKind::Point; setP(out, c.X(), c.Y(), c.Z()); setD(out, 0, 0, 1);
                out.label = QObject::tr("球心");
                return true;
            }
            default:
                return fail(QObject::tr("不支援此曲面類型（支援：平面、圓柱、圓錐、環面、球）"));
            }
        }
        case TopAbs_EDGE: {
            const TopoDS_Edge e = TopoDS::Edge(sub);
            BRepAdaptor_Curve c(e);
            out.source = RefSource::Edge;
            if (c.GetType() == GeomAbs_Line) {
                const gp_Lin l = c.Line();
                const gp_Pnt o = c.Value(c.FirstParameter());
                out.kind = RefKind::Axis; setP(out, o.X(), o.Y(), o.Z());
                setD(out, l.Direction().X(), l.Direction().Y(), l.Direction().Z());
                out.label = QObject::tr("直線邊");
                return true;
            }
            if (c.GetType() == GeomAbs_Circle) {
                const gp_Circ ci = c.Circle();
                const gp_Ax1 ax = ci.Axis();
                out.kind = RefKind::Axis; setP(out, ci.Location().X(), ci.Location().Y(), ci.Location().Z());
                setD(out, ax.Direction().X(), ax.Direction().Y(), ax.Direction().Z());
                out.label = QObject::tr("圓邊軸（R=%1）").arg(ci.Radius(), 0, 'g', 6);
                return true;
            }
            return fail(QObject::tr("不支援此曲線類型（支援：直線、圓／圓弧）"));
        }
        case TopAbs_VERTEX: {
            const gp_Pnt o = BRep_Tool::Pnt(TopoDS::Vertex(sub));
            out.source = RefSource::Vertex;
            out.kind = RefKind::Point; setP(out, o.X(), o.Y(), o.Z()); setD(out, 0, 0, 1);
            out.label = QObject::tr("頂點");
            return true;
        }
        default:
            return fail(QObject::tr("請選取面、邊或頂點"));
        }
    } catch (const Standard_Failure& ex) {
        return fail(QObject::tr("取得幾何失敗：%1").arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown"));
    }
}

int indexOf(const TopoDS_Shape& shape, const TopoDS_Shape& sub)
{
    if (shape.IsNull() || sub.IsNull()) return -1;
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, sub.ShapeType(), map);
    const int i = map.FindIndex(sub);
    return i > 0 ? i : -1;
}

QVector<MateRefData> enumerate(const TopoDS_Shape& shape, int maxPerKind)
{
    QVector<MateRefData> out;
    if (shape.IsNull()) return out;
    for (RefSource src : { RefSource::Face, RefSource::Edge, RefSource::Vertex }) {
        TopTools_IndexedMapOfShape map;
        TopExp::MapShapes(shape, typeOf(src), map);
        int added = 0;
        for (int i = 1; i <= map.Extent() && added < maxPerKind; ++i) {
            MateRefData r;
            if (!fromSubShape(map(i), r, nullptr)) continue;
            r.topoIndex = i;
            const QString what = src == RefSource::Face ? QObject::tr("面") : src == RefSource::Edge ? QObject::tr("邊") : QObject::tr("頂點");
            r.label = QStringLiteral("%1 #%2 · %3").arg(what).arg(i).arg(r.label);
            if (r.kind == RefKind::Plane || r.kind == RefKind::Axis)
                r.label += QStringLiteral(" @(%1, %2, %3)").arg(r.p[0], 0, 'g', 5).arg(r.p[1], 0, 'g', 5).arg(r.p[2], 0, 'g', 5);
            else
                r.label += QStringLiteral(" (%1, %2, %3)").arg(r.p[0], 0, 'g', 5).arg(r.p[1], 0, 'g', 5).arg(r.p[2], 0, 'g', 5);
            out.append(r);
            ++added;
        }
    }
    return out;
}

bool refresh(const TopoDS_Shape& shape, MateRefData& ref)
{
    if (ref.source == RefSource::Datum || ref.topoIndex < 1 || shape.IsNull()) return false;
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, typeOf(ref.source), map);
    if (ref.topoIndex > map.Extent()) return false;
    MateRefData fresh = ref;
    if (!fromSubShape(map(ref.topoIndex), fresh, nullptr)) return false;
    if (fresh.kind != ref.kind) return false;                       // 種類改變 → 視為對不上，保留原幾何
    for (int i = 0; i < 3; ++i) { ref.p[i] = fresh.p[i]; ref.d[i] = fresh.d[i]; }
    return true;
}

bool boundsOf(const TopoDS_Shape& shape, double center[3], double* diagonal)
{
    if (shape.IsNull()) return false;
    Bnd_Box b;
    try { BRepBndLib::Add(shape, b); } catch (const Standard_Failure&) { return false; }
    if (b.IsVoid()) return false;
    double x0, y0, z0, x1, y1, z1;
    b.Get(x0, y0, z0, x1, y1, z1);
    center[0] = 0.5 * (x0 + x1); center[1] = 0.5 * (y0 + y1); center[2] = 0.5 * (z0 + z1);
    if (diagonal) *diagonal = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
    return true;
}

QString shapeToBase64(const TopoDS_Shape& s)
{
    if (s.IsNull()) return QString();
    std::ostringstream os;
    BRepTools::Write(s, os);
    const std::string str = os.str();
    return QString::fromLatin1(QByteArray(str.data(), int(str.size())).toBase64());
}

TopoDS_Shape shapeFromBase64(const QString& b64)
{
    TopoDS_Shape s;
    if (b64.isEmpty()) return s;
    const QByteArray raw = QByteArray::fromBase64(b64.toLatin1());
    std::istringstream is(std::string(raw.constData(), size_t(raw.size())));
    BRep_Builder bb;
    BRepTools::Read(s, is, bb);
    return s;
}

} // namespace assemblygeom
} // namespace cad
} // namespace aicad
