#include "ParameterStore.h"
#include <QJSEngine>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QRegularExpression>
#include <QJSValue>
#include <functional>
#include <cmath>

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
    // ⚠️ 修正：「相關參數也要同步重新計算更新」——這裡原本只更新
    // name 自己這一筆，若有其他本地參數的表達式依賴 name（例如
    // d2 = "d1 * 2"，這裡改的是 d1），d2 快取的 .value 完全沒被動到，
    // 會一直沿用改動前的舊值，直到「剛好」有別的地方觸發一次
    // recomputeAll() 才會被動更新，造成「改了 d1，用到 d2 的約束/畫面
    // 卻沒有跟著變」的現象。改成呼叫 recomputeAll()：它會依拓樸排序
    // 重新算過「所有」本地參數（含 name 自己與所有直接/間接依賴它的
    // 參數），對每個「值真的改變」的參數各自 emit parameterChanged，
    // 最後統一 emit 一次 parametersRecomputed()，取代這裡原本各自獨立
    // 的 emit。
    recomputeAll();
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
    // ⚠️ 修正：同上一個 setLocal() 重載——把「連鎖更新所有依賴此參數的
    // 其他本地參數」交給 recomputeAll() 統一處理，而不是只 emit name
    // 自己的 parameterChanged。
    recomputeAll();
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

QString ParameterStore::prepareEngine(QJSEngine& engine, const QString& exprStr) const {
    QString src = exprStr.trimmed();
    if (src.startsWith(QLatin1Char('=')))
        src = src.mid(1).trimmed();

    if (m_parentStore) {
        for (const QString& n : m_parentStore->names())
            engine.globalObject().setProperty(n, m_parentStore->value(n));
    }
    for (auto it = m_params.cbegin(); it != m_params.cend(); ++it)
        engine.globalObject().setProperty(it.key(), it.value().value);

    engine.evaluate(QStringLiteral(
        "var sin=Math.sin, cos=Math.cos, tan=Math.tan, sqrt=Math.sqrt, "
        "abs=Math.abs, pow=Math.pow, floor=Math.floor, ceil=Math.ceil, "
        "round=Math.round, min=Math.min, max=Math.max, log=Math.log, "
        "PI=Math.PI; "
        "function _if(c,a,b){ return c ? a : b; }"));

    // if( → _if(（if 是 JS 保留字，見 evaluate() 原本的說明）
    static const QRegularExpression ifCallRe(QStringLiteral("\\bif\\s*\\("));
    return src.replace(ifCallRe, QStringLiteral("_if("));
}

std::pair<bool, bool> ParameterStore::evaluateCondition(const QString& exprStr) const {
    if (exprStr.trimmed().isEmpty()) return std::make_pair(true, true);   // 無條件＝永遠啟用

    QJSEngine engine;
    const QString rewritten = prepareEngine(engine, exprStr);
    QJSValue result = engine.evaluate(rewritten);
    if (result.isError()) {
        qWarning() << "[ParameterStore] condition eval failed:" << exprStr << result.toString();
        return std::make_pair(false, true);
    }
    if (result.isBool())   return std::make_pair(true, result.toBool());
    if (result.isNumber()) {
        const double v = result.toNumber();
        return std::make_pair(!std::isnan(v), !std::isnan(v) && v != 0.0);
    }
    qWarning() << "[ParameterStore] condition is neither bool nor number:" << exprStr;
    return std::make_pair(false, true);
}

