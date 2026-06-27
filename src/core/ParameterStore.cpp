#include "ParameterStore.h"
#include <QJSEngine>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QRegularExpression>

namespace aicad::core {

ParameterStore::ParameterStore(QObject* parent) : QObject(parent) {}

// ── 作用域鏈 ──────────────────────────────────────────────────────────────

void ParameterStore::setParentStore(ParameterStore* parent) {
    if (m_parentStore == parent) return;
    if (m_parentStore)
        disconnect(m_parentStore, &ParameterStore::parametersRecomputed,
                   this, &ParameterStore::recomputeAll);
    m_parentStore = parent;
    if (m_parentStore)
        connect(m_parentStore, &ParameterStore::parametersRecomputed,
                this, &ParameterStore::recomputeAll,
                Qt::UniqueConnection);
}

// ── 本地操作 ──────────────────────────────────────────────────────────────

void ParameterStore::setLocal(const QString& name, double value) {
    Param p;
    p.expr  = QString::number(value, 'g', 15);
    p.value = value;
    p.deps  = QStringList();          // 避免 {} 歧義
    m_params[name] = p;
    Q_EMIT parameterChanged(name, value);
    Q_EMIT parametersRecomputed();
}

void ParameterStore::setLocal(const QString& name, const QString& expression) {
    if (wouldCreateCycle(name, expression)) {
        qWarning() << "[ParameterStore] Cycle detected for" << name << "=" << expression;
        return;
    }
    auto [ok, v] = evaluate(expression);
    Param p;
    p.expr  = expression;
    p.value = ok ? v : 0.0;
    p.deps  = extractDependencies(expression);
    m_params[name] = p;
    if (ok) Q_EMIT parameterChanged(name, v);
    Q_EMIT parametersRecomputed();
}

bool ParameterStore::hasLocal(const QString& name) const {
    return m_params.contains(name);
}

void ParameterStore::removeLocal(const QString& name) {
    if (m_params.remove(name) != 0)
        Q_EMIT parametersRecomputed();
}

QStringList ParameterStore::localNames() const {
    return m_params.keys();
}

// ── 帶 fallback 的查詢 ───────────────────────────────────────────────────

bool ParameterStore::has(const QString& name) const {
    if (m_params.contains(name)) return true;
    if (m_parentStore) return m_parentStore->has(name);
    return false;
}

double ParameterStore::value(const QString& name) const {
    if (m_params.contains(name)) return m_params.value(name).value;
    if (m_parentStore) return m_parentStore->value(name);
    return 0.0;
}

QString ParameterStore::expression(const QString& name) const {
    if (m_params.contains(name)) return m_params.value(name).expr;
    if (m_parentStore) return m_parentStore->expression(name);
    return QString();
}

QStringList ParameterStore::names() const {
    QStringList result = m_params.keys();
    if (m_parentStore) {
        for (const QString& n : m_parentStore->names()) {
            if (!result.contains(n))
                result.append(n);
        }
    }
    return result;
}

// ── 求值（含 scope chain）────────────────────────────────────────────────

std::pair<bool, double> ParameterStore::evaluate(const ParameterExpr& expr) const {
    return evaluate(expr.expression);
}

std::pair<bool, double> ParameterStore::evaluate(const QString& exprStr) const {
    if (exprStr.isEmpty()) return std::make_pair(false, 0.0);

    // 1. 純數字
    bool ok;
    double v = exprStr.toDouble(&ok);
    if (ok) return std::make_pair(true, v);

    // 2. 建立 JS 引擎，先注入 parent scope，再注入本地（覆蓋）
    QJSEngine engine;

    if (m_parentStore) {
        for (const QString& n : m_parentStore->names())
            engine.globalObject().setProperty(n, m_parentStore->value(n));
    }
    for (auto it = m_params.cbegin(); it != m_params.cend(); ++it)
        engine.globalObject().setProperty(it.key(), it.value().value);

    QJSValue result = engine.evaluate(exprStr);
    if (result.isError() || !result.isNumber()) {
        qWarning() << "[ParameterStore] eval failed:" << exprStr << result.toString();
        return std::make_pair(false, 0.0);
    }
    return std::make_pair(true, result.toNumber());
}

// ── 依賴圖管理 ───────────────────────────────────────────────────────────

QStringList ParameterStore::extractDependencies(const QString& expr) const {
    static QRegularExpression re(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*"));
    static const QStringList jsKeywords = QStringList()
        << "Math" << "abs" << "sqrt" << "sin" << "cos" << "tan" << "PI"
        << "floor" << "ceil" << "round" << "min" << "max" << "pow" << "log";

    QStringList deps;
    auto it = re.globalMatch(expr);
    while (it.hasNext()) {
        QString tok = it.next().captured(0);
        if (!jsKeywords.contains(tok) && !deps.contains(tok))
            deps.append(tok);
    }
    return deps;
}

bool ParameterStore::wouldCreateCycle(const QString& name,
                                       const QString& expr) const {
    QStringList deps = extractDependencies(expr);
    QStringList visited;
    std::function<bool(const QString&)> dfs = [&](const QString& n) -> bool {
        if (n == name) return true;
        if (visited.contains(n)) return false;
        visited.append(n);
        if (!m_params.contains(n)) return false;
        for (const QString& dep : m_params.value(n).deps)
            if (dfs(dep)) return true;
        return false;
    };
    for (const QString& dep : deps)
        if (dfs(dep)) return true;
    return false;
}

QStringList ParameterStore::topoSort() const {
    // Kahn's algorithm
    QHash<QString, int>         inDegree;
    QHash<QString, QStringList> adj;

    for (auto it = m_params.cbegin(); it != m_params.cend(); ++it) {
        inDegree.insert(it.key(), 0);
        adj.insert(it.key(), QStringList());   // 明確型別，避免 {} 歧義
    }
    for (auto it = m_params.cbegin(); it != m_params.cend(); ++it) {
        for (const QString& dep : it.value().deps) {
            if (m_params.contains(dep)) {
                adj[dep].append(it.key());
                inDegree[it.key()]++;
            }
        }
    }

    QStringList queue;
    QStringList order;
    for (auto it = inDegree.cbegin(); it != inDegree.cend(); ++it)
        if (it.value() == 0) queue.append(it.key());

    while (!queue.isEmpty()) {
        QString cur = queue.takeFirst();
        order.append(cur);
        for (const QString& dep : adj.value(cur)) {
            if (--inDegree[dep] == 0)
                queue.append(dep);
        }
    }
    return order;
}

QStringList ParameterStore::evaluationOrder() const {
    return topoSort();
}

QStringList ParameterStore::dependentsOf(const QString& name) const {
    QStringList result;
    for (auto it = m_params.cbegin(); it != m_params.cend(); ++it)
        if (it.value().deps.contains(name))
            result.append(it.key());
    return result;
}

bool ParameterStore::rename(const QString& oldName, const QString& newName) {
    if (!m_params.contains(oldName) || m_params.contains(newName))
        return false;

    Param p = m_params.take(oldName);

    for (auto it = m_params.begin(); it != m_params.end(); ++it) {
        if (it.value().deps.contains(oldName)) {
            QString e = it.value().expr;
            e.replace(QRegularExpression(
                QStringLiteral("\\b%1\\b").arg(
                    QRegularExpression::escape(oldName))), newName);
            it.value().expr = e;
            it.value().deps.removeAll(oldName);
            it.value().deps.append(newName);
        }
    }

    p.deps.replaceInStrings(oldName, newName);
    m_params[newName] = p;
    Q_EMIT parametersRecomputed();
    return true;
}

bool ParameterStore::remove(const QString& name) {
    if (!m_params.contains(name)) return false;
    m_params.remove(name);
    Q_EMIT parametersRecomputed();
    return true;
}

void ParameterStore::recomputeAll() {
    for (const QString& name : topoSort()) {
        if (!m_params.contains(name)) continue;
        auto [ok, v] = evaluate(m_params[name].expr);
        if (ok && !qFuzzyCompare(m_params[name].value + 1.0, v + 1.0)) {
            m_params[name].value = v;
            Q_EMIT parameterChanged(name, v);
        }
    }
    Q_EMIT parametersRecomputed();
}

// ── 序列化（只序列化本地值）─────────────────────────────────────────────

QJsonObject ParameterStore::toJson() const {
    QJsonObject obj;
    for (auto it = m_params.cbegin(); it != m_params.cend(); ++it)
        obj[it.key()] = it.value().expr;
    return obj;
}

bool ParameterStore::fromJson(const QJsonObject& json) {
    m_params.clear();
    for (auto it = json.constBegin(); it != json.constEnd(); ++it)
        setLocal(it.key(), it.value().toString());
    return true;
}

} // namespace aicad::core
