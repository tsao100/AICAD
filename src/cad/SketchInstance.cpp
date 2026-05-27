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

namespace aicad::cad {

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
                QVector3D p1 = plane->toWorld(geom->points[0].x(), geom->points[0].y());
                QVector3D p2 = plane->toWorld(geom->points[1].x(), geom->points[1].y());

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
                    QVector3D p1 = plane->toWorld(geom->points[i].x(), geom->points[i].y());
                    QVector3D p2 = plane->toWorld(
                        geom->points[(i + 1) % geom->points.size()].x(),
                        geom->points[(i + 1) % geom->points.size()].y());

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
    if (result.status == SolveStatus::Conflict) {
        setError("Constraint conflict in SketchInstance.");
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
