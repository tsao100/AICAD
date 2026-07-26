#pragma once
#include <QString>
#include <QHash>
#include <QVariant>
#include <QObject>
#include <QStringList>
#include <QJsonObject>
#include <utility>

namespace aicad::core {

/**
 * 參數表達式：可以是純數值或含參數名的算式
 */
struct ParameterExpr {
    QString expression;
    double  cachedValue;

    ParameterExpr() : cachedValue(0.0) {}
    explicit ParameterExpr(double v)
        : expression(QString::number(v)), cachedValue(v) {}
    explicit ParameterExpr(const QString& expr, double fallback = 0.0)
        : expression(expr), cachedValue(fallback) {}

    bool isLiteral() const {
        bool ok; expression.toDouble(&ok); return ok;
    }
};

/**
 * @brief 具名參數倉庫 — 支援父子作用域鏈與依賴圖
 *
 * 查詢順序（Scope Chain）：
 *   instance store → sketch store → document global store → 找不到 → 錯誤
 *
 * 本地操作（setLocal / hasLocal / removeLocal）只影響本層，不影響父層。
 * has() / value() / names() 會沿鏈查找。
 */
class ParameterStore : public QObject {
    Q_OBJECT
public:
    explicit ParameterStore(QObject* parent = nullptr);

    // ── 作用域鏈 ──────────────────────────────────────────────────
    /**
     * 設定父 store（查不到本地值時向上委派）
     * instance store → sketch store → document global store
     */
    void setParentStore(ParameterStore* parent);
    ParameterStore* parentStore() const { return m_parentStore; }

    // ── 本地操作（不影響父層）────────────────────────────────────
    void setLocal(const QString& name, double value);
    void setLocal(const QString& name, const QString& expression);
    bool hasLocal(const QString& name) const;
    void removeLocal(const QString& name);
    QStringList localNames() const;

    // ── 舊版相容介面（set = setLocal，不含 scope chain 語意）─────
    void set(const QString& name, double value)      { setLocal(name, value); }
    void set(const QString& name, const QString& e)  { setLocal(name, e); }

    // ── 帶 fallback 的查詢（優先本地，找不到往父層找）────────────
    bool        has(const QString& name) const;
    double      value(const QString& name) const;
    QString     expression(const QString& name) const;
    QStringList names() const;          ///< 合併本地 + parent（去重）

    // ── 求值（表達式中的參數名會沿作用域鏈解析）─────────────────
    std::pair<bool, double> evaluate(const ParameterExpr& expr) const;
    std::pair<bool, double> evaluate(const QString& exprStr) const;

    // ── 依賴圖管理 ────────────────────────────────────────────────
    bool        rename(const QString& oldName, const QString& newName);
    bool        remove(const QString& name);
    QStringList dependentsOf(const QString& name) const;
    bool        wouldCreateCycle(const QString& name, const QString& expr) const;
    void        recomputeAll();
    QStringList evaluationOrder() const;  ///< 拓撲排序結果

    // ── 序列化（只序列化本地值，不含 parent）────────────────────
    QJsonObject toJson() const;
    bool        fromJson(const QJsonObject& json);

Q_SIGNALS:
    void parameterChanged(const QString& name, double newValue);
    void parametersRecomputed();

private:
    struct Param { QString expr; double value; QStringList deps; };
    QHash<QString, Param>  m_params;       // 本地參數
    ParameterStore*        m_parentStore = nullptr;

    QStringList extractDependencies(const QString& expr) const;
    QStringList topoSort() const;
};

} // namespace aicad::core
