#include "AlignedProfileArray.h"
#include "Document.h"
#include "ProfileLoftSolid.h"
#include "ChamferSolid.h"
#include "Sketch.h"
#include "Plane.h"
#include "PlaneManager.h"
#include "SketchInstance.h"
#include "railway/RailwayAlignment.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalBlocker>
#include <QStringList>
#include <QUuid>
#include <QHash>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace aicad {
namespace cad {

namespace {

constexpr double kEps = 1e-6;

/// RAII：批次更新 FeatureTree（begin/end 之間的 treeStructureChanged 合併為一次）。
struct TreeBatch {
    Document* d;
    explicit TreeBatch(Document* doc) : d(doc) { if (d) d->beginTreeBatch(); }
    ~TreeBatch() { if (d) d->endTreeBatch(); }
    TreeBatch(const TreeBatch&) = delete;
    TreeBatch& operator=(const TreeBatch&) = delete;
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// 構造 / 析構
// ─────────────────────────────────────────────────────────────────────────────

AlignedProfileArray::AlignedProfileArray(Document* parent)
    : Feature(parent)
{
    setName("ProfileArray");
}

AlignedProfileArray::~AlignedProfileArray() {
    clearInstances();
}

// ─────────────────────────────────────────────────────────────────────────────
// 參考設定
// ─────────────────────────────────────────────────────────────────────────────

Sketch* AlignedProfileArray::resolveProfile(const ProfileArraySegment& seg) const {
    if (seg.profileId.isEmpty()) return nullptr;
    Document* doc = document();
    if (!doc) return nullptr;
    return qobject_cast<Sketch*>(doc->findFeature(seg.profileId));
}

void AlignedProfileArray::syncMasterConnections() {
    // 目前所有段用到的 profile
    QSet<Sketch*> wanted;
    for (const ProfileArraySegment& seg : m_segments)
        if (Sketch* sk = resolveProfile(seg)) wanted.insert(sk);

    // 移除不再使用（或已被刪除）的連線
    for (auto it = m_masterConns.begin(); it != m_masterConns.end(); ) {
        if (!wanted.contains(it.key())) {
            disconnect(it.value());
            it = m_masterConns.erase(it);
        } else {
            ++it;
        }
    }
    // 補上新的：profile 幾何/約束改變 → 全部測站重建。
    // ✅ 透過 Document::rebuildFeature() 而不是直接呼叫 rebuild()：後者才會
    //    Q_EMIT featureShapeUpdated()，CadView 監聽這個訊號重新整理畫面。
    for (Sketch* sk : wanted) {
        if (m_masterConns.contains(sk)) continue;
        m_masterConns.insert(sk, connect(sk, &Feature::shapeChanged, this, [this]() {
            if (Document* doc = document()) doc->rebuildFeature(this);
            else rebuild();
        }));
    }
}

void AlignedProfileArray::setMasterSketch(Sketch* master) {
    // 舊版單段介面：設定第一段的 profile（沒有任何段時先建立一段）。
    if (m_segments.isEmpty()) {
        ProfileArraySegment seg;
        seg.name = QStringLiteral("Seg1");
        m_segments.append(seg);
    }
    m_segments[0].profileId = master ? master->id() : QString();
    syncMasterConnections();
}

QString AlignedProfileArray::masterSketchId() const {
    return m_segments.isEmpty() ? QString() : m_segments.first().profileId;
}

Sketch* AlignedProfileArray::masterSketch() const {
    return m_segments.isEmpty() ? nullptr : resolveProfile(m_segments.first());
}

railway::TrackCenterLine* AlignedProfileArray::trackCenterLine() const {
    return m_tcl.data();
}

QList<SketchInstance*> AlignedProfileArray::instances() const {
    QList<SketchInstance*> out;
    out.reserve(m_instances.size());
    for (const QPointer<SketchInstance>& p : m_instances)
        if (p) out.append(p.data());
    return out;
}

void AlignedProfileArray::setTrackCenterLine(railway::TrackCenterLine* tcl) {
    if (m_tcl == tcl) return;

    if (m_tcl) disconnect(m_tclDataChangedConn);
    m_tcl = tcl;
    m_routeValid = false;
    m_autoCache.clear();

    if (m_tcl) {
        // Alignment 編輯（含 cant/H 的來源資料）改變 → 全部測站重建（走
        // Document::rebuildFeature() 以確保畫面刷新）。
        m_tclDataChangedConn = connect(
            m_tcl, &railway::TrackCenterLine::dataChanged,
            this, [this]() {
                if (Document* doc = document()) doc->rebuildFeature(this);
                else rebuild();
            });
    }
}

QString AlignedProfileArray::trackCenterLineId() const {
    return m_tcl ? m_tcl->id() : m_pendingTclId;
}

// ─────────────────────────────────────────────────────────────────────────────
// 分段資料
// ─────────────────────────────────────────────────────────────────────────────

void AlignedProfileArray::ensureSegmentIds()
{
    QSet<QString> seen;
    for (ProfileArraySegment& s : m_segments) {
        if (s.id.isEmpty() || seen.contains(s.id))
            s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        seen.insert(s.id);
    }
}

QString AlignedProfileArray::segmentId(int idx) const
{
    return (idx >= 0 && idx < m_segments.size()) ? m_segments[idx].id : QString();
}

int AlignedProfileArray::segmentIndexById(const QString& id) const
{
    if (id.isEmpty()) return -1;
    for (int i = 0; i < m_segments.size(); ++i)
        if (m_segments[i].id == id) return i;
    return -1;
}

void AlignedProfileArray::setSegments(const QList<ProfileArraySegment>& segs,
                                      const QStringList& extraColumns)
{
    m_segments     = segs;
    m_extraColumns = extraColumns;
    ensureSegmentIds();        // 表格新增的列沒有 id → 在這裡配發
    syncMasterConnections();
}

bool AlignedProfileArray::commitSegments(const QList<ProfileArraySegment>& segs,
                                         const QStringList& extraColumns)
{
    setSegments(segs, extraColumns);

    Document* doc = document();
    if (!doc) return rebuild();

    doc->refreshFeatureDependencies(this);   // 依賴的 profile 可能已換
    doc->rebuildFeature(this);
    const bool ok = errorMessage().isEmpty();

    // 每個分段一個 Loft：新增的列建立新 Loft、刪除的列移除其 Loft。
    // 必須在陣列重建完成（測站已就緒）之後，Loft 建立時才取得到各站輪廓。
    syncSegmentLofts();
    return ok;
}

QHash<QString, ProfileLoftSolid*> AlignedProfileArray::collectSegmentLofts() const
{
    QHash<QString, ProfileLoftSolid*> out;
    Document* doc = document();
    if (!doc) return out;
    for (Feature* f : doc->features()) {
        auto* l = qobject_cast<ProfileLoftSolid*>(f);
        if (l && l->sourceArray() == this && !l->segmentId().isEmpty())
            out.insert(l->segmentId(), l);
    }
    return out;
}

void AlignedProfileArray::reparentInstances()
{
    Document* doc = document();
    if (!doc) return;
    const QHash<QString, ProfileLoftSolid*> lofts = collectSegmentLofts();

    TreeBatch treeBatch(doc);
    for (int i = 0; i < m_instances.size(); ++i) {
        SketchInstance* inst = m_instances[i];
        if (!inst) continue;
        Feature* want = this;
        if (i < m_stationSeg.size()) {
            const int s = m_stationSeg[i];
            if (s >= 0 && s < m_segments.size())
                if (ProfileLoftSolid* l = lofts.value(m_segments[s].id, nullptr))
                    want = l;
        }
        if (inst->parent() != want) {
            inst->setFeatureParent(want);
            doc->setTreeItemParent(inst->id(), want->id());
        }
    }
}

void AlignedProfileArray::syncSegmentLofts()
{
    Document* doc = document();
    if (!doc) return;
    ensureSegmentIds();

    // 先取快照：下面會新增／移除 Feature
    QList<ProfileLoftSolid*> mine;
    const QList<Feature*> snapshot = doc->features();
    for (Feature* f : snapshot) {
        auto* l = qobject_cast<ProfileLoftSolid*>(f);
        if (l && l->sourceArray() == this) mine.append(l);
    }

    QSet<QString> segIds;
    for (const ProfileArraySegment& s : m_segments) segIds.insert(s.id);

    // 1) 舊檔：一個 Loft 涵蓋全部分段（segmentId 為空）→ 轉成第一個還沒有 Loft 的分段
    QSet<QString> used;
    for (ProfileLoftSolid* l : mine)
        if (!l->segmentId().isEmpty() && segIds.contains(l->segmentId()))
            used.insert(l->segmentId());
    QList<ProfileLoftSolid*> toRemove;
    for (ProfileLoftSolid* l : mine) {
        if (!l->segmentId().isEmpty()) continue;
        QString target;
        for (const ProfileArraySegment& s : m_segments)
            if (!used.contains(s.id)) { target = s.id; break; }
        if (target.isEmpty()) toRemove.append(l);
        else { l->setSegmentId(target); used.insert(target); }
    }

    // 2) 分段已不存在（列被刪除）或重複的 Loft → 移除（連同以它為來源的 Chamfer）
    QSet<QString> have;
    for (ProfileLoftSolid* l : mine) {
        if (toRemove.contains(l)) continue;
        if (!segIds.contains(l->segmentId()) || have.contains(l->segmentId()))
            toRemove.append(l);
        else
            have.insert(l->segmentId());
    }
    for (ProfileLoftSolid* l : toRemove) {
        for (Feature* f : snapshot) {
            if (auto* cf = qobject_cast<ChamferSolid*>(f))
                if (cf->sourceFeature() == l) doc->removeFeature(cf);
        }
        // 先把測站移回陣列，避免它們短暫掛在即將刪除的 Loft 下
        for (SketchInstance* inst : m_instances) {
            if (inst && inst->parent() == l) {
                inst->setFeatureParent(this);
                doc->setTreeItemParent(inst->id(), id());
            }
        }
        doc->removeFeature(l);
    }

    // 3) 每個分段恰好一個 Loft；名稱跟著分段名稱
    TreeBatch treeBatch(doc);
    for (const ProfileArraySegment& s : m_segments) {
        const QString wantName = QString("%1_%2_Loft").arg(name(), s.name);
        ProfileLoftSolid* existing = nullptr;
        for (ProfileLoftSolid* l : mine)
            if (!toRemove.contains(l) && l->segmentId() == s.id) { existing = l; break; }
        if (!existing) {
            doc->createProfileLoftSolid(this, wantName, s.id);
        } else if (existing->name() != wantName) {
            existing->setName(wantName);
            doc->setTreeItemName(existing->id(), wantName);
        }
    }

    // 4) 測站掛到各自分段的 Loft 之下
    reparentInstances();
}

QString AlignedProfileArray::uniqueSegmentName(const QString& base) const {
    QSet<QString> used;
    for (const ProfileArraySegment& s : m_segments) used.insert(s.name);
    for (int i = 1; ; ++i) {
        const QString n = base + QString::number(i);
        if (!used.contains(n)) return n;
    }
}

void AlignedProfileArray::setRange(double startChainage, double endChainage, double interval, bool autoInterval) {
    if (m_segments.isEmpty()) {
        ProfileArraySegment seg;
        seg.name = QStringLiteral("Seg1");
        m_segments.append(seg);
        ensureSegmentIds();
    }
    m_segments[0].start    = startChainage;
    m_segments[0].end      = endChainage;
    m_segments[0].interval = interval;
    m_segments[0].autoInterval = autoInterval;
}

double AlignedProfileArray::startChainage() const {
    if (m_segments.isEmpty()) return 0.0;
    double v = m_segments.first().start;
    for (const auto& s : m_segments) v = std::min(v, s.start);
    return v;
}

double AlignedProfileArray::endChainage() const {
    if (m_segments.isEmpty()) return 0.0;
    double v = m_segments.first().end;
    for (const auto& s : m_segments) v = std::max(v, s.end);
    return v;
}

double AlignedProfileArray::interval() const {
    return m_segments.isEmpty() ? 20.0 : m_segments.first().interval;
}

// ─────────────────────────────────────────────────────────────────────────────
// rebuild() 核心（對應規劃 §2.2）
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
//  里程超出線形範圍 → 接續端點相連的線形
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// 線形在里程 p 的 (E, N, Z) 與切線方位角
struct EndPoint { double e = 0, n = 0, z = 0, az = 0; };

EndPoint endPointOf(const railway::TrackCenterLine* t, double p)
{
    EndPoint ep;
    const QPointF xy = t->getXYForCalc(p, 0.0);
    ep.e  = xy.x();
    ep.n  = xy.y();
    ep.z  = t->getElevationForCalc(p);
    ep.az = t->getAzimuthForCalc(p);
    return ep;
}

/// 兩端點是否相連：E,N 距離與 Z 差在容差內，且切線方向大致連續。
/// @return 相連時回傳「方向差 (rad)」供多個候選時挑最順的；不相連回傳 -1
double connectionScore(const EndPoint& a, const EndPoint& b, double azB)
{
    if (std::hypot(a.e - b.e, a.n - b.n) > AlignedProfileArray::kConnectTolXY) return -1.0;
    if (std::abs(a.z - b.z)              > AlignedProfileArray::kConnectTolZ)  return -1.0;
    double d = std::fmod(std::abs(a.az - azB), 2.0 * M_PI);
    if (d > M_PI) d = 2.0 * M_PI - d;
    return (d <= AlignedProfileArray::kConnectTolAz) ? d : -1.0;
}

} // namespace

void AlignedProfileArray::ensureRoute() const
{
    if (m_routeValid) return;
    m_route.clear();
    m_routeValid = true;
    if (!m_tcl) return;

    double p0 = 0.0, p1 = 0.0;
    if (!m_tcl->chainageRangeForCalc(p0, p1)) return;

    RouteLeg main;
    main.tcl = m_tcl;
    main.a0  = p0;
    main.a1  = p1;
    main.off = 0.0;
    m_route.append(main);

    Document* doc = document();
    if (!doc) return;
    const QList<railway::TrackCenterLine*> all = doc->trackCenterLines();

    QSet<const railway::TrackCenterLine*> used;
    used.insert(m_tcl.data());

    // 往前接（終點接到下一條的起點）與往後接（上一條的終點接到本線形起點）
    for (int dir = 0; dir < 2; ++dir) {
        const bool forward = (dir == 0);
        railway::TrackCenterLine* cur = m_tcl.data();
        double curA = forward ? p1 : p0;          // 目前路徑端點的陣列里程
        double cur0 = p0, cur1 = p1;              // 目前線形的里程範圍

        for (int hop = 0; hop < 16; ++hop) {
            const EndPoint here = endPointOf(cur, forward ? cur1 : cur0);

            railway::TrackCenterLine* best = nullptr;
            double bestScore = 1e9, b0 = 0.0, b1 = 0.0;
            for (railway::TrackCenterLine* cand : all) {
                if (!cand || used.contains(cand)) continue;
                double c0, c1;
                if (!cand->chainageRangeForCalc(c0, c1)) continue;
                // 往前接：候選的「起點」要接到本端終點；往後接：候選的「終點」接到本端起點
                const double pc = forward ? c0 : c1;
                const EndPoint there = endPointOf(cand, pc);
                const double sc = connectionScore(here, there, there.az);
                if (sc >= 0.0 && sc < bestScore) {
                    best = cand; bestScore = sc; b0 = c0; b1 = c1;
                }
            }
            if (!best) break;

            RouteLeg leg;
            leg.tcl = best;
            if (forward) {
                leg.a0  = curA;
                leg.a1  = curA + (b1 - b0);
                leg.off = b0 - curA;
                curA    = leg.a1;
                m_route.append(leg);
            } else {
                leg.a1  = curA;
                leg.a0  = curA - (b1 - b0);
                leg.off = b1 - curA;
                curA    = leg.a0;
                m_route.prepend(leg);
            }
            used.insert(best);
            cur = best; cur0 = b0; cur1 = b1;
        }
    }
}

AlignedProfileArray::AlignmentSample AlignedProfileArray::resolveAlignment(double p) const
{
    AlignmentSample s;
    s.tcl = m_tcl.data();
    s.p   = p;
    if (!m_tcl) return s;

    ensureRoute();
    constexpr double kEdge = 0.01;     // ALD 樁號各自四捨五入造成的微小越界不算超出
    for (const RouteLeg& leg : m_route) {
        if (!leg.tcl) continue;
        if (p >= leg.a0 - kEdge && p <= leg.a1 + kEdge) {
            s.tcl       = leg.tcl.data();
            s.p         = p + leg.off;
            s.connected = (leg.tcl.data() != m_tcl.data());
            return s;
        }
    }
    s.outOfRange = !m_route.isEmpty();   // 路徑已建好但沒有任何一段涵蓋 → 只能外推
    return s;
}

AlignedProfileArray::SegmentAlignments
AlignedProfileArray::alignmentsForRange(double start, double end) const
{
    SegmentAlignments out;
    if (!m_tcl) return out;
    if (end < start) std::swap(start, end);

    m_routeValid = false;      m_autoCache.clear();   // 對話框編輯中可能剛新增／移動過線形：每次都重新判定
    ensureRoute();
    if (m_route.isEmpty()) {
        // 主線形沒有元素（無法取得範圍）：只能回報主線形名稱
        out.names << m_tcl->name();
        return out;
    }

    constexpr double kEdge = 0.01;
    double covered = start;            // 已被路徑涵蓋到的里程
    for (const RouteLeg& leg : m_route) {
        if (!leg.tcl) continue;
        if (leg.a1 < start - kEdge || leg.a0 > end + kEdge) continue;   // 與本段無交集
        const QString nm = leg.tcl->name();
        if (!out.names.contains(nm)) out.names << nm;
        covered = std::max(covered, std::min(leg.a1, end));
    }
    // 路徑涵蓋不到的部分（起點之前或終點之後）→ 越界
    if (start < m_route.first().a0 - kEdge || end > m_route.last().a1 + kEdge)
        out.outOfRange = true;
    if (out.names.isEmpty()) out.names << m_tcl->name();   // 整段都越界：仍顯示主線形
    return out;
}

void AlignedProfileArray::syncRouteConnections()
{
    for (const QMetaObject::Connection& c : m_routeConns) disconnect(c);
    m_routeConns.clear();
    ensureRoute();
    for (const RouteLeg& leg : m_route) {
        if (!leg.tcl || leg.tcl.data() == m_tcl.data()) continue;
        // 相連線形的資料改變 → 路徑失效、陣列重建（與主線形的 dataChanged 相同處理）
        m_routeConns.append(connect(
            leg.tcl.data(), &railway::TrackCenterLine::dataChanged, this, [this]() {
                m_routeValid = false;
    m_autoCache.clear();
                if (Document* doc = document()) doc->rebuildFeature(this);
                else rebuild();
            }));
    }
}

SketchInstance* AlignedProfileArray::createProbeAt(double p, QString* error) const
{
    auto fail = [&](const QString& m) -> SketchInstance* { if (error) *error = m; return nullptr; };

    Document* doc = document();
    if (!doc) return fail(QStringLiteral("沒有文件"));

    int si = -1;
    for (int i = 0; i < m_segments.size(); ++i)
        if (p >= m_segments[i].start - kEps && p <= m_segments[i].end + kEps) { si = i; break; }
    if (si < 0) return fail(QStringLiteral("里程 %1 不在任何分段內").arg(p, 0, 'f', 3));
    const ProfileArraySegment& seg = m_segments[si];

    Sketch* master = resolveProfile(seg);
    if (!master) return fail(QStringLiteral("分段 %1 沒有斷面草圖").arg(si));

    const StationFrame sf = frameAt(p);
    if (!sf.ok) return fail(QStringLiteral("里程 %1 超出線形範圍").arg(p, 0, 'f', 3));

    Plane* plane = nullptr;
    for (Plane* pl : m_planes) if (pl) { plane = pl; break; }
    if (!plane) return fail(QStringLiteral("陣列尚未建立測站"));

    auto* probe = new SketchInstance(doc);
    probe->setName(QStringLiteral("probe"));
    probe->setMasterSketch(master);
    probe->setPlane(plane);
    probe->setScale(m_masterUnitScale);
    probe->setExternallyDriven(true);

    SketchInstance::PlacementFrame fr;
    fr.ox = sf.ox; fr.oy = sf.oy; fr.oz = sf.oz;
    fr.xx = sf.xx; fr.xy = sf.xy; fr.xz = sf.xz;
    fr.yx = sf.yx; fr.yy = sf.yy; fr.yz = sf.yz;
    fr.valid = true;
    probe->setFrame(fr);

    // 與測站相同的參數（cant、H、Thick、w、自訂欄位）與簽章
    const auto params = stationParameters(p, seg);
    QString sig = seg.profileId + QLatin1Char('|');
    for (const auto& kv : params)
        sig += kv.first + QLatin1Char('=') + QString::number(kv.second, 'g', 12) + QLatin1Char(';');
    {
        const QSignalBlocker blocker(probe->parameterStore());
        for (const auto& kv : params)
            probe->parameterStore()->setLocal(kv.first, kv.second);
    }

    if (!probe->rebuildWith(sig, nullptr, /*force=*/true)) {
        delete probe;
        return fail(QStringLiteral("里程 %1 的斷面求解失敗").arg(p, 0, 'f', 3));
    }
    return probe;
}

AlignedProfileArray::StationFrame AlignedProfileArray::frameAt(double p) const
{
    // ⚠️ 公式必須與 rebuild() 的「放置座標系」段落保持一致（改一處請同步改另一處）
    StationFrame out;
    const AlignmentSample as = resolveAlignment(p);
    if (!as.tcl || as.outOfRange) return out;

    const railway::TrackCenterLine* T = as.tcl;
    const double  pc  = as.p;
    const QPointF xy  = T->getXYForCalc(pc, 0.0);
    const double  z0  = T->getElevationForCalc(pc);
    const double  az  = T->getAzimuthForCalc(pc);              // [rad], CW from N
    const double  g   = T->getSlopeForCalc(pc) / 100.0;        // dz/dp

    const double sA = std::sin(az), cA = std::cos(az);
    const double tn = std::sqrt(1.0 + g * g);
    const double tx = sA / tn, ty = cA / tn, tz = g / tn;      // 3D 切線（朝里程增加）

    out.xx =  cA; out.xy = -sA; out.xz = 0.0;                  // 右手邊水平法線
    out.zx = -tx; out.zy = -ty; out.zz = -tz;                  // Z = -T
    out.yx = out.zy * out.xz - out.zz * out.xy;                // Y = Z × X
    out.yy = out.zz * out.xx - out.zx * out.xz;
    out.yz = out.zx * out.xy - out.zy * out.xx;
    out.ox = xy.x(); out.oy = xy.y(); out.oz = z0;
    out.ok = true;
    return out;
}

bool AlignedProfileArray::isCurveAt(double p) const
{
    // 曲率 = 方位角在 p 附近 ±0.5 m 的變化量 / 弧長；直線方位角恆定 → 0。
    // 門檻 1/50000 m⁻¹（R < 50 km 視為曲線）：遠高於數值雜訊、遠低於實際曲線。
    constexpr double kHalf = 0.5, kMinCurvature = 2e-5;
    const AlignmentSample s = resolveAlignment(p);
    if (!s.tcl || s.outOfRange) return false;
    double c0 = 0.0, c1 = 0.0;
    double lo = s.p - kHalf, hi = s.p + kHalf;
    if (s.tcl->chainageRangeForCalc(c0, c1)) { lo = std::max(lo, c0); hi = std::min(hi, c1); }
    if (hi - lo < 0.05) return false;
    const double a0 = s.tcl->getAzimuthForCalc(lo);
    const double a1 = s.tcl->getAzimuthForCalc(hi);
    const double d  = std::atan2(std::sin(a1 - a0), std::cos(a1 - a0));   // 折回 [-π, π]
    return std::abs(d) / (hi - lo) > kMinCurvature;
}

QVector<double> AlignedProfileArray::autoStationChainages(double start, double end) const
{
    constexpr double kStraightStep = 3.0;    // 直線段目標間距 [m]
    constexpr double kCurveStep    = 1.5;    // 曲線段（圓弧＋緩和曲線）目標間距 [m]
    constexpr double kMinZone      = 0.5;    // 短於此的區間併入相鄰區間（避免曲率判定在交界附近抖動）

    QVector<double> out;
    if (end < start - kEps) return out;
    if (end - start <= kEps) { out << start; return out; }

    const QPair<double, double> key(start, end);
    auto cit = m_autoCache.constFind(key);
    if (cit != m_autoCache.constEnd()) return cit.value();

    // 1) 以固定步長判定每個取樣點是直線還是曲線（極長範圍放大步長，取樣數上限約 2 萬）
    const double len  = end - start;
    const double step = std::max(0.25, len / 20000.0);
    const int    N    = std::max(1, static_cast<int>(std::ceil(len / step)));
    auto pAt = [&](int i) { return std::min(start + i * step, end); };

    QVector<char> cls(N + 1);
    for (int i = 0; i <= N; ++i) cls[i] = isCurveAt(pAt(i)) ? 1 : 0;

    // 2) 類別改變處以二分法精確找出交界 → 區間
    struct Zone { double a, b; bool curve; };
    QVector<Zone> zones;
    double zoneStart = start;
    for (int i = 1; i <= N; ++i) {
        if (cls[i] == cls[i - 1]) continue;
        double lo = pAt(i - 1), hi = pAt(i);
        const char c0 = cls[i - 1];
        for (int it = 0; it < 24; ++it) {
            const double mid = 0.5 * (lo + hi);
            if ((isCurveAt(mid) ? 1 : 0) == c0) lo = mid; else hi = mid;
        }
        const double t = 0.5 * (lo + hi);
        zones.append({ zoneStart, t, c0 == 1 });
        zoneStart = t;
    }
    zones.append({ zoneStart, end, cls[N] == 1 });

    // 3) 過短的區間併入前一個（沒有前一個就併入後一個）
    for (int i = 0; i < zones.size() && zones.size() > 1; ) {
        if (zones[i].b - zones[i].a >= kMinZone) { ++i; continue; }
        if (i > 0) { zones[i - 1].b = zones[i].b; zones.removeAt(i); }
        else       { zones[1].a = zones[0].a;     zones.removeAt(0); }
    }

    // 4) 各區間等分：n = floor(長度 / 目標間距)（至少 1）→ 實際間距 = 長度 / n（≥ 目標間距），
    //    即「多出來的長度均分到各段」。區間起點必有測站；末站補 end。
    for (const Zone& z : zones) {
        const double zl   = z.b - z.a;
        const double tgt  = z.curve ? kCurveStep : kStraightStep;
        const int    n    = std::max(1, static_cast<int>(std::floor(zl / tgt + 1e-9)));
        const double each = zl / n;
        for (int k = 0; k < n; ++k) out.append(z.a + k * each);
    }
    out.append(end);

    m_autoCache.insert(key, out);
    return out;
}

QVector<AlignedProfileArray::StationRef> AlignedProfileArray::computeStationRefs() const {
    QVector<StationRef> refs;
    for (int si = 0; si < m_segments.size(); ++si) {
        const ProfileArraySegment& seg = m_segments[si];

        if (seg.autoInterval) {                       // 依線形自動取間距
            if (seg.end < seg.start - kEps) continue;
            for (double p : autoStationChainages(seg.start, seg.end))
                refs.append({ p, si });
            continue;
        }

        if (seg.interval <= kEps || seg.end < seg.start - kEps) continue;

        // 最後一站 = 不大於 end 的最大 interval 倍數（與單段版本相同的規則）。
        const int n = static_cast<int>(
            std::floor((seg.end - seg.start) / seg.interval + kEps)) + 1;
        for (int i = 0; i < std::max(n, 1); ++i)
            refs.append({ seg.start + i * seg.interval, si });
    }
    return refs;
}

QVector<double> AlignedProfileArray::computeStations() const {
    QVector<double> v;
    for (const StationRef& r : computeStationRefs()) v.append(r.p);
    return v;
}

QVector<double> AlignedProfileArray::stationChainages() const {
    return computeStations();
}

QVector<std::pair<QString, double>>
AlignedProfileArray::stationParameters(double p, const ProfileArraySegment& seg) const {
    QVector<std::pair<QString, double>> v;

    auto setParam = [&v](const QString& name, double value) {
        for (auto& kv : v)
            if (kv.first == name) { kv.second = value; return; }
        v.append({ name, value });
    };

    // 1) 線形資料（ALD 匯入優先，見 TrackCenterLine ForCalc 系列方法）
    // 里程超出主線形時改用相連線形的資料（見 resolveAlignment）
    const AlignmentSample as = resolveAlignment(p);
    setParam(QStringLiteral("cant"), as.tcl->getCantForCalc(as.p));
    setParam(QStringLiteral("H"),    as.tcl->getAppliedHForCalc(as.p));

    // 2) 本段的表格值：有填才覆寫；優先於線形資料。
    if (seg.hasThick)
        setParam(QStringLiteral("Thick"), seg.thick);
    if (seg.hasW)
        setParam(QStringLiteral("w"), seg.w);
    for (auto it = seg.extra.cbegin(); it != seg.extra.cend(); ++it)
        setParam(it.key(), it.value());

    return v;
}

quint64 AlignedProfileArray::masterFingerprint(Sketch* m) {
    if (!m) return 0;

    quint64 h = 1469598103934665603ULL;
    auto mix  = [&h](quint64 v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
    auto mixD = [&mix](double d) { quint64 b = 0; std::memcpy(&b, &d, sizeof b); mix(b); };
    auto mixS = [&mix](const QString& s) { mix(static_cast<quint64>(qHash(s))); };

    for (const SketchGeometry* g : m->geometriesRef()) {
        if (!g) continue;
        mixS(g->uuid);
        for (const QVector2D& q : g->points) { mixD(q.x()); mixD(q.y()); }
    }
    for (const SketchConstraint& c : m->constraints()) {
        mix(static_cast<quint64>(c.type));
        mixD(c.value);
        mixS(c.paramExpr);
        mix(c.driving ? 1u : 0u);
        for (const GeomRef& r : c.refs) mixS(r.geomUuid);
    }
    // 參數作用域鏈（master → document）的所有運算式：
    for (const aicad::core::ParameterStore* st = m->parameterStore(); st; st = st->parentStore())
        for (const QString& n : st->localNames()) { mixS(n); mixS(st->expression(n)); }
    return h ? h : 1;
}

bool AlignedProfileArray::rebuild() {
    if (m_segments.isEmpty()) { setError("No segments defined."); return false; }
    if (!m_tcl)               { setError("Track centerline not set."); return false; }

    Document* doc = document();
    if (!doc) { setError("AlignedProfileArray has no owning Document."); return false; }

    // 0. 驗證每一段，並解析各段的 profile。
    QVector<Sketch*> masters(m_segments.size(), nullptr);
    for (int si = 0; si < m_segments.size(); ++si) {
        const ProfileArraySegment& seg = m_segments[si];
        if (!seg.autoInterval && seg.interval <= kEps) {
            setError(QString("Segment '%1': interval must be positive.").arg(seg.name));
            return false;
        }
        if (seg.end < seg.start - kEps) {
            setError(QString("Segment '%1': end chainage must be >= start chainage.").arg(seg.name));
            return false;
        }
        masters[si] = resolveProfile(seg);
        if (!masters[si]) {
            setError(QString("Segment '%1': profile sketch not found.").arg(seg.name));
            return false;
        }
    }
    syncMasterConnections();
    ensureSegmentIds();

    // 線形路徑每次重建都重新判定（主線形／相連線形資料可能已變），並連接相連線形的變動訊號
    m_routeValid = false;
    m_autoCache.clear();
    syncRouteConnections();
    QStringList outOfRangeStations;

    // 1. 測站清單（先依段、段內依里程）。
    const QVector<StationRef> stations = computeStationRefs();

    // 一次建立／刪除上百個測站節點時，FeatureTree 只在結束時重整一次。
    TreeBatch treeBatch(doc);

    // 2. 依「位置索引」diff 既有 instance。
    while (m_instances.size() > stations.size()) {
        SketchInstance* inst = m_instances.takeLast();
        Plane* pl = m_planes.isEmpty() ? nullptr : m_planes.takeLast();
        if (inst) doc->removeFeature(inst);
        if (pl)   PlaneManager::instance()->deletePlane(pl);
    }

    // 各段內的測站序號（命名用）
    QVector<int> idxInSeg(stations.size(), 0);
    {
        QVector<int> counter(m_segments.size(), 0);
        for (int i = 0; i < stations.size(); ++i)
            idxInSeg[i] = counter[stations[i].seg]++;
    }
    auto stationName = [&](int i) {
        return QString("%1_%2_Station%3")
            .arg(name(), m_segments[stations[i].seg].name)
            .arg(idxInSeg[i]);
    };

    // 既有位置：若該位置現在屬於「另一個 profile」，instance 的 master 固定在建立時，
    // 無法就地更換 → 移除舊 instance，下面補建新的（平面沿用）。
    for (int i = 0; i < m_instances.size(); ++i) {
        SketchInstance* inst = m_instances[i];
        if (inst && inst->masterSketch() != masters[stations[i].seg]) {
            doc->removeFeature(inst);
            m_instances[i] = nullptr;
        }
    }

    while (m_instances.size() < stations.size()) {
        const int idx = m_instances.size();
        Plane* pl = PlaneManager::instance()->createPlane(
            QVector3D(0, 0, 0), QVector3D(0, 0, 1), QVector3D(1, 0, 0),
            stationName(idx));
        m_planes.append(pl);
        m_instances.append(nullptr);
    }
    for (int i = 0; i < stations.size(); ++i) {
        if (m_instances[i]) continue;

        // rebuildNow=false：下面迴圈會以正確的參數與放置座標系建構，
        // 這裡先 rebuild 一次只是白白多解算一次。
        SketchInstance* inst = doc->createSketchInstance(
            masters[stations[i].seg], m_planes[i], {}, stationName(i), /*rebuildNow=*/false);
        if (inst) {
            inst->setFeatureParent(this);
            inst->setScale(m_masterUnitScale);
            // 重建改由本陣列統一驅動（master 訊號／參數重算／相依傳播不再各自重解）。
            inst->setExternallyDriven(true);
            doc->setTreeItemParent(inst->id(), id());
        }
        m_instances[i] = inst;
    }

    // 名稱跟著段名／序號走（段改名、段長度改變造成序號位移時）
    for (int i = 0; i < stations.size(); ++i) {
        SketchInstance* inst = m_instances[i];
        if (!inst) continue;
        const QString nm = stationName(i);
        if (inst->name() != nm) {
            inst->setName(nm);
            doc->setTreeItemName(inst->id(), nm);
        }
    }

    m_stationSeg.resize(stations.size());
    for (int i = 0; i < stations.size(); ++i) m_stationSeg[i] = stations[i].seg;

    // 新建／換段的測站先掛到各自分段的 Loft（Loft 尚未建立者暫掛陣列，
    // 由 syncSegmentLofts() 稍後補掛）
    reparentInstances();

    // 3. 逐站：放置座標系 + 參數 + 重建
    //
    //   profile 沒變 → 各站 2D 解可重用（簽章相同者只重建 3D wire）；
    //   profile 變了 → 該 profile 的測站全部重解，但「簽章相同」的測站（例如
    //   cant/H/Thick 相同的直線段、圓曲線段）只解第一站，其餘直接複製其解。
    QHash<Sketch*, quint64> fpOf;               // 本輪各 profile 的指紋
    QHash<QString, quint64> newFp;
    QHash<QString, SketchInstance*> donors;     // 簽章 → 本輪已求解的測站
    QStringList failedStations;                 // 本輪重建失敗的測站名稱

    for (int i = 0; i < stations.size(); ++i) {
        const double p = stations[i].p;
        const ProfileArraySegment& seg = m_segments[stations[i].seg];
        Sketch* master = masters[stations[i].seg];
        SketchInstance* inst = m_instances[i];
        Plane* pl = m_planes[i];
        if (!inst || !pl) continue;

        if (!fpOf.contains(master)) {
            const quint64 f = masterFingerprint(master);
            fpOf.insert(master, f);
            newFp.insert(seg.profileId, f);
        }
        const bool force = (fpOf.value(master) != m_lastMasterFp.value(seg.profileId, 0));

        // ── 放置座標系（全程 double）──────────────────────────────────────
        //   X  = 線形右手邊（面向里程增加方向）的水平法線方向
        //   -Z = 縱坡（含坡度）的切線方向  ⇒  Z = -T
        //   Y  = Z × X（平坦時 Y = 世界 +Z，朝上）
        // 原點 = 中心線上里程 p 的點（w 是 profile 內的參數，與位置無關）；高程取中心線高程。
        // 不套用 cant 旋轉：cant/H/Thick 只當作 sketch instance 內部束制求解用的參數。
        // 里程超出主線形範圍時，改取端點相連的線形（見 resolveAlignment）
        const AlignmentSample as = resolveAlignment(p);
        if (as.outOfRange)
            outOfRangeStations << QString::number(p, 'f', 3);
        const railway::TrackCenterLine* T = as.tcl;
        const double  pc  = as.p;
        const QPointF xy  = T->getXYForCalc(pc, 0.0);
        const double  z0  = T->getElevationForCalc(pc);
        const double  az  = T->getAzimuthForCalc(pc);              // [rad], CW from N
        const double  g   = T->getSlopeForCalc(pc) / 100.0;        // dz/dp

        const double sA = std::sin(az), cA = std::cos(az);
        const double tn = std::sqrt(1.0 + g * g);

        // 3D 切線 T（朝里程增加）= (sinA, cosA, g) / |.|
        const double tx = sA / tn, ty = cA / tn, tz = g / tn;
        // 右手邊水平法線
        const double xx =  cA, xy_ = -sA, xz = 0.0;
        // Z = -T
        const double zx = -tx, zy = -ty, zz = -tz;
        // Y = Z × X
        const double yx = zy * xz - zz * xy_;
        const double yy = zz * xx - zx * xz;
        const double yz = zx * xy_ - zy * xx;

        SketchInstance::PlacementFrame fr;
        fr.ox = xy.x(); fr.oy = xy.y(); fr.oz = z0;
        fr.xx = xx; fr.xy = xy_; fr.xz = xz;
        fr.yx = yx; fr.yy = yy; fr.yz = yz;
        fr.valid = true;
        inst->setFrame(fr);

        // Plane 僅供 UI 顯示／相容（float 精度）；數值沒變就不要重設，
        // 避免每次都發出 geometryChanged 觸發畫面更新。
        {
            const QVector3D o(static_cast<float>(fr.ox), static_cast<float>(fr.oy), static_cast<float>(fr.oz));
            const QVector3D n(static_cast<float>(zx), static_cast<float>(zy), static_cast<float>(zz));
            const QVector3D xa(static_cast<float>(xx), static_cast<float>(xy_), static_cast<float>(xz));
            auto far_ = [](const QVector3D& a, const QVector3D& b) {
                return (a - b).lengthSquared() > 1e-10f;
            };
            if (far_(pl->origin(), o) || far_(pl->normal(), n) || far_(pl->xAxis(), xa))
                pl->setCoordinateSystem(o, n, xa);
        }

        // ── 參數與簽章 ───────────────────────────────────────────────────
        // 簽章含 profile id：不同 profile 的測站絕不共用解。Thick／w／自訂欄位都在 params 內。
        const auto params = stationParameters(p, seg);
        QString sig = seg.profileId + QLatin1Char('|');
        for (const auto& kv : params)
            sig += kv.first + QLatin1Char('=') + QString::number(kv.second, 'g', 12) + QLatin1Char(';');

        inst->setScale(m_masterUnitScale);
        if (force || inst->solveSignature() != sig) {
            // setLocal() 每次都會 Q_EMIT parametersRecomputed()；擋掉，改由下面
            // rebuildWith() 單次驅動。
            const QSignalBlocker blocker(inst->parameterStore());

            // 先清掉「這次不再覆寫」的舊本地參數（例如表格把 Thick 清成空白）。
            QSet<QString> keep;
            for (const auto& kv : params) keep.insert(kv.first);
            for (const QString& old : inst->parameterStore()->localNames())
                if (!keep.contains(old)) inst->parameterStore()->removeLocal(old);

            for (const auto& kv : params)
                inst->parameterStore()->setLocal(kv.first, kv.second);
        }

        SketchInstance* donor = donors.value(sig, nullptr);
        // 舊版忽略回傳值：求解失敗的測站保留舊形狀，卻回報整體成功，使用者
        // 看到的就是「套用後圖形沒有更新」。改為彙總失敗測站並回報。
        if (!inst->rebuildWith(sig, donor, force))
            failedStations << inst->name();
        else if (inst->solveFellBack())
            failedStations << (inst->name() + QStringLiteral(" (solver conflict)"));
        if (!donor && !inst->solveSignature().isEmpty())
            donors.insert(sig, inst);
    }

    m_lastMasterFp = newFp;

    QStringList problems;
    if (!outOfRangeStations.isEmpty()) {
        // 找不到相連線形可接：舊版靜默外推，座標不是真實線形 → 明確回報
        QString list = outOfRangeStations.mid(0, 5).join(QStringLiteral(", "));
        if (outOfRangeStations.size() > 5) list += QStringLiteral(" ...");
        problems << QString("%1 station(s) are outside the alignment range and no connected "
                            "alignment (matching E,N,Z) was found; geometry is extrapolated: %2")
                        .arg(outOfRangeStations.size()).arg(list);
    }
    if (!failedStations.isEmpty()) {
        QString list = failedStations.mid(0, 5).join(QStringLiteral(", "));
        if (failedStations.size() > 5) list += QStringLiteral(" ...");
        problems << QString("%1 station(s) failed to rebuild (previous or unsolved master shape kept): %2")
                        .arg(failedStations.size()).arg(list);
    }
    if (problems.isEmpty()) clearError();
    else                    setError(problems.join(QLatin1Char('\n')));
    doc->setTreeItemName(id(), QString("%1 (%2 segments, %3 stations)")
                                   .arg(name()).arg(m_segments.size()).arg(m_instances.size()));
    Q_EMIT shapeChanged();
    return problems.isEmpty();
}

void AlignedProfileArray::clearInstances() {
    Document* doc = document();
    for (int i = 0; i < m_instances.size(); ++i) {
        SketchInstance* inst = m_instances[i];
        Plane* pl = i < m_planes.size() ? m_planes[i] : nullptr;
        if (inst && doc) doc->removeFeature(inst);
        if (pl) PlaneManager::instance()->deletePlane(pl);
    }
    m_instances.clear();
    m_planes.clear();
    m_stationSeg.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// stationWireLoops（供 ProfileLoftSolid 使用，§6.3）
// ─────────────────────────────────────────────────────────────────────────────

QVector<QList<TopoDS_Wire>> AlignedProfileArray::stationWireLoops() const {
    QVector<QList<TopoDS_Wire>> loops;
    loops.reserve(m_instances.size());
    for (SketchInstance* inst : m_instances) {
        loops.append(inst ? inst->allClosedWires() : QList<TopoDS_Wire>());
    }
    return loops;
}

// ─────────────────────────────────────────────────────────────────────────────
// 依賴 / 序列化
// ─────────────────────────────────────────────────────────────────────────────

QSet<QString> AlignedProfileArray::featureDependencies() const {
    QSet<QString> deps;
    for (const ProfileArraySegment& seg : m_segments)
        if (!seg.profileId.isEmpty() && resolveProfile(seg))
            deps.insert(seg.profileId);
    // TrackCenterLine 不是 Feature，不登記進 Feature 依賴圖；
    // 其變更改由 dataChanged 訊號直接連到 rebuild()（見 setTrackCenterLine）。
    return deps;
}

QJsonObject AlignedProfileArray::toJson() const {
    QJsonObject obj = Feature::toJson();
    obj["type"]              = typeString();
    obj["trackCenterLineId"] = trackCenterLineId();
    obj["masterUnitScale"]   = m_masterUnitScale;

    QJsonArray segArr;
    for (const ProfileArraySegment& s : m_segments) {
        QJsonObject o;
        o["id"]        = s.id;
        o["name"]      = s.name;
        o["start"]     = s.start;
        o["end"]       = s.end;
        o["interval"]  = s.interval;
        o["autoInterval"] = s.autoInterval;     // 缺 = false（舊檔：固定間距）
        o["endChamfer"]     = s.endChamfer;
        o["endChamferSize"] = s.endChamferSize;
        o["profileId"] = s.profileId;
        if (s.hasThick) o["thick"] = s.thick;     // 缺 = 空白 = 不覆寫
        if (s.hasW) o["wValue"] = s.w;            // 缺 = 空白 = 不覆寫
        QJsonObject ex;
        for (auto it = s.extra.cbegin(); it != s.extra.cend(); ++it)
            ex[it.key()] = it.value();
        o["extra"] = ex;
        segArr.append(o);
    }
    obj["segments"]     = segArr;
    obj["extraColumns"] = QJsonArray::fromStringList(m_extraColumns);

    // 舊版欄位（第一段）：讓舊版程式讀新檔時仍能建出第一段，不至於整個失敗。
    if (!m_segments.isEmpty()) {
        const ProfileArraySegment& f = m_segments.first();
        obj["masterSketchId"] = f.profileId;
        obj["startChainage"]  = f.start;
        obj["endChainage"]    = f.end;
        obj["interval"]       = f.interval;
    }
    // 刻意不存子 SketchInstance、不存 cant/H：兩者皆於 rebuild() 從 TCL 即時算出。
    return obj;
}

bool AlignedProfileArray::fromJson(const QJsonObject& json) {
    Feature::fromJson(json);
    m_pendingTclId    = json["trackCenterLineId"].toString();
    m_masterUnitScale = json.contains("masterUnitScale")
        ? json["masterUnitScale"].toDouble(0.001) : 0.001;

    m_segments.clear();
    m_extraColumns.clear();

    if (json.contains("segments")) {
        for (const QJsonValue& v : json["segments"].toArray()) {
            const QJsonObject o = v.toObject();
            ProfileArraySegment s;
            s.id        = o["id"].toString();      // 舊檔沒有 → 載入後由 ensureSegmentIds() 配發
            s.name      = o["name"].toString();
            s.start     = o["start"].toDouble(0.0);
            s.end       = o["end"].toDouble(0.0);
            s.interval  = o["interval"].toDouble(20.0);
            s.autoInterval = o["autoInterval"].toBool(false);
            s.endChamfer     = o["endChamfer"].toBool(true);          // 缺 = 預設要倒角
            s.endChamferSize = o["endChamferSize"].toDouble(0.025);
            s.profileId = o["profileId"].toString();
            s.hasThick  = o.contains("thick");
            s.thick     = s.hasThick ? o["thick"].toDouble(0.0) : 0.0;
            s.hasW      = o.contains("wValue");
            s.w         = s.hasW ? o["wValue"].toDouble(0.0) : 0.0;
            const QJsonObject ex = o["extra"].toObject();
            for (auto it = ex.constBegin(); it != ex.constEnd(); ++it)
                s.extra.insert(it.key(), it.value().toDouble(0.0));
            if (s.name.isEmpty()) s.name = QString("Seg%1").arg(m_segments.size() + 1);
            m_segments.append(s);
        }
        for (const QJsonValue& v : json["extraColumns"].toArray())
            m_extraColumns.append(v.toString());
    } else {
        // 舊檔（單段）：單一 master / 起訖 / 間距 → 轉成一列。
        ProfileArraySegment s;
        s.name      = QStringLiteral("Seg1");
        s.profileId = json["masterSketchId"].toString();
        s.start     = json["startChainage"].toDouble(0.0);
        s.end       = json["endChainage"].toDouble(0.0);
        s.interval  = json["interval"].toDouble(20.0);
        m_segments.append(s);
    }
    ensureSegmentIds();
    return true;
}

void AlignedProfileArray::resolveReferences(Document* doc) {
    if (!doc) return;

    // profile 以 id 字串保存、在需要時才解析；這裡只負責確認並接上訊號。
    for (const ProfileArraySegment& seg : m_segments) {
        if (!resolveProfile(seg))
            qWarning() << "[AlignedProfileArray]" << name()
                       << "Cannot resolve profile of segment" << seg.name << seg.profileId;
    }
    syncMasterConnections();
    doc->refreshFeatureDependencies(this);

    if (!m_pendingTclId.isEmpty()) {
        for (railway::TrackCenterLine* tcl : doc->trackCenterLines()) {
            if (tcl && tcl->id() == m_pendingTclId) {
                setTrackCenterLine(tcl);
                m_pendingTclId.clear();
                break;
            }
        }
        if (!m_pendingTclId.isEmpty()) {
            qWarning() << "[AlignedProfileArray]" << name()
                       << "Cannot resolve trackCenterLineId:" << m_pendingTclId;
        }
    }
}

} // namespace cad
} // namespace aicad
