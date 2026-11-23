/**
 * @file TrackSlabPlan.cpp
 */
#include "TrackSlabPlan.h"
#include "ProfileLoftSolid.h"
#include "AlignedProfileArray.h"
#include "SketchInstance.h"
#include "Sketch.h"
#include "Document.h"
#include "sketch/SketchAnnotation.h"
#include "../core/ParameterStore.h"
#include "../core/geometry/ProjectOrigin.h"

#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QHash>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>
#include <QVector>
#include <QLineF>
#include <algorithm>
#include <memory>
#include <cmath>

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GeomAbs_CurveType.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

namespace aicad {
namespace cad {

TrackSlabPlan::TrackSlabPlan(Document* parent)
    : Feature(parent)
{
    setName("SlabPlan");
}

// ─────────────────────────────────────────────────────────────────────────────
// 來源 Loft（文件中所有 Loft）
// ─────────────────────────────────────────────────────────────────────────────
QList<ProfileLoftSolid*> TrackSlabPlan::allLofts() const
{
    QList<ProfileLoftSolid*> out;
    if (const Document* doc = document())
        for (Feature* f : doc->features())
            if (auto* l = qobject_cast<ProfileLoftSolid*>(f))
                if (l->sourceArray()) out.append(l);
    return out;
}

bool TrackSlabPlan::isApplicable(const Document* doc)
{
    if (!doc) return false;
    for (Feature* f : doc->features()) {
        auto* l = qobject_cast<ProfileLoftSolid*>(f);
        if (!l) continue;
        AlignedProfileArray* arr = l->sourceArray();
        Sketch* master = arr ? arr->masterSketch() : nullptr;
        if (master && master->parameterStore() && master->parameterStore()->has(cantParameterName()))
            return true;
    }
    return false;
}

void TrackSlabPlan::requestRebuild()
{
    if (m_rebuildPending) return;
    m_rebuildPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_rebuildPending = false;
        if (Document* doc = document()) doc->rebuildFeature(this);
        else rebuild();
    });
}

void TrackSlabPlan::refreshSources()
{
    for (const QMetaObject::Connection& c : std::as_const(m_conns)) disconnect(c);
    m_conns.clear();

    QStringList ids;
    for (ProfileLoftSolid* l : allLofts()) {
        ids << l->id();
        // Loft 形狀（或其來源陣列）變了 → 重畫
        m_conns << connect(l, &Feature::shapeChanged, this, [this]() { requestRebuild(); });
    }
    ids.sort();
    const QString sig = ids.join(QLatin1Char('|'));
    if (sig != m_loftSig) {
        m_loftSig = sig;
        requestRebuild();
    }
}

