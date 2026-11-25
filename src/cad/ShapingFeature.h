/**
 * @file ShapingFeature.h
 * @brief 「成形特徵」共同基底：擠出／旋轉／掃掠／放樣都先產生一個「工具形狀」，
 *        再依 SolidOp 與目標實體做運算——
 *          NewBody：工具形狀本身就是結果（原本 Extrude 的行為）
 *          Fuse   ：目標 ∪ 工具（加料）
 *          Cut    ：目標 − 工具（挖除）
 *          Common ：目標 ∩ 工具
 *
 * 目標實體（target）是文件裡另一個實體 Feature。依賴與重建行為比照 ChamferSolid：
 *   • 目標被列入 featureDependencies() → 目標變成「中間結果」，3D 視圖只顯示最後的結果
 *   • 目標 shape 變動 → 自動重建本特徵
 *   • 特徵樹上本特徵掛在目標底下
 */
#pragma once

#include "Feature.h"
#include "SolidOps.h"

#include <QPointer>
#include <QSet>
#include <QString>
#include <QMetaObject>

namespace aicad {
namespace cad {

class ShapingFeature : public Feature {
    Q_OBJECT
public:
    explicit ShapingFeature(Document* parent = nullptr);
    ~ShapingFeature() override = default;

    SolidOp  operation() const { return m_op; }
    Feature* target() const { return m_target.data(); }
    QString  targetId() const { return m_target ? m_target->id() : m_pendingTargetId; }

    /**
     * @brief 設定運算方式與目標實體。不會觸發重建（呼叫端接著 markDirty() 或另外改參數）。
     *        op 為 NewBody 時目標一律清除。
     */
    void setOperationAndTarget(SolidOp op, Feature* target);

    /// 讀檔後解析暫存的 targetId（Document::load 呼叫）
    void resolveTargetReference(Document* doc);

    /// 最近一次重建所用的「工具形狀」（陣列／鏡射的「特徵」模式要重複它）
    TopoDS_Shape toolShape() const { return m_lastTool; }

protected:
    /// 子類別 featureDependencies() 應把這個集合併進去
    QSet<QString> targetDependencies() const;

    /// 寫入／讀出 operation、targetId
    void writeOpJson(QJsonObject& obj) const;
    void readOpJson(const QJsonObject& obj);

    /**
     * @brief 把工具形狀依 operation() 與目標實體合成最終結果。
     *        失敗時已 setError()，回傳 Null shape。
     */
    TopoDS_Shape combineWithTarget(const TopoDS_Shape& tool);

private:
    void syncDependenciesAndTree();

    TopoDS_Shape m_lastTool;      ///< 最近一次 combineWithTarget() 收到的工具形狀

    SolidOp                 m_op = SolidOp::NewBody;
    QPointer<Feature>       m_target;
    QMetaObject::Connection m_targetConn;
    QString                 m_pendingTargetId;
};

} // namespace cad
} // namespace aicad
