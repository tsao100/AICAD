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
#include <QPointer>

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

    QJsonObject toJson()  const override;
    bool fromJson(const QJsonObject& json) override;
    QSet<QString> featureDependencies() const override;

    /** 解析 fromJson() 暫存的 sourceArrayId。 */
    void resolveReferences(Document* doc);

private:
    QPointer<AlignedProfileArray> m_array;
    QMetaObject::Connection m_arrayRebuiltConn;
    QString m_pendingSourceArrayId;
};

} // namespace cad
} // namespace aicad