bool TrackSlabPlan::exportDxf(const QString& filePath, QString* error) const
{
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };
    if (m_polylines.isEmpty()) return fail(QStringLiteral("道版圖目前沒有圖形可匯出"));

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return fail(QStringLiteral("無法寫入檔案：%1").arg(file.errorString()));

    QTextStream out(&file);
    // 內容全為 ASCII，不需設定編碼（setEncoding/QStringConverter 是 Qt6 專有 API）
    auto g = [&](int code, const QString& v) { out << code << '\n' << v << '\n'; };
    auto gi = [&](int code, int v)           { out << code << '\n' << v << '\n'; };
    auto gd = [&](int code, double v)        { out << code << '\n' << QString::number(v, 'f', 6) << '\n'; };

    // ── HEADER（R12）──
    g(0, "SECTION"); g(2, "HEADER");
    g(9, "$ACADVER"); g(1, "AC1009");
    g(0, "ENDSEC");

    // ── TABLES：LTYPE（CONTINUOUS）、LAYER ──
    g(0, "SECTION"); g(2, "TABLES");
    g(0, "TABLE"); g(2, "LTYPE"); gi(70, 1);
    g(0, "LTYPE"); g(2, "CONTINUOUS"); gi(70, 0); g(3, "Solid line"); gi(72, 65); gi(73, 0); gd(40, 0.0);
    g(0, "ENDTAB");

    struct LayerDef { const char* name; int color; };
    const LayerDef layers[] = { { "SLAB_OUTLINE", 1 }, { "SLAB_OTHER", 7 } };
    g(0, "TABLE"); g(2, "LAYER"); gi(70, 2);
    for (const LayerDef& l : layers) {
        g(0, "LAYER"); g(2, l.name); gi(70, 0); gi(62, l.color); g(6, "CONTINUOUS");
    }
    g(0, "ENDTAB");
    g(0, "ENDSEC");

    // ── ENTITIES：每條折線一個 POLYLINE（TM2 全域 E,N；z = 0）──
    core::geometry::ProjectOrigin::ensureDefault();
    const auto& origin = core::geometry::ProjectOrigin::instance();

    g(0, "SECTION"); g(2, "ENTITIES");
    for (const Polyline& pl : m_polylines) {
        if (pl.pts.size() < 2) continue;
        if (pl.kind == 2) continue;                    // 已併入外圍輪廓的最外側縱向邊：不重複輸出
        const bool outline = (pl.kind == 3);
        const char* layer = outline ? "SLAB_OUTLINE" : "SLAB_OTHER";
        g(0, "POLYLINE"); g(8, layer); gi(66, 1);
        gd(10, 0.0); gd(20, 0.0); gd(30, 0.0);
        gi(70, outline ? 1 : 0);                       // 70 = 1：封閉
        for (const QPointF& p : pl.pts) {
            const QPointF en = origin.toGlobal(p.x(), p.y());
            g(0, "VERTEX"); g(8, layer);
            gd(10, en.x()); gd(20, en.y()); gd(30, 0.0);
        }
        g(0, "SEQEND"); g(8, layer);
    }
    g(0, "ENDSEC");
    g(0, "EOF");

    out.flush();
    if (out.status() != QTextStream::Ok) return fail(QStringLiteral("寫入失敗"));
    return true;
}

QJsonObject TrackSlabPlan::toJson() const
{
    QJsonObject obj = Feature::toJson();
    obj["type"] = typeString();
    return obj;
}

