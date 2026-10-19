#include "AlignedProfileArray.h"
#include "Document.h"
#include "Sketch.h"
#include "Plane.h"
#include "PlaneManager.h"
#include "SketchInstance.h"
#include "railway/RailwayAlignment.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalBlocker>
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

void AlignedProfileArray::setSegments(const QList<ProfileArraySegment>& segs,
                                      const QStringList& extraColumns)
{
    m_segments     = segs;
    m_extraColumns = extraColumns;
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
    return errorMessage().isEmpty();
}

QString AlignedProfileArray::uniqueSegmentName(const QString& base) const {
    QSet<QString> used;
    for (const ProfileArraySegment& s : m_segments) used.insert(s.name);
    for (int i = 1; ; ++i) {
        const QString n = base + QString::number(i);
        if (!used.contains(n)) return n;
    }
}

void AlignedProfileArray::setRange(double startChainage, double endChainage, double interval) {
    if (m_segments.isEmpty()) {
        ProfileArraySegment seg;
        seg.name = QStringLiteral("Seg1");
        m_segments.append(seg);
    }
    m_segments[0].start    = startChainage;
    m_segments[0].end      = endChainage;
    m_segments[0].interval = interval;
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

QVector<AlignedProfileArray::StationRef> AlignedProfileArray::computeStationRefs() const {
    QVector<StationRef> refs;
    for (int si = 0; si < m_segments.size(); ++si) {
        const ProfileArraySegment& seg = m_segments[si];
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
    setParam(QStringLiteral("cant"), m_tcl->getCantForCalc(p));
    setParam(QStringLiteral("H"),    m_tcl->getAppliedHForCalc(p));

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
        if (seg.interval <= kEps) {
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

    // 3. 逐站：放置座標系 + 參數 + 重建
    //
    //   profile 沒變 → 各站 2D 解可重用（簽章相同者只重建 3D wire）；
    //   profile 變了 → 該 profile 的測站全部重解，但「簽章相同」的測站（例如
    //   cant/H/Thick 相同的直線段、圓曲線段）只解第一站，其餘直接複製其解。
    QHash<Sketch*, quint64> fpOf;               // 本輪各 profile 的指紋
    QHash<QString, quint64> newFp;
    QHash<QString, SketchInstance*> donors;     // 簽章 → 本輪已求解的測站

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
        const QPointF xy  = m_tcl->getXYForCalc(p, 0.0);
        const double  z0  = m_tcl->getElevationForCalc(p);
        const double  az  = m_tcl->getAzimuthForCalc(p);              // [rad], CW from N
        const double  g   = m_tcl->getSlopeForCalc(p) / 100.0;        // dz/dp

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
        inst->rebuildWith(sig, donor, force);
        if (!donor && !inst->solveSignature().isEmpty())
            donors.insert(sig, inst);
    }

    m_lastMasterFp = newFp;

    clearError();
    doc->setTreeItemName(id(), QString("%1 (%2 segments, %3 stations)")
                                   .arg(name()).arg(m_segments.size()).arg(m_instances.size()));
    Q_EMIT shapeChanged();
    return true;
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
        o["name"]      = s.name;
        o["start"]     = s.start;
        o["end"]       = s.end;
        o["interval"]  = s.interval;
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
            s.name      = o["name"].toString();
            s.start     = o["start"].toDouble(0.0);
            s.end       = o["end"].toDouble(0.0);
            s.interval  = o["interval"].toDouble(20.0);
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
