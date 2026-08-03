/**
 * @file ProfileLoftSolid.h
 * @brief 將 AlignedProfileArray 各測站的封閉輪廓，沿樁號順序放樣成實體
 *        （對應 ProfileArrayAlongAlignment_ImplementationPlan.md §6, Step 9）
 *
 * 使用 BRepOffsetAPI_ThruSections，isSolid=true、isRuled=false（spline 放樣，
 * 依規劃書之決議，見 §6.2 / §8）。各站的封閉輪廓由
 * AlignedProfileArray::stationWireLoops() 取得——一個斷面草圖裡可以有不只
 * 一個互不相連的封閉輪廓（例如左右兩個獨立的墊塊），此時每個迴圈各自沿
 * 測站方向放樣成一個實體，全部合併成一個 compound 當作最終形狀。
 */
#pragma once

#include "Feature.h"
#include <QString>
#include <QSet>
#include <QVector>
#include <QPointer>
#include <TopoDS_Edge.hxx>

namespace aicad {
namespace cad {

class AlignedProfileArray;

class ProfileLoftSolid : public Feature {
    Q_OBJECT
public:
    explicit ProfileLoftSolid(Document* parent = nullptr);
    ~ProfileLoftSolid() override = default;

    FeatureType type()       const override { return FeatureType::Loft; }
    QString     typeString() const override { return QStringLiteral("Loft"); }

    void setSourceArray(AlignedProfileArray* arr);
    // ⚠️ 不能寫成 inline { return m_array.data(); }：AlignedProfileArray 這裡只是
    //    前向宣告，QPointer<T>::data() 需要 T 是完整型別才能 static_cast，
    //    inline 展開會在任何 #include 這個標頭、但還沒看過 AlignedProfileArray
    //    完整定義的地方編譯失敗。實作移到 .cpp（那裡已
    //    #include "AlignedProfileArray.h"）。
    AlignedProfileArray* sourceArray() const;
    QString sourceArrayId() const;

    bool rebuild() override;

    /**
     * @brief 斷面角點定址（供 Chamfer 互動選邊反查、以及 Lisp 腳本直接指定
     *        邊使用，見 ChamferSolid.h / LispBindings.cpp 的 chamfer-edges）。
     *
     * 每個封閉輪廓（loop）沿測站方向放樣後，輪廓上每一個角點都會對應到
     *「剛好一條」貫穿全部測站的縱向邊（已用獨立驗證程式確認 —— 見
     * verify_thrusections_generated.cpp 及對話紀錄）。角點編號＝
     * BRepTools_WireExplorer 走訪第一站（station 0）該迴圈 wire 的頂點順序，
     * 只要 master 草圖的線段順序不變，這個編號在每次 rebuild() 之間保持
     * 穩定，即使測站數量、里程、高程改變也不受影響。
     *
     * rebuild() 失敗時，這三個查詢方法仍回傳「上一次成功 rebuild()」時的
     * 結果（跟 shape() 的行為一致 —— 失敗不會清空既有資料）。
     */
    int loopCount() const { return m_loopCornerEdges.size(); }
    int cornerCount(int loopIndex) const;
    /** 找不到（index 超出範圍，或該角點當初 Generated() 沒查到邊）回傳 Null Edge。 */
    TopoDS_Edge longitudinalEdge(int loopIndex, int cornerIndex) const;

    /**
     * @brief 目前 shape() 的實體體積（document 座標單位的立方，通常是 m³，
     *        因為 rebuild() 產生的 compound 座標已經是 TrackCenterLine/
     *        document 慣用的公尺制）。用 BRepGProp::VolumeProperties 對
     *        shape() 整個 compound 一次計算——compound 底下不論是一個或
     *        多個 loop 各自放樣出的 solid，VolumeProperties 都會自動加總。
     *        shape() 為空（例如 rebuild() 失敗過）時回傳 0。
     */
    double volume() const;

    QJsonObject toJson()  const override;
    bool fromJson(const QJsonObject& json) override;
    QSet<QString> featureDependencies() const override;

    /** 解析 fromJson() 暫存的 sourceArrayId。 */
    void resolveReferences(Document* doc);

private:
    QPointer<AlignedProfileArray> m_array;
    QMetaObject::Connection m_arrayRebuiltConn;
    QString m_pendingSourceArrayId;

    /// 外層 index = loopIndex，內層 index = cornerIndex（見 loopCount()/
    /// cornerCount()/longitudinalEdge() 的說明）。只在 rebuild() 成功時更新。
    QVector<QVector<TopoDS_Edge>> m_loopCornerEdges;
};

} // namespace cad
} // namespace aicad
