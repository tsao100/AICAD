/**
 * @file PlanProfileGenerator.cpp
 */
#include "PlanProfileGenerator.h"

#include "DrawingSheet.h"
#include "DrawingSheetManager.h"
#include "PlanProfilePainter.h"
#include "cad/Document.h"
#include "railway/RailwayAlignment.h"

#include <QUuid>
#include <QList>
#include <QSet>
#include <QHash>
#include <QPointF>
#include <QDate>
#include <cmath>
#include <algorithm>

namespace aicad {
namespace drawing {

namespace {

QSizeF paperOf(const PlanProfileSettings& s)
{
    SheetConfig cfg;
    cfg.paperSize   = static_cast<PaperSize>(s.paperSize);
    cfg.orientation = static_cast<PaperOrientation>(s.orientation);
    return cfg.paperSizeMM();
}

constexpr int kIndexRowsPerSheet = 22;

bool hasVertical(const railway::TrackCenterLine* t)
{
    return t && t->vertical() && !t->vertical()->isEmpty();
}

QVector<DrawingSheet*> bookSheets(DrawingSheetManager* mgr, const QString& bookId)
{
    QVector<DrawingSheet*> v;
    for (DrawingSheet* s : mgr->sheets())
        if (s->bookId() == bookId) v.append(s);
    return v;
}

QVector<DrawingSheet*> ppSheets(DrawingSheetManager* mgr, const QString& bookId)
{
    QVector<DrawingSheet*> v;
    for (DrawingSheet* s : bookSheets(mgr, bookId))
        if (s->planProfile().enabled && !s->planProfile().isIndex && !s->isCover()) v.append(s);
    return v;
}

QVector<DrawingSheet*> tableSheets(DrawingSheetManager* mgr, const QString& bookId)
{
    QVector<DrawingSheet*> v;
    for (DrawingSheet* s : bookSheets(mgr, bookId))
        if (s->planProfile().tableKind == 1 || s->planProfile().tableKind == 2) v.append(s);
    return v;
}

QVector<DrawingSheet*> schemaSheets(DrawingSheetManager* mgr, const QString& bookId)
{
    QVector<DrawingSheet*> v;
    for (DrawingSheet* s : bookSheets(mgr, bookId))
        if (s->planProfile().tableKind == 3) v.append(s);
    return v;
}

QVector<DrawingSheet*> depotSheets(DrawingSheetManager* mgr, const QString& bookId)
{
    QVector<DrawingSheet*> v;
    for (DrawingSheet* s : bookSheets(mgr, bookId))
        if (s->planProfile().tableKind == 4) v.append(s);
    return v;
}

QVector<DrawingSheet*> indexSheets(DrawingSheetManager* mgr, const QString& bookId)
{
    QVector<DrawingSheet*> v;
    for (DrawingSheet* s : bookSheets(mgr, bookId))
        if (s->planProfile().isIndex) v.append(s);
    return v;
}

DrawingSheet* coverSheet(DrawingSheetManager* mgr, const QString& bookId)
{
    for (DrawingSheet* s : bookSheets(mgr, bookId))
        if (s->isCover()) return s;
    return nullptr;
}

PlanProfileSettings effectiveSettings(const PlanProfileSettings& g, const QJsonObject& ov,
                                      const QString& tclId)
{
    QJsonObject j = g.toJson();
    for (const QString& k : ov.keys())
        if (planProfileKeyOverridable(k)) j[k] = ov.value(k);
    PlanProfileSettings s = g;
    s.fromJson(j);
    s.tclId = tclId;
    return s;
}

void applyPaper(DrawingSheet* sh, const PlanProfileSettings& s)
{
    SheetConfig cfg = sh->config();
    const PaperSize ps = static_cast<PaperSize>(s.paperSize);
    const PaperOrientation po = static_cast<PaperOrientation>(s.orientation);
    if (cfg.paperSize != ps || cfg.orientation != po) {
        cfg.paperSize = ps;
        cfg.orientation = po;
        sh->applyConfig(cfg);
    }
}

/** 片段所屬線形名稱（去重、以 → 連接）與起訖里程文字 */
void pieceLabels(cad::Document* doc, const PlanProfileLayer& l,
                 QString* name, QString* a, QString* b)
{
    QStringList names;
    const railway::TrackCenterLine* firstT = nullptr;
    const railway::TrackCenterLine* lastT = nullptr;
    for (const PlanProfileSegment& g : l.pieces) {
        const railway::TrackCenterLine* t = doc->findTrackCenterLine(g.tclId);
        if (!t) continue;
        if (!firstT) firstT = t;
        lastT = t;
        if (names.isEmpty() || names.last() != t->name()) names << t->name();
    }
    *name = names.join(QStringLiteral("→"));
    *a = (firstT && !l.pieces.isEmpty())
             ? planProfileFormatChainage(firstT, l.settings, l.pieces.first().start) : QString();
    *b = (lastT && !l.pieces.isEmpty())
             ? planProfileFormatChainage(lastT, l.settings, l.pieces.last().end) : QString();
}

/** 產生／更新平面、縱面線形資料表圖頁（每條涉及的線形：平面表、縱面表，超過一頁自動分頁）。 */
void rebuildAlignmentTableSheets(DrawingSheetManager* mgr, cad::Document* doc,
                                 const QString& bookId, const PlanProfileSettings& g)
{
    QVector<DrawingSheet*> old = tableSheets(mgr, bookId);
    if (!g.generateDataTables) {
        for (DrawingSheet* s : old) mgr->removeSheet(s->id());
        return;
    }

    // 圖冊涉及的線形（依出現順序、去重）
    QStringList tclIds;
    for (DrawingSheet* sh : ppSheets(mgr, bookId))
        for (const PlanProfileSegment& seg : sh->planProfile().pieces)
            if (!tclIds.contains(seg.tclId)) tclIds << seg.tclId;
    // 沒有成串（未被選為起始線形、也不在串接內）的其他線形也要有資料表圖頁
    for (railway::TrackCenterLine* t : doc->trackCenterLines())
        if (t && !tclIds.contains(t->id())) tclIds << t->id();

    struct Need { int kind; int page, pages; };
    QVector<Need> needs;
    // 各線形依序接續列出（平面表、縱面表各一份），占滿一頁再接下一頁
    for (int kind = 1; kind <= 2; ++kind) {
        const int pages = planProfileTablePageCount(doc, kind, tclIds, g, paperOf(g));
        for (int pg = 1; pg <= pages; ++pg) {
            Need n; n.kind = kind; n.page = pg; n.pages = pages;
            needs.append(n);
        }
    }

    auto keyOf = [](int kind, const QString& tcl, int page) {
        return QStringLiteral("%1|%2|%3").arg(kind).arg(tcl).arg(page);
    };
    QHash<QString, DrawingSheet*> reuse;
    for (DrawingSheet* s : old) {
        const PlanProfileLayer& l = s->planProfile();
        if (l.tableTclIds.isEmpty()) { mgr->removeSheet(s->id()); continue; }      // 舊版「一線形一表」的圖頁：改由接續列出取代
        const QString k = keyOf(l.tableKind, QString(), l.tablePage);
        if (reuse.contains(k)) mgr->removeSheet(s->id()); else reuse.insert(k, s);
    }
    QSet<DrawingSheet*> used;
    int seq = 0;
    for (const Need& n : needs) {
        ++seq;
        DrawingSheet* sh = reuse.value(keyOf(n.kind, QString(), n.page), nullptr);
        if (!sh) {
            sh = mgr->newSheet(QString());
            sh->setBookId(bookId);
        }
        used.insert(sh);
        PlanProfileLayer layer = sh->planProfile();
        layer.enabled = false;
        layer.isIndex = false;
        layer.tableKind = n.kind;
        layer.tableTclId.clear();
        layer.tableTclIds = tclIds;
        layer.tablePage = n.page;
        layer.tablePages = n.pages;
        layer.groupId = bookId;
        layer.settings = g;
        layer.settings.tclId = tclIds.value(0);
        sh->setPlanProfile(layer);
        applyPaper(sh, g);

        const QString title = QStringLiteral("%1%2")
            .arg(n.kind == 1 ? QStringLiteral("平面線形資料表") : QStringLiteral("縱斷面線形資料表"),
                 n.pages > 1 ? QStringLiteral(" (%1/%2)").arg(n.page).arg(n.pages) : QString());
        TitleBlock tb = sh->titleBlock();
        tb.drawingTitle  = title;
        tb.drawingNumber = QStringLiteral("PP-%1-%2").arg(n.kind == 1 ? QStringLiteral("HZ") : QStringLiteral("VT"))
                               .arg(seq, 2, 10, QLatin1Char('0'));
        tb.sheetNumber   = QStringLiteral("DT-%1").arg(seq);
        sh->setTitleBlock(tb);
    }
    for (DrawingSheet* s : reuse) if (!used.contains(s)) mgr->removeSheet(s->id());
}


// ─────────────────────────────────────────────────────────────────────────────
// 簡圖（tableKind==3）：線形串、渡線／支線拓樸
// ─────────────────────────────────────────────────────────────────────────────

/** 由 startId 起往下接：端點相接的候選中，挑「方位偏轉最小」者（直行優先於分歧）。 */
constexpr double kPiS = 3.14159265358979323846;

QStringList schemaChain(cad::Document* doc, const QString& startId, double tolXY, double tolZ,
                        const QStringList& noChain = QStringList())
{
    QStringList chain;
    const railway::TrackCenterLine* cur = doc->findTrackCenterLine(startId);
    if (!cur) return chain;
    QSet<QString> visited;
    chain << cur->id();
    visited.insert(cur->id());
    if (noChain.contains(startId) || cur->noChain()) return chain;   // 單獨：不接續
    auto dirAt = [](const railway::TrackCenterLine* t, double c, double sgn) {
        const QPointF a = t->getXYForCalc(c);
        const QPointF b = t->getXYForCalc(c + sgn * 0.5);
        return std::atan2(sgn * (b.y() - a.y()), sgn * (b.x() - a.x()));
    };
    for (int guard = 0; guard < 10000; ++guard) {
        double a = 0, b = 0;
        if (!planProfileChainageRange(cur, a, b)) break;
        const QPointF endPt = cur->getXYForCalc(b);
        const double endZ = hasVertical(cur) ? cur->getElevationForCalc(b) : 0.0;
        const double azEnd = dirAt(cur, b, -1.0);
        // 端點 E、N、Z 都要接近；其中距離最近者優先，距離相近（差 0.05 m 內）時取轉向最小者
        struct Cand { const railway::TrackCenterLine* t; double dist, def; };
        QVector<Cand> cands;
        for (railway::TrackCenterLine* t : doc->trackCenterLines()) {
            if (!t || visited.contains(t->id()) || noChain.contains(t->id()) || t->noChain()) continue;
            double ta = 0, tb = 0;
            if (!planProfileChainageRange(t, ta, tb)) continue;
            const QPointF sp = t->getXYForCalc(ta);
            double d3 = std::hypot(sp.x() - endPt.x(), sp.y() - endPt.y());
            if (d3 > tolXY) continue;
            if (hasVertical(cur) && hasVertical(t)) {
                const double dz = std::fabs(t->getElevationForCalc(ta) - endZ);
                if (dz > tolZ) continue;
                d3 = std::sqrt(d3 * d3 + dz * dz);
            }
            double d = dirAt(t, ta, 1.0) - azEnd;
            while (d > kPiS) d -= 2 * kPiS;
            while (d < -kPiS) d += 2 * kPiS;
            cands.append({t, d3, std::fabs(d)});
        }
        double minD = 1e18;
        for (const Cand& c : cands) minD = std::min(minD, c.dist);
        const railway::TrackCenterLine* best = nullptr;
        double bestDef = 1e9;
        for (const Cand& c : cands)
            if (c.dist <= minD + 0.05 && c.def < bestDef) { bestDef = c.def; best = c.t; }
        if (!best) break;
        chain << best->id();
        visited.insert(best->id());
        cur = best;
    }
    return chain;
}

/** 線形串頁面分組：首頁前兩項、末頁後兩項、其餘每頁一項。 */
QVector<QStringList> schemaGroups(const QStringList& chain)
{
    QVector<QStringList> g;
    const int n = chain.size();
    if (n == 0) return g;
    if (n <= 2) { g.append(chain); return g; }
    g.append(QStringList{chain[0], chain[1]});
    for (int i = 2; i < n - 2; ++i) g.append(QStringList{chain[i]});
    g.append(QStringList{chain[n - 2], chain[n - 1]});
    return g;
}

struct SchemaFull {
    QVector<QStringList> chains;      // 各線形串（完整）
    QStringList          names;       // 各串名稱
    QVector<bool>        isD;         // 各串是否為 D（下行）串
    QVector<SchemaLink>  links;       // 全部渡線／支線（kind 1 以 aTcl/bTcl 兩端、kind 2 以 aTcl）
    QHash<QString, int>  bandOf;      // 線形 id → 串索引
};

SchemaFull schemaBuild(cad::Document* doc, const PlanProfileSettings& g)
{
    SchemaFull f;
    // 每個起始線形各成一串（不合併反向起點）；名稱以 D 結尾者排在上方（U 在下）
    QVector<QStringList> cs; QStringList ns;
    for (const QString& sid : g.startIds()) {
        const QStringList c = schemaChain(doc, sid, g.joinTol, g.joinTolZ, g.noChainIds);
        if (c.isEmpty()) continue;
        const railway::TrackCenterLine* t0 = doc->findTrackCenterLine(sid);
        cs.append(c);
        ns.append(t0 ? t0->name() : sid);
    }
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < cs.size(); ++i) {
            const bool isD = ns[i].endsWith(QLatin1Char('D'), Qt::CaseInsensitive);
            if ((pass == 0) != isD) continue;
            f.chains.append(cs[i]);
            f.names.append(ns[i]);
            f.isD.append(isD);
            for (const QString& id : cs[i])
                if (!f.bandOf.contains(id)) f.bandOf.insert(id, f.chains.size() - 1);
        }
    if (f.chains.isEmpty()) return f;

