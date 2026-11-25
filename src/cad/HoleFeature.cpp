/**
 * @file HoleFeature.cpp
 */
#include "HoleFeature.h"
#include "Document.h"
#include "Plane.h"

#include <QDebug>
#include <QJsonObject>
#include <QVector3D>

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_State.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace aicad {
namespace cad {

namespace {

/// 點是否在目標實體內部（目標可能是 compound，逐一檢查其中的 solid）
bool pointInside(const TopoDS_Shape& shape, const gp_Pnt& p)
{
    for (TopExp_Explorer ex(shape, TopAbs_SOLID); ex.More(); ex.Next()) {
        BRepClass3d_SolidClassifier cls(ex.Current(), p, 1e-7);
        if (cls.State() == TopAbs_IN) return true;
    }
    return false;
}

} // namespace

HoleFeature::HoleFeature(Document* parent)
    : ShapingFeature(parent)
{
    setName("Hole");
}

void HoleFeature::setSketch(Sketch* sketch)
{
    if (m_sketch.data() == sketch) return;

    if (m_sketch) disconnect(m_sketch.data(), nullptr, this, nullptr);
    m_sketch = sketch;

    if (m_sketch) {
        connect(m_sketch.data(), &Sketch::shapeChanged,    this, &HoleFeature::rebuildRequested);
        connect(m_sketch.data(), &Sketch::geometryChanged, this, &HoleFeature::rebuildRequested);
    }
}

bool HoleFeature::validate(const Params& p, QString* err)
{
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };

    if (p.diameter <= 1e-6) return fail(QStringLiteral("孔徑必須大於 0"));
    if (!p.throughAll && p.depth <= 1e-6) return fail(QStringLiteral("孔深必須大於 0"));

    if (p.type == HoleType::Counterbore) {
        if (p.cboreDiameter <= p.diameter + 1e-6)
            return fail(QStringLiteral("沉孔直徑必須大於孔徑"));
        if (p.cboreDepth <= 1e-6) return fail(QStringLiteral("沉孔深度必須大於 0"));
        if (!p.throughAll && p.depth <= p.cboreDepth + 1e-6)
            return fail(QStringLiteral("孔深必須大於沉孔深度"));
    } else if (p.type == HoleType::Countersink) {
        if (p.csinkDiameter <= p.diameter + 1e-6)
            return fail(QStringLiteral("錐孔表面直徑必須大於孔徑"));
        if (p.csinkAngle <= 1.0 || p.csinkAngle >= 179.0)
            return fail(QStringLiteral("錐角必須介於 1° ~ 179°"));
    }
    return true;
}

bool HoleFeature::rebuild()
{
    if (operation() != SolidOp::Cut || !target()) {
        setError(QStringLiteral("孔需要一個目標實體（運算方式：挖除）"));
        return false;
    }
    if (!m_sketch) { setError(QStringLiteral("尚未指定孔位草圖")); return false; }

    QString err;
    if (!validate(m_p, &err)) { setError(err); return false; }

    const TopoDS_Shape targetShape = target()->shape();
    if (targetShape.IsNull()) {
        setError(QStringLiteral("目標實體「%1」目前沒有有效的形狀").arg(target()->name()));
        return false;
    }

    // ── 孔位（世界座標）──
    QList<gp_Pnt> centers;
    auto addCenter = [&](const QVector2D& c) {
        const QVector3D w = m_sketch->planeToWorld(c);
        const gp_Pnt p(w.x(), w.y(), w.z());
        for (const gp_Pnt& q : centers)
            if (q.Distance(p) < 1e-6) return;              // 重複的位置只算一次
        centers.append(p);
    };
    for (SketchGeometry* g : m_sketch->geometries()) {
        if (!g) continue;
        if (g->type == SketchGeometryType::Point) {
            auto* pt = static_cast<SketchPoint*>(g);
            // 圓心與端點是繪圖時自動產生的點，不當孔位（圓的圓心下面另外處理）
            if (pt->origin == SketchPoint::Origin::Explicit) addCenter(pt->pos);
        } else if (g->type == SketchGeometryType::Circle) {
            addCenter(static_cast<SketchCircle*>(g)->center);
        }
    }
    if (centers.isEmpty()) {
        setError(QStringLiteral("孔位草圖裡沒有「點」或「圓」——請在草圖放置點（或畫圓，取圓心）作為孔位"));
        return false;
    }

    // ── 孔的方向 ──
    if (!m_sketch->plane()) { setError(QStringLiteral("草圖沒有平面")); return false; }
    const QVector3D nv = m_sketch->plane()->normal();
    gp_Vec normal(nv.x(), nv.y(), nv.z());
    if (normal.Magnitude() < 1e-9) { setError(QStringLiteral("草圖平面法向無效")); return false; }
    normal.Normalize();

    Bnd_Box box;
    BRepBndLib::Add(targetShape, box);
    if (box.IsVoid()) { setError(QStringLiteral("目標實體沒有有效的邊界")); return false; }
    const double diag = std::sqrt(box.SquareExtent());
    const double probe = std::max(1e-3, diag * 1e-3);

    // 自動朝向實體內部：往「負法向」與「正法向」各探一小段，哪邊落在實體內就往哪邊鑽
    gp_Vec dirVec = normal.Reversed();
    {
        bool decided = false;
        for (const gp_Pnt& c : centers) {
            const bool inNeg = pointInside(targetShape, c.Translated(normal.Reversed() * probe));
            const bool inPos = pointInside(targetShape, c.Translated(normal * probe));
            if (inNeg != inPos) { dirVec = inNeg ? normal.Reversed() : normal; decided = true; break; }
        }
        Q_UNUSED(decided);
    }
    if (m_p.flip) dirVec.Reverse();
    const gp_Dir dir(dirVec);

    // ── 每個孔的工具形狀 ──
    const double eps = std::max(0.05, diag * 1e-4);          // 從表面外側一點點開始，避免共面
    const double r   = m_p.diameter * 0.5;
    const double totalDepth = m_p.throughAll ? diag * 1.5 : m_p.depth;

    QList<TopoDS_Shape> tools;
    try {
        for (const gp_Pnt& c : centers) {
            const gp_Pnt start = c.Translated(gp_Vec(dir) * (-eps));
            const gp_Ax2 ax(start, dir);

            TopoDS_Shape one;
            switch (m_p.type) {
            case HoleType::Simple: {
                one = BRepPrimAPI_MakeCylinder(ax, r, totalDepth + eps).Shape();
                break;
            }
            case HoleType::Counterbore: {
                TopoDS_Shape cb = BRepPrimAPI_MakeCylinder(ax, m_p.cboreDiameter * 0.5,
                                                           m_p.cboreDepth + eps).Shape();
                TopoDS_Shape sh = BRepPrimAPI_MakeCylinder(ax, r, totalDepth + eps).Shape();
                BRepAlgoAPI_Fuse fz(cb, sh);
                if (!fz.IsDone() || fz.HasErrors()) { setError(QStringLiteral("沉頭孔工具建立失敗")); return false; }
                one = fz.Shape();
                break;
            }
            case HoleType::Countersink: {
                const double half = m_p.csinkAngle * 0.5 * M_PI / 180.0;
                const double tanH = std::tan(half);
                const double R = m_p.csinkDiameter * 0.5;
                const double coneH = (R - r) / tanH;                      // 表面到孔徑處的深度
                // 錐體往外延伸 eps：起點半徑 = R + eps*tan(half)
                TopoDS_Shape cone = BRepPrimAPI_MakeCone(ax, R + eps * tanH, r, coneH + eps).Shape();
                TopoDS_Shape sh = BRepPrimAPI_MakeCylinder(ax, r, totalDepth + eps).Shape();
                BRepAlgoAPI_Fuse fz(cone, sh);
                if (!fz.IsDone() || fz.HasErrors()) { setError(QStringLiteral("錐孔工具建立失敗")); return false; }
                one = fz.Shape();
                break;
            }
            }
            if (one.IsNull()) { setError(QStringLiteral("孔的工具形狀建立失敗")); return false; }
            tools.append(one);
        }

        TopoDS_Shape tool = solidops::fuseAll(tools, &err);
        if (tool.IsNull()) { setError(err.isEmpty() ? QStringLiteral("孔工具聯集失敗") : err); return false; }

        TopoDS_Shape result = combineWithTarget(tool);
        if (result.IsNull()) return false;

        result = solidops::finalizeSolid(result, &err);
        if (result.IsNull()) {
            setError(err.isEmpty() ? QStringLiteral("運算結果沒有任何實體") : err);
            return false;
        }
        setShape(result);
        return true;

    } catch (const Standard_Failure& ex) {
        setError(QStringLiteral("孔精靈發生 OCC 例外：%1")
                     .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown"));
        return false;
    }
}

QSet<QString> HoleFeature::featureDependencies() const
{
    QSet<QString> deps = targetDependencies();
    if (m_sketch) deps.insert(m_sketch->id());
    return deps;
}

void HoleFeature::resolveReferences(Document* doc)
{
    if (!doc) return;
    resolveTargetReference(doc);

    if (!m_pendingSketchId.isEmpty()) {
        if (auto* sk = qobject_cast<Sketch*>(doc->findFeature(m_pendingSketchId))) {
            setSketch(sk);
            if (!sk->parent()) sk->setFeatureParent(this);
            m_pendingSketchId.clear();
        } else {
            qWarning() << "[HoleFeature]" << name() << "cannot resolve sketchId:" << m_pendingSketchId;
        }
    }
    doc->refreshFeatureDependencies(this);
}

QJsonObject HoleFeature::toJson() const
{
    QJsonObject obj = Feature::toJson();
    obj["sketchId"]      = m_sketch ? m_sketch->id() : m_pendingSketchId;
    obj["holeType"]      = static_cast<int>(m_p.type);
    obj["diameter"]      = m_p.diameter;
    obj["throughAll"]    = m_p.throughAll;
    obj["depth"]         = m_p.depth;
    obj["cboreDiameter"] = m_p.cboreDiameter;
    obj["cboreDepth"]    = m_p.cboreDepth;
    obj["csinkDiameter"] = m_p.csinkDiameter;
    obj["csinkAngle"]    = m_p.csinkAngle;
    obj["flip"]          = m_p.flip;
    obj["spec"]          = m_p.spec;
    writeOpJson(obj);
    return obj;
}

bool HoleFeature::fromJson(const QJsonObject& json)
{
    if (!Feature::fromJson(json)) return false;

    m_pendingSketchId = json["sketchId"].toString();
    Params p;
    const int t = json["holeType"].toInt(0);
    p.type          = t == 1 ? HoleType::Counterbore : (t == 2 ? HoleType::Countersink : HoleType::Simple);
    p.diameter      = json["diameter"].toDouble(p.diameter);
    p.throughAll    = json["throughAll"].toBool(true);
    p.depth         = json["depth"].toDouble(p.depth);
    p.cboreDiameter = json["cboreDiameter"].toDouble(p.cboreDiameter);
    p.cboreDepth    = json["cboreDepth"].toDouble(p.cboreDepth);
    p.csinkDiameter = json["csinkDiameter"].toDouble(p.csinkDiameter);
    p.csinkAngle    = json["csinkAngle"].toDouble(p.csinkAngle);
    p.flip          = json["flip"].toBool(false);
    p.spec          = json["spec"].toString();
    m_p = p;
    readOpJson(json);
    return true;
}

} // namespace cad
} // namespace aicad
