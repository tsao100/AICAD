// cripting/LispEngine.h
#pragma once
#include <QString>
#include <QVariant>
#include <QHash>
#include <QVector>
#include <functional>

namespace aicad::scripting {

class LispEngine {
public:
    using Args = QVector<QVariant>;
    using Func = std::function<QVariant(const Args&)>;
    using Fallback =
        std::function<QVariant(const QString&, const Args&)>;

    void setFallbackHandler(Fallback fb);

    LispEngine();

    void registerFunction(const QString& name, Func fn);

    // Evaluate single expression: "(line 0 0 100 0)"
    QVariant eval(const QString& code);

private:
    QVariant evalExpression(const QString& expr);
    QStringList tokenize(const QString& expr);

    QHash<QString, Func> m_functions;
    Fallback m_fallback;

};

} // namespace
