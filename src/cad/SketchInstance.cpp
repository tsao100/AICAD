#include "SketchInstance.h"
#include "Document.h"
#include "PlaneManager.h"
#include "../core/ParameterStore.h"
#include <algorithm>

#include <QDebug>
#include <QJsonArray>
#include <QUuid>

#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <gp_Pnt.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <gp_Dir.hxx>
#include <Standard_Failure.hxx>
#include <Precision.hxx>
#include <cmath>
#include <functional>

namespace aicad::cad {

namespace {
/**
 * @brief 求解後任何一個幾何點的座標若非有限值（NaN/Inf），代表求解器已經
 *        發散（例如反覆對同一個 instance 連續 solve、每次都拿上一次壞掉的
 *        解當初始猜測）。這種幾何若被 buildWires()/setShape() 用來組出
 *        TopoDS 形狀，OCCT 在稍後 tessellate／顯示這個形狀時可能直接
 *        丟出未捕捉的例外讓整個程式當掉，而不是像 SolveStatus::Conflict
 *        那樣被溫和地擋下來——所以這裡要在建 wire 之前就先擋。
 */
bool allGeometryFinite(const QList<SketchGeometry*>& geoms) {
    for (const SketchGeometry* g : geoms) {
        if (!g) continue;
        for (const QVector2D& p : g->points) {
            if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
                return false;
        }
    }
    return true;
}

/**
 * @brief 完整深拷貝一組幾何（含所有 UUID 參照的重映射）。
 *
 * 舊版 cloneGeometries() 的缺陷（本函式修正的對象）：
 *   - SketchPoint 沒有 case，掉進 default 分支，被複製成「基底類別
 *     SketchGeometry」物件。求解器 packVariables() 卻對它 static_cast<SketchPoint*>
 *     讀取 pos 欄位 —— 讀到基底物件之外的記憶體（未定義行為），這就是「同樣資料
 *     有時收斂、有時 CONFLICT」的真正原因。
 *   - Line 的 startUuid/endUuid、Circle/Arc/Ellipse 的 centerUuid、Arc 的
 *     start/endUuid、Polyline 的 vertexUuids、Spline 的 controlPointUuids 全部
 *     沒有複製（空字串），所以線段與端點 SketchPoint 完全沒有連動。
 *   - Arc 與 master 共用同一個 Geom_TrimmedCurve handle。
 *
 * @param src    來源幾何（master 的，或另一個已求解 instance 的）
 * @param remap  輸出：來源 uuid → 新 uuid
 */
QList<SketchGeometry*> cloneGeometryList(const QList<SketchGeometry*>& src,
                                         QHash<QString, QString>& remap)
{
    QList<SketchGeometry*> out;
    QList<const SketchGeometry*> origs;
    out.reserve(src.size());
    origs.reserve(src.size());
    remap.reserve(src.size());

    // pass 1：建立副本（建構子自己會產生新 UUID，直接沿用，不另外再生成一次）
    for (const SketchGeometry* o : src) {
        if (!o) continue;
        SketchGeometry* c = nullptr;

        if (auto* g = dynamic_cast<const SketchPoint*>(o)) {
            c = new SketchPoint(g->pos, g->origin, g->role);
        } else if (auto* g = dynamic_cast<const SketchLine*>(o)) {
            c = new SketchLine(g->start, g->end, g->role);
        } else if (auto* g = dynamic_cast<const SketchArc*>(o)) {
            Handle(Geom_TrimmedCurve) cv;
            if (!g->curve.IsNull())
                cv = Handle(Geom_TrimmedCurve)::DownCast(g->curve->Copy());
            c = new SketchArc(cv, g->role);
        } else if (auto* g = dynamic_cast<const SketchCircle*>(o)) {
            c = new SketchCircle(g->center, g->radius, g->role);
        } else if (auto* g = dynamic_cast<const SketchEllipse*>(o)) {
            c = new SketchEllipse(g->center, g->majorRadius, g->minorRadius,
                                  g->angle, g->role);
        } else if (auto* g = dynamic_cast<const SketchSpline*>(o)) {
            c = new SketchSpline(g->points, g->role);
        } else if (auto* g = dynamic_cast<const SketchPolyline*>(o)) {
            c = new SketchPolyline(g->points, g->closed, g->role);
        } else if (auto* g = dynamic_cast<const SketchPolygon*>(o)) {
            c = new SketchPolygon(g->points, g->closed, g->role);
        } else {
            c = new SketchGeometry(o->type, o->role);
        }

        c->points = o->points;          // 所有型別一律保留點集（Point 的 points[0]==pos）
        remap.insert(o->uuid, c->uuid);
        out.append(c);
        origs.append(o);
    }

    // pass 2：重映射 UUID 參照（空字串 / 找不到者維持原值）
    auto R = [&remap](const QString& u) { return remap.value(u, u); };
    for (int i = 0; i < out.size(); ++i) {
        const SketchGeometry* o = origs[i];
        SketchGeometry*       c = out[i];

        if (auto* g = dynamic_cast<const SketchLine*>(o)) {
            auto* d = static_cast<SketchLine*>(c);
            d->startUuid = R(g->startUuid);
            d->endUuid   = R(g->endUuid);
        } else if (auto* g = dynamic_cast<const SketchArc*>(o)) {
            auto* d = static_cast<SketchArc*>(c);
            d->startUuid  = R(g->startUuid);
            d->endUuid    = R(g->endUuid);
            d->centerUuid = R(g->centerUuid);
        } else if (auto* g = dynamic_cast<const SketchCircle*>(o)) {
            static_cast<SketchCircle*>(c)->centerUuid = R(g->centerUuid);
        } else if (auto* g = dynamic_cast<const SketchEllipse*>(o)) {
            static_cast<SketchEllipse*>(c)->centerUuid = R(g->centerUuid);
        } else if (auto* g = dynamic_cast<const SketchSpline*>(o)) {
            auto* d = static_cast<SketchSpline*>(c);
            d->controlPointUuids.clear();
            for (const QString& u : g->controlPointUuids) d->controlPointUuids.append(R(u));
        } else if (auto* g = dynamic_cast<const SketchPolyline*>(o)) {
            auto* d = static_cast<SketchPolyline*>(c);
            d->vertexUuids.clear();
            for (const QString& u : g->vertexUuids) d->vertexUuids.append(R(u));
        }
    }
    return out;
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// 構造 / 析構
// ─────────────────────────────────────────────────────────────────────────────

SketchInstance::SketchInstance(Document* parent)
    : Feature(parent)
{
    m_instanceStore = new aicad::core::ParameterStore(this);

    // instance store 參數變更 → 副本重建
    connect(m_instanceStore, &aicad::core::ParameterStore::parametersRecomputed,
            this, &SketchInstance::rebuild);
}

SketchInstance::~SketchInstance() {
    // 斷開 master 訊號連接
    if (m_master) {
        disconnect(m_masterRebuildConn);
        disconnect(m_masterDestroyConn);
    }
    qDeleteAll(m_geomClones);
}

// ─────────────────────────────────────────────────────────────────────────────
// Master 草圖設定
// ─────────────────────────────────────────────────────────────────────────────

void SketchInstance::setMasterSketch(Sketch* master) {
    // 斷開舊連接
    if (m_master) {
        disconnect(m_masterRebuildConn);
        disconnect(m_masterDestroyConn);
    }

    m_master = master;

    if (m_master) {
        // instance store → sketch store → global store
        m_instanceStore->setParentStore(m_master->parameterStore());

        // master 重建 → 副本重建
        m_masterRebuildConn = connect(
            m_master, &Sketch::shapeChanged,
            this, &SketchInstance::onMasterRebuilt);

        // master 刪除警告
        m_masterDestroyConn = connect(
            m_master, &QObject::destroyed,
            this, &SketchInstance::onMasterAboutToDestroy);
    }
}

QString SketchInstance::masterSketchId() const {
    return m_master ? m_master->id() : m_pendingMasterSketchId;
}

// ─────────────────────────────────────────────────────────────────────────────
// 平面設定
// ─────────────────────────────────────────────────────────────────────────────

void SketchInstance::setPlane(Plane* plane) {
    m_plane = plane;
}

// ─────────────────────────────────────────────────────────────────────────────
// Override 管理
// ─────────────────────────────────────────────────────────────────────────────

QStringList SketchInstance::overriddenParams() const {
    return m_instanceStore->localNames();
}

void SketchInstance::clearOverride(const QString& paramName) {
    m_instanceStore->removeLocal(paramName);
    Q_EMIT overrideChanged(paramName);
}

void SketchInstance::clearAllOverrides() {
    for (const QString& n : m_instanceStore->localNames())
        m_instanceStore->removeLocal(n);
    rebuild();
}

bool SketchInstance::isOverridden(const QString& paramName) const {
    return m_instanceStore->hasLocal(paramName);
}

// ─────────────────────────────────────────────────────────────────────────────
// 幾何克隆輔助
// ─────────────────────────────────────────────────────────────────────────────

QList<SketchGeometry*> SketchInstance::cloneGeometries(
        const Sketch* master,
        QHash<QString, QString>& uuidRemap) const
{
    return cloneGeometryList(master->geometriesRef(), uuidRemap);
}

QList<SketchConstraint> SketchInstance::cloneConstraints(
        const Sketch* master,
        const QHash<QString, QString>& uuidRemap) const
{
    // ✅ 整個 struct 複製，只換 uuid 與重映射 refs。
    // 舊版只手動複製 type/value/paramExpr/driving/refs，漏掉 value2、fixedAbsolute、
    // distMode、distSideSign/DirX/DirY、dimLineOffset、implicitOf…：
    //   - PointToLine / LineToLine 距離退化成兩點距離（Thick、d7~d13 形狀全錯）
    //   - 距離的「在哪一側」記憶（distSide*）遺失，數值經過 0 時翻面
    //   - Fixed 約束退化成「以當下幾何重新取樣」，產生漂移
    QList<SketchConstraint> clones;
    const QList<SketchConstraint>& src = master->constraints();
    clones.reserve(src.size());

    for (const SketchConstraint& orig : src) {
        SketchConstraint c = orig;
        c.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        for (GeomRef& ref : c.refs)
            ref.geomUuid = uuidRemap.value(ref.geomUuid, ref.geomUuid);
        clones.append(c);
    }
    return clones;
}

// ─────────────────────────────────────────────────────────────────────────────
// Wire 建立
// ─────────────────────────────────────────────────────────────────────────────

gp_Pnt SketchInstance::toWorld(double u, double v) const {
    if (m_frame.valid) {
        return gp_Pnt(m_frame.ox + m_frame.xx * u + m_frame.yx * v,
                      m_frame.oy + m_frame.xy * u + m_frame.yy * v,
                      m_frame.oz + m_frame.xz * u + m_frame.yz * v);
    }
    const QVector3D p = m_plane->toWorld(u, v);
    return gp_Pnt(p.x(), p.y(), p.z());
}

QList<TopoDS_Wire> SketchInstance::buildWires(
        const QList<SketchGeometry*>& geoms) const
{
    QList<TopoDS_Wire> wires;
    if (!m_plane && !m_frame.valid) return wires;

    for (const SketchGeometry* geom : geoms) {
        if (!geom || geom->isConstruction()) continue;

        try {
            if (geom->type == SketchGeometryType::Line && geom->points.size() >= 2) {
                const gp_Pnt p1 = toWorld(geom->points[0].x() * m_scale, geom->points[0].y() * m_scale);
                const gp_Pnt p2 = toWorld(geom->points[1].x() * m_scale, geom->points[1].y() * m_scale);

                BRepBuilderAPI_MakeEdge edgeMk(p1, p2);
                if (!edgeMk.IsDone()) continue;

                BRepBuilderAPI_MakeWire wireMk(edgeMk.Edge());
                if (wireMk.IsDone())
                    wires.append(wireMk.Wire());

            } else if (geom->type == SketchGeometryType::Polyline && geom->points.size() >= 2) {
                const auto* pl = dynamic_cast<const SketchPolyline*>(geom);
                const bool closed = pl ? pl->closed
                                       : static_cast<const SketchPolygon*>(geom)->closed;
                BRepBuilderAPI_MakeWire wireMk;
                const int n    = geom->points.size();
                const int nSeg = closed ? n : n - 1;
                for (int i = 0; i < nSeg; ++i) {
                    const gp_Pnt p1 = toWorld(geom->points[i].x() * m_scale,
                                              geom->points[i].y() * m_scale);
                    const gp_Pnt p2 = toWorld(geom->points[(i + 1) % n].x() * m_scale,
                                              geom->points[(i + 1) % n].y() * m_scale);
                    BRepBuilderAPI_MakeEdge e(p1, p2);
                    if (e.IsDone()) wireMk.Add(e.Edge());
                }
                if (wireMk.IsDone())
                    wires.append(wireMk.Wire());
            }
        } catch (const Standard_Failure& ex) {
            qWarning() << "[SketchInstance] buildWires OCC error:" << ex.GetMessageString();
        }
    }
    return wires;
}

// ─────────────────────────────────────────────────────────────────────────────
// 求解 / 建形
// ─────────────────────────────────────────────────────────────────────────────

bool SketchInstance::solveFromMaster() {
    auto cloneAll = [this]() {
        QHash<QString, QString> remap;
        qDeleteAll(m_geomClones);
        m_geomClones       = cloneGeometries(m_master, remap);
        m_uuidRemap        = remap;
        m_constraintClones = cloneConstraints(m_master, remap);
    };

    cloneAll();

    // 以「已解析作用域」求值：instance 覆寫的 cant/H/Thick 會連帶重算 master 裡
    // 以它們推導的衍生參數（d2=if(cant>0,0,abs(cant))…）；一次解析、共用一個 JS 引擎。
    {
        const aicad::core::ParameterScope scope(*m_instanceStore);
        for (auto& c : m_constraintClones) {
            if (c.isDimensional())
                c.evaluateValue(scope);
            // 合併整合（Firefox 第 11 項 × Edge 沿線斷面）：條件啟用的約束要以
            // 「本站 instance 的覆寫值」重新判定（H/cant 每站不同，master 當下的
            // conditionOff 不能直接沿用）。無法求值時維持啟用（fail-safe）。
            if (!c.enableExpr.trimmed().isEmpty()) {
                const auto [ok, enabled] = scope.evaluateCondition(c.enableExpr);
                c.conditionOff = ok && !enabled;
            }
        }
    }
    // Sketch::activeConstraints() 只過濾 master 自己的求解；instance 直接把 clone
    // 丟給 solver，所以這裡要自行排除被條件停用的約束。
    m_constraintClones.erase(
        std::remove_if(m_constraintClones.begin(), m_constraintClones.end(),
                       [](const SketchConstraint& c) { return !c.isActive(); }),
        m_constraintClones.end());

    // 2D 解算空間 = master 草圖座標，法向用 master 平面（舊版誤用 instance 平面）。
    gp_Dir normal(0, 0, 1);
    if (const Plane* mp = m_master->plane()) {
        const QVector3D n = mp->normal();
        if (n.lengthSquared() > 1e-12f) normal = gp_Dir(n.x(), n.y(), n.z());
    }

    const SolveResult result = m_solver.solve(m_geomClones, m_constraintClones, normal);
    bool ok = (result.status != SolveStatus::Conflict) && allGeometryFinite(m_geomClones);

    if (!ok) {
        // 求解失敗時 m_geomClones 已被就地改壞，不能拿去建 wire。退回「未求解的
        // master 原始座標」：對沒用 cant/H 參數化的斷面是正確答案，否則是 cant=H=0
        // 的近似形，好過整站缺失導致 Loft 整個失敗。
        // （舊版在此還會再用新 solver 重試一次；clone 缺陷修正後失敗是確定性的，
        //   重試只是浪費一次解算。）
        qWarning() << "[SketchInstance]" << name()
                   << "solve failed (status=" << static_cast<int>(result.status)
                   << ") — using unsolved master geometry for this station.";
        cloneAll();
        ok = allGeometryFinite(m_geomClones);
    }
    return ok;
}

bool SketchInstance::adoptSolution(const SketchInstance& d) {
    if (d.m_geomClones.isEmpty()) return false;

    QHash<QString, QString> donorToMine;
    qDeleteAll(m_geomClones);
    m_geomClones = cloneGeometryList(d.m_geomClones, donorToMine);

    // master uuid → donor clone uuid → 我的 clone uuid（供 orderedWireFrom 使用）
    m_uuidRemap.clear();
    m_uuidRemap.reserve(d.m_uuidRemap.size());
    for (auto it = d.m_uuidRemap.cbegin(); it != d.m_uuidRemap.cend(); ++it)
        m_uuidRemap.insert(it.key(), donorToMine.value(it.value(), it.value()));

    // 求解後不再需要約束副本（沒有任何外部存取者），省略。
    m_constraintClones.clear();
    return true;
}

bool SketchInstance::buildShape() {
    m_wires = buildWires(m_geomClones);

    if (!m_wires.isEmpty()) {
        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        for (const TopoDS_Wire& w : m_wires)
            builder.Add(compound, w);
        setShape(compound);          // setShape() 本身已發出 shapeChanged()
    } else {
        setShape(TopoDS_Shape());
    }

    clearError();
    Q_EMIT instanceRebuilt();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// rebuild() 核心
// ─────────────────────────────────────────────────────────────────────────────

bool SketchInstance::rebuild() {
    // 由 AlignedProfileArray 統一驅動時，自動觸發（master 訊號／參數重算／相依傳播）
    // 一律略過，避免同一次陣列重建中每站被解算 3~4 次。
    if (m_externallyDriven) return true;

    if (!m_master) {
        setError("Master sketch not set.");
        return false;
    }
    if (!m_plane && !m_frame.valid) {
        setError("Plane not set for SketchInstance.");
        return false;
    }

    if (!solveFromMaster()) {
        setError("Solver diverged to non-finite coordinates in SketchInstance.");
        return false;
    }
    m_solveSig.clear();    // 手動重建：簽章未知，下次 rebuildWith() 不可誤用
    return buildShape();
}

bool SketchInstance::rebuildWith(const QString& signature,
                                 const SketchInstance* donor,
                                 bool forceResolve)
{
    if (!m_master) { setError("Master sketch not set."); return false; }
    if (!m_plane && !m_frame.valid) { setError("Plane not set for SketchInstance."); return false; }

    const bool haveSolution = !m_geomClones.isEmpty() && !m_solveSig.isEmpty();

    if (!forceResolve && haveSolution && m_solveSig == signature) {
        // 只有放置位置改變（2D 斷面不變）→ 跳過求解，只重建 3D wire。
    } else if (donor && donor != this && donor->m_solveSig == signature
               && !donor->m_geomClones.isEmpty() && adoptSolution(*donor)) {
        m_solveSig = signature;
    } else {
        if (!solveFromMaster()) {
            m_solveSig.clear();
            setError("Solver diverged to non-finite coordinates in SketchInstance.");
            return false;
        }
        m_solveSig = signature;
    }
    return buildShape();
}

// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// hasClosedProfile
// ─────────────────────────────────────────────────────────────────────────────

bool SketchInstance::hasClosedProfile() const {
    for (const TopoDS_Wire& w : m_wires)
        if (w.Closed()) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// orderedWireFrom / allClosedWires — 封閉 wire 走訪（供 Loft 使用）
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// 2D 座標比對容差（草圖單位，通常為 mm）。
constexpr double kWirePosTol = 1e-5;

/// 有向線段：from → to。
struct DirSegment {
    QVector2D from;
    QVector2D to;
};

bool nearlyEqual(const QVector2D& a, const QVector2D& b) {
    return (a - b).lengthSquared() < (kWirePosTol * kWirePosTol);
}

/// 收集所有有向線段（支援範圍與 buildWires() 一致：Line + 封閉 Polyline）。
/// 順序 = m_geomClones 的原始順序，master 每次 clone 出來的順序都相同，
/// 這是後面「用第一個未走訪線段當迴圈起點」能在各測站間保持一致的關鍵。
QList<DirSegment> collectSegments(const QList<SketchGeometry*>& geoms) {
    QList<DirSegment> segments;
    for (const SketchGeometry* geom : geoms) {
        if (!geom || geom->isConstruction()) continue;

        if (geom->type == SketchGeometryType::Line && geom->points.size() >= 2) {
            segments.append({ geom->points[0], geom->points[1] });

        } else if (geom->type == SketchGeometryType::Polyline && geom->points.size() >= 2) {
            const auto* pl = dynamic_cast<const SketchPolyline*>(geom);
            const bool closed = pl ? pl->closed
                                   : static_cast<const SketchPolygon*>(geom)->closed;
            int n = geom->points.size();
            int nSeg = closed ? n : n - 1;
            for (int i = 0; i < nSeg; ++i)
                segments.append({ geom->points[i], geom->points[(i + 1) % n] });
        }
    }
    return segments;
}

/// 從 startPos 出發走一圈：每步優先選「from == 目前點」的未使用線段（維持
/// 原方向），找不到才退而求其次選「to == 目前點」的線段並反向使用。
/// 走回 startPos 就算成功；`used` 由呼叫端提供並會被本函式標記。
/// 回傳空 list 代表走不出封閉迴圈（開放輪廓）。
QList<DirSegment> walkLoopFrom(const QList<DirSegment>& segments,
                                QVector<bool>& used,
                                const QVector2D& startPos) {
    QVector2D current = startPos;
    QList<DirSegment> ordered;
    const int maxSteps = segments.size() + 1;

    for (int step = 0; step < maxSteps; ++step) {
        int chosen = -1;
        bool flip = false;

        for (int i = 0; i < segments.size(); ++i) {
            if (used[i]) continue;
            if (nearlyEqual(segments[i].from, current)) { chosen = i; flip = false; break; }
        }
        if (chosen < 0) {
            for (int i = 0; i < segments.size(); ++i) {
                if (used[i]) continue;
                if (nearlyEqual(segments[i].to, current)) { chosen = i; flip = true; break; }
            }
        }
        if (chosen < 0) break;

        used[chosen] = true;
        DirSegment seg = segments[chosen];
        if (flip) std::swap(seg.from, seg.to);
        ordered.append(seg);
        current = seg.to;

        if (nearlyEqual(current, startPos))
            break;
    }

    if (ordered.isEmpty() || !nearlyEqual(current, startPos))
        return {};
    return ordered;
}

/// 依走訪順序（DirSegment list）建立 3D TopoDS_Wire。
TopoDS_Wire makeWireFromSegments(const QList<DirSegment>& ordered,
                                 const std::function<gp_Pnt(double, double)>& toWorld,
                                 double scale) {
    TopoDS_Wire result;
    if (ordered.isEmpty()) return result;
    try {
        BRepBuilderAPI_MakeWire wireMk;
        for (const DirSegment& seg : ordered) {
            const gp_Pnt p1 = toWorld(seg.from.x() * scale, seg.from.y() * scale);
            const gp_Pnt p2 = toWorld(seg.to.x()   * scale, seg.to.y()   * scale);
            BRepBuilderAPI_MakeEdge edgeMk(p1, p2);
            if (edgeMk.IsDone())
                wireMk.Add(edgeMk.Edge());
        }
        if (wireMk.IsDone())
            result = wireMk.Wire();
    } catch (const Standard_Failure& ex) {
        qWarning() << "[SketchInstance] makeWireFromSegments OCC error:" << ex.GetMessageString();
    }
    return result;
}

} // namespace

TopoDS_Wire SketchInstance::orderedWireFrom(const QString& masterPointUuid) const {
    TopoDS_Wire result;
    if ((!m_plane && !m_frame.valid) || masterPointUuid.isEmpty())
        return result;

    // 1. master 起點 UUID → 本副本 clone UUID → 起點座標
    QString cloneUuid = m_uuidRemap.value(masterPointUuid);
    if (cloneUuid.isEmpty()) {
        qWarning() << "[SketchInstance] orderedWireFrom: no clone mapping for"
                   << masterPointUuid;
        return result;
    }

    QVector2D startPos;
    bool foundStart = false;
    for (const SketchGeometry* g : m_geomClones) {
        if (g && g->uuid == cloneUuid && !g->points.isEmpty()) {
            startPos = g->points[0];
            foundStart = true;
            break;
        }
    }
    if (!foundStart) {
        qWarning() << "[SketchInstance] orderedWireFrom: clone geometry not found for"
                   << cloneUuid;
        return result;
    }

    const QList<DirSegment> segments = collectSegments(m_geomClones);
    if (segments.isEmpty())
        return result;

    QVector<bool> used(segments.size(), false);
    const QList<DirSegment> ordered = walkLoopFrom(segments, used, startPos);
    if (ordered.isEmpty()) {
        qWarning() << "[SketchInstance] orderedWireFrom: profile not closed from given start point";
        return result;
    }

    return makeWireFromSegments(
        ordered, [this](double u, double v) { return toWorld(u, v); }, m_scale);
}

QList<TopoDS_Wire> SketchInstance::allClosedWires() const {
    QList<TopoDS_Wire> result;
    if (!m_plane && !m_frame.valid) return result;

    // 支援一個草圖裡有多個互不相連的封閉輪廓（例如左右兩個獨立的墊塊斷面）：
    // 依 m_geomClones 的原始順序掃描，每碰到一個還沒被用過的線段，就以它的
    // from 點當作一個新迴圈的起點去走。因為 clone 順序在每個測站都相同，
    // 這個「第一個未走訪線段」的選法在各測站之間是穩定、一致的，不需要
    // 額外指定「起點 UUID」。
    const QList<DirSegment> segments = collectSegments(m_geomClones);
    if (segments.isEmpty()) return result;

    QVector<bool> used(segments.size(), false);
    for (int i = 0; i < segments.size(); ++i) {
        if (used[i]) continue;

        const QVector2D startPos = segments[i].from;
        const QList<DirSegment> ordered = walkLoopFrom(segments, used, startPos);
        if (ordered.isEmpty()) {
            // 這條線段所屬的鏈走不成封閉迴圈（開放輪廓，例如未封閉的草圖線）。
            // 這條線段本身已經被 walkLoopFrom 標記過（若它是第一步）或維持
            // 未標記；為避免無窮迴圈，至少把 segments[i] 標記掉再繼續掃描。
            used[i] = true;
            continue;
        }

        TopoDS_Wire w = makeWireFromSegments(
            ordered, [this](double u, double v) { return toWorld(u, v); }, m_scale);
        if (!w.IsNull())
            result.append(w);
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// 序列化
// ─────────────────────────────────────────────────────────────────────────────

QJsonObject SketchInstance::toJson() const {
    QJsonObject obj = Feature::toJson();
    obj["type"]          = typeString();
    obj["masterSketchId"] = masterSketchId();
    obj["planeId"]       = m_plane ? m_plane->id() : QString();
    obj["instanceParams"] = m_instanceStore->toJson();
    return obj;
}

bool SketchInstance::fromJson(const QJsonObject& json) {
    if (!Feature::fromJson(json)) return false;

    m_pendingMasterSketchId = json["masterSketchId"].toString();
    m_pendingPlaneId        = json["planeId"].toString();

    // instanceParams（parent 在 Document post-pass 設定後才能正確求值）
    m_instanceStore->fromJson(json["instanceParams"].toObject());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 依賴
// ─────────────────────────────────────────────────────────────────────────────

QSet<QString> SketchInstance::featureDependencies() const {
    QSet<QString> deps;
    if (m_master) deps.insert(m_master->id());
    return deps;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private Slots
// ─────────────────────────────────────────────────────────────────────────────

void SketchInstance::onMasterRebuilt() {
    rebuild();
}

void SketchInstance::onMasterAboutToDestroy() {
    m_master = nullptr;
    m_instanceStore->setParentStore(nullptr);
    setError("Master sketch was deleted.");
    qWarning() << "[SketchInstance]" << name() << "master sketch destroyed.";
}

} // namespace aicad::cad
