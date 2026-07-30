#pragma once
#include "SketchAnnotation.h"
#include <QString>

namespace aicad::cad {

/**
 * @brief 標註文字組字器 —— GDIM 昇級規劃 v2 Phase 3
 *
 * 對應 GDIM_昇級規劃_v2.md 第 1.3 節：
 *   「Prefix/Suffix/Tolerance/Precision 的文字組字邏輯抽成共用的
 *     AnnotationTextFormatter，被所有（AnnotationAIS）子類別共用，
 *     不必在每個型別分支各寫一份」
 *
 * 這是一個純字串格式化工具（不含任何 OCCT / 3D 繪製邏輯），可獨立於
 * 渲染層之外單元測試。目前已接上 AIS_DimensionLine::labelText()
 * （見該類別 setAnnotation()），尚未接上的部分（Limit 模式雙行堆疊、
 * Basic Dimension 外框方塊）留待渲染層真正拆分為 AnnotationAIS 家族
 * 時再繪製——這裡先把「該顯示什麼文字」這件事做對、做穩。
 */
class AnnotationTextFormatter {
public:
    struct FormattedLabel {
        QString mainText;    ///< 主要顯示文字（已套用 prefix/suffix，
                             ///< Symmetric 公差已附加在同一行）
        QString upperText;   ///< Deviation/Limit 模式的上偏差／上限（獨立一行；
                             ///< 空字串 = 不使用，即 None/Symmetric/Basic 模式）
        QString lowerText;   ///< Deviation/Limit 模式的下偏差／下限
        bool    boxed = false;       ///< Basic Dimension：外框方框
        bool    circled = false;     ///< Inspection Dimension：外框圓圈（此處以括號模擬）
    };

    /// 依標註內容格式化顯示文字。value/value2 為已計算好的數值
    /// （例如量測或驅動後的長度、半徑、X 座標等），precision 取自 annotation.precision。
    static FormattedLabel format(const SketchAnnotation& annotation, double value, double value2 = 0.0);

    /// 不需要完整 SketchAnnotation 的版本，供仍以 SketchConstraint 驅動渲染
    /// （尚未遷移到 SketchAnnotation 的舊路徑）的呼叫端使用一致的組字規則。
    static FormattedLabel formatValue(double value, const QString& prefix, const QString& suffix,
                                       const ToleranceSpec& tolerance, int precision,
                                       bool isBasic, bool isInspection);

private:
    static QString formatNumber(double v, int precision);
};

} // namespace aicad::cad
