#pragma once
#include <QString>
#include <QHash>
#include <QVariant>
#include <QObject>

namespace aicad::core {

/**
 * 參數表達式：可以是純數值或含參數名的算式
 * 例："100"、"width * 2"、"height + offset"
 */
struct ParameterExpr {
    QString expression;   ///< 原始表達式字串（設計意圖）
    double  cachedValue;  ///< 上次求值結果

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
 * 具名參數倉庫（Document 層級）
 * 提供算式求值（支援基本四則與參數名替換）
 */
class ParameterStore : public QObject {
    Q_OBJECT
public:
    explicit ParameterStore(QObject* parent = nullptr);

    // 新增 / 更新參數
    void set(const QString& name, double value);
    void set(const QString& name, const QString& expression);

    // 查詢
    bool        has(const QString& name) const;
    double      value(const QString& name) const;       ///< 0.0 if not found
    QString     expression(const QString& name) const;
    QStringList names() const;

    // 求值：將 expr 中的參數名替換後計算
    // 回傳 {ok, result}
    std::pair<bool, double> evaluate(const ParameterExpr& expr) const;
    std::pair<bool, double> evaluate(const QString& exprStr) const;

    QJsonObject toJson() const;
    bool        fromJson(const QJsonObject& json);

Q_SIGNALS:
    void parameterChanged(const QString& name, double newValue);

private:
    struct Param { QString expr; double value; };
    QHash<QString, Param> m_params;
};

} // namespace aicad::core
