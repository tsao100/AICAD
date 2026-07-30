#pragma once
#include "SketchAnnotation.h"
#include <QList>
#include <QString>

namespace aicad::cad {

/**
 * @brief 標註規範檢查 —— GDIM 昇級規劃 v2 Phase 9（優先度最低，先求有、不求全）
 *
 * 對應 GDIM.md 第九節「規範檢查（Standards Check）：依 ISO、JIS、ANSI 等
 * 標註規範提醒不符合之處」。
 *
 * 這裡先實作一組**通用、不特別具爭議性**的基本規則（不論 ISO/JIS/ANSI，
 * 這些規則普遍成立），而非真的去實作三套規範各自的完整條文——那是一個
 * 需要對照正式規範文件逐條核對的大工程，優先度本來就最低，這裡先把
 * 「規則引擎」這個掛勾點做出來，讓日後要加特定規範的細節規則時，
 * 只需要在 checkAnnotation() 裡新增規則、不需要再改呼叫端。
 *
 * 規則只回傳警告（不阻擋建立），呼叫端（例如 GeneralDimCommand::
 * commitDimension()）決定要不要顯示、要不要讓使用者確認。
 */
enum class StandardsProfile {
    Generic,  ///< 不特定規範，只檢查普遍適用的基本規則（目前唯一實作的 profile）
    ISO,
    ANSI,
    JIS,
};

struct StandardsWarning {
    QString code;     ///< 短代碼（例如 "NEG_RADIUS"），供之後做「忽略此類警告」用
    QString message;  ///< 顯示給使用者的訊息（中文）
};

class AnnotationStandardsChecker {
public:
    /// 依標註內容與已算好的數值，回傳規範警告清單（可能為空）。
    /// profile 目前只有 Generic 真正實作規則；ISO/ANSI/JIS 會先套用
    /// Generic 規則，日後可在各自分支加規範特定的細節規則。
    static QList<StandardsWarning> checkAnnotation(
        const SketchAnnotation& annotation, double value,
        StandardsProfile profile = StandardsProfile::Generic);
};

} // namespace aicad::cad