std::pair<bool, double> ParameterStore::evaluate(const QString& exprStr) const {
    if (exprStr.isEmpty()) return std::make_pair(false, 0.0);

    // 0. 容許 Excel 習慣的前綴 '='（例如 "=if(cant<0, abs(cant), 0)"）
    QString src = exprStr.trimmed();
    if (src.startsWith(QLatin1Char('=')))
        src = src.mid(1).trimmed();

    // 1. 純數字
    bool ok;
    double v = src.toDouble(&ok);
    if (ok) return std::make_pair(true, v);

    QJSEngine engine;
    const QString rewritten = prepareEngine(engine, src);

    QJSValue result = engine.evaluate(rewritten);
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
        << "floor" << "ceil" << "round" << "min" << "max" << "pow" << "log"
        << "if" << "_if" << "true" << "false";

    QString e = expr.trimmed();
    if (e.startsWith(QLatin1Char('=')))
        e = e.mid(1);

    QStringList deps;
    auto it = re.globalMatch(e);
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
    // ⚠️ 舊版逐一 setLocal()：QJsonObject 的鍵依字母排序（d1, d10, d11…, d2, … d9），
    //    所以 d10="d9/2" 會在 d9 還沒載入時就被求值 → "ReferenceError: d9 is not
    //    defined"（log 裡每個檔案十幾次），而且每個 setLocal() 又各自 recomputeAll()
    //    （每次求值各 new 一個 QJSEngine）→ n 個參數 = O(n²) 次求值，載入很慢。
    //    現在：先把所有「運算式」原樣放進去（不求值、不重算），全部放完後
    //    依拓樸順序只重算一次。
    m_params.clear();
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        const QString name = it.key();
        const QString expr = it.value().toString();
        if (wouldCreateCycle(name, expr)) {
            qWarning() << "[ParameterStore] Cycle detected for" << name << "=" << expr;
            continue;
        }
        Param p;
        p.expr  = expr;
        p.value = 0.0;
        p.deps  = extractDependencies(expr);
        m_params[name] = p;
    }
    recomputeAll();
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// ParameterScope
// ═════════════════════════════════════════════════════════════════════════════

