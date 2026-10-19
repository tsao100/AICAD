#pragma once
#include <QString>
#include <QHash>
#include <QVariant>
#include <QObject>
#include <QStringList>
#include <QJsonObject>
#include <utility>
#include <memory>

class QJSEngine;
class QJSValue;

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

    /**
     * 第 11 項：條件式求值（約束啟用條件，如 "H>0"、"H<=0 && W>5"）。
     * evaluate() 只接受數值結果，布林結果（JS 的 true/false）會被判成失敗，
     * 所以另開一個入口：布林直接取值，數值非 0 視為真。
     * 回傳 (成功?, 條件結果)。空字串視為「沒有條件」＝ (true, true)。
     * 參數名沿作用域鏈解析，與 evaluate() 相同。
     */
    std::pair<bool, bool> evaluateCondition(const QString& exprStr) const;

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

    /// evaluate()/evaluateCondition() 共用：注入參數與數學函式，回傳改寫後的原始碼
    QString prepareEngine(QJSEngine& engine, const QString& exprStr) const;
    QStringList extractDependencies(const QString& expr) const;
    QStringList topoSort() const;
};

/**
 * @brief 「已解析作用域」快照 —— 把 store 的整條作用域鏈（本層 → 父層 → …）
 *        一次解析成 name→value 表，並提供快速的表達式求值。
 *
 * 為什麼需要它（相對於 ParameterStore::evaluate()）：
 *   ParameterStore::evaluate() 求值時，父層參數只注入「快取值」（父層當下的
 *   .value），不會用子層的覆寫值去「重新推導」父層參數的表達式。因此：
 *     master:   cant = -130,  d2 = if(cant>0,0,abs(cant)),  d3 = abs(cant)/2
 *     instance: cant 覆寫成 +20
 *   instance 內 evaluate("d2") 仍會拿到 master 快取的 d2(=130)，而不是
 *   以 cant=+20 重算的 0 —— 子層覆寫對「間接引用 cant 的衍生參數」完全無效。
 *   （沿線斷面陣列的 cant/H/Thick 每站不同，正是依賴這個語意。）
 *
 *   ParameterScope 的做法：收集鏈上所有「定義」（運算式），子層同名者覆蓋
 *   父層，再依相依順序整批重新求值，因此衍生參數都以子層覆寫值為準。
 *
 * 效能：
 *   - 整個 scope 共用「最多一個」QJSEngine（惰性建立；全為純數字／單一識別字
 *     的參數完全不會建立引擎）。ParameterStore::evaluate() 則是每次求值都
 *     new 一個 QJSEngine（建立成本以毫秒計），一次 SketchInstance::rebuild()
 *     要對十幾個尺寸約束各求值一次。
 *   - evaluate(expr) 對「純數字」與「單一已知識別字（例如 "d1"）」走快速路徑，
 *     不經 JS。
 */
class ParameterScope {
public:
    explicit ParameterScope(const ParameterStore& store);
    ~ParameterScope();
    ParameterScope(const ParameterScope&) = delete;
    ParameterScope& operator=(const ParameterScope&) = delete;

    bool   has(const QString& name) const { return m_values.contains(name); }
    double value(const QString& name) const { return m_values.value(name, 0.0); }

    /// 以本 scope 的（已解析）參數值求任意表達式；失敗回 {false, 0}。
    /// 語法與 ParameterStore::evaluate() 相同（前綴 '='、if(...)、Math 函式）。
    std::pair<bool, double> evaluate(const QString& exprStr) const;

    /// 條件式求值（對應 ParameterStore::evaluateCondition）：布林直接取值，
    /// 數值非 0 視為真；空字串＝(true,true)。回傳 (成功?, 條件結果)。
    /// 供 SketchInstance 以「已解析作用域」重新判定約束的 enableExpr。
    std::pair<bool, bool> evaluateCondition(const QString& exprStr) const;

private:
    QJSValue runScript(const QString& src) const;   // evaluate()/evaluateCondition() 共用

    QHash<QString, double>           m_values;
    mutable std::unique_ptr<QJSEngine> m_engine;
};

} // namespace aicad::core
