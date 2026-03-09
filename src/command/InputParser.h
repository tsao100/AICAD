#ifndef INPUTPARSER_H
#define INPUTPARSER_H

#include <QString>
#include <QVector2D>
#include <QVariant>

namespace aicad {
namespace command {

enum class InputType {
    Unknown,
    AbsoluteCoordinate,    // 100,200
    RelativeCoordinate,    // @50,30
    PolarCoordinate,       // @100<45
    Number,                // 123.45
    Option,                // Close, Undo
    String                 // "text"
};

struct ParsedInput {
    InputType type;
    QVariant value;
    bool isValid;
    QString errorMessage;

    ParsedInput()
        : type(InputType::Unknown)
        , isValid(false) {}

    ParsedInput(InputType t, const QVariant& v, bool valid = true, const QString& error = QString())
        : type(t)
        , value(v)
        , isValid(valid)
        , errorMessage(error) {}
};

class InputParser {
public:
    // 主解析方法
    static ParsedInput parse(const QString& input, InputType expectedType = InputType::Unknown);

    // 特定類型解析
    static QVector2D parseCoordinate(const QString& input, const QVector2D& basePoint = QVector2D());
    static double parseNumber(const QString& input, bool* ok = nullptr);
    static double parseAngle(const QString& input, bool* ok = nullptr);
    static QString parseString(const QString& input);
    static QString parseOption(const QString& input, const QStringList& validOptions);

    // 類型檢測
    static bool isAbsoluteCoordinate(const QString& input);
    static bool isRelativeCoordinate(const QString& input);
    static bool isPolarCoordinate(const QString& input);
    static bool isNumber(const QString& input);
    static bool isOption(const QString& input);

    // 格式化
    static QString formatCoordinate(const QVector2D& point, int precision = 4);
    static QString formatNumber(double value, int precision = 4);
    static QString formatAngle(double degrees, int precision = 2);

private:
    static QVector2D parseAbsoluteCoord(const QString& input);
    static QVector2D parseRelativeCoord(const QString& input, const QVector2D& basePoint);
    static QVector2D parsePolarCoord(const QString& input, const QVector2D& basePoint);

    static double degreesToRadians(double degrees);
    static double radiansToDegrees(double radians);
};

} // namespace command
} // namespace aicad

#endif
