#include "ProfileLoftSolid.h"
#include "AlignedProfileArray.h"
#include "Document.h"

#include <QJsonObject>
#include <QDebug>
#include <QVector>

#include <BRepOffsetAPI_ThruSections.hxx>
#include <GeomAbs_Shape.hxx>
#include <Approx_ParametrizationType.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <gp_Pnt.hxx>
#include <cmath>

namespace aicad {
namespace cad {

ProfileLoftSolid::ProfileLoftSolid(Document* parent)
    : Feature(parent)
{
    setName("ProfileLoft");
}

namespace {
/**
 * @brief 防禦性檢查：wire 上任何一個頂點座標若非有限值（NaN/Inf），代表
 *        上游（通常是求解器發散的 SketchInstance）產生了壞掉的幾何。
 *        SketchInstance::rebuild() 已經會擋掉這種情況，這裡是第二道防線——
 *        萬一還是有非有限座標流進來，寧可讓 Loft 回報錯誤，也不要把它交給
 *        BRepOffsetAPI_ThruSections，避免稍後在放樣/顯示階段讓 OCCT 丟出
 *        未捕捉例外把整個程式帶垮。
 */
bool wireCoordinatesFinite(const TopoDS_Wire& wire) {
    for (TopExp_Explorer exp(wire, TopAbs_VERTEX); exp.More(); exp.Next()) {
        const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(exp.Current()));
        if (!std::isfinite(p.X()) || !std::isfinite(p.Y()) || !std::isfinite(p.Z()))
            return false;
    }
    return true;
}
} // namespace

void ProfileLoftSolid::setSourceArray(AlignedProfileArray* arr) {
    if (m_array == arr) return;

    if (m_array) disconnect(m_arrayRebuiltConn);
    m_array = arr;

    if (m_array) {
        // ✅ 走 Document::rebuildFeature() 而不是直接呼叫 rebuild()：直接呼叫只會
        //    更新資料（setShape()/shapeChanged()），不會觸發 CadView 監聽的
        //    featureShapeUpdated → displayAllFeatures()，畫面不會跟著刷新。這正是
        //    「Loft 有時看得到、有時看不到」的根因——能不能看到，取決於上一次
        //    「剛好」有沒有發生過一次完整的文件重新整理（例如重新載入檔案）。
        m_arrayRebuiltConn = connect(
            m_array, &Feature::shapeChanged,
            this, [this]() {
                if (Document* doc = document()) doc->rebuildFeature(this);
                else rebuild();
            });
        // 讓 Document::rebuildFeatureTreeItems()（讀檔後重建 tree）也能
        // 正確把此節點掛在來源 AlignedProfileArray 底下（同 Extrude 對其
        // sketch 的做法：Feature 層的 parent 只影響樹狀結構，不動 QObject 所有權）。
        setFeatureParent(m_array);
    }
}

QString ProfileLoftSolid::sourceArrayId() const {
    return m_array ? m_array->id() : m_pendingSourceArrayId;
}

AlignedProfileArray* ProfileLoftSolid::sourceArray() const {
    return m_array.data();
}

bool ProfileLoftSolid::rebuild() {
    if (!m_array) { setError("No source profile array specified"); return false; }

    const QVector<QList<TopoDS_Wire>> stationLoops = m_array->stationWireLoops();
    if (stationLoops.size() < 2) {
        setError("Profile array needs at least 2 stations to loft");
        return false;
    }

    // 每一站應該偵測到同樣數量的封閉輪廓（同一個 master 草圖 clone 出來的）。
    // 用第一個「有輪廓」的站當作預期數量，藉此在資料本身損壞前先給出明確錯誤。
    int loopCount = -1;
    for (const QList<TopoDS_Wire>& loops : stationLoops) {
        if (!loops.isEmpty()) { loopCount = loops.size(); break; }
    }
    if (loopCount <= 0) {
        setError("No closed profile found in any station — cannot loft");
        return false;
    }
    for (int s = 0; s < stationLoops.size(); ++s) {
        if (stationLoops[s].size() != loopCount) {
            setError(QString("Station %1 has %2 closed profile(s), expected %3 — "
                              "cannot loft (check the master sketch for unclosed geometry)")
                          .arg(s).arg(stationLoops[s].size()).arg(loopCount));
            return false;
        }
    }

    try {
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);

        // 每一個迴圈（例如左右兩個獨立墊塊）各自沿測站方向放樣成一個實體，
        // 全部實體再組成一個 compound 當作本特徵的最終形狀。
        for (int loopIdx = 0; loopIdx < loopCount; ++loopIdx) {
            QVector<TopoDS_Wire> valid;
            valid.reserve(stationLoops.size());
            for (int s = 0; s < stationLoops.size(); ++s) {
                const TopoDS_Wire& w = stationLoops[s][loopIdx];
                if (w.IsNull()) {
                    setError(QString("Station %1, loop %2 has no closed profile — cannot loft")
                                  .arg(s).arg(loopIdx));
                    return false;
                }
                if (!wireCoordinatesFinite(w)) {
                    setError(QString("Station %1, loop %2 has non-finite coordinates — cannot loft")
                                  .arg(s).arg(loopIdx));
                    return false;
                }
                valid.append(w);
            }

            // isSolid=true（封閉實體）、isRuled=false（沿站位以 spline 曲面放樣，
            // 依規劃 §6.2/§8 決議）。isRuled=false 只是「不要用直紋面」，實際
            // 曲面平滑度還要另外明確設定，否則預設可能仍偏向逐站分段近似：
            // 開 SetSmoothing + 要求 C2 連續，並用弦長參數化讓斷面間的過渡更自然。
            BRepOffsetAPI_ThruSections generator(/*isSolid=*/true, /*isRuled=*/false);
            generator.SetSmoothing(Standard_True);
            generator.SetContinuity(GeomAbs_C2);
            generator.SetParType(Approx_ChordLength);
            generator.CheckCompatibility(Standard_True);
            for (const TopoDS_Wire& w : valid)
                generator.AddWire(w);
            generator.Build();

            if (!generator.IsDone()) {
                setError(QString("BRepOffsetAPI_ThruSections failed to build a solid for loop %1")
                              .arg(loopIdx));
                return false;
            }
            builder.Add(compound, generator.Shape());
        }

        setShape(compound);
        return true;

    } catch (const Standard_Failure& ex) {
        setError(QString("OCC error during loft: %1")
                     .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown"));
        return false;
    }
}

QJsonObject ProfileLoftSolid::toJson() const {
    QJsonObject obj = Feature::toJson();
    obj["type"]           = typeString();
    obj["sourceArrayId"]  = sourceArrayId();
    return obj;
}

bool ProfileLoftSolid::fromJson(const QJsonObject& json) {
    Feature::fromJson(json);
    m_pendingSourceArrayId = json["sourceArrayId"].toString();
    return true;
}

QSet<QString> ProfileLoftSolid::featureDependencies() const {
    QSet<QString> deps;
    if (m_array) deps.insert(m_array->id());
    return deps;
}

void ProfileLoftSolid::resolveReferences(Document* doc) {
    if (!doc || m_pendingSourceArrayId.isEmpty()) return;

    if (auto* arr = qobject_cast<AlignedProfileArray*>(doc->findFeature(m_pendingSourceArrayId))) {
        setSourceArray(arr);
        m_pendingSourceArrayId.clear();
    } else {
        qWarning() << "[ProfileLoftSolid]" << name()
                   << "Cannot resolve sourceArrayId:" << m_pendingSourceArrayId;
    }
}

} // namespace cad
} // namespace aicad
