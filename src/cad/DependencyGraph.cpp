#include "DependencyGraph.h"
#include <QQueue>
#include <QDebug>

namespace aicad::cad {

void DependencyGraph::ensureNode(const QString& id) {
    if (!m_deps.contains(id))  m_deps[id]  = {};
    if (!m_rdeps.contains(id)) m_rdeps[id] = {};
}

void DependencyGraph::addDependency(const QString& nodeId, const QString& depId) {
    ensureNode(nodeId);
    ensureNode(depId);
    m_deps[nodeId].insert(depId);
    m_rdeps[depId].insert(nodeId);
}

void DependencyGraph::removeNode(const QString& nodeId) {
    // 清除它對外的依賴
    for (const QString& dep : m_deps.value(nodeId))
        m_rdeps[dep].remove(nodeId);
    // 清除別人對它的依賴
    for (const QString& rdep : m_rdeps.value(nodeId))
        m_deps[rdep].remove(nodeId);
    m_deps.remove(nodeId);
    m_rdeps.remove(nodeId);
}

QSet<QString> DependencyGraph::dependenciesOf(const QString& nodeId) const {
    return m_deps.value(nodeId);
}

QSet<QString> DependencyGraph::dependantsOf(const QString& nodeId) const {
    return m_rdeps.value(nodeId);
}

QList<QString> DependencyGraph::topologicalOrder(bool* hasCycle) const {
    // inDegree[A] = A 的前驅數（有多少 feature 以 A 為依賴）
    // m_deps[A] = A 依賴的集合，m_rdeps[A] = 依賴 A 的集合
    QHash<QString, int> inDegree;
    for (auto it = m_deps.cbegin(); it != m_deps.cend(); ++it)
        inDegree[it.key()] += 0;           // 確保每個 node 都存在
    for (auto it = m_rdeps.cbegin(); it != m_rdeps.cend(); ++it)
        inDegree[it.key()] = it.value().size();  // rdeps 大小 = 入度

    // 從入度為 0 的節點（無前驅，即「葉」 / 被依賴的基礎節點）開始
    QQueue<QString> queue;
    for (auto it = inDegree.cbegin(); it != inDegree.cend(); ++it)
        if (it.value() == 0) queue.enqueue(it.key());

    QList<QString> order;
    while (!queue.isEmpty()) {
        QString node = queue.dequeue();
        order.append(node);
        // ✅ 修正：應遞減「依賴 node 的人」的入度（沿 rdeps 方向）
        for (const QString& dependant : m_rdeps.value(node)) {
            if (--inDegree[dependant] == 0)
                queue.enqueue(dependant);
        }
    }

    bool cycle = (order.size() != inDegree.size());
    if (hasCycle) *hasCycle = cycle;
    if (cycle) {
        qWarning() << "[DependencyGraph] Cycle detected!";
        return {};
    }
    // ✅ 不需要 reverse：order 已是「被依賴者先、依賴者後」（Sketch → Extrude）
    return order;
}

QList<QString> DependencyGraph::affectedBy(const QString& changedId) const {
    // BFS 沿 rdeps 方向收集所有受影響節點
    QSet<QString> visited;
    QQueue<QString> q;
    q.enqueue(changedId);
    visited.insert(changedId);
    while (!q.isEmpty()) {
        QString cur = q.dequeue();
        for (const QString& rdep : m_rdeps.value(cur)) {
            if (!visited.contains(rdep)) {
                visited.insert(rdep);
                q.enqueue(rdep);
            }
        }
    }
    visited.remove(changedId);

    // 以拓撲順序回傳受影響者
    bool cycle;
    QList<QString> fullOrder = topologicalOrder(&cycle);
    QList<QString> result;
    for (const QString& id : fullOrder)
        if (visited.contains(id)) result.append(id);
    return result;
}

void DependencyGraph::clear() {
    m_deps.clear();
    m_rdeps.clear();
}

} // namespace aicad::cad
