#include "ParameterStore.h"
#include <QJSEngine>   // Qt 內建 JS 引擎做算式求值
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace aicad::core {

ParameterStore::ParameterStore(QObject* parent) : QObject(parent) {}

void ParameterStore::set(const QString& name, double value) {
    m_params[name] = { QString::number(value, 'g', 15), value };
    Q_EMIT parameterChanged(name, value);
}

void ParameterStore::set(const QString& name, const QString& expression) {
    auto [ok, v] = evaluate(expression);
    m_params[name] = { expression, ok ? v : 0.0 };
    if (ok) Q_EMIT parameterChanged(name, v);
}

bool   ParameterStore::has(const QString& name) const { return m_params.contains(name); }
double ParameterStore::value(const QString& name) const {
    return m_params.value(name).value;
}
QString ParameterStore::expression(const QString& name) const {
    return m_params.value(name).expr;
}
QStringList ParameterStore::names() const { return m_params.keys(); }

std::pair<bool, double> ParameterStore::evaluate(const ParameterExpr& expr) const {
    return evaluate(expr.expression);
}

std::pair<bool, double> ParameterStore::evaluate(const QString& exprStr) const {
    // 1. 先試純數字
    bool ok;
    double v = exprStr.toDouble(&ok);
    if (ok) return {true, v};

    // 2. 把已知參數名替換成數值後交給 QJSEngine
    QJSEngine engine;
    for (auto it = m_params.cbegin(); it != m_params.cend(); ++it)
        engine.globalObject().setProperty(it.key(), it.value().value);

    QJSValue result = engine.evaluate(exprStr);
    if (result.isError() || !result.isNumber()) {
        qWarning() << "[ParameterStore] eval failed:" << exprStr << result.toString();
        return {false, 0.0};
    }
    return {true, result.toNumber()};
}

QJsonObject ParameterStore::toJson() const {
    QJsonObject obj;
    for (auto it = m_params.cbegin(); it != m_params.cend(); ++it)
        obj[it.key()] = it.value().expr;   // 儲存表達式（設計意圖）
    return obj;
}

bool ParameterStore::fromJson(const QJsonObject& json) {
    m_params.clear();
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        set(it.key(), it.value().toString());
    }
    return true;
}

} // namespace aicad::core
