#include "InputParser.h"
#include <QRegularExpression>
#include <QtMath>
#include <QDebug>

namespace aicad {
namespace command {

ParsedInput InputParser::parse(const QString& input, InputType expectedType) {
    QString trimmed = input.trimmed();

    if (trimmed.isEmpty()) {
        return ParsedInput(InputType::Unknown, QVariant(), false, "Empty input");
    }

    // 根據期望類型或自動檢測
    if (expectedType == InputType::Unknown) {
        // 自動檢測類型
        if (isPolarCoordinate(trimmed)) {
            QVector2D coord = parsePolarCoord(trimmed, QVector2D());
            return ParsedInput(InputType::PolarCoordinate, coord, true);
        }
        else if (isRelativeCoordinate(trimmed)) {
            QVector2D coord = parseRelativeCoord(trimmed, QVector2D());
            return ParsedInput(InputType::RelativeCoordinate, coord, true);
        }
        else if (isAbsoluteCoordinate(trimmed)) {
            QVector2D coord = parseAbsoluteCoord(trimmed);
            return ParsedInput(InputType::AbsoluteCoordinate, coord, true);
        }
        else if (isNumber(trimmed)) {
            bool ok;
            double num = parseNumber(trimmed, &ok);
            if (ok) {
                return ParsedInput(InputType::Number, num, true);
            }
        }
        else {
            // 視為選項或字串
            return ParsedInput(InputType::Option, trimmed, true);
        }
    }
    else {
        // 根據期望類型解析
        switch (expectedType) {
        case InputType::AbsoluteCoordinate:
        case InputType::RelativeCoordinate:
        case InputType::PolarCoordinate: {
            QVector2D coord = parseCoordinate(trimmed);
            return ParsedInput(expectedType, coord, true);
        }

        case InputType::Number: {
            bool ok;
            double num = parseNumber(trimmed, &ok);
            if (ok) {
                return ParsedInput(InputType::Number, num, true);
            }
            return ParsedInput(InputType::Number, QVariant(), false, "Invalid number");
        }

        case InputType::String:
            return ParsedInput(InputType::String, parseString(trimmed), true);

        case InputType::Option:
            return ParsedInput(InputType::Option, trimmed, true);

        default:
            break;
        }
    }

    return ParsedInput(InputType::Unknown, QVariant(), false, "Cannot parse input");
}

QVector2D InputParser::parseCoordinate(const QString& input, const QVector2D& basePoint) {
    QString trimmed = input.trimmed();

    if (isPolarCoordinate(trimmed)) {
        return parsePolarCoord(trimmed, basePoint);
    }
    else if (isRelativeCoordinate(trimmed)) {
        return parseRelativeCoord(trimmed, basePoint);
    }
    else if (isAbsoluteCoordinate(trimmed)) {
        return parseAbsoluteCoord(trimmed);
    }

    return QVector2D();
}

QVector2D InputParser::parseAbsoluteCoord(const QString& input) {
    // 格式: "100,200" 或 "100, 200"
    QStringList parts = input.split(',');

    if (parts.size() != 2) {
        return QVector2D();
    }

    bool okX, okY;
    double x = parts[0].trimmed().toDouble(&okX);
    double y = parts[1].trimmed().toDouble(&okY);

    if (okX && okY) {
        return QVector2D(x, y);
    }

    return QVector2D();
}

QVector2D InputParser::parseRelativeCoord(const QString& input, const QVector2D& basePoint) {
    // 格式: "@50,30"
    QString withoutAt = input;
    if (withoutAt.startsWith('@')) {
        withoutAt = withoutAt.mid(1);
    }

    QVector2D relative = parseAbsoluteCoord(withoutAt);
    return basePoint + relative;
}

QVector2D InputParser::parsePolarCoord(const QString& input, const QVector2D& basePoint) {
    // 格式: "@100<45" 或 "100<45"
    QString cleaned = input;
    if (cleaned.startsWith('@')) {
        cleaned = cleaned.mid(1);
    }

    QStringList parts = cleaned.split('<');

    if (parts.size() != 2) {
        return QVector2D();
    }

    bool okDist, okAngle;
    double distance = parts[0].trimmed().toDouble(&okDist);
    double angle = parts[1].trimmed().toDouble(&okAngle);

    if (okDist && okAngle) {
        double radians = degreesToRadians(angle);
        double dx = distance * qCos(radians);
        double dy = distance * qSin(radians);

        return basePoint + QVector2D(dx, dy);
    }

    return QVector2D();
}

double InputParser::parseNumber(const QString& input, bool* ok) {
    QString trimmed = input.trimmed();

    // 移除前導/尾隨符號
    if (trimmed.startsWith('+')) {
        trimmed = trimmed.mid(1);
    }

    double value = trimmed.toDouble(ok);
    return value;
}

double InputParser::parseAngle(const QString& input, bool* ok) {
    // 支援度數符號
    QString cleaned = input.trimmed();
    cleaned.remove("°");
    cleaned.remove("deg");

    return parseNumber(cleaned, ok);
}

QString InputParser::parseString(const QString& input) {
    QString result = input.trimmed();

    // 移除引號（如果有）
    if (result.startsWith('"') && result.endsWith('"')) {
        result = result.mid(1, result.length() - 2);
    }
    else if (result.startsWith('\'') && result.endsWith('\'')) {
        result = result.mid(1, result.length() - 2);
    }

    return result;
}

QString InputParser::parseOption(const QString& input, const QStringList& validOptions) {
    QString trimmed = input.trimmed().toUpper();

    // 完整匹配
    for (const QString& option : validOptions) {
        if (option.toUpper() == trimmed) {
            return option;
        }
    }

    // 前綴匹配
    for (const QString& option : validOptions) {
        if (option.toUpper().startsWith(trimmed)) {
            return option;
        }
    }

    return input;
}

bool InputParser::isAbsoluteCoordinate(const QString& input) {
    // 格式: "數字,數字"
    static QRegularExpression re("^[+-]?\\d+\\.?\\d*\\s*,\\s*[+-]?\\d+\\.?\\d*$");
    return re.match(input.trimmed()).hasMatch();
}

bool InputParser::isRelativeCoordinate(const QString& input) {
    // 格式: "@數字,數字"
    QString trimmed = input.trimmed();
    if (!trimmed.startsWith('@')) {
        return false;
    }

    QString withoutAt = trimmed.mid(1);
    return isAbsoluteCoordinate(withoutAt);
}

bool InputParser::isPolarCoordinate(const QString& input) {
    // 格式: "@距離<角度" 或 "距離<角度"
    static QRegularExpression re("^@?[+-]?\\d+\\.?\\d*\\s*<\\s*[+-]?\\d+\\.?\\d*$");
    return re.match(input.trimmed()).hasMatch();
}

bool InputParser::isNumber(const QString& input) {
    static QRegularExpression re("^[+-]?\\d+\\.?\\d*$");
    return re.match(input.trimmed()).hasMatch();
}

bool InputParser::isOption(const QString& input) {
    // 選項通常是字母開頭
    static QRegularExpression re("^[a-zA-Z]\\w*$");
    return re.match(input.trimmed()).hasMatch();
}

QString InputParser::formatCoordinate(const QVector2D& point, int precision) {
    return QString("%1,%2")
        .arg(point.x(), 0, 'f', precision)
        .arg(point.y(), 0, 'f', precision);
}

QString InputParser::formatNumber(double value, int precision) {
    return QString::number(value, 'f', precision);
}

QString InputParser::formatAngle(double degrees, int precision) {
    return QString::number(degrees, 'f', precision) + "°";
}

InputParser::ParsedPrompt InputParser::parsePrompt(const QString& promptText) {
    ParsedPrompt result;
    static QRegularExpression re(R"(\[([^\]]+)\])");
    auto m = re.match(promptText);
    if (!m.hasMatch()) return result;

    const QStringList tokens = m.captured(1).split('/', Qt::SkipEmptyParts);

    // ★ 宣告移到 loop 外
    static QRegularExpression rePre(R"(\(([^)]*)\)(.+))");   // (U)ndo
    static QRegularExpression rePost(R"(^(.+)\(([^)]*)\)$)"); // 退回(U)

    for (const QString& token : tokens) {
        QString opt = token.trimmed();
        ParsedOption po;

        auto mPost = rePost.match(opt);
        if (mPost.hasMatch()) {
            po.label    = mPost.captured(1).trimmed();
            po.shortcut = mPost.captured(2).trimmed().toUpper();
        } else {
            auto mPre = rePre.match(opt);
            if (mPre.hasMatch()) {
                po.shortcut = mPre.captured(1).trimmed().toUpper();
                po.label    = po.shortcut + mPre.captured(2).trimmed();
            } else {
                po.label    = opt;
                po.shortcut = opt.isEmpty() ? QString() : QString(opt[0].toUpper());
            }
        }
        if (!po.label.isEmpty())
            result.options.append(po);
    }
    return result;
}

QString InputParser::matchOption(const QString& input,
                                 const QStringList& options) {
    if (input.isEmpty()) return {};
    const QString up = input.toUpper();
    // 1. 完整名稱
    for (const QString& opt : options)
        if (opt.toUpper() == up) return opt;
    // 2. 首字母（單字元輸入）
    if (input.length() == 1)
        for (const QString& opt : options)
            if (!opt.isEmpty() && opt[0].toUpper() == up[0]) return opt;
    return {};
}

double InputParser::degreesToRadians(double degrees) {
    return degrees * M_PI / 180.0;
}

double InputParser::radiansToDegrees(double radians) {
    return radians * 180.0 / M_PI;
}

// ── KEY=VALUE 解析 ────────────────────────────────────────────────────────────
//
// 識別 "KEY=<number>" 格式（不分大小寫，允許前後空格）。
// 例如：tryParseKeyValueDouble("R=600", "R", v) → v=600, return true
//        tryParseKeyValueDouble(" L1 = 150 ", "L1", v) → v=150, return true
//        tryParseKeyValueDouble("600", "R", v) → return false
//
bool InputParser::tryParseKeyValueDouble(const QString& input,
                                         const QString& expectedKey,
                                         double& outValue)
{
    const QString trimmed = input.trimmed();
    const int eqIdx = trimmed.indexOf('=');
    if (eqIdx < 0)
        return false;

    const QString key     = trimmed.left(eqIdx).trimmed();
    const QString valPart = trimmed.mid(eqIdx + 1).trimmed();

    if (key.compare(expectedKey, Qt::CaseInsensitive) != 0)
        return false;

    bool ok = false;
    const double val = valPart.toDouble(&ok);
    if (!ok)
        return false;

    outValue = val;
    return true;
}

// ── tryParseKeyedOrPlainDouble ─────────────────────────────────────────────
//
// 先嘗試 "KEY=<num>"；若失敗則嘗試純數字。
// 適用於：使用者可輸入 "R=600" 也可直接輸入 "600"。
//
bool InputParser::tryParseKeyedOrPlainDouble(const QString& input,
                                              const QString& preferredKey,
                                              double& outValue)
{
    if (tryParseKeyValueDouble(input, preferredKey, outValue))
        return true;

    bool ok = false;
    const double val = input.trimmed().toDouble(&ok);
    if (ok) {
        outValue = val;
        return true;
    }
    return false;
}

} // namespace command
} // namespace aicad