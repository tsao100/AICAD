#include "SketchLoopFinder.h"
#include <cad/Sketch.h>
#include <QMap>
#include <cmath>

namespace aicad { namespace cad {

SketchLoopFinder::SketchLoopFinder(double tol) : m_tol(tol) {}

// ── 核心：snapVertex ─────────────────────────────────────────────────────────
int SketchLoopFinder::snapVertex(QVector<Vertex>& verts,
                                 const QVector2D& p) const
{
    for (int i = 0; i < verts.size(); ++i)
        if ((verts[i].pos - p).length() < m_tol)
            return i;
    verts.append({ p, {} });
    return verts.size() - 1;
}

// ── linkTwins：為每條半邊尋找反向半邊 ────────────────────────────────────────
void SketchLoopFinder::linkTwins(QVector<HalfEdge>& he) const
{
    for (int i = 0; i < he.size(); ++i) {
        if (he[i].twin >= 0) continue;
        for (int j = i + 1; j < he.size(); ++j) {
            if (he[j].from == he[i].to && he[j].to == he[i].from) {
                he[i].twin = j;
                he[j].twin = i;
                break;
            }
        }
    }
}

// ── buildNextPointers：以「最右轉角」規則建立 next 指標 ─────────────────────
void SketchLoopFinder::buildNextPointers(QVector<Vertex>& verts,
                                         QVector<HalfEdge>& he) const
{
    // 對每個頂點，將離開的半邊依角度排序，
    // twin(e).next = 下一條（逆時針方向最近）的離開邊
    for (int v = 0; v < verts.size(); ++v) {
        auto& outEdges = verts[v].outEdges;
        // 按角度排序
        // ✅ 修正：outEdges 中每條邊 he[e].from == v 恆成立，直接計算方向向量
        std::sort(outEdges.begin(), outEdges.end(), [&](int a, int b) {
            auto da = verts[he[a].to].pos - verts[v].pos;
            auto db = verts[he[b].to].pos - verts[v].pos;
            return std::atan2(da.y(), da.x()) < std::atan2(db.y(), db.x());
        });

        // twin(outEdges[i]).next = outEdges[(i+1) % n]（最右轉）
        int n = outEdges.size();
        for (int i = 0; i < n; ++i) {
            int e    = outEdges[i];
            int twin = he[e].twin;
            if (twin >= 0)
                he[twin].next = outEdges[(i - 1 + n) % n];
        }
    }
}

// ── signedArea（shoelace）────────────────────────────────────────────────────
float SketchLoopFinder::signedArea(const QVector<int>& loop,
                                   const QVector<Vertex>& verts) const
{
    float area = 0;
    int n = loop.size();
    for (int i = 0; i < n; ++i) {
        auto& a = verts[loop[i]].pos;
        auto& b = verts[loop[(i + 1) % n]].pos;
        area += (a.x() * b.y() - b.x() * a.y());
    }
    return area * 0.5f;
}

// ── pointInPolygon（ray casting）────────────────────────────────────────────
bool SketchLoopFinder::pointInPolygon(const QVector2D& pt,
                                      const QVector<int>& loop,
                                      const QVector<Vertex>& verts) const
{
    int n = loop.size(), cnt = 0;
    for (int i = 0; i < n; ++i) {
        QVector2D a = verts[loop[i]].pos;
        QVector2D b = verts[loop[(i + 1) % n]].pos;
        if (((a.y() <= pt.y()) && (b.y() > pt.y())) ||
            ((b.y() <= pt.y()) && (a.y() > pt.y()))) {
            float xInter = a.x() + (pt.y() - a.y()) / (b.y() - a.y()) * (b.x() - a.x());
            if (pt.x() < xInter) ++cnt;
        }
    }
    return (cnt % 2) != 0;
}

// ── findRegions：主入口 ───────────────────────────────────────────────────────
QVector<SketchRegion> SketchLoopFinder::findRegions(const Sketch* sketch) const
{
    QVector<Vertex>   verts;
    QVector<HalfEdge> halfEdges;
    QVector<SketchRegion> circleRegions;

    // 1. 從 Normal 幾何取出線段端點，建半邊
    for (const SketchGeometry* g : sketch->geometries()) {
        if (g->role != GeomRole::Normal) continue;

        QVector<QVector2D> pts;
        switch (g->type) {
        case SketchGeometryType::Line: {
            auto* l = static_cast<const SketchLine*>(g);
            pts = { l->start, l->end };
            break;
        }
        case SketchGeometryType::Arc: {
            const auto* a = static_cast<const SketchArc*>(g);
            if (a->curve.IsNull()) continue;                         // ← 修正：用 curve 不用 points
            gp_Pnt sp = a->curve->StartPoint();
            gp_Pnt ep = a->curve->EndPoint();
            const cad::Plane* pl = sketch->plane();
            if (!pl) continue;
            auto toLocal = [&](const gp_Pnt& p) -> QVector2D {
                QVector3D v(p.X() - pl->origin().x(),
                            p.Y() - pl->origin().y(),
                            p.Z() - pl->origin().z());
                return QVector2D(QVector3D::dotProduct(v, pl->xAxis()),
                                 QVector3D::dotProduct(v, pl->yAxis()));
            };
            pts = { toLocal(sp), toLocal(ep) };
            break;
        }
        case SketchGeometryType::Circle:
            // 圓：自成一個封閉 loop，跳過端點建表
            {
                SketchRegion r;
                r.uuid             = g->uuid;
                r.outerLoop.edgeUuids = { g->uuid };
                r.outerLoop.isOuter   = true;
                circleRegions.append(r);   // ← 修正：實際存入，不再是 local 變數
                continue;
            }
        default:
            if (g->points.size() >= 2) {
                pts = g->points;
                // ✅ 閉合多段線：補上最後一點回到起點，讓迴圈能建出閉合邊
                if (g->type == SketchGeometryType::Polyline) {
                    const auto* pl = static_cast<const SketchPolyline*>(g);
                    if (pl->closed && !pts.isEmpty())
                        pts.append(pts.first());
                }
            } else continue;
        }

        // 每對相鄰點建一對半邊
        for (int i = 0; i + 1 < pts.size(); ++i) {
            int u = snapVertex(verts, pts[i]);
            int v = snapVertex(verts, pts[i + 1]);
            if (u == v) continue;

            int fwd = halfEdges.size();
            halfEdges.append({ u, v, -1, -1, g->uuid, false });
            halfEdges.append({ v, u, fwd, -1, g->uuid, false });
            halfEdges[fwd].twin = fwd + 1;

            verts[u].outEdges.append(fwd);
            verts[v].outEdges.append(fwd + 1);
        }
    }

    linkTwins(halfEdges);
    buildNextPointers(verts, halfEdges);

    // 2. 遍歷半邊收集 face loops
    QVector<QVector<int>> rawLoops;   // 每個 loop = 頂點 index 序列
    QVector<QVector<QString>> loopEdges;

    for (int i = 0; i < halfEdges.size(); ++i) {
        if (halfEdges[i].visited || halfEdges[i].next < 0) continue;
        QVector<int>     vloop;
        QVector<QString> eloop;
        int cur = i;
        int guard = halfEdges.size();
        while (!halfEdges[cur].visited && guard-- > 0) {
            halfEdges[cur].visited = true;
            vloop.append(halfEdges[cur].from);
            eloop.append(halfEdges[cur].geomUuid);
            cur = halfEdges[cur].next;
            if (cur == i) break;
        }
        if (vloop.size() >= 3) {
            rawLoops.append(vloop);
            loopEdges.append(eloop);
        }
    }

    // 3. 計算 signed area，分 outer / hole
    //    outer: CCW (area > 0)，hole: CW (area < 0)
    QVector<SketchLoop> outers, holes;
    for (int i = 0; i < rawLoops.size(); ++i) {
        float area = signedArea(rawLoops[i], verts);
        if (std::abs(area) < 1e-8) continue;   // 退化 loop
        SketchLoop sl;
        sl.edgeUuids = loopEdges[i];
        sl.isOuter   = (area > 0);
        if (sl.isOuter) outers.append(sl);
        else            holes.append(sl);
    }

    // 4. 組合 Region（含頂點快取）
    QVector<SketchRegion> regions;

    for (int oi = 0; oi < outers.size(); ++oi) {
        SketchLoop& ol = outers[oi];
        // 取得此 outer loop 對應的頂點列表
        int oidx = loopEdges.indexOf(ol.edgeUuids);

        SketchRegion sr;
        sr.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        sr.outerLoop = ol;
        if (oidx >= 0) {
            for (int vi : rawLoops[oidx])
                sr.outerLoop.vertices.append(verts[vi].pos);
        }

        for (int hi = 0; hi < holes.size(); ++hi) {
            SketchLoop& hl = holes[hi];
            int hidx = loopEdges.indexOf(hl.edgeUuids);
            if (hidx < 0) continue;

            // hole centroid → 判斷是否屬於此 outer loop
            QVector2D centroid(0, 0);
            for (int vi : rawLoops[hidx]) centroid += verts[vi].pos;
            centroid /= rawLoops[hidx].size();

            if (sr.outerLoop.vertices.isEmpty() ||
                !SketchRegion::raycast(centroid, sr.outerLoop.vertices))
                continue;

            // 填入頂點後加入 holes
            hl.vertices.clear();
            for (int vi : rawLoops[hidx])
                hl.vertices.append(verts[vi].pos);
            sr.holes.append(hl);
        }
        regions.append(sr);
    }

    regions.append(circleRegions);
    return regions;
}

}} // namespace aicad::cad