    // 主線取樣（每 2 m 一點）
    struct Samp { QPointF p; double ch; };
    QHash<QString, QVector<Samp>> samples;
    for (const QStringList& c : f.chains)
        for (const QString& id : c) {
            const railway::TrackCenterLine* t = doc->findTrackCenterLine(id);
            double a = 0, b = 0;
            if (!planProfileChainageRange(t, a, b)) continue;
            QVector<Samp> v;
            for (double x = a; x < b; x += 2.0) v.append({t->getXYForCalc(x), x});
            v.append({t->getXYForCalc(b), b});
            samples.insert(id, v);
        }

    QHash<QString, QRectF> sbox;
    for (auto it = samples.constBegin(); it != samples.constEnd(); ++it) {
        QRectF r;
        bool first = true;
        for (const Samp& s : it.value()) {
            if (first) { r = QRectF(s.p, QSizeF(0, 0)); first = false; }
            else { r.setLeft(std::min(r.left(), s.p.x())); r.setTop(std::min(r.top(), s.p.y()));
                   r.setRight(std::max(r.right(), s.p.x())); r.setBottom(std::max(r.bottom(), s.p.y())); }
        }
        sbox.insert(it.key(), r);
    }
    struct Hit { QString tcl; double ch = 0; double tx = 1, ty = 0; double dist = 1e18; bool ok = false; };
    const double tol = std::max(0.5, g.joinTol);
    auto nearest = [&](const QPointF& q) {
        Hit h;
        for (auto it = samples.constBegin(); it != samples.constEnd(); ++it) {
            const QRectF bb = sbox.value(it.key()).adjusted(-tol, -tol, tol, tol);
            if (!bb.contains(q)) continue;
            const QVector<Samp>& v = it.value();
            for (int i = 0; i + 1 < v.size(); ++i) {
                const QPointF a = v[i].p, b = v[i + 1].p;
                const double dx = b.x() - a.x(), dy = b.y() - a.y();
                const double L2 = dx * dx + dy * dy;
                double u = L2 > 1e-12 ? ((q.x() - a.x()) * dx + (q.y() - a.y()) * dy) / L2 : 0.0;
                u = std::max(0.0, std::min(1.0, u));
                const double d = std::hypot(a.x() + u * dx - q.x(), a.y() + u * dy - q.y());
                if (d < h.dist) {
                    const double L = std::sqrt(L2);
                    h.dist = d; h.tcl = it.key();
                    h.ch = v[i].ch + u * (v[i + 1].ch - v[i].ch);
                    h.tx = L > 1e-9 ? dx / L : 1; h.ty = L > 1e-9 ? dy / L : 0;
                }
            }
        }
        h.ok = h.dist <= tol;
        return h;
    };

