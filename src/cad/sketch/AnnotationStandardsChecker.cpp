#include "AnnotationStandardsChecker.h"
#include <cmath>

namespace aicad::cad {

QList<StandardsWarning> AnnotationStandardsChecker::checkAnnotation(
    const SketchAnnotation& a, double value, StandardsProfile /*profile*/)
{
    QList<StandardsWarning> warnings;

    // ── 規則 1：半徑/直徑/長度/弧長不應為負值或近似 0（幾何上無意義）──────
    const bool isMagnitudeKind =
        a.kind == AnnotationKind::Radius   || a.kind == AnnotationKind::Diameter ||
        a.kind == AnnotationKind::Length   || a.kind == AnnotationKind::ArcLength ||
        a.kind == AnnotationKind::Distance || a.kind == AnnotationKind::HorizDist ||
        a.kind == AnnotationKind::VertDist;
    if (isMagnitudeKind) {
        if (value < 0.0) {
            warnings.append({"NEG_MAGNITUDE",
                QStringLiteral("數值為負（%1），此類標註通常應為正值").arg(value)});
        } else if (std::abs(value) < 1e-6) {
            warnings.append({"ZERO_MAGNITUDE",
                QStringLiteral("數值接近 0，可能是退化幾何（重合點/零長度）")});
        }
    }

    // ── 規則 2：角度標註超出常見範圍（0°~360°）提醒複查 ─────────────────────
    if (a.kind == AnnotationKind::AngleDim) {
        double deg = value * 180.0 / M_PI;
        if (deg < 0.0 || deg > 360.0) {
            warnings.append({"ANGLE_RANGE",
                QStringLiteral("角度值 %1° 超出常見的 0°~360° 範圍，請確認是否為預期值")
                .arg(deg, 0, 'f', 2)});
        }
    }

    // ── 規則 3：Basic Dimension 與數值公差同時設定，語意矛盾 ─────────────────
    // （Basic Dimension 依慣例代表「理論精確尺寸」，本身不應再疊加公差顯示）
    if (a.isBasic && a.tolerance.mode != ToleranceMode::None
                  && a.tolerance.mode != ToleranceMode::Basic) {
        warnings.append({"BASIC_WITH_TOLERANCE",
            QStringLiteral("已標記為 Basic Dimension，但公差模式不是「無」，"
                            "依慣例 Basic Dimension 不應再顯示數值公差")});
    }

    // ── 規則 4：精度過高，超出一般工程圖常見範圍（提醒複查，不代表一定錯）───
    if (a.precision > 4) {
        warnings.append({"PRECISION_HIGH",
            QStringLiteral("精度設定為小數點後 %1 位，超出一般工程圖常見範圍"
                            "（通常 0~4 位），請確認是否為預期精度").arg(a.precision)});
    }

    // ── 規則 5：Deviation/Limit 公差模式下，上限小於下限視為矛盾設定 ─────────
    if ((a.tolerance.mode == ToleranceMode::Deviation ||
         a.tolerance.mode == ToleranceMode::Limit) &&
        a.tolerance.upper < a.tolerance.lower) {
        warnings.append({"TOLERANCE_INVERTED",
            QStringLiteral("公差上限（%1）小於下限（%2），請確認是否寫反")
            .arg(a.tolerance.upper).arg(a.tolerance.lower)});
    }

    return warnings;
}

} // namespace aicad::cad
