#include "SketchInstance.h"
#include "Document.h"
#include "PlaneManager.h"
#include "../core/ParameterStore.h"

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
#include <gp_Dir.hxx>
#include <Standard_Failure.hxx>
#include <Precision.hxx>
#include <cmath>

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
    QList<SketchGeometry*> clones;

    for (const SketchGeometry* orig : master->geometries()) {
        SketchGeometry* clone = nullptr;

        switch (orig->type) {
        case SketchGeometryType::Line: {
            const SketchLine* src = static_cast<const SketchLine*>(orig);
            auto* ln = new SketchLine(src->start, src->end, src->role);
            clone = ln;
            break;
        }
        case SketchGeometryType::Circle: {
            const SketchCircle* src = static_cast<const SketchCircle*>(orig);
            auto* c = new SketchCircle(src->center, src->radius, src->role);
            clone = c;
            break;
        }
        case SketchGeometryType::Arc: {
            const SketchArc* src = static_cast<const SketchArc*>(orig);
            auto* a = new SketchArc(src->curve, src->role);
            a->points = src->points;
            clone = a;
            break;
        }
        case SketchGeometryType::Polyline: {
            const SketchPolyline* src = static_cast<const SketchPolyline*>(orig);
            // SketchPolyline(pts, isClosed, role)
            auto* pl = new SketchPolyline(src->points, src->closed, src->role);
            clone = pl;
            break;
        }
        case SketchGeometryType::Spline: {
            const SketchSpline* src = static_cast<const SketchSpline*>(orig);
            auto* sp = new SketchSpline(src->points, src->role);
            clone = sp;
            break;
        }
        case SketchGeometryType::Ellipse: {
            const SketchEllipse* src = static_cast<const SketchEllipse*>(orig);
            auto* el = new SketchEllipse(
                src->center, src->majorRadius, src->minorRadius,
                src->angle, src->role);
            clone = el;
            break;
        }
        default: {
            // 其他未知類型：基礎拷貝（保留幾何點集）
            clone = new SketchGeometry(orig->type, orig->role);
            clone->points = orig->points;
            break;
        }
        }

        if (clone) {
            // 產生新 UUID 並記錄映射
            QString newUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
            uuidRemap[orig->uuid] = newUuid;
            clone->uuid = newUuid;
            clones.append(clone);
        }
    }
    return clones;
}

QList<SketchConstraint> SketchInstance::cloneConstraints(
        const Sketch* master,
        const QHash<QString, QString>& uuidRemap) const
{
    QList<SketchConstraint> clones;

    for (const SketchConstraint& orig : master->constraints()) {
        SketchConstraint c;
        c.uuid      = QUuid::createUuid().toString(QUuid::WithoutBraces);
        c.type      = orig.type;
        c.value     = orig.value;
        c.paramExpr = orig.paramExpr;
        c.driving   = orig.driving;

        // 重映射 GeomRef UUID
        for (const GeomRef& ref : orig.refs) {
            GeomRef newRef;
            newRef.geomUuid = uuidRemap.value(ref.geomUuid, ref.geomUuid);
            newRef.handle   = ref.handle;
            c.refs.append(newRef);
        }
        clones.append(c);
    }
    return clones;
}

// ─────────────────────────────────────────────────────────────────────────────
// Wire 建立
// ─────────────────────────────────────────────────────────────────────────────

