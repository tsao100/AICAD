/**
 * @file AssemblyMate.h
 * @brief 組合中的「配合」：兩個幾何參照（點／軸線／平面；屬於某個零件或世界）之間的關係。
 *
 * 類型：重合、共線、共面、平行、垂直、距離、夾角（見 AssemblySolver.h 的對照表）。
 * 與座標軸／座標平面的關係 = 其中一個參照屬於世界（ownerId 為空），例如「零件某面與世界 XY 平面共面、距離 30」。
 * 距離／夾角的值可用數字或參數運算式（距離為組合單位、夾角為度）。
 *
 * 配合本身沒有形狀；Document::solveAssembly() 蒐集全部零件與配合，求解後更新零件位姿。
 * 抑制（suppressed）的配合不參與求解；無法滿足的配合會顯示錯誤狀態。
 */
#pragma once

#include "AssemblyGeom.h"
#include "AssemblySolver.h"
#include "Feature.h"

namespace aicad {
namespace cad {

class AssemblyMate : public Feature {
    Q_OBJECT
public:
    explicit AssemblyMate(Document* parent = nullptr);
    ~AssemblyMate() override = default;

    FeatureType type()       const override { return FeatureType::AssemblyMate; }
    QString     typeString() const override { return QStringLiteral("AssemblyMate"); }

    /// 配合的完整定義（對話框編輯用）
    struct Data {
        asmsolve::MateType type   = asmsolve::MateType::Coplanar;
        MateRefData        a, b;
        asmsolve::Orient   orient = asmsolve::Orient::Any;
        QString            valueExpr = QStringLiteral("0");   ///< Distance：長度；Angle：度
    };
    Data data() const;
    /// 設定定義（先驗證參照組合與運算式；失敗不修改）。成功後呼叫端接著要求重新求解。
    bool setData(const Data& d, QString* error = nullptr);

    asmsolve::MateType mateType() const { return m_data.type; }
    const MateRefData& refA() const { return m_data.a; }
    const MateRefData& refB() const { return m_data.b; }
    MateRefData&       refA() { return m_data.a; }
    MateRefData&       refB() { return m_data.b; }
    asmsolve::Orient   orient() const { return m_data.orient; }
    QString            valueExpression() const { return m_data.valueExpr; }

    /// 目前求得的數值：距離為組合單位、夾角為「度」；沒有值的配合為 0
    double value() const { return m_value; }
    bool   usesValue()  const;
    bool   usesOrient() const;

    /// 運算式求值（可用文件參數）。失敗回傳 false 並說明。
    static bool evalExpr(const Document* doc, const QString& expr, double& out);

    /// 一行描述，例如「共面 · 零件A 面 #3 ↔ 零件B 面 #7」
    QString description() const;
    static QString typeLabel(asmsolve::MateType t);
    static QString orientLabel(asmsolve::Orient o);
    /// 參照所屬零件的名稱（世界 → 「世界」）；零件已不存在 → 「（已刪除）」
    QString ownerName(const MateRefData& r) const;

    /// 此配合是否參照了該零件
    bool referencesPart(const QString& partId) const { return m_data.a.ownerId == partId || m_data.b.ownerId == partId; }

    /// 依各零件目前的（來源同步後的）形狀，用面／邊序號重新取得參照幾何。回傳有更新的參照數。
    int refreshReferences();

    bool rebuild() override;            ///< 重新求值，並排程重新求解整個組合

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;

    QSet<QString> featureDependencies() const override { return {}; }

private:
    Data   m_data;
    double m_value = 0.0;
};

} // namespace cad
} // namespace aicad
