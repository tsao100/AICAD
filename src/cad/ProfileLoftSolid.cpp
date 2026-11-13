#include "ProfileLoftSolid.h"
#include "SketchInstance.h"
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
#include <BRepTools_WireExplorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <gp_Pnt.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
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

int ProfileLoftSolid::cornerCount(int loopIndex) const {
    if (loopIndex < 0 || loopIndex >= m_loopCornerEdges.size()) return 0;
    return m_loopCornerEdges[loopIndex].size();
}

TopoDS_Edge ProfileLoftSolid::longitudinalEdge(int loopIndex, int cornerIndex) const {
    if (loopIndex < 0 || loopIndex >= m_loopCornerEdges.size()) return TopoDS_Edge();
    const QVector<TopoDS_Edge>& corners = m_loopCornerEdges[loopIndex];
    if (cornerIndex < 0 || cornerIndex >= corners.size()) return TopoDS_Edge();
    return corners[cornerIndex];
}

bool ProfileLoftSolid::rebuild() {
    if (!m_array) { setError("No source profile array specified"); return false; }

    const QVector<QList<TopoDS_Wire>> stationLoops = m_array->stationWireLoops();
    const QVector<int> stationSeg = m_array->stationSegmentIndices();

    // 每個分段一個 Loft：只放樣自己負責的那一段（舊檔 segmentId 為空 = 全部分段）。
    int targetSeg = -1;
    if (!m_segmentId.isEmpty()) {
        targetSeg = m_array->segmentIndexById(m_segmentId);
        if (targetSeg < 0) {
            setError("The segment this loft belongs to no longer exists");
            return false;
        }
    }
    if (targetSeg < 0 && stationLoops.size() < 2) {
        setError("Profile array needs at least 2 stations to loft");
        return false;
    }

    // 依「段」分組：同一段內的連續測站一起放樣。不同段可能是不同的 profile
    //（拓撲不同、無法互相放樣），所以段與段之間不放樣、各自成一個實體。
    struct Group { int seg; int first; int count; };
    QVector<Group> groups;
    for (int s = 0; s < stationLoops.size(); ++s) {
        const int seg = (s < stationSeg.size()) ? stationSeg[s] : 0;
        if (groups.isEmpty() || groups.last().seg != seg)
            groups.append({ seg, s, 1 });
        else
            groups.last().count++;
    }

    if (targetSeg >= 0) {
        QVector<Group> mine;
        for (const Group& g : groups) if (g.seg == targetSeg) mine.append(g);
        groups = mine;
    }

    int loftableGroups = 0;
    for (const Group& g : groups) if (g.count >= 2) ++loftableGroups;
    if (loftableGroups == 0) {
        setError(targetSeg >= 0
            ? QString("Segment %1 needs at least 2 stations to loft").arg(targetSeg)
            : QString("No segment has at least 2 stations — cannot loft"));
        return false;
    }

    try {
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);

        // 全域 loop 索引 = 依段順序串接（ChamferSolid 以此索引取角點邊）。
        QVector<QVector<TopoDS_Edge>> newLoopCornerEdges;

        for (const Group& grp : groups) {
            if (grp.count < 2) {
                qWarning() << "[ProfileLoftSolid]" << name() << "segment" << grp.seg
                           << "has only 1 station — skipped (needs >= 2 to loft).";
                continue;
            }

            int loopCount = -1;
            for (int s = grp.first; s < grp.first + grp.count; ++s) {
                if (!stationLoops[s].isEmpty()) { loopCount = stationLoops[s].size(); break; }
            }
            if (loopCount <= 0) {
                QString hint;
                const auto insts = m_array->instances();
                if (grp.first >= 0 && grp.first < insts.size() && insts[grp.first]) {
                    const QStringList bad = insts[grp.first]->unsupportedProfileGeometry();
                    if (!bad.isEmpty())
                        hint = QString(" (the profile sketch contains unsupported geometry: %1)")
                                   .arg(bad.join(", "));
                }
                setError(QString("Segment %1: no closed profile found in any station — cannot loft%2")
                              .arg(grp.seg).arg(hint));
                return false;
            }
            for (int s = grp.first; s < grp.first + grp.count; ++s) {
                if (stationLoops[s].size() != loopCount) {
                    setError(QString("Station %1 (segment %2) has %3 closed profile(s), expected %4 — "
                                      "cannot loft (check the profile sketch for unclosed geometry)")
                                  .arg(s).arg(grp.seg).arg(stationLoops[s].size()).arg(loopCount));
                    return false;
                }
            }

            for (int loopIdx = 0; loopIdx < loopCount; ++loopIdx) {
                QVector<TopoDS_Wire> valid;
                valid.reserve(grp.count);
                for (int s = grp.first; s < grp.first + grp.count; ++s) {
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

                BRepOffsetAPI_ThruSections generator(/*isSolid=*/true, /*isRuled=*/false);
                generator.SetSmoothing(Standard_True);
                generator.SetContinuity(GeomAbs_C2);
                generator.SetParType(Approx_ChordLength);
                generator.CheckCompatibility(Standard_True);
                for (const TopoDS_Wire& w : valid)
                    generator.AddWire(w);
                generator.Build();

                if (!generator.IsDone()) {
                    setError(QString("BRepOffsetAPI_ThruSections failed to build a solid "
                                      "(segment %1, loop %2)").arg(grp.seg).arg(loopIdx));
                    return false;
                }
                builder.Add(compound, generator.Shape());

                QVector<TopoDS_Edge> cornerEdges;
                for (BRepTools_WireExplorer wexp(valid.first()); wexp.More(); wexp.Next()) {
                    const TopoDS_Vertex v = wexp.CurrentVertex();
                    const TopTools_ListOfShape& gen = generator.Generated(v);

                    TopoDS_Edge cornerEdge;
                    if (!gen.IsEmpty()) {
                        if (gen.Extent() != 1) {
                            qWarning() << "[ProfileLoftSolid]" << name() << "segment" << grp.seg
                                       << "loop" << loopIdx << "corner" << cornerEdges.size()
                                       << ": Generated(vertex) returned" << gen.Extent()
                                       << "shapes (expected 1); using the first EDGE.";
                        }
                        for (TopTools_ListIteratorOfListOfShape it(gen); it.More(); it.Next()) {
                            if (it.Value().ShapeType() == TopAbs_EDGE) {
                                cornerEdge = TopoDS::Edge(it.Value());
                                break;
                            }
                        }
                    }
                    cornerEdges.append(cornerEdge);
                }
                newLoopCornerEdges.append(cornerEdges);
            }
        }

        // ⚠️ 順序很重要：必須先更新角點邊，再 setShape()。setShape() 會同步發出
        //    shapeChanged，而 ChamferSolid 連著這個訊號並立刻 rebuild，它會透過
        //    longitudinalEdge() 讀取 m_loopCornerEdges；若先 setShape()，Chamfer 讀到的
        //    是「舊形狀的角點邊」配對到「新形狀」，導致分段表套用新值後，
        //    畫面上（Loft 被 Chamfer 消耗而隱藏，只看得到 Chamfer 結果）沒有跟著更新。
        m_loopCornerEdges = newLoopCornerEdges;
        setShape(compound);
        clearError();
        return true;

    } catch (const Standard_Failure& ex) {
        setError(QString("OCC error during loft: %1")
                     .arg(ex.GetMessageString() ? ex.GetMessageString() : "unknown"));
        return false;
    }
}

double ProfileLoftSolid::volume() const {
    const TopoDS_Shape s = shape();
    if (s.IsNull())
        return 0.0;
    GProp_GProps gp;
    // VolumeProperties 對 compound 內每個 solid 各自計算後加總，不需要
    // 自己再跑 TopExp_Explorer 迭代 compound 底下的每個 loop/solid。
    BRepGProp::VolumeProperties(s, gp);
    return gp.Mass();
}

QJsonObject ProfileLoftSolid::toJson() const {
    QJsonObject obj = Feature::toJson();
    obj["type"]           = typeString();
    obj["sourceArrayId"]  = sourceArrayId();
    obj["segmentId"]      = m_segmentId;
    return obj;
}

bool ProfileLoftSolid::fromJson(const QJsonObject& json) {
    Feature::fromJson(json);
    m_pendingSourceArrayId = json["sourceArrayId"].toString();
    m_segmentId            = json["segmentId"].toString();   // 舊檔沒有 → 空
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