bool TrackSlabPlan::fromJson(const QJsonObject& json)
{
    Feature::fromJson(json);     // 舊版（每個 Loft 一張）的 sourceLoftId 一律忽略
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 幾何
// ─────────────────────────────────────────────────────────────────────────────
namespace {

/// 某測站的軌道面斷線（世界座標）：通過 a、單位方向 dir 的無限長直線
struct CantLine {
    bool   ok = false;
    gp_Pnt a;
    gp_Vec dir;          ///< 單位向量，由斷線起點指向終點（斷面草圖 +X 側）
    gp_Vec up;           ///< 斷面平面內與 dir 垂直、朝上（+Z）的單位向量（軌道面「朝上」的法線）
    gp_Vec profNormal;   ///< 斷面平面法線（frame X × Y）
    double oE = 0, oN = 0;   ///< 測站在線形上的平面位置（E, N）＝放置座標系原點
    double lE = 1, lN = 0;   ///< 橫向（斷面 +X 軸）在水平面上的單位方向（E, N 分量）
};

/// 參數名稱集合：cant 本身 + 所有（遞移）依賴 cant 的參數
QSet<QString> cantDependentNames(const core::ParameterStore* store)
{
    QSet<QString> names;
    const QString root = TrackSlabPlan::cantParameterName();
    names.insert(root);
    if (!store) return names;
    QStringList queue{ root };
    while (!queue.isEmpty()) {
        const QString n = queue.takeFirst();
        for (const QString& dep : store->dependentsOf(n)) {
            if (names.contains(dep)) continue;
            names.insert(dep);
            queue.append(dep);
        }
    }
    return names;
}

bool exprDependsOn(const QString& expr, const QSet<QString>& names)
{
    if (expr.trimmed().isEmpty()) return false;
    static const QRegularExpression re(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*"));
    auto it = re.globalMatch(expr);
    while (it.hasNext())
        if (names.contains(it.next().captured())) return true;
    return false;
}

/// 取某測站的「受 cant 約束的直線」。找不到時 ok=false，並在 err 說明原因。
CantLine findCantLine(SketchInstance* inst, const QSet<QString>& cantNames, QString* err)
{
    CantLine out;
    auto fail = [&](const QString& m) { if (err) *err = m; return out; };

    if (!inst) return fail(QStringLiteral("測站不存在"));
    const SketchInstance::PlacementFrame& f = inst->frame();
    if (!f.valid) return fail(QStringLiteral("測站 %1 沒有放置座標系").arg(inst->name()));

    // 本站「實際套用值」的唯讀檢視草圖：幾何是本站求解後的結果，標註運算式保留
    std::unique_ptr<Sketch> view(inst->createViewSketch());
    if (!view) return fail(QStringLiteral("測站 %1 尚未求解，無法取得斷面幾何").arg(inst->name()));

    QSet<QString> ownGeom;                                  // 本草圖自己的幾何（排除基準軸 datum 等內部物件）
    for (SketchGeometry* g : view->geometriesRef()) if (g) ownGeom.insert(g->uuid);

    // ① 運算式依賴 cant 的標註所參照的點（參照到線則取其兩端點）
    QSet<QString> cantPoints;
    for (const SketchAnnotation& an : view->annotations()) {
        if (!exprDependsOn(an.paramExpr, cantNames)) continue;
        for (const GeomRef& r : an.refs) {
            if (!ownGeom.contains(r.geomUuid)) continue;
            SketchGeometry* g = view->findGeometry(r.geomUuid);
            if (!g) continue;
            if (g->type == SketchGeometryType::Point) {
                cantPoints.insert(g->uuid);
            } else if (g->type == SketchGeometryType::Line) {
                auto* l = static_cast<SketchLine*>(g);
                cantPoints.insert(l->startUuid);
                cantPoints.insert(l->endUuid);
            }
        }
    }
    if (cantPoints.isEmpty())
        return fail(QStringLiteral("斷面草圖中沒有任何尺寸依賴參數「%1」").arg(TrackSlabPlan::cantParameterName()));

    // ② 兩端點都是 cant 點的直線（優先建構線）
    SketchLine* best = nullptr;
    for (SketchGeometry* g : view->geometriesRef()) {
        if (!g || g->type != SketchGeometryType::Line) continue;
        if (g->role == GeomRole::Centerline) continue;          // 組件座標線不是軌道面斷線
        auto* l = static_cast<SketchLine*>(g);
        if (!cantPoints.contains(l->startUuid) || !cantPoints.contains(l->endUuid)) continue;
        if (!best || (l->isConstruction() && !best->isConstruction())) best = l;
    }
    if (!best)
        return fail(QStringLiteral("找不到兩端都受 cant 約束的直線"));

    auto pointPos = [&](const QString& uuid, QVector2D& pos) {
        SketchGeometry* g = view->findGeometry(uuid);
        if (!g || g->type != SketchGeometryType::Point) return false;
        pos = static_cast<SketchPoint*>(g)->pos;
        return true;
    };
    QVector2D p0, p1;
    if (!pointPos(best->startUuid, p0) || !pointPos(best->endUuid, p1))
        return fail(QStringLiteral("軌道面直線的端點座標無法取得"));

    const double sc = inst->scale();
    auto toWorld = [&](const QVector2D& p) {
        const double u = p.x() * sc, v = p.y() * sc;
        return gp_Pnt(f.ox + f.xx * u + f.yx * v,
                      f.oy + f.xy * u + f.yy * v,
                      f.oz + f.xz * u + f.yz * v);
    };
    const gp_Pnt A = toWorld(p0), B = toWorld(p1);
    gp_Vec d(A, B);
    if (d.Magnitude() < 1e-9) return fail(QStringLiteral("軌道面直線長度為 0"));
    d.Normalize();

    gp_Vec xAxis(f.xx, f.xy, f.xz), yAxis(f.yx, f.yy, f.yz);
    gp_Vec n = xAxis.Crossed(yAxis);
    if (n.Magnitude() < 1e-12) return fail(QStringLiteral("放置座標系退化"));
    n.Normalize();

    gp_Vec up = n.Crossed(d);
    if (up.Magnitude() < 1e-12) return fail(QStringLiteral("軌道面直線與斷面平面垂直"));
    up.Normalize();
    if (up.Z() < 0.0) up.Reverse();

    // 平面位置與橫向：斷面 +X 軸的水平分量（垂直於線形切線）
    double lx = f.xx, ly = f.xy;
    const double lm = std::hypot(lx, ly);
    if (lm < 1e-9) return fail(QStringLiteral("斷面 X 軸為鉛直，無法決定橫向"));
    lx /= lm; ly /= lm;

    out.ok = true; out.a = A; out.dir = d; out.up = up; out.profNormal = n;
    out.oE = f.ox; out.oN = f.oy; out.lE = lx; out.lN = ly;
    return out;
}

/// 輪廓中一條邊（依 wire 走訪順序）
struct ProfEdge {
    gp_Pnt  p0, p1;                 ///< 走訪方向的起點／終點
    QVector<gp_Pnt> mid;            ///< 走訪方向上的中間取樣點（直線為空）
    gp_Vec  tangentMid;             ///< 走訪方向的中點切線（單位）
};

constexpr int kArcSamples = 12;

bool collectProfileEdges(const TopoDS_Wire& wire, QVector<ProfEdge>& out)
{
    out.clear();
    for (BRepTools_WireExplorer we(wire); we.More(); we.Next()) {
        const TopoDS_Edge e = we.Current();
        BRepAdaptor_Curve c(e);
        const bool rev = (e.Orientation() == TopAbs_REVERSED);

        ProfEdge pe;
        pe.p0 = BRep_Tool::Pnt(TopExp::FirstVertex(e, Standard_True));
        pe.p1 = BRep_Tool::Pnt(TopExp::LastVertex(e, Standard_True));

        const double f = c.FirstParameter(), l = c.LastParameter();
        auto paramAt = [&](double t01) { return rev ? l - (l - f) * t01 : f + (l - f) * t01; };

        if (c.GetType() != GeomAbs_Line) {
            for (int j = 1; j < kArcSamples; ++j)
                pe.mid.append(c.Value(paramAt(double(j) / kArcSamples)));
        }
        gp_Pnt pm; gp_Vec t;
        c.D1(0.5 * (f + l), pm, t);
        if (t.Magnitude() < 1e-12) t = gp_Vec(pe.p0, pe.p1);
        if (rev) t.Reverse();
        if (t.Magnitude() > 1e-12) t.Normalize();
        pe.tangentMid = t;
        out.append(pe);
    }
    return !out.isEmpty();
}

/// 封閉輪廓在斷面平面上的方向：+1 = 對 profNormal 逆時針、-1 = 順時針
int loopOrientation(const QVector<ProfEdge>& edges, const CantLine& cl)
{
    // 以 Newell 法向量在 profNormal 上的投影判斷（面積向量 ∑ p_i × p_{i+1}）
    gp_Vec area(0, 0, 0);
    for (const ProfEdge& e : edges) {
        const gp_Vec a(e.p0.X(), e.p0.Y(), e.p0.Z()), b(e.p1.X(), e.p1.Y(), e.p1.Z());
        area += a.Crossed(b);
    }
    return (area.Dot(cl.profNormal) >= 0.0) ? 1 : -1;
}

double lateralT(const gp_Pnt& p, const CantLine& cl)
{
    return gp_Vec(cl.a, p).Dot(cl.dir);
}

/// 展平後的平面位置（E, N）：線形上該測站的位置，沿橫向（垂直線形）偏移 t。
/// t 是沿軌道面斷線量得的長度（已展平），所以圖形沿著線形走，彎道處自然隨線形轉彎。
gp_Pnt planPoint(const gp_Pnt& p, const CantLine& cl)
{
    const double t = lateralT(p, cl);
    return gp_Pnt(cl.oE + t * cl.lE, cl.oN + t * cl.lN, 0.0);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// rebuild
// ─────────────────────────────────────────────────────────────────────────────
bool TrackSlabPlan::rebuild()
{
    const QList<ProfileLoftSolid*> lofts = allLofts();
    if (lofts.isEmpty()) { setError("文件中沒有 Loft"); return false; }

    constexpr double kTopThreshold = 0.3;     // 外法線 · 軌道面朝上方向 > 0.3 視為「上方」邊

    try {
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        int wireCount = 0;
        QString firstError;
        QHash<Sketch*, QSet<QString>> cantNamesCache;

        QVector<Polyline> polys;      // 同步記錄折線（供 DXF 匯出），成功時才覆寫 m_polylines

        // 每個「分段 × 輪廓迴圈」的最外側（最左／最右）縱向邊，之後串成封閉的外圍輪廓
        struct GroupOutline {
            int              loop = 0;
            double           startCh = 0.0;
            QPointF          startOrigin, endOrigin;     // 該分段頭／尾測站在線形上的平面位置
            QVector<QPointF> left, right;                // 沿樁號遞增
        };
        QVector<GroupOutline> outlines;

        // 回傳該折線在 polys 中的索引；失敗回傳 -1
        auto addPolyline = [&](const QVector<gp_Pnt>& pts, int kind) -> int {
            if (pts.size() < 2) return -1;
            BRepBuilderAPI_MakePolygon poly;
            for (const gp_Pnt& p : pts) poly.Add(p);
            if (!poly.IsDone() || poly.Wire().IsNull()) return -1;
            builder.Add(compound, poly.Wire());
            ++wireCount;
            Polyline pl;
            pl.kind = kind;
            pl.pts.reserve(pts.size());
            for (const gp_Pnt& p : pts) pl.pts.append(QPointF(p.X(), p.Y()));
            polys.append(pl);
            return polys.size() - 1;
        };

        // 單一 Loft（一個分段）→ 回傳空字串 = 成功，否則為失敗原因
        auto processLoft = [&](ProfileLoftSolid* loft) -> QString {
            AlignedProfileArray* arr = loft->sourceArray();
            if (!arr) return QStringLiteral("沒有斷面陣列");

            const QList<SketchInstance*>      insts = arr->instances();
            const QVector<QList<TopoDS_Wire>> loops = arr->stationWireLoops();
            const QVector<int>                segOf = arr->stationSegmentIndices();
            const QVector<double>             chain = arr->stationChainages();
            if (insts.size() != loops.size() || insts.size() != chain.size())
                return QStringLiteral("測站、輪廓與樁號數量不一致");

            int targetSeg = -1;
            if (!loft->segmentId().isEmpty()) {
                targetSeg = arr->segmentIndexById(loft->segmentId());
                if (targetSeg < 0) return QStringLiteral("所屬分段已不存在");
            }

            // 與 Loft 相同：同一段內的連續測站為一組
            struct Group { int seg; int first; int count; };
            QVector<Group> groups;
            for (int s = 0; s < insts.size(); ++s) {
                const int seg = (s < segOf.size()) ? segOf[s] : 0;
                if (targetSeg >= 0 && seg != targetSeg) continue;
                if (groups.isEmpty() || groups.last().seg != seg || groups.last().first + groups.last().count != s)
                    groups.append({ seg, s, 1 });
                else
                    groups.last().count++;
            }

            bool any = false;
            for (const Group& g : groups) {
                if (g.count < 2) continue;
                any = true;

                // 各測站的軌道面斷線
                QVector<CantLine> cant(g.count);
                for (int i = 0; i < g.count; ++i) {
                    SketchInstance* inst = insts[g.first + i];
                    Sketch* master = inst ? inst->masterSketch() : nullptr;
                    if (master && !cantNamesCache.contains(master))
                        cantNamesCache.insert(master, cantDependentNames(master->parameterStore()));
                    QString e;
                    cant[i] = findCantLine(inst, master ? cantNamesCache.value(master)
                                                        : QSet<QString>{ cantParameterName() }, &e);
                    if (!cant[i].ok)
                        return QStringLiteral("分段 %1 測站 %2：%3").arg(g.seg).arg(i).arg(e);
                }

                const int loopCount = loops[g.first].size();
                if (loopCount <= 0) return QStringLiteral("分段 %1 沒有封閉輪廓").arg(g.seg);
                for (int i = 0; i < g.count; ++i)
                    if (loops[g.first + i].size() != loopCount)
                        return QStringLiteral("分段 %1 測站 %2 的封閉輪廓數量與第一站不同").arg(g.seg).arg(i);

                for (int k = 0; k < loopCount; ++k) {
                    QVector<QVector<ProfEdge>> edges(g.count);
                    for (int i = 0; i < g.count; ++i) {
                        if (!collectProfileEdges(loops[g.first + i][k], edges[i]))
                            return QStringLiteral("分段 %1 測站 %2 迴圈 %3 沒有邊").arg(g.seg).arg(i).arg(k);
                        if (edges[i].size() != edges[0].size())
                            return QStringLiteral("分段 %1 測站 %2 迴圈 %3 的邊數與第一站不同").arg(g.seg).arg(i).arg(k);
                    }
                    const int m = edges[0].size();

                    // 「上方」邊：以第一站判定，之後各站沿用同一組索引（各站拓撲相同）
                    const int orient = loopOrientation(edges[0], cant[0]);
                    QVector<bool> isTop(m, false);
                    QSet<int>     topVertices;
                    for (int j = 0; j < m; ++j) {
                        gp_Vec outward = edges[0][j].tangentMid.Crossed(cant[0].profNormal * double(orient));
                        if (outward.Magnitude() < 1e-12) continue;
                        outward.Normalize();
                        if (outward.Dot(cant[0].up) > kTopThreshold) {
                            isTop[j] = true;
                            topVertices.insert(j);
                            topVertices.insert((j + 1) % m);
                        }
                    }

                    // ① 縱向邊：上方邊的每個頂點，逐站連成沿線形的折線
                    QList<int> verts = topVertices.values();
                    std::sort(verts.begin(), verts.end());
                    int    idxLeft = -1, idxRight = -1;
                    double tLeft = 0.0, tRight = 0.0;
                    for (int v : verts) {
                        QVector<gp_Pnt> pl;
                        pl.reserve(g.count);
                        double meanT = 0.0;
                        for (int i = 0; i < g.count; ++i) {
                            pl.append(planPoint(edges[i][v].p0, cant[i]));
                            meanT += lateralT(edges[i][v].p0, cant[i]);
                        }
                        meanT /= g.count;
                        const int idx = addPolyline(pl, 0);
                        if (idx < 0) continue;
                        if (idxLeft  < 0 || meanT < tLeft)  { idxLeft  = idx; tLeft  = meanT; }
                        if (idxRight < 0 || meanT > tRight) { idxRight = idx; tRight = meanT; }
                    }
                    // 最左／最右的縱向邊 = 該分段此輪廓的最外圍（至少要有兩條不同的線）
                    if (idxLeft >= 0 && idxRight >= 0 && idxLeft != idxRight) {
                        GroupOutline go;
                        go.loop        = k;
                        go.startCh     = chain[g.first];
                        go.startOrigin = QPointF(cant[0].oE, cant[0].oN);
                        go.endOrigin   = QPointF(cant[g.count - 1].oE, cant[g.count - 1].oN);
                        go.left        = polys[idxLeft].pts;
                        go.right       = polys[idxRight].pts;
                        polys[idxLeft].kind  = 2;      // 已併入外圍輪廓
                        polys[idxRight].kind = 2;
                        outlines.append(go);
                    }

                    // ② 端部邊：分段頭尾兩站的上方邊
                    for (int endIdx : { 0, g.count - 1 }) {
                        for (int j = 0; j < m; ++j) {
                            if (!isTop[j]) continue;
                            const ProfEdge& pe = edges[endIdx][j];
                            QVector<gp_Pnt> pl;
                            pl.append(planPoint(pe.p0, cant[endIdx]));
                            for (const gp_Pnt& mp : pe.mid)
                                pl.append(planPoint(mp, cant[endIdx]));
                            pl.append(planPoint(pe.p1, cant[endIdx]));
                            addPolyline(pl, 1);
                        }
                    }
                }
            }
            return any ? QString() : QStringLiteral("至少需要 2 個測站");
        };

        for (ProfileLoftSolid* loft : lofts) {
            // 沒有 cant 參數的斷面不適用：略過（不算錯誤）
            Sketch* master = loft->sourceArray() ? loft->sourceArray()->masterSketch() : nullptr;
            if (!master || !master->parameterStore() || !master->parameterStore()->has(cantParameterName()))
                continue;
            const QString err = processLoft(loft);
            if (!err.isEmpty()) {
                qWarning() << "[TrackSlabPlan]" << loft->name() << ":" << err;
                if (firstError.isEmpty()) firstError = QString("%1：%2").arg(loft->name(), err);
            }
        }

        if (wireCount == 0) {
            setError(firstError.isEmpty() ? QStringLiteral("沒有找到朝上的邊，無法產生道版圖") : firstError);
            return false;
        }

        // ── 最外圍輪廓：同一輪廓迴圈、頭尾相接的分段串成一條封閉聚合線 ──────────────
        //    左側（最左縱向邊）沿樁號遞增 → 末端直線連到右側 → 右側（最右縱向邊）反向走回 → 封閉。
        //    相鄰分段斷面寬度不同時，交界處自然形成一小段直線的「階差」。
        {
            std::sort(outlines.begin(), outlines.end(), [](const GroupOutline& a, const GroupOutline& b) {
                return a.loop != b.loop ? a.loop < b.loop : a.startCh < b.startCh;
            });

            auto flush = [&](QVector<QPointF>& L, QVector<QPointF>& R) {
                if (L.size() < 2 || R.size() < 2) { L.clear(); R.clear(); return; }
                QVector<QPointF> ring = L;
                for (int i = R.size() - 1; i >= 0; --i) ring.append(R[i]);
                // 去除連續重複點（交界處兩段端點重合時）
                QVector<QPointF> clean;
                for (const QPointF& p : ring)
                    if (clean.isEmpty() || QLineF(clean.last(), p).length() > 1e-6) clean.append(p);
                if (clean.size() > 1 && QLineF(clean.first(), clean.last()).length() <= 1e-6) clean.removeLast();
                if (clean.size() >= 3) {
                    BRepBuilderAPI_MakePolygon poly;
                    for (const QPointF& p : clean) poly.Add(gp_Pnt(p.x(), p.y(), 0.0));
                    poly.Close();
                    if (poly.IsDone() && !poly.Wire().IsNull()) {
                        builder.Add(compound, poly.Wire());
                        ++wireCount;
                    }
                    Polyline pl;
                    pl.kind = 3;
                    pl.pts  = clean;               // 封閉：DXF 以 POLYLINE 的 closed 旗標表示，不重複首點
                    polys.append(pl);
                }
                L.clear(); R.clear();
            };

            constexpr double kJoinTol = 0.01;      // 前一分段尾站與下一分段頭站在線形上的位置重合（m）
            QVector<QPointF> L, R;
            QPointF prevEnd;
            int     curLoop = -1;
            for (const GroupOutline& go : std::as_const(outlines)) {
                const bool continues = !L.isEmpty() && go.loop == curLoop
                                       && QLineF(prevEnd, go.startOrigin).length() < kJoinTol;
                if (!continues) flush(L, R);
                L += go.left;
                R += go.right;
                prevEnd = go.endOrigin;
                curLoop = go.loop;
            }
            flush(L, R);
        }

        m_polylines = polys;
        setShape(compound);
        if (firstError.isEmpty()) clearError();
        else setError(QString("部分分段失敗：%1").arg(firstError));   // 其餘分段仍已繪出
        return true;

    } catch (const Standard_Failure& ex) {
        setError(QString("OCC error during slab plan: %1")
                     .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown"));
        return false;
    }
}

} // namespace cad
} // namespace aicad
