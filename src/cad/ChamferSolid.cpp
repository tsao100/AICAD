#include "ChamferSolid.h"
#include "Document.h"
#include "ProfileLoftSolid.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <Standard_Failure.hxx>

#include <algorithm>
#include <limits>

namespace aicad {
namespace cad {

ChamferSolid::ChamferSolid(Document* parent)
    : Feature(parent)
{
    setName("Chamfer");
}

void ChamferSolid::setSourceFeature(Feature* src) {
    if (m_source == src) return;

    if (m_source) disconnect(m_sourceRebuiltConn);
    m_source = src;

    if (m_source) {
        // ✅ 沿用 ProfileLoftSolid::setSourceArray() 的作法：走
        //    Document::rebuildFeature() 而非直接呼叫 rebuild()，確保
        //    CadView 監聽的 featureShapeUpdated（Qt::QueuedConnection）
        //    能收到通知並重繪，避免「來源改了但畫面沒跟著更新」。
        m_sourceRebuiltConn = connect(
            m_source, &Feature::shapeChanged,
            this, [this]() {
                if (Document* doc = document()) doc->rebuildFeature(this);
                else rebuild();
            });
        setFeatureParent(m_source);
    }
}

QString ChamferSolid::sourceFeatureId() const {
    return m_source ? m_source->id() : m_pendingSourceFeatureId;
}

Feature* ChamferSolid::sourceFeature() const {
    return m_source.data();
}

EdgeSignature ChamferSolid::makeSignature(const TopoDS_Edge& edge, Feature* source) {
    EdgeSignature sig;
    if (edge.IsNull()) return sig;

    BRepAdaptor_Curve curve(edge);
    sig.midPoint = curve.Value((curve.FirstParameter() + curve.LastParameter()) * 0.5);

    GProp_GProps gp;
    BRepGProp::LinearProperties(edge, gp);
    sig.length = gp.Mass();

    // 來源是 ProfileLoftSolid 時，反查這條邊對應哪個 (loopIndex, cornerIndex)——
    // 找到的話優先用角點定址（精確查表），不用退回幾何猜配對。
    if (auto* loft = qobject_cast<ProfileLoftSolid*>(source)) {
        for (int loop = 0; loop < loft->loopCount() && sig.loopIndex < 0; ++loop) {
            const int corners = loft->cornerCount(loop);
            for (int corner = 0; corner < corners; ++corner) {
                const TopoDS_Edge candidate = loft->longitudinalEdge(loop, corner);
                if (!candidate.IsNull() && edge.IsSame(candidate)) {
                    sig.loopIndex   = loop;
                    sig.cornerIndex = corner;
                    break;
                }
            }
        }
        // 找不到（例如使用者點的是端帽/側面邊界邊，不是縱向角點邊）就保留
        // loopIndex=-1，rebuild() 會自動退回幾何猜配對——這是預期行為，
        // 不是錯誤，不需要在這裡警告。
    }

    return sig;
}

TopoDS_Edge ChamferSolid::findMatchingEdge(const TopoDS_Shape& candidateShape,
                                            const EdgeSignature& sig)
{
    if (candidateShape.IsNull()) return TopoDS_Edge();

    // 用 IndexedMap（而非直接 TopExp_Explorer）取得去重後的邊集合：
    // compound 內同一條邊常被相鄰兩個面各引用一次，Explorer 走訪會重複列出。
    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(candidateShape, TopAbs_EDGE, edgeMap);

    TopoDS_Edge best;
    double bestScore = std::numeric_limits<double>::max();

    // 容許的最大中點距離：抓一個跟自身邊長同數量級的寬鬆門檻。幾何變動太大
    // （例如 Loft 路廊整段改掉）時寧可配對失敗，也不要倒角到明顯不對的邊上。
    const double maxDist = std::max(sig.length, 1.0) * 5.0;

    for (int i = 1; i <= edgeMap.Extent(); ++i) {
        const TopoDS_Edge& e = TopoDS::Edge(edgeMap(i));
        const EdgeSignature cand = makeSignature(e);

        const double dist = sig.midPoint.Distance(cand.midPoint);
        if (dist > maxDist) continue;

        const double lenDiff = std::abs(cand.length - sig.length);
        // 中點距離為主要判準，邊長差異當作次要 tie-break（權重較小）。
        const double score = dist + lenDiff * 0.1;
        if (score < bestScore) {
            bestScore = score;
            best = e;
        }
    }
    return best;
}

bool ChamferSolid::rebuild() {
    if (!m_source) { setError("No source solid feature specified"); return false; }

    const TopoDS_Shape base = m_source->shape();
    if (base.IsNull()) { setError("Source feature has no shape"); return false; }

    if (m_edgeSignatures.isEmpty()) { setError("No edges selected for chamfer"); return false; }
    if (m_distance <= 0.0) { setError("Chamfer distance must be positive"); return false; }

    try {
        BRepFilletAPI_MakeChamfer mkChamfer(base);

        // 來源若是 ProfileLoftSolid，角點定址（sig.loopIndex >= 0）可以直接
        // 查表拿到邊，不需要幾何猜配對；來源改變後只要斷面角點數量沒變，
        // 這裡永遠拿到正確的邊（見 EdgeSignature／ProfileLoftSolid.h 說明）。
        ProfileLoftSolid* loft = qobject_cast<ProfileLoftSolid*>(m_source.data());

        int matched = 0, missed = 0;
        for (const EdgeSignature& sig : m_edgeSignatures) {
            TopoDS_Edge e;
            if (loft && sig.loopIndex >= 0) {
                e = loft->longitudinalEdge(sig.loopIndex, sig.cornerIndex);
            }
            if (e.IsNull()) {
                // 角點定址查無此邊（例如斷面角點數量真的變了），或這條邊
                // 本來就不是角點定址（幾何猜配對模式）——退回幾何比對。
                e = findMatchingEdge(base, sig);
            }
            if (e.IsNull()) { ++missed; continue; }
            mkChamfer.Add(m_distance, e);
            ++matched;
        }

        if (matched == 0) {
            setError("No matching edges found on the (possibly rebuilt) source solid — "
                      "please redo the chamfer edge selection");
            return false;
        }

        mkChamfer.Build();
        if (!mkChamfer.IsDone()) {
            setError("BRepFilletAPI_MakeChamfer failed to build the chamfered solid "
                      "(distance may be too large for the selected edges)");
            return false;
        }

        setShape(mkChamfer.Shape());

        if (missed > 0) {
            qWarning() << "[ChamferSolid]" << name() << "-" << missed
                       << "edge(s) could not be re-matched after the source solid changed; "
                          "skipped (" << matched << "matched OK).";
        }
        return true;

    } catch (const Standard_Failure& ex) {
        setError(QString("OCC error during chamfer: %1")
                     .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown"));
        return false;
    }
}

QJsonObject ChamferSolid::toJson() const {
    QJsonObject obj = Feature::toJson();
    obj["type"]            = typeString();
    obj["sourceFeatureId"] = sourceFeatureId();
    obj["distance"]        = m_distance;

    QJsonArray edges;
    for (const EdgeSignature& sig : m_edgeSignatures) {
        QJsonObject e;
        e["mx"]         = sig.midPoint.X();
        e["my"]         = sig.midPoint.Y();
        e["mz"]         = sig.midPoint.Z();
        e["length"]     = sig.length;
        e["loopIndex"]  = sig.loopIndex;
        e["cornerIndex"] = sig.cornerIndex;
        edges.append(e);
    }
    obj["edges"] = edges;
    return obj;
}

bool ChamferSolid::fromJson(const QJsonObject& json) {
    Feature::fromJson(json);
    m_pendingSourceFeatureId = json["sourceFeatureId"].toString();
    m_distance = json["distance"].toDouble();

    m_edgeSignatures.clear();
    for (const QJsonValue& v : json["edges"].toArray()) {
        QJsonObject e = v.toObject();
        EdgeSignature sig;
        sig.midPoint    = gp_Pnt(e["mx"].toDouble(), e["my"].toDouble(), e["mz"].toDouble());
        sig.length      = e["length"].toDouble();
        // .toInt(-1)：讀舊版（本次修改前）存的檔案時欄位不存在，預設視為
        // 「非角點定址」，rebuild() 會自動退回幾何猜配對，不會壞掉。
        sig.loopIndex   = e["loopIndex"].toInt(-1);
        sig.cornerIndex = e["cornerIndex"].toInt(-1);
        m_edgeSignatures.append(sig);
    }
    return true;
}

QSet<QString> ChamferSolid::featureDependencies() const {
    QSet<QString> deps;
    if (m_source) deps.insert(m_source->id());
    return deps;
}

void ChamferSolid::resolveReferences(Document* doc) {
    if (!doc || m_pendingSourceFeatureId.isEmpty()) return;

    if (auto* src = doc->findFeature(m_pendingSourceFeatureId)) {
        setSourceFeature(src);
        m_pendingSourceFeatureId.clear();
    } else {
        qWarning() << "[ChamferSolid]" << name()
                   << "Cannot resolve sourceFeatureId:" << m_pendingSourceFeatureId;
    }
}

} // namespace cad
} // namespace aicad
