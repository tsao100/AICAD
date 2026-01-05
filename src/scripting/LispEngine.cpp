// src/scripting/LispEngine.cpp

#include "LispEngine.h"
#include <QDebug>

namespace aicad::scripting {

LispEngine::LispEngine() {}

void LispEngine::registerFunction(const QString& name, Func fn) {
    m_functions.insert(name.toLower(), fn);
}

QVariant LispEngine::eval(const QString& code) {
    QString expr = code.trimmed();
    if (!expr.startsWith("(") || !expr.endsWith(")")) {
        qWarning() << "[Lisp] Invalid expression:" << code;
        return {};
    }
    return evalExpression(expr.mid(1, expr.length() - 2));
}

QVariant LispEngine::evalExpression(const QString& expr)
{
    QStringList tokens = tokenize(expr);
    if (tokens.isEmpty())
        return {};

    QString fn = tokens.takeFirst().toLower();

    Args args;
    for (const auto& t : tokens)
        args << parseValue(t);

    if (m_functions.contains(fn))
        return m_functions[fn](args);

    // ★ Phase 3：交給 fallback
    if (m_fallback)
        return m_fallback(fn, args);

    qWarning() << "[Lisp] Unknown function:" << fn;
    return {};
}

QStringList LispEngine::tokenize(const QString& expr) {
    // Phase 1: space-based tokenizer
    return expr.split(QRegExp("\\s+"), Qt::SkipEmptyParts);
}

} // namespace