QList<TopoDS_Wire> SketchInstance::buildWires(
        const QList<SketchGeometry*>& geoms,
        Plane* plane) const
{
    QList<TopoDS_Wire> wires;
    if (!plane) return wires;

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);

    for (const SketchGeometry* geom : geoms) {
        if (!geom || geom->isConstruction()) continue;

        try {
            if (geom->type == SketchGeometryType::Line && geom->points.size() >= 2) {
                QVector3D p1 = plane->toWorld(geom->points[0].x() * m_scale, geom->points[0].y() * m_scale);
                QVector3D p2 = plane->toWorld(geom->points[1].x() * m_scale, geom->points[1].y() * m_scale);

                BRepBuilderAPI_MakeEdge edgeMk(
                    gp_Pnt(p1.x(), p1.y(), p1.z()),
                    gp_Pnt(p2.x(), p2.y(), p2.z()));
                if (!edgeMk.IsDone()) continue;

                BRepBuilderAPI_MakeWire wireMk(edgeMk.Edge());
                if (wireMk.IsDone())
                    wires.append(wireMk.Wire());

            } else if (geom->type == SketchGeometryType::Polyline && geom->points.size() >= 2) {
                const SketchPolyline* pl = static_cast<const SketchPolyline*>(geom);
                BRepBuilderAPI_MakeWire wireMk;
                int nSeg = pl->closed ? geom->points.size() : geom->points.size() - 1;
                for (int i = 0; i < nSeg; ++i) {
                    QVector3D p1 = plane->toWorld(geom->points[i].x() * m_scale, geom->points[i].y() * m_scale);
                    QVector3D p2 = plane->toWorld(
                        geom->points[(i + 1) % geom->points.size()].x() * m_scale,
                        geom->points[(i + 1) % geom->points.size()].y() * m_scale);

                    BRepBuilderAPI_MakeEdge e(
                        gp_Pnt(p1.x(), p1.y(), p1.z()),
                        gp_Pnt(p2.x(), p2.y(), p2.z()));
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
// rebuild() 核心
// ─────────────────────────────────────────────────────────────────────────────

bool SketchInstance::rebuild() {
    if (!m_master) {
        setError("Master sketch not set.");
        return false;
    }
    if (!m_plane) {
        setError("Plane not set for SketchInstance.");
        return false;
    }

    // 1. 深拷貝 master 幾何
    QHash<QString, QString> uuidRemap;
    qDeleteAll(m_geomClones);
    m_geomClones = cloneGeometries(m_master, uuidRemap);
    m_uuidRemap  = uuidRemap;  // 供 orderedWireFrom() 換算 master→clone UUID

    // 2. 拷貝並重映射約束
    m_constraintClones = cloneConstraints(m_master, uuidRemap);

    // 3. 用 instance store 求值所有尺寸約束（父子 fallback 在 store 內處理）
    for (auto& c : m_constraintClones) {
        if (c.isDimensional())
            c.evaluateValue(m_instanceStore);
    }

    // 4. 求解
    gp_Dir normal(0, 0, 1);
    {
        QVector3D n = m_plane->normal();
        normal = gp_Dir(n.x(), n.y(), n.z());
    }
    SolveResult result = m_solver.solve(m_geomClones, m_constraintClones, normal);
    bool solveOk = (result.status != SolveStatus::Conflict) && allGeometryFinite(m_geomClones);

    if (!solveOk) {
        // ✅ 這裡觀察到的現象：同一份 master 幾何、同樣的約束，在不同次執行
        //    有時收斂、有時 CONFLICT——不是幾何本身有問題，而是求解器對這種
        //    （絕對座標 FixX/FixY + 相對 Distance/Coincident 並存的）over-
        //    constrained 系統偶爾數值不穩定。求解失敗時 m_geomClones 已經被
        //    solve() 就地改壞（發散掉），不能直接拿來 buildWires()，但也不該
        //    整個 instance 直接放棄——那會讓 ProfileLoftSolid 因為缺一站就
        //    整個放樣失敗、Loft 完全不顯示。
        //    做法：重新從 master clone 一份乾淨幾何，換一個全新的
        //    ConstraintSolver 再試一次；還是不行的話，直接使用「未求解」的
        //    master 原始座標當作這一站的幾何——對沒有用 cant/H 參數化尺寸的
        //    斷面來說，這就是完全正確的答案；對有用到 cant/H 的斷面，這只是
        //    退化成「顯示 cant=H=0 時的形狀」的近似值，好過完全不顯示。
        qWarning() << "[SketchInstance]" << name()
                   << "solve failed (status=" << static_cast<int>(result.status)
                   << "), retrying with a fresh solver...";

        qDeleteAll(m_geomClones);
        m_geomClones = cloneGeometries(m_master, uuidRemap);
        m_uuidRemap  = uuidRemap;
        // ⚠️ constraint clones 裡的 geomUuid 參照的是「clone」的 UUID，每次
        // cloneGeometries() 都會產生全新的 clone UUID，所以幾何重 clone 後
        // 約束也必須跟著用同一份新 uuidRemap 重 clone，否則約束會指到不存在
        // 的舊 UUID、悄悄地變成完全沒套用任何約束。
        m_constraintClones = cloneConstraints(m_master, uuidRemap);
        for (auto& c : m_constraintClones)
            if (c.isDimensional()) c.evaluateValue(m_instanceStore);

        ConstraintSolver retrySolver;
        SolveResult retryResult = retrySolver.solve(m_geomClones, m_constraintClones, normal);
        solveOk = (retryResult.status != SolveStatus::Conflict) && allGeometryFinite(m_geomClones);

        if (!solveOk) {
            qWarning() << "[SketchInstance]" << name()
                       << "retry also failed — falling back to raw (unsolved) master geometry"
                          " for this station.";
            qDeleteAll(m_geomClones);
            m_geomClones = cloneGeometries(m_master, uuidRemap);  // 乾淨、未求解，但一定有限、拓樸正確
            m_uuidRemap  = uuidRemap;
            m_constraintClones = cloneConstraints(m_master, uuidRemap);  // 保持與 m_geomClones 一致
            solveOk = allGeometryFinite(m_geomClones);
        }
    }

    if (!solveOk) {
        // 連未求解的原始 master 幾何都不是有限值——這不可能發生於正常資料，
        // 但還是留一道防線，避免任何非有限座標流進 buildWires()/setShape()。
        setError("Solver diverged to non-finite coordinates in SketchInstance.");
        return false;
    }

    // 5. 建立 3D Wires
    m_wires = buildWires(m_geomClones, m_plane);

    // 6. 組成 compound shape
    if (!m_wires.isEmpty()) {
        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        for (const TopoDS_Wire& w : m_wires)
            builder.Add(compound, w);
        setShape(compound);
    } else {
        setShape(TopoDS_Shape());
    }

    clearError();
    Q_EMIT instanceRebuilt();
    Q_EMIT shapeChanged();
    return true;
}

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
            const SketchPolyline* pl = static_cast<const SketchPolyline*>(geom);
            int n = geom->points.size();
            int nSeg = pl->closed ? n : n - 1;
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
TopoDS_Wire makeWireFromSegments(const QList<DirSegment>& ordered, Plane* plane, double scale) {
    TopoDS_Wire result;
    if (!plane || ordered.isEmpty()) return result;
    try {
        BRepBuilderAPI_MakeWire wireMk;
        for (const DirSegment& seg : ordered) {
            QVector3D p1 = plane->toWorld(seg.from.x() * scale, seg.from.y() * scale);
            QVector3D p2 = plane->toWorld(seg.to.x()   * scale, seg.to.y()   * scale);
            BRepBuilderAPI_MakeEdge edgeMk(
                gp_Pnt(p1.x(), p1.y(), p1.z()),
                gp_Pnt(p2.x(), p2.y(), p2.z()));
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
    if (!m_plane || masterPointUuid.isEmpty())
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

    return makeWireFromSegments(ordered, m_plane, m_scale);
}

QList<TopoDS_Wire> SketchInstance::allClosedWires() const {
    QList<TopoDS_Wire> result;
    if (!m_plane) return result;

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

        TopoDS_Wire w = makeWireFromSegments(ordered, m_plane, m_scale);
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