namespace {

/// 與 ParameterStore::extractDependencies() 相同的識別字擷取（該函式是 private
/// 非 static 成員，這裡另寫一份檔內版本，避免為此放寬 ParameterStore 的封裝）。
QStringList scopeExtractIdentifiers(const QString& exprIn) {
    static const QRegularExpression re(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*"));
    static const QStringList jsKeywords = QStringList()
        << "Math" << "abs" << "sqrt" << "sin" << "cos" << "tan" << "PI"
        << "floor" << "ceil" << "round" << "min" << "max" << "pow" << "log"
        << "if" << "_if" << "true" << "false";

    QString e = exprIn.trimmed();
    if (e.startsWith(QLatin1Char('='))) e = e.mid(1);

    QStringList ids;
    auto it = re.globalMatch(e);
    while (it.hasNext()) {
        const QString tok = it.next().captured(0);
        if (!jsKeywords.contains(tok) && !ids.contains(tok))
            ids.append(tok);
    }
    return ids;
}

/// 單一識別字（例如 "d1"）？—— 快速路徑用。
bool isBareIdentifier(const QString& s) {
    if (s.isEmpty()) return false;
    const QChar c0 = s.at(0);
    if (!(c0.isLetter() || c0 == QLatin1Char('_'))) return false;
    for (const QChar c : s)
        if (!(c.isLetterOrNumber() || c == QLatin1Char('_'))) return false;
    return true;
}

} // namespace

ParameterScope::ParameterScope(const ParameterStore& store) {
    // 1. 收集定義：由最外層（document）到最內層（instance），內層同名者覆蓋外層。
    QList<const ParameterStore*> chain;
    for (const ParameterStore* s = &store; s; s = s->parentStore())
        chain.prepend(s);

    QHash<QString, QString> defs;
    for (const ParameterStore* s : chain)
        for (const QString& n : s->localNames())
            defs[n] = s->expression(n);   // n 為 s 的本地名稱 → 取到的就是 s 自己的運算式

    // 2. 依相依順序（DFS）逐一求值；循環相依者以 0 結算（與既有行為一致：求值失敗 → 0）。
    enum class St { Unvisited, InProgress, Done };
    QHash<QString, St> state;
    m_values.reserve(defs.size());

    std::function<void(const QString&)> resolve = [&](const QString& name) {
        const St st = state.value(name, St::Unvisited);
        if (st == St::Done) return;
        if (st == St::InProgress) { m_values[name] = 0.0; return; }   // cycle
        state[name] = St::InProgress;

        const QString expr = defs.value(name);
        for (const QString& dep : scopeExtractIdentifiers(expr))
            if (defs.contains(dep)) resolve(dep);

        const auto [ok, v] = evaluate(expr);
        m_values[name] = ok ? v : 0.0;
        state[name] = St::Done;
    };
    for (auto it = defs.cbegin(); it != defs.cend(); ++it)
        resolve(it.key());
}

ParameterScope::~ParameterScope() = default;

std::pair<bool, double> ParameterScope::evaluate(const QString& exprStr) const {
    if (exprStr.isEmpty()) return {false, 0.0};

    QString src = exprStr.trimmed();
    if (src.startsWith(QLatin1Char('=')))
        src = src.mid(1).trimmed();

    // 快速路徑 1：純數字
    bool ok = false;
    const double num = src.toDouble(&ok);
    if (ok) return {true, num};

    // 快速路徑 2：單一已解析的識別字
    if (isBareIdentifier(src)) {
        auto it = m_values.constFind(src);
        if (it != m_values.constEnd()) return {true, it.value()};
        // 未知識別字 → 落到 JS 引擎，保持與 ParameterStore::evaluate() 相同的失敗行為
    }

    const QJSValue result = runScript(src);
    if (result.isError() || !result.isNumber()) {
        qWarning() << "[ParameterScope] eval failed:" << exprStr << result.toString();
        return {false, 0.0};
    }
    return {true, result.toNumber()};
}

std::pair<bool, bool> ParameterScope::evaluateCondition(const QString& exprStr) const {
    if (exprStr.trimmed().isEmpty()) return {true, true};   // 無條件＝永遠啟用

    QString src = exprStr.trimmed();
    if (src.startsWith(QLatin1Char('=')))
        src = src.mid(1).trimmed();

    // 語意與 ParameterStore::evaluateCondition() 一致：布林取值、數值非 0 為真，
    // 無法求值回 (false, true)——呼叫端維持啟用（fail-safe）。
    const QJSValue result = runScript(src);
    if (result.isError()) {
        qWarning() << "[ParameterScope] condition eval failed:" << exprStr << result.toString();
        return {false, true};
    }
    if (result.isBool())   return {true, result.toBool()};
    if (result.isNumber()) {
        const double v = result.toNumber();
        return {!std::isnan(v), !std::isnan(v) && v != 0.0};
    }
    qWarning() << "[ParameterScope] condition is neither bool nor number:" << exprStr;
    return {false, true};
}

// 一般路徑：整個 scope 共用一個 QJSEngine（惰性建立）
QJSValue ParameterScope::runScript(const QString& srcIn) const {
    if (!m_engine) {
        m_engine = std::make_unique<QJSEngine>();
        m_engine->evaluate(QStringLiteral(
            "var sin=Math.sin, cos=Math.cos, tan=Math.tan, sqrt=Math.sqrt, "
            "abs=Math.abs, pow=Math.pow, floor=Math.floor, ceil=Math.ceil, "
            "round=Math.round, min=Math.min, max=Math.max, log=Math.log, "
            "PI=Math.PI; "
            "function _if(c,a,b){ return c ? a : b; }"));
    }
    // 只注入這個運算式實際用到的識別字（比整批注入便宜），值永遠取最新解析結果。
    for (const QString& id : scopeExtractIdentifiers(srcIn)) {
        auto it = m_values.constFind(id);
        if (it != m_values.constEnd())
            m_engine->globalObject().setProperty(id, it.value());
    }

    static const QRegularExpression ifCallRe(QStringLiteral("\\bif\\s*\\("));
    QString src = srcIn;
    const QString rewritten = src.replace(ifCallRe, QStringLiteral("_if("));
    return m_engine->evaluate(rewritten);
}

} // namespace aicad::core
