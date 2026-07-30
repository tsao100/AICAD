#pragma once
#include "SketchConstraint.h"
#include "SketchAnnotation.h"
#include <QList>
#include <QString>
#include <QChar>
#include <QVector2D>

namespace aicad::cad {

/**
 * @brief GDIM 型別推斷器 —— GDIM 昇級規劃 v2 Phase 2：多候選引擎
 *
 * 舊版 classify() 是「單一候選、字母選單、命中即定案」：每次只回傳一個
 * MenuKey，要求使用者輸入字母（L/D/R/X/Y/C...）才能決定尺寸型別。
 *
 * 新版 classifyAll() 直接回傳「目前選取狀態下所有合法候選」的陣列，
 * 呼叫端（GeneralDimCommand）自行決定預設高亮第幾個、如何用 TAB/SPACE
 * 循環、按 Enter 或點擊時如何確認——分類器本身不再管理「選單狀態」。
 *
 * MenuKey / MenuOption / needMenu 整套機制已完全移除。
 */
class GeneralDimClassifier {
public:

    struct Candidate {
        AnnotationKind kind       = AnnotationKind::Distance;
        DistanceMode   distMode   = DistanceMode::PointToPoint;
        QString        label;              ///< UI 顯示用（取代舊 nextPrompt 字串選單）
        QChar          shortcut;            ///< 可選：仍給一個快捷字母，但不強制先讀選單
                                             ///< （TAB/SPACE 循環才是主要互動方式）
        /// true = 選定此候選後，還需要使用者再選一個幾何才能真正定案
        /// （取代舊 MenuOption::needSecond，例如「量距第二點(D)」選項）。
        bool           needsSecondPick = false;

        /// Auto_DIM.md 第六節：若兩線已知垂直（見 GeometryRelationshipAnalyzer），
        /// 角度是固定的 90°，只具參考意義（Reference），不是使用者需要決定
        /// 的自由數值。UI 可依此欄位加註「(Reference)」字樣，不影響求解。
        bool           isReferenceOnly = false;

        /// 兩線相交時，補角（180°－夾角）作為第二個候選使用；此候選的
        /// kind 仍是 AngleDim，只是 GeneralDimCommand 量測時改算補角。
        bool           useSupplementAngle = false;

        /// 若非空，確認此候選時應改用這組 refs（而非呼叫端目前的 m_refs），
        /// 例如「整條線」候選底下衍生出的「水平投影/垂直投影」，實際量測
        /// 對象是該線的兩個端點，不是線本身的 WholeGeom 參考。
        QList<GeomRef> pairedRefs;
    };

    /// 依目前選取的幾何參考（0～2 個），回傳所有合法候選。
    /// refs.isEmpty()：回傳空陣列，呼叫端應顯示 initialPrompt()。
    /// refs.size()==1：回傳該幾何可建立的所有標註型別（見 classifySingle）。
    /// refs.size()==2：回傳這組幾何配對唯一決定的標註型別（見 classifyPair；
    ///                 目前配對規則本身即決定唯一結果，故僅回傳 0 或 1 筆，
    ///                 若日後配對規則也需要多候選，於此擴充即可）。
    static QList<Candidate> classifyAll(const QList<GeomRef>& refs, const Sketch* sketch);

    static QString initialPrompt() {
        return "GDIM 選取幾何元素（線段 / 圓 / 弧 / 點）";
    }

    /// 候選清單的提示文字，供 CommandLineManager 顯示
    /// （例：「線段：線長(L) / 量距第二點(D)　[Tab/Space 切換候選，Enter 確認]」）
    static QString candidatesPrompt(const QList<Candidate>& candidates, int highlightIndex);

    static bool isPointLike(const GeomRef& r, const Sketch* sketch);

    /**
     * @brief Auto_DIM.md 第八節：依滑鼠位置在候選清單中挑出最合理的一個
     *
     * 目前實作涵蓋文件表格中明確列出的兩種情況：
     *   - Circle：滑鼠在圓內 → 半徑(R)；滑鼠在圓外 → 直徑(Ø)
     *   - Arc：滑鼠靠近圓心 → 半徑(R)；滑鼠靠近弧本身/外側 → 弧長(~)
     *
     * 其餘幾何（Point/Line 的候選之間沒有自然的「滑鼠位置」對應關係，
     * 例如 X 座標 vs Y 座標無法用滑鼠位置區分）維持原本靠 Tab/Space/
     * 快捷字母/再次點擊確認的方式，回傳 -1 表示「不做滑鼠推論，維持
     * 呼叫端目前的高亮索引」。
     */
    static int pickCandidateByMouse(const QList<Candidate>& candidates,
                                     const GeomRef& primaryRef,
                                     const Sketch* sketch,
                                     const QVector2D& mousePlanePt);

private:
    static QList<Candidate> classifySingle(const GeomRef& r, const Sketch* sketch);
    static QList<Candidate> classifyPair  (const GeomRef& a, const GeomRef& b,
                                            const Sketch* sketch);
};

} // namespace aicad::cad
