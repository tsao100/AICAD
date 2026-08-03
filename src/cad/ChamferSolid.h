/**
 * @file ChamferSolid.h
 * @brief 對來源實體 Feature（例如 ProfileLoftSolid、Extrude）的選取邊執行
 *        倒角（BRepFilletAPI_MakeChamfer），產生新的 Chamfer 特徵。
 *
 * 設計決議（2026-08-01，與 Peggy 討論確認）：
 *   1. 倒角距離：單一距離套用於本特徵所有選取邊（不支援每邊各自距離／
 *      非對稱 D1/D2 倒角）。
 *   2. 來源幾何被修改（例如來源 Loft 的 AlignedProfileArray 改站位）後，
 *      不做「一次性快照＋報錯要求重選」，而是用幾何簽章（EdgeSignature）
 *      盡力在新 shape 中重新對應回原本選取的邊——不保證 100% 成功，
 *      配對不到的邊會被略過並記錄警告（見 ChamferSolid.cpp rebuild()）。
 *   3. 選邊互動（ChamferCommand + CadView::beginEdgePicking()）：指令一
 *      啟動就直接對畫面上所有已顯示的實體 Feature 開放邊選取，點哪條
 *      算哪條，不需要先選取來源實體整體。
 */
#pragma once

#include "Feature.h"
#include <QString>
#include <QVector>
#include <QPointer>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

namespace aicad {
namespace cad {

/**
 * @brief 一條被選取邊的定址資訊。
 *
 * 有兩種模式，rebuild() 依優先順序嘗試（見 ChamferSolid.cpp）：
 *
 *   1. **角點定址**（loopIndex/cornerIndex >= 0）：來源 Feature 是
 *      ProfileLoftSolid 時的精確查表方式——直接呼叫
 *      ProfileLoftSolid::longitudinalEdge(loopIndex, cornerIndex)。
 *      這是精確查表，不是猜測，來源幾何（測站數量/里程/高程）改變後仍然
 *      100% 找得回同一條邊，只要斷面角點數量本身沒變。互動選邊
 *      （ChamferCommand）與 Lisp 腳本（chamfer-edges）都走這個模式；
 *      互動選邊時由 ChamferSolid::makeSignature() 對點選到的邊做反查
 *      （比對 IsSame()）自動判斷出 loopIndex/cornerIndex。
 *      （驗證依據見 verify_thrusections_generated.cpp 及對話紀錄：
 *      每個斷面角點在目前 ThruSections 設定下穩定對應剛好一條縱向邊。）
 *
 *   2. **幾何猜配對**（loopIndex < 0，退回模式）：來源不是
 *      ProfileLoftSolid（例如 Extrude），或角點定址查無此邊時的備援——
 *      用邊的中點世界座標 + 邊長，在新 shape 的所有邊中找「中點最近且
 *      長度相近」者（見 ChamferSolid::findMatchingEdge）。不保證
 *      100% 成功，來源幾何變動太大時可能配對錯誤或完全配對不到（後者
 *      會被略過並警告，不會讓整個特徵失敗，除非所有邊都配對失敗）。
 */
struct EdgeSignature {
    gp_Pnt midPoint;      ///< 邊中點世界座標（挑選當下，來源 shape 上的座標）
    double length = 0.0;  ///< 邊長（挑選當下）

    /// >=0 表示這是 Loft 的斷面角點定址（見上方說明），優先於幾何猜配對使用。
    int loopIndex   = -1;
    int cornerIndex = -1;
};

class ChamferSolid : public Feature {
    Q_OBJECT
public:
    explicit ChamferSolid(Document* parent = nullptr);
    ~ChamferSolid() override = default;

    FeatureType type()       const override { return FeatureType::Chamfer; }
    QString     typeString() const override { return QStringLiteral("Chamfer"); }

    /** 來源實體 Feature（任何 shape() 非空的 Feature，例如 Loft/Extrude）。 */
    void setSourceFeature(Feature* src);
    // ⚠️ 比照 ProfileLoftSolid::sourceArray() 的理由：不能寫成 inline，
    //    這裡 Document 只是前向宣告，QPointer<T>::data() 需要完整型別。
    Feature* sourceFeature() const;
    QString sourceFeatureId() const;

    void setDistance(double d) { m_distance = d; }
    double distance() const { return m_distance; }

    void setEdgeSignatures(const QVector<EdgeSignature>& sigs) { m_edgeSignatures = sigs; }
    const QVector<EdgeSignature>& edgeSignatures() const { return m_edgeSignatures; }

    /**
     * @brief 供 ChamferCommand 在使用者挑選邊的當下直接呼叫，計算該邊的定址資訊。
     * @param edge   被選取的邊
     * @param source 該邊所屬的來源 Feature（可為 nullptr）。若是
     *               ProfileLoftSolid，會反查（比對 IsSame()）出對應的
     *               loopIndex/cornerIndex，優先於幾何簽章使用；不是
     *               ProfileLoftSolid（或 nullptr）時只填中點座標＋邊長，
     *               走幾何猜配對模式（見 EdgeSignature 說明）。
     */
    static EdgeSignature makeSignature(const TopoDS_Edge& edge, Feature* source = nullptr);

    bool rebuild() override;

    QJsonObject toJson()  const override;
    bool fromJson(const QJsonObject& json) override;
    QSet<QString> featureDependencies() const override;

    /** 解析 fromJson() 暫存的 sourceFeatureId。 */
    void resolveReferences(Document* doc);

private:
    /** 在 candidateShape 中找出與 sig 幾何最接近的邊；找不到合理配對回傳 Null Edge。 */
    static TopoDS_Edge findMatchingEdge(const TopoDS_Shape& candidateShape,
                                         const EdgeSignature& sig);

    QPointer<Feature> m_source;
    QMetaObject::Connection m_sourceRebuiltConn;
    QString m_pendingSourceFeatureId;

    double m_distance = 0.0;
    QVector<EdgeSignature> m_edgeSignatures;
};

} // namespace cad
} // namespace aicad
