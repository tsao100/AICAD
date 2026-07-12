#include "AlignedProfileArray.h"
#include "Document.h"
#include "Sketch.h"
#include "Plane.h"
#include "PlaneManager.h"
#include "SketchInstance.h"
#include "railway/RailwayAlignment.h"

#include <QDebug>
#include <QJsonObject>
#include <QSignalBlocker>
#include <cmath>

namespace aicad {
namespace cad {

namespace {

constexpr double kEps = 1e-6;

/// Rodrigues' 旋轉公式：將向量 v 繞單位軸 axis 旋轉 angle（弳度）。
QVector3D rotateAboutAxis(const QVector3D& v, const QVector3D& axis, double angle)
{
    const QVector3D k = axis.normalized();
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return v * c
         + QVector3D::crossProduct(k, v) * s
         + k * QVector3D::dotProduct(k, v) * (1.0 - c);
}

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

void AlignedProfileArray::setMasterSketch(Sketch* master) {
    if (m_master == master) return;

    if (m_master) disconnect(m_masterRebuildConn);
    m_master = master;

    if (m_master) {
        // master 幾何/約束改變 → 全部測站重建（同 SketchInstance::onMasterRebuilt 的精神）。
        // ✅ 透過 Document::rebuildFeature() 而不是直接呼叫 rebuild()：後者才會
        //    Q_EMIT featureShapeUpdated()，CadView 監聽這個訊號做 displayAllFeatures()
        //    重新整理畫面。直接呼叫 rebuild() 只會更新資料，畫面不會跟著刷新——
        //    這正是「Loft 有時看得到、有時看不到」的根因：有沒有顯示，取決於上一次
        //    「剛好」有沒有發生一次完整的文件重新整理。
        m_masterRebuildConn = connect(
            m_master, &Feature::shapeChanged,
            this, [this]() {
                if (Document* doc = document()) doc->rebuildFeature(this);
                else rebuild();
            });
    }
}

QString AlignedProfileArray::masterSketchId() const {
    return m_master ? m_master->id() : m_pendingMasterSketchId;
}

Sketch* AlignedProfileArray::masterSketch() const {
    return m_master.data();
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
        // Alignment 編輯（含 cant/H 的來源資料）改變 → 全部測站重建（同上，走
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

void AlignedProfileArray::setRange(double startChainage, double endChainage, double interval) {
    m_startChainage = startChainage;
    m_endChainage   = endChainage;
    m_interval      = interval;
}

// ─────────────────────────────────────────────────────────────────────────────
// rebuild() 核心（對應規劃 §2.2）
// ─────────────────────────────────────────────────────────────────────────────

QVector<double> AlignedProfileArray::computeStations() const {
    QVector<double> stations;
    if (m_interval <= kEps || m_endChainage < m_startChainage - kEps)
        return stations;

    const int n = static_cast<int>(
        std::floor((m_endChainage - m_startChainage) / m_interval + kEps)) + 1;
    stations.reserve(std::max(n, 1));
    for (int i = 0; i < std::max(n, 1); ++i)
        stations.append(m_startChainage + i * m_interval);
    return stations;
}

QVector<double> AlignedProfileArray::stationChainages() const {
    return computeStations();
}

bool AlignedProfileArray::rebuild() {
    if (!m_master) { setError("Master sketch not set."); return false; }
    if (!m_tcl)    { setError("Track centerline not set."); return false; }
    if (m_interval <= kEps) { setError("Interval must be positive."); return false; }
    if (m_endChainage < m_startChainage - kEps) {
        setError("End chainage must be >= start chainage.");
        return false;
    }

    Document* doc = document();
    if (!doc) { setError("AlignedProfileArray has no owning Document."); return false; }

    // 1. 等距測站清單（§2.2 step 1 / §3 Q3）：
    //    最後一站 = 不大於 endChainage 的最大 interval 倍數，不強制卡在 endChainage。
    const QVector<double> stations = computeStations();

    // 2. 依「位置索引」diff 既有 instance（不依 chainage 值比對——
    //    避免 interval/endChainage 改變時錯位，教訓同浮動曲線那次重工）。
    while (m_instances.size() > stations.size()) {
        SketchInstance* inst = m_instances.takeLast();
        Plane* pl = m_planes.isEmpty() ? nullptr : m_planes.takeLast();
        if (inst) doc->removeFeature(inst);
        if (pl)   PlaneManager::instance()->deletePlane(pl);
    }
    while (m_instances.size() < stations.size()) {
        const int idx = m_instances.size();
        Plane* pl = PlaneManager::instance()->createPlane(
            QVector3D(0, 0, 0), QVector3D(0, 0, 1), QVector3D(1, 0, 0),
            QString("%1_Station%2").arg(name()).arg(idx));

        SketchInstance* inst = doc->createSketchInstance(
            m_master, pl, {},
            QString("%1_Station%2").arg(name()).arg(idx));
        if (inst) {
            inst->setFeatureParent(this);
            inst->setScale(m_masterUnitScale);
            // Step 7：SketchInstance 的 tree item 預設掛在 master sketch 底下
            // （同 createSketchInstance 內部的預設行為），這裡依規劃書改掛到
            // 本 AlignedProfileArray 節點下，比照 Extrude 對其 sketch 的做法。
            doc->setTreeItemParent(inst->id(), id());
        }

        m_planes.append(pl);
        m_instances.append(inst);
    }

    // 3. 逐站計算平面與 cant/H，寫入 instance 並重建
    for (int i = 0; i < stations.size(); ++i) {
        const double p = stations[i];
        SketchInstance* inst = m_instances[i];
        Plane* pl = m_planes[i];
        if (!inst || !pl) continue;

        const double h       = m_tcl->getAppliedH(p);
        const double cantVal = m_tcl->getCant(p);

        const QVector3D origin = m_tcl->getXYZ(p, h);
        const double az     = m_tcl->getAzimuth(p);   // [rad], CW from N
        const double slopePc = m_tcl->getSlope(p);     // [%]

        // 3D 切線：水平分量 (sin az, cos az, 0) 疊加縱坡分量（依約定：坡度[%] = 100*dz/dp）
        const QVector3D horizT(std::sin(az), std::cos(az), 0.0);
        QVector3D tangent = horizT + QVector3D(0.0f, 0.0f, static_cast<float>(slopePc / 100.0));
        if (tangent.lengthSquared() < kEps) tangent = horizT;
        tangent.normalize();

        // 未旋轉的左側橫向量（RailwayAlignmentElement.cpp 的既有慣例）
        const QVector3D lateral0(-std::cos(az), std::sin(az), 0.0);

        // cant → 繞切線的傾角，再旋轉橫向量（Plane::setCoordinateSystem 會再對
        // xAxis 做 Gram-Schmidt 正交化，所以這裡不需要手動保證與 tangent 正交）。
        const double cantAngle = railway::cantToAngle(cantVal);
        const QVector3D xAxis = rotateAboutAxis(lateral0, tangent, cantAngle);

        pl->setCoordinateSystem(origin, tangent, xAxis);

        inst->setScale(m_masterUnitScale);
        {
            // ✅ setLocal() 每次都會 Q_EMIT parametersRecomputed()，而 SketchInstance
            //    建構時已把這個訊號接到自己的 rebuild()；若不擋掉，"cant" 設定後
            //    會先以「舊/預設 H」solve 一次、"H" 設定後再 solve 一次、下面又手動
            //    呼叫一次 rebuild() —— 同一個 instance 一次陣列重建內被連續 solve
            //    3 次，且每次都拿上一次（可能是壞掉的）解當初始猜測，容易把約束
            //    求解器帶到 NaN／衝突。改成兩個參數都設定完才觸發一次 rebuild()。
            const QSignalBlocker blocker(inst->parameterStore());
            inst->parameterStore()->setLocal("cant", cantVal);
            inst->parameterStore()->setLocal("H", h);
        }
        inst->rebuild();
    }

    clearError();
    doc->setTreeItemName(id(), QString("%1 (%2 stations)").arg(name()).arg(m_instances.size()));
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
    if (m_master) deps.insert(m_master->id());
    // TrackCenterLine 不是 Feature，不登記進 Feature 依賴圖；
    // 其變更改由 dataChanged 訊號直接連到 rebuild()（見 setTrackCenterLine）。
    return deps;
}

QJsonObject AlignedProfileArray::toJson() const {
    QJsonObject obj = Feature::toJson();
    obj["type"]           = typeString();
    obj["masterSketchId"] = masterSketchId();
    obj["trackCenterLineId"] = trackCenterLineId();
    obj["startChainage"]  = m_startChainage;
    obj["endChainage"]    = m_endChainage;
    obj["interval"]       = m_interval;
    obj["masterUnitScale"] = m_masterUnitScale;
    // 刻意不存子 SketchInstance、不存 cant/H：兩者皆於 rebuild() 從 TCL 即時算出。
    return obj;
}

bool AlignedProfileArray::fromJson(const QJsonObject& json) {
    Feature::fromJson(json);
    m_pendingMasterSketchId = json["masterSketchId"].toString();
    m_pendingTclId          = json["trackCenterLineId"].toString();
    m_startChainage = json["startChainage"].toDouble(0.0);
    m_endChainage   = json["endChainage"].toDouble(0.0);
    m_interval      = json["interval"].toDouble(20.0);
    m_masterUnitScale = json.contains("masterUnitScale")
        ? json["masterUnitScale"].toDouble(0.001) : 0.001;
    return true;
}

void AlignedProfileArray::resolveReferences(Document* doc) {
    if (!doc) return;

    if (!m_pendingMasterSketchId.isEmpty()) {
        if (auto* sk = qobject_cast<Sketch*>(doc->findFeature(m_pendingMasterSketchId))) {
            setMasterSketch(sk);
            m_pendingMasterSketchId.clear();
        } else {
            qWarning() << "[AlignedProfileArray]" << name()
                       << "Cannot resolve masterSketchId:" << m_pendingMasterSketchId;
        }
    }

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
