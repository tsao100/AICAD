#pragma once
#include <QHash>
#include <QSet>
#include <QList>
#include <QString>

namespace aicad::cad {

/**
 * 有向無環圖（DAG）：追蹤 Feature 間的依賴關係
 *
 * 邊語意：A → B  表示「A 依賴 B」（B 必須先 rebuild）
 *
 * 提供 Kahn 演算法拓撲排序，
 * 結果即為安全的 rebuild 順序（被依賴者在前）。
 */
class DependencyGraph {
public:
    DependencyGraph() = default;

    // 聲明「nodeId 依賴 depId」
    void addDependency(const QString& nodeId, const QString& depId);

    // 移除節點（特徵刪除時呼叫）
    void removeNode(const QString& nodeId);

    // 取得某節點的直接依賴（它依賴誰）
    QSet<QString> dependenciesOf(const QString& nodeId) const;

    // 取得所有直接依賴某節點的節點（誰依賴它）
    QSet<QString> dependantsOf(const QString& nodeId) const;

    // Kahn 拓撲排序，回傳安全 rebuild 順序
    // 若有循環回傳空 list 並設 hasCycle = true
    QList<QString> topologicalOrder(bool* hasCycle = nullptr) const;

    // 從某節點往下（順著依賴方向）取得所有需要重建的節點
    QList<QString> affectedBy(const QString& changedId) const;

    void clear();

    void ensureNode(const QString& id);

private:
    // deps[A] = {B, C}  → A 依賴 B 和 C
    QHash<QString, QSet<QString>> m_deps;
    // rdeps[B] = {A}     → B 被 A 所依賴
    QHash<QString, QSet<QString>> m_rdeps;

};

} // namespace aicad::cad