    for (railway::TrackCenterLine* t : doc->trackCenterLines()) {
        if (!t || f.bandOf.contains(t->id())) continue;
        double a = 0, b = 0;
        if (!planProfileChainageRange(t, a, b) || !(b - a > 1e-6)) continue;
        const QPointF ps = t->getXYForCalc(a), pe = t->getXYForCalc(b);
        const Hit hs = nearest(ps), he = nearest(pe);
        const bool os = hs.ok, oe = he.ok;
        if (!os && !oe) continue;
        SchemaLink k;
        k.name = t->name();
        if (os && oe) {
            if (f.bandOf.value(hs.tcl) == f.bandOf.value(he.tcl)) continue;     // 同一串兩端：不畫
            k.kind = 1;
            k.aTcl = hs.tcl; k.aCh = hs.ch;
            k.bTcl = he.tcl; k.bCh = he.ch;
        } else {
            const Hit& h = os ? hs : he;
            const double e = std::min(1.0, (b - a) / 2.0);
            const QPointF q0 = os ? ps : pe;
            const QPointF q1 = os ? t->getXYForCalc(a + e) : t->getXYForCalc(b - e);
            double dx = q1.x() - q0.x(), dy = q1.y() - q0.y();
            const double L = std::hypot(dx, dy);
            if (L > 1e-9) { dx /= L; dy /= L; }
            k.kind = 2;
            k.aTcl = h.tcl; k.aCh = h.ch;
            k.side = (h.tx * dy - h.ty * dx) >= 0 ? 1 : -1;
            k.dir  = (h.tx * dx + h.ty * dy) >= 0 ? 1 : -1;
        }
        f.links.append(k);
    }
    return f;
}

/** 產生／更新簡圖圖頁。 */
void rebuildSchematicSheets(DrawingSheetManager* mgr, cad::Document* doc,
                            const QString& bookId, const PlanProfileSettings& g)
{
    QVector<DrawingSheet*> old = schemaSheets(mgr, bookId);
    if (!g.generateSchematic) {
        for (DrawingSheet* s : old) mgr->removeSheet(s->id());
        return;
    }
    const SchemaFull f = schemaBuild(doc, g);
    QVector<QVector<QStringList>> groups;
    for (const QStringList& c : f.chains) groups.append(schemaGroups(c));
    // 每頁只畫兩串：第 i 個 D 串與第 i 個 U 串成對；各對依序分頁
    QVector<int> dIdx, uIdx;
    for (int b = 0; b < f.chains.size(); ++b) (f.isD[b] ? dIdx : uIdx).append(b);
    struct PageSpec { QVector<int> bands; int pg; };
    QVector<PageSpec> specs;
    for (int i = 0; i < std::max(dIdx.size(), uIdx.size()); ++i) {
        QVector<int> bs;
        if (i < dIdx.size()) bs.append(dIdx[i]);
        if (i < uIdx.size()) bs.append(uIdx[i]);
        int np = 0;
        for (int b : bs) np = std::max<int>(np, groups[b].size());
        for (int p = 0; p < np; ++p) specs.append({bs, p});
    }
    const int pages = specs.size();
    QHash<int, DrawingSheet*> reuse;
    for (DrawingSheet* s : old) {
        const int pg = s->planProfile().tablePage;
        if (reuse.contains(pg) || pg < 1 || pg > pages) mgr->removeSheet(s->id()); else reuse.insert(pg, s);
    }
    for (int pg = 1; pg <= pages; ++pg) {
        DrawingSheet* sh = reuse.value(pg, nullptr);
        if (!sh) { sh = mgr->newSheet(QString()); sh->setBookId(bookId); }

        PlanProfileLayer layer = sh->planProfile();
        layer.enabled = false;
        layer.isIndex = false;
        layer.tableKind = 3;
        layer.tableTclId.clear();
        layer.tablePage = pg;
        layer.tablePages = pages;
        layer.groupId = bookId;
        layer.settings = g;
        layer.schemaBands.clear();
        layer.schemaLinks.clear();
        QSet<QString> pageItems;
        QHash<QString, int> bandIdx;
        QStringList nm;
        const PageSpec& spec = specs[pg - 1];
        for (int b : spec.bands) {
            SchemaBand sb;
            sb.name = f.names[b];
            if (spec.pg < groups[b].size()) sb.tclIds = groups[b][spec.pg];
            for (const QString& id : sb.tclIds) { pageItems.insert(id); bandIdx.insert(id, b); }
            layer.schemaBands.append(sb);
            nm << sb.name;
        }
        for (SchemaLink k : f.links) {
            const bool ia = pageItems.contains(k.aTcl);
            if (k.kind == 1) {
                const bool ib = pageItems.contains(k.bTcl);
                if (ia && ib) { layer.schemaLinks.append(k); continue; }
                if (!ia && !ib) continue;
                // 只有一端在本頁：當作往另一串方向的短支線
                const QString inTcl = ia ? k.aTcl : k.bTcl;
                const double inCh = ia ? k.aCh : k.bCh;
                const QString outTcl = ia ? k.bTcl : k.aTcl;
                SchemaLink s2;
                s2.name = k.name; s2.kind = 2; s2.aTcl = inTcl; s2.aCh = inCh;
                s2.side = f.bandOf.value(outTcl) < bandIdx.value(inTcl) ? 1 : -1;
                s2.dir = ia ? 1 : -1;
                layer.schemaLinks.append(s2);
            } else if (ia) {
                layer.schemaLinks.append(k);
            }
        }
        sh->setPlanProfile(layer);
        applyPaper(sh, g);

        TitleBlock tb = sh->titleBlock();
        tb.drawingTitle  = QStringLiteral("簡圖 — %1%2").arg(nm.join(QStringLiteral(" / ")),
                               pages > 1 ? QStringLiteral(" (%1/%2)").arg(pg).arg(pages) : QString());
        tb.drawingNumber = QStringLiteral("PP-SC-%1").arg(pg, 2, 10, QLatin1Char('0'));
        tb.sheetNumber   = QStringLiteral("SC-%1").arg(pg);
        sh->setTitleBlock(tb);
    }
}


/** 產生／更新機廠平面圖圖頁：每個矩形視圖一張，依「排序」欄由小到大。 */
void rebuildDepotSheets(DrawingSheetManager* mgr, cad::Document* doc,
                        const QString& bookId, const PlanProfileSettings& g)
{
    Q_UNUSED(doc);
    QVector<DrawingSheet*> old = depotSheets(mgr, bookId);
    if (!g.generateDepot || g.depotViews.isEmpty()) {
        for (DrawingSheet* s : old) mgr->removeSheet(s->id());
        return;
    }
    QVector<DepotView> views = g.depotViews;
    std::stable_sort(views.begin(), views.end(),
                     [](const DepotView& a, const DepotView& b) { return a.order < b.order; });
    const int N = views.size();
    QHash<int, DrawingSheet*> reuse;
    for (DrawingSheet* s : old) {
        const int pg = s->planProfile().tablePage;
        if (reuse.contains(pg) || pg < 1 || pg > N) mgr->removeSheet(s->id()); else reuse.insert(pg, s);
    }
    for (int i = 0; i < N; ++i) {
        DrawingSheet* sh = reuse.value(i + 1, nullptr);
        if (!sh) { sh = mgr->newSheet(QString()); sh->setBookId(bookId); }
        PlanProfileLayer layer = sh->planProfile();
        layer.enabled = false;
        layer.isIndex = false;
        layer.tableKind = 4;
        layer.tableTclId.clear();
        layer.tablePage = i + 1;
        layer.tablePages = N;
        layer.groupId = bookId;
        layer.settings = g;
        layer.depotView = views[i];
        sh->setPlanProfile(layer);
        applyPaper(sh, g);
        const QString nm = views[i].name.isEmpty() ? QStringLiteral("視圖 %1").arg(i + 1) : views[i].name;
        TitleBlock tb = sh->titleBlock();
        tb.drawingTitle  = QStringLiteral("機廠平面圖 — %1").arg(nm);
        tb.drawingNumber = QStringLiteral("PP-DP-%1").arg(i + 1, 2, 10, QLatin1Char('0'));
        tb.sheetNumber   = QStringLiteral("DP-%1").arg(i + 1);
        sh->setTitleBlock(tb);
    }
}

} // namespace

bool planProfileKeyOverridable(const QString& key)
{
    static const QSet<QString> kFixed = {
        "tclId", "tclIds", "noChainIds", "followChain", "joinTol", "joinTolZ", "useFullRange", "startCh", "endCh",
        "hScale", "segmentLength", "overlap", "paperSize", "orientation", "leftLabelW",
        "bookName", "replaceExisting", "generateIndex", "generateCover", "generateDataTables", "generateSchematic", "generateDepot", "depotViews", "hzCols", "vtCols", "tableDecimals", "tableAzDecimals", "radiusSigned"
    };
    return !kFixed.contains(key);
}

QString planProfileExpand(QString t, const QString& name, int n, int N,
                          const QString& a, const QString& b)
{
    t.replace(QStringLiteral("{name}"), name);
    t.replace(QStringLiteral("{n}"), QString::number(n));
    t.replace(QStringLiteral("{N}"), QString::number(N));
    t.replace(QStringLiteral("{start}"), a);
    t.replace(QStringLiteral("{end}"), b);
    return t;
}

QStringList planProfileChain(cad::Document* doc, const QString& startId,
                             double tolXY, double tolZ, bool follow, const QStringList& noChain)
{
    QStringList chain;
    if (!doc) return chain;
    const railway::TrackCenterLine* cur = doc->findTrackCenterLine(startId);
    if (!cur) return chain;
    QSet<QString> visited;
    chain << cur->id();
    visited.insert(cur->id());

    while (follow && !noChain.contains(startId) && !cur->noChain()) {
        double a = 0, b = 0;
        if (!planProfileChainageRange(cur, a, b)) break;
        const QPointF endPt = cur->getXYForCalc(b);
        const double endZ = hasVertical(cur) ? cur->getElevationForCalc(b) : 0.0;

        const railway::TrackCenterLine* best = nullptr;
        double bestD = tolXY;
        for (railway::TrackCenterLine* t : doc->trackCenterLines()) {
            if (!t || visited.contains(t->id()) || noChain.contains(t->id()) || t->noChain()) continue;
            double ta = 0, tb = 0;
            if (!planProfileChainageRange(t, ta, tb)) continue;
            const QPointF sp = t->getXYForCalc(ta);
            const double d = std::hypot(sp.x() - endPt.x(), sp.y() - endPt.y());
            if (d > bestD) continue;
            // Z 也要接近（兩條都有縱斷面才比較）
            if (hasVertical(cur) && hasVertical(t) &&
                std::fabs(t->getElevationForCalc(ta) - endZ) > tolZ) continue;
            bestD = d; best = t;
        }
        if (!best) break;
        chain << best->id();
        visited.insert(best->id());
        cur = best;
    }
    return chain;
}

QVector<ContChainageUpdate> planProfilePropagateContChainage(cad::Document* doc, railway::TrackCenterLine* src,
                                                              double srcContStart, double srcContEnd, int srcDir,
                                                              double tolXY, double tolZ)
{
    QVector<ContChainageUpdate> result;
    if (!doc || !src || src->noChain()) return result;

    struct Node { railway::TrackCenterLine* t; double cs, ce; int dir; int depth; };
    QHash<QString, int> childCount;           // (父線形 id + 端點) → 已接上的線形數，第一條為主串、其後為分支   // cs／ce：起／終點（里程小／大端）的連續里程
    QList<Node> queue;
    QSet<QString> seen;
    queue.append({ src, srcContStart, srcContEnd, srcDir < 0 ? -1 : 1, 0 });
    seen.insert(src->id());

    auto near3d = [&](const railway::TrackCenterLine* a, double ca, const railway::TrackCenterLine* b, double cb) {
        const QPointF p = a->getXYForCalc(ca), q = b->getXYForCalc(cb);
        if (std::hypot(p.x() - q.x(), p.y() - q.y()) > tolXY) return false;
        if (hasVertical(a) && hasVertical(b) &&
            std::fabs(a->getElevationForCalc(ca) - b->getElevationForCalc(cb)) > tolZ) return false;
        return true;
    };
    // 以「里程 anchorCh 處連續里程＝val、方向 dir」重寫整條線形的連續里程
    auto rewrite = [](railway::TrackCenterLine* t, double anchorCh, double val, int dir) {
        QVector<railway::AlignmentPoint> raw = t->horizontal()->rawPoints();
        if (raw.isEmpty()) return;
        for (railway::AlignmentPoint& p : raw) p.contChainage = val + dir * (p.chainage - anchorCh);
        t->loadHorizontal(raw);
    };
    auto dirOf = [](const railway::TrackCenterLine* t) {
        const auto raw = t->horizontal()->rawPoints();
        return (raw.size() > 1 && raw[0].contChainage > raw[1].contChainage) ? -1 : 1;
    };

    // 廣度優先：同一端點可能接多條線形（有意重複／分岐），全部一併連動；
    // 同一實體點連續里程相同；沿同一軌道方向相接者增減方向相同，起點對起點／終點對終點者相反。
    for (int qi = 0; qi < queue.size(); ++qi) {
        const Node cur = queue[qi];
        double ca = 0, cb = 0;
        if (!planProfileChainageRange(cur.t, ca, cb)) continue;
        for (railway::TrackCenterLine* t : doc->trackCenterLines()) {
            if (!t || seen.contains(t->id()) || t->noChain()) continue;
            if (!t->horizontal() || t->horizontal()->isEmpty()) continue;
            double ta = 0, tb = 0;
            if (!planProfileChainageRange(t, ta, tb)) continue;
            double anchorCh = 0, val = 0;
            int dir = cur.dir;
            QString link, side;
            if      (near3d(cur.t, cb, t, ta)) { anchorCh = ta; val = cur.ce; link = QStringLiteral("終點接起點"); side = QStringLiteral("e"); }
            else if (near3d(t, tb, cur.t, ca)) { anchorCh = tb; val = cur.cs; link = QStringLiteral("起點接終點"); side = QStringLiteral("s"); }
            else if (near3d(cur.t, ca, t, ta)) { anchorCh = ta; val = cur.cs; dir = -cur.dir; link = QStringLiteral("起點對起點"); side = QStringLiteral("s"); }
            else if (near3d(cur.t, cb, t, tb)) { anchorCh = tb; val = cur.ce; dir = -cur.dir; link = QStringLiteral("終點對終點"); side = QStringLiteral("e"); }
            else continue;
            seen.insert(t->id());

            const double os = t->horizontal()->getContinuousChainage(ta);
            const double oe = t->horizontal()->getContinuousChainage(tb);
            const int oldDir = dirOf(t);
            rewrite(t, anchorCh, val, dir);
            const double ns = t->horizontal()->getContinuousChainage(ta);
            const double ne = t->horizontal()->getContinuousChainage(tb);
            queue.append({ t, ns, ne, dir, cur.depth + 1 });

            ContChainageUpdate u;
            u.tclId = t->id(); u.name = t->name().isEmpty() ? t->id() : t->name();
            u.parentId = cur.t->id(); u.parentName = cur.t->name().isEmpty() ? cur.t->id() : cur.t->name();
            u.link = link; u.depth = cur.depth + 1;
            u.branch = childCount[cur.t->id() + side]++ > 0;
            u.oldStart = os; u.newStart = ns; u.newEnd = ne; u.dir = dir;
            u.changed = std::fabs(ns - os) > 1e-9 || std::fabs(ne - oe) > 1e-9 || oldDir != dir;
            result.append(u);
        }
    }
    return result;
}

double planProfilePiecesLength(const QVector<PlanProfileSegment>& pieces)
{
    double L = 0;
    for (const PlanProfileSegment& g : pieces) L += g.end - g.start;
    return L;
}

QVector<QVector<PlanProfileSegment>> planProfileSegments(cad::Document* doc,
                                                         const PlanProfileSettings& s,
                                                         QString* error)
{
    QVector<QVector<PlanProfileSegment>> out;
    if (!doc) { if (error) *error = QStringLiteral("沒有作用中文件"); return out; }

    const QStringList starts = s.startIds();
    if (starts.isEmpty()) { if (error) *error = QStringLiteral("請至少選擇一條起始線形"); return out; }

    const double len = planProfileSegmentLength(paperOf(s), s);
    const double step = len - s.overlap;
    if (!(len > 0) || !(step > 0)) {
        if (error) *error = QStringLiteral("分段長度必須大於重疊長度");
        return out;
    }

    QSet<QString> used;
    for (const QString& sid : starts) {
        if (!doc->findTrackCenterLine(sid)) {
            if (error) *error = QStringLiteral("找不到線形：%1").arg(sid);
            return {};
        }
        // 指定里程範圍時只處理起始線形本身，不再往後串接
        const QStringList chain = planProfileChain(doc, sid, s.joinTol, s.joinTolZ,
                                                   s.followChain && s.useFullRange, s.noChainIds);
        // 整串線形：依序收集各線形的里程範圍，並累計成串接全長
        QVector<PlanProfileSegment> lines;
        double total = 0;
        for (const QString& id : chain) {
            if (used.contains(id)) continue;
            used.insert(id);
            const railway::TrackCenterLine* t = doc->findTrackCenterLine(id);
            double a = 0, b = 0;
            if (!planProfileChainageRange(t, a, b)) continue;
            if (!s.useFullRange && id == sid) { a = std::max(a, s.startCh); b = std::min(b, s.endCh); }
            if (!(b - a > 1e-6)) continue;
            PlanProfileSegment g; g.tclId = id; g.start = a; g.end = b;
            lines.append(g);
            total += b - a;
        }
        if (lines.isEmpty()) continue;

        // 以整串全長切成一頁一頁；每頁可能跨越多條線形
        for (double x = 0; x < total - 1e-6; x += step) {
            const double e = std::min(x + len, total);
            QVector<PlanProfileSegment> pieces;
            double cum = 0;
            for (const PlanProfileSegment& ln : lines) {
                const double l0 = cum, l1 = cum + (ln.end - ln.start);
                cum = l1;
                const double a = std::max(x, l0), b = std::min(e, l1);
                if (b - a < 1e-6) continue;
                PlanProfileSegment g;
                g.tclId = ln.tclId;
                g.start = ln.start + (a - l0);
                g.end   = ln.start + (b - l0);
                pieces.append(g);
            }
            if (!pieces.isEmpty()) out.append(pieces);
            if (e >= total - 1e-6) break;
            if (out.size() > 1000) {
                if (error) *error = QStringLiteral("分段數超過 1000，請放大比例或分段長度");
                return {};
            }
        }
    }
    if (out.isEmpty() && error) *error = QStringLiteral("里程範圍無效或線形沒有水平線形");
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// 圖冊
// ─────────────────────────────────────────────────────────────────────────────

int generatePlanProfileSheets(DrawingSheetManager* mgr, cad::Document* doc,
                              const PlanProfileSettings& s, QString* error)
{
    if (!mgr || !doc) { if (error) *error = QStringLiteral("沒有作用中文件"); return 0; }

    QString bookId;
    QString name = s.bookName.isEmpty() ? QStringLiteral("平縱面圖冊") : s.bookName;
    if (s.replaceExisting) {
        for (const DrawingBook& b : mgr->books())
            if (b.name == name) { bookId = b.id; break; }
    } else {
        QString n = name; int k = 2;
        auto exists = [&](const QString& t) { for (const DrawingBook& b : mgr->books()) if (b.name == t) return true; return false; };
        while (exists(n)) n = QStringLiteral("%1 (%2)").arg(name).arg(k++);
        name = n;
    }

    PlanProfileSettings g = s;
    g.bookName = name;
    const bool created = bookId.isEmpty();
    if (created) bookId = mgr->addBook(name, g.toJson())->id;

    if (!updatePlanProfileBook(mgr, doc, bookId, g, error)) {
        if (created) mgr->removeBook(bookId, true);
        return 0;
    }
    return ppSheets(mgr, bookId).size();
}

bool updatePlanProfileDepotViews(DrawingSheetManager* mgr, cad::Document* doc,
                                 const QString& bookId, const QVector<DepotView>& views, QString* error)
{
    if (!mgr || !doc) { if (error) *error = QStringLiteral("沒有作用中文件"); return false; }
    DrawingBook* bk = mgr->findBook(bookId);
    if (!bk) { if (error) *error = QStringLiteral("找不到圖冊"); return false; }
    PlanProfileSettings g;
    g.fromJson(bk->settings);
    g.depotViews = views;
    g.generateDepot = true;
    bk->settings = g.toJson();
    rebuildDepotSheets(mgr, doc, bookId, g);
    if (g.generateIndex) rebuildPlanProfileIndex(mgr, doc, bookId);
    normalizeSheetOrder(mgr);
    mgr->touch();
    return true;
}

bool updatePlanProfileBook(DrawingSheetManager* mgr, cad::Document* doc,
                           const QString& bookId, const PlanProfileSettings& g, QString* error)
{
    if (!mgr || !doc) { if (error) *error = QStringLiteral("沒有作用中文件"); return false; }
    DrawingBook* bk = mgr->findBook(bookId);
    if (!bk) { if (error) *error = QStringLiteral("找不到圖冊"); return false; }

    const auto segs = planProfileSegments(doc, g, error);
    if (segs.isEmpty()) return false;

    const QString oldName = bk->name;
    bk->name = g.bookName.isEmpty() ? oldName : g.bookName;
    bk->settings = g.toJson();

    // 平縱面圖：原有的原地更新（保留身分、順序與局部設定），多的刪、少的補
    QVector<DrawingSheet*> pp = ppSheets(mgr, bookId);
    while (pp.size() > segs.size()) { mgr->removeSheet(pp.last()->id()); pp.removeLast(); }
    const int N = segs.size();
    for (int i = 0; i < N; ++i) {
        DrawingSheet* sh;
        if (i < pp.size()) {
            sh = pp[i];
        } else {
            sh = mgr->newSheet(QString());
            sh->setBookId(bookId);
            pp.append(sh);
        }
        PlanProfileLayer layer = sh->planProfile();
        layer.enabled = true;
        layer.isIndex = false;
        layer.pieces  = segs[i];
        layer.startCh = segs[i].first().start;
        layer.endCh   = segs[i].last().end;
        layer.groupId = bookId;
        layer.settings = effectiveSettings(g, layer.overrides, segs[i].first().tclId);
        sh->setPlanProfile(layer);
        applyPaper(sh, g);
    }

    // 封面
    DrawingSheet* cover = coverSheet(mgr, bookId);
    if (g.generateCover) {
        if (!cover) {
            cover = mgr->newSheet(bk->name);
            cover->setBookId(bookId);
            cover->setIsCover(true);
            TitleBlock tb = cover->titleBlock();
            tb.drawingTitle  = bk->name;
            tb.drawingNumber = QStringLiteral("COVER");
            tb.revision      = QStringLiteral("A");
            tb.designDate    = QDate::currentDate().toString(Qt::ISODate);
            tb.customFields[QStringLiteral("subtitle")] =
                QStringLiteral("平縱面圖  H 1:%1  V 1:%2").arg(g.hScale, 0, 'f', 0).arg(g.vScale, 0, 'f', 0);
            cover->setTitleBlock(tb);
        } else if (cover->titleBlock().drawingTitle == oldName && oldName != bk->name) {
            TitleBlock tb = cover->titleBlock();
            tb.drawingTitle = bk->name;
            cover->setTitleBlock(tb);
        }
        applyPaper(cover, g);
    } else if (cover) {
        mgr->removeSheet(cover->id());
    }

    renumberPlanProfileSheets(mgr, doc, bookId);
    rebuildAlignmentTableSheets(mgr, doc, bookId, g);
    rebuildSchematicSheets(mgr, doc, bookId, g);
    rebuildDepotSheets(mgr, doc, bookId, g);
    if (g.generateIndex) {
        rebuildPlanProfileIndex(mgr, doc, bookId);
    } else {
        for (DrawingSheet* ix : indexSheets(mgr, bookId)) mgr->removeSheet(ix->id());
    }
    normalizeSheetOrder(mgr);
    mgr->touch();
    if (!pp.isEmpty()) mgr->setActiveSheet(pp.first());
    return true;
}

bool applyPlanProfileSheetSettings(DrawingSheetManager* mgr, cad::Document* doc,
                                   DrawingSheet* sh, const PlanProfileSettings& edited)
{
    if (!mgr || !doc || !sh) return false;
    PlanProfileLayer layer = sh->planProfile();
    if (!layer.enabled) return false;
    const QString firstTcl = layer.pieces.isEmpty() ? layer.settings.tclId : layer.pieces.first().tclId;

    DrawingBook* bk = mgr->findBook(sh->bookId());
    if (!bk) {
        layer.settings = edited;
        layer.settings.tclId = firstTcl;
        sh->setPlanProfile(layer);
        mgr->touch();
        return true;
    }
    PlanProfileSettings g;
    g.fromJson(bk->settings);
    const QJsonObject gj = g.toJson(), ej = edited.toJson();
    QJsonObject ov;
    for (const QString& k : ej.keys())
        if (planProfileKeyOverridable(k) && ej.value(k) != gj.value(k)) ov[k] = ej.value(k);
    layer.overrides = ov;
    layer.settings = effectiveSettings(g, ov, firstTcl);
    sh->setPlanProfile(layer);
    renumberPlanProfileSheets(mgr, doc, sh->bookId());
    if (g.generateIndex) rebuildPlanProfileIndex(mgr, doc, sh->bookId());
    normalizeSheetOrder(mgr);
    mgr->touch();
    return true;
}

void clearPlanProfileSheetOverrides(DrawingSheetManager* mgr, cad::Document* doc, DrawingSheet* sh)
{
    if (!sh) return;
    DrawingBook* bk = mgr->findBook(sh->bookId());
    if (!bk) return;
    PlanProfileSettings g;
    g.fromJson(bk->settings);
    applyPlanProfileSheetSettings(mgr, doc, sh, g);   // 與全域相同 → 覆寫為空
}

void renumberPlanProfileSheets(DrawingSheetManager* mgr, cad::Document* doc, const QString& bookId)
{
    if (!mgr || !doc) return;
    const QVector<DrawingSheet*> pp = ppSheets(mgr, bookId);
    const int N = pp.size();
    for (int i = 0; i < N; ++i) {
        DrawingSheet* sh = pp[i];
        PlanProfileLayer layer = sh->planProfile();
        layer.index = i + 1;
        layer.count = N;
        QString name, a, b;
        pieceLabels(doc, layer, &name, &a, &b);

        TitleBlock tb = sh->titleBlock();
        tb.drawingTitle  = planProfileExpand(layer.settings.titleTemplate,  name, i + 1, N, a, b);
        tb.drawingNumber = planProfileExpand(layer.settings.numberTemplate, name, i + 1, N, a, b);
        tb.sheetNumber   = QStringLiteral("%1/%2").arg(i + 1).arg(N);
        tb.projectName   = QStringLiteral("H 1:%1  V 1:%2")
                               .arg(layer.settings.hScale, 0, 'f', 0).arg(layer.settings.vScale, 0, 'f', 0);
        sh->setTitleBlock(tb);
        sh->setPlanProfile(layer);
    }
    // 接圖：同一串（groupId 相同）且相鄰的圖互相標示圖號
    for (int i = 0; i < N; ++i) {
        PlanProfileLayer layer = pp[i]->planProfile();
        const bool hasPrev = i > 0 && pp[i - 1]->planProfile().groupId == layer.groupId;
        const bool hasNext = i + 1 < N && pp[i + 1]->planProfile().groupId == layer.groupId;
        layer.prevNo = hasPrev ? pp[i - 1]->titleBlock().drawingNumber : QString();
        layer.nextNo = hasNext ? pp[i + 1]->titleBlock().drawingNumber : QString();
        pp[i]->setPlanProfile(layer);
    }
}

void rebuildPlanProfileIndex(DrawingSheetManager* mgr, cad::Document* doc, const QString& bookId)
{
    if (!mgr || !doc) return;
    const QVector<DrawingSheet*> pp = ppSheets(mgr, bookId);
    QVector<DrawingSheet*> existing = indexSheets(mgr, bookId);
    if (pp.isEmpty()) {
        for (DrawingSheet* ix : existing) mgr->removeSheet(ix->id());
        return;
    }

    const PlanProfileSettings base = pp.first()->planProfile().settings;
    // 目錄本身（含封面）也要列出：總列數 = 封面 + 目錄頁數 + 圖數
    DrawingSheet* coverSh = coverSheet(mgr, bookId);
    QVector<DrawingSheet*> listed;      // 圖冊內除封面、目錄以外的圖紙（含線形資料表），依目前順序
    for (DrawingSheet* s : bookSheets(mgr, bookId))
        if (!s->isCover() && !s->planProfile().isIndex) listed.append(s);
    const int baseRows = listed.size() + (coverSh ? 1 : 0);
    int pages = 1;
    while (pages * kIndexRowsPerSheet < baseRows + pages) ++pages;
    auto pageTitle = [&](int pg) {
        return pages > 1 ? QStringLiteral("圖目錄 (%1/%2)").arg(pg + 1).arg(pages)
                         : QStringLiteral("圖目錄");
    };
    auto pageNo = [&](int pg) {
        return pages > 1 ? QStringLiteral("PP-INDEX-%1").arg(pg + 1) : QStringLiteral("PP-INDEX");
    };
    while (existing.size() > pages) { mgr->removeSheet(existing.last()->id()); existing.removeLast(); }

    for (int pg = 0; pg < pages; ++pg) {
        const QString title = pageTitle(pg);
        DrawingSheet* sh;
        if (pg < existing.size()) {
            sh = existing[pg];
        } else {
            sh = mgr->newSheet(title);
            sh->setBookId(bookId);
            existing.append(sh);
        }
        applyPaper(sh, base);

        TitleBlock tb = sh->titleBlock();
        tb.drawingTitle  = title;
        tb.drawingNumber = pageNo(pg);
        sh->setTitleBlock(tb);

        sh->enableDrawingIndex(true);
        DrawingIndex* idx = sh->drawingIndex();
        idx->setTitle(title);
        idx->setPosition(QPointF(20.0, 28.0));
        while (!idx->entries().isEmpty()) idx->removeEntry(0);
        // 列表：封面、各目錄頁、各平縱面圖（依圖冊順序）
        QVector<DrawingIndexEntry> all;
        if (coverSh) {
            const TitleBlock& ctb = coverSh->titleBlock();
            DrawingIndexEntry e;
            e.sheetNumber = QStringLiteral("COVER");
            e.drawingNumber = ctb.drawingNumber;
            e.title = ctb.drawingTitle.isEmpty() ? QStringLiteral("封面") : ctb.drawingTitle;
            e.revision = ctb.revision;
            all.append(e);
        }
        for (int q = 0; q < pages; ++q) {
            DrawingIndexEntry e;
            e.sheetNumber = QStringLiteral("INDEX %1/%2").arg(q + 1).arg(pages);
            e.drawingNumber = pageNo(q);
            e.title = pageTitle(q);
            all.append(e);
        }
        for (DrawingSheet* ls : listed) {
            const PlanProfileLayer& l = ls->planProfile();
            const TitleBlock& ptb = ls->titleBlock();
            DrawingIndexEntry e;
            e.sheetNumber   = ptb.sheetNumber;
            e.drawingNumber = ptb.drawingNumber;
            e.title         = ptb.drawingTitle;
            e.revision      = ptb.revision;
            if (l.tableKind == 4) {
                e.remark = QStringLiteral("機廠平面圖 %1").arg(l.depotView.name);
            } else if (l.tableKind == 3) {
                QStringList nm;
                for (const SchemaBand& sb : l.schemaBands) nm << sb.name;
                e.remark = QStringLiteral("簡圖 %1").arg(nm.join(QStringLiteral("/")));
            } else if (l.tableKind > 0) {
                e.remark = QStringLiteral("%1 %2/%3").arg(l.tableKind == 1 ? QStringLiteral("平面資料表")
                                                                            : QStringLiteral("縱面資料表"))
                               .arg(l.tablePage).arg(l.tablePages);
            } else {
                QString a, b;
                if (!l.pieces.isEmpty()) {
                    const railway::TrackCenterLine* t0 = doc->findTrackCenterLine(l.pieces.first().tclId);
                    const railway::TrackCenterLine* t1 = doc->findTrackCenterLine(l.pieces.last().tclId);
                    a = planProfileFormatRemarkChainage(t0, l.settings, l.pieces.first().start);
                    b = planProfileFormatRemarkChainage(t1, l.settings, l.pieces.last().end);
                }
                e.remark = QStringLiteral("%1~%2").arg(a, b);
            }
            all.append(e);
        }
        for (int i = pg * kIndexRowsPerSheet; i < std::min<int>(all.size(), (pg + 1) * kIndexRowsPerSheet); ++i)
            idx->addEntry(all[i]);

        PlanProfileLayer layer;
        layer.enabled = false;
        layer.isIndex = true;
        layer.settings = base;
        layer.groupId = bookId;
        sh->setPlanProfile(layer);
    }
}

void normalizeSheetOrder(DrawingSheetManager* mgr)
{
    if (!mgr) return;
    QVector<DrawingSheet*> order;
    auto take = [&](DrawingSheet* s) { if (!order.contains(s)) order.append(s); };
    for (const DrawingBook& b : mgr->books()) {
        const QVector<DrawingSheet*> all = bookSheets(mgr, b.id);
        for (DrawingSheet* s : all) if (s->isCover()) take(s);
        for (DrawingSheet* s : all) if (s->planProfile().isIndex) take(s);
        // 簡圖 → 平縱面圖 → 機廠平面圖 → 線形資料表 → 其餘
        for (DrawingSheet* s : all) if (s->planProfile().tableKind == 3) take(s);
        for (DrawingSheet* s : all) if (s->planProfile().enabled && !s->planProfile().isIndex) take(s);
        for (DrawingSheet* s : all) if (s->planProfile().tableKind == 4) take(s);
        for (DrawingSheet* s : all) if (s->planProfile().tableKind == 1 || s->planProfile().tableKind == 2) take(s);
        for (DrawingSheet* s : all) take(s);
    }
    for (DrawingSheet* s : mgr->sheets()) take(s);
    mgr->setSheetOrder(order);
}

const PlanProfileSettings* planProfileSettingsOf(const DrawingSheet* sheet)
{
    if (sheet && sheet->planProfile().enabled) return &sheet->planProfile().settings;
    return nullptr;
}

} // namespace drawing
} // namespace aicad
