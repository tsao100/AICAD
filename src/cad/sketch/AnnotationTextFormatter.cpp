#include "AnnotationTextFormatter.h"

namespace aicad::cad {

QString AnnotationTextFormatter::formatNumber(double v, int precision) {
    int p = (precision < 0) ? 2 : precision;
    return QString::number(v, 'f', p);
}

AnnotationTextFormatter::FormattedLabel AnnotationTextFormatter::formatValue(
    double value, const QString& prefix, const QString& suffix,
    const ToleranceSpec& tolerance, int precision,
    bool isBasic, bool isInspection)
{
    FormattedLabel out;
    const QString num = formatNumber(value, precision);
    out.mainText = prefix + num + suffix;

    switch (tolerance.mode) {
    case ToleranceMode::None:
        break;
    case ToleranceMode::Symmetric:
        // 同一行附加 ±，例如 "50.00 ±0.20"
        out.mainText += QString(" \xC2\xB1%1").arg(formatNumber(tolerance.upper, precision));
        break;
    case ToleranceMode::Deviation:
        // 上/下偏差各自獨立一行，例如 "+0.10" / "-0.05"
        out.upperText = QString("+%1").arg(formatNumber(tolerance.upper, precision));
        out.lowerText = QString("-%1").arg(formatNumber(tolerance.lower, precision));
        break;
    case ToleranceMode::Limit:
        // 上限＝value+upper，下限＝value-lower，各自獨立一行
        out.upperText = formatNumber(value + tolerance.upper, precision);
        out.lowerText = formatNumber(value - tolerance.lower, precision);
        break;
    case ToleranceMode::Basic:
        isBasic = true;  // Basic 公差模式本身即代表 Basic Dimension
        break;
    }

    if (isBasic)
        out.boxed = true;
    if (isInspection) {
        out.circled = true;
        // 簡化表示：真正的圓圈繪製留待 AnnotationAIS 家族實作，
        // 這裡先以括號模擬「已標示為 Inspection」的文字外觀
        out.mainText = "(" + out.mainText + ")";
    }

    return out;
}

AnnotationTextFormatter::FormattedLabel AnnotationTextFormatter::format(
    const SketchAnnotation& annotation, double value, double /*value2*/)
{
    return formatValue(value, annotation.prefix, annotation.suffix,
                        annotation.tolerance, annotation.precision,
                        annotation.isBasic, annotation.isInspection);
}

} // namespace aicad::cad
