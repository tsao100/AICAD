#pragma once
#include "SketchConstraint.h"
#include "SketchAnnotation.h"
#include <QList>
#include <QString>
#include <QVector2D>
#include <optional>

namespace aicad::cad {

/**
 * @brief GDIM 型別推斷器 —— GDIM 昇級規劃 v3（無選單版）
 *
 * 對應《GDIM_滑鼠動作組合清單_無選單版.md》：型別不是被選出來的，是被
 * 「量」出來的——遊標當下位置本身就是分類函式的輸入。點擊只是把當下
 * 已經在預覽的型別鎖定下來，不存在任何離散選單、候選陣列、Tab/Space
 * 循環或字母快捷鍵。
 *
 * v2 Phase 2 的 Candidate / classifyAll() / WaitCandidate 整套「多候選
 * 引擎」已完全移除，改為兩個純函式：
 *   - inferSingle()：單一幾何 + 滑鼠位置 → 唯一的單幾何型別推斷結果
 *   - inferPair()  ：兩個幾何（+ 滑鼠位置，僅點+點需要）→ 唯一的雙幾何
 *                    型別推斷結果
 * 兩者都直接回傳「現在這一刻應該預覽/鎖定成什麼」，呼叫端（GeneralDimCommand）
 * 不需要管理任何「目前選到第幾個候選」的狀態。
 */
class GeneralDimClassifier {
public:

    struct Inference {
        AnnotationKind kind       = AnnotationKind::Distance;
        DistanceMode   distMode   = DistanceMode::PointToPoint;
        QString        label;              ///< UI/命令列顯示用

        /// Auto_DIM.md 精神延續：若兩線已知垂直，角度固定 90°，只具參考
        /// 意義，UI 可依此加註「(Reference)」字樣，不影響求解。
        bool           isReferenceOnly    = false;

        /// 相交兩線的補角（180°－夾角）。WaitDimPlace 階段依滑鼠落在哪個
        /// 象限（見 GeneralDimCommand::subscribePreview）動態切換。
        bool           useSupplementAngle = false;

        /// 若非空，鎖定此推斷結果時應改用這組 refs（而非呼叫端原始傳入的
        /// [a, b]）。用於：
        ///   - 點 + 線／線 + 點 → 正規化成 [點, 線]（PointToLine 量測需要
        ///     refs[0]=點、refs[1]=線的固定順序）
        ///   - 線 + 弧／線 + 圓 → 正規化成 [圓心衍生點, 線]
        ///   - 弧/圓 + 弧/圓 → 正規化成兩個圓心衍生點
        QList<GeomRef> pairedRefs;
    };

    static QString initialPrompt() {
        return "GDIM 選取幾何元素（線段 / 圓 / 弧 / 點）作為起點";
    }

    static bool isPointLike(const GeomRef& r, const Sketch* sketch);

    /**
     * @brief 單一幾何，純粹依滑鼠位置推斷型別（無選單版第 1 節表格）。
     *
     * 用於：
     *   - Idle 狀態 hover 一個幾何時的即時預覽
     *   - Anchored 狀態下，滑鼠目前沒有落在「可配對的第二幾何」上時的
     *     即時預覽（此時仍只有一個幾何，型別由滑鼠相對這個幾何的位置
     *     決定，即時跟著滑鼠切換）
     *
     * 回傳 std::nullopt 表示此幾何無法建立任何單幾何標註（理論上不會
     * 發生，因為 isPointLike/Line/Circle/Arc 皆有對應規則）。
     */
    static std::optional<Inference> inferSingle(const GeomRef& r, const Sketch* sketch,
                                                 const QVector2D& mousePt);

    /**
     * @brief 判斷 (a, b) 是否為合法的雙幾何配對（不考慮滑鼠位置）。
     *
     * 用於 Anchored 狀態下，判斷「滑鼠目前 hover 到的第二個幾何」是否
     * 真的能與起點配對——能配對就切換成雙幾何型別預覽，不能配對就
     * 維持單幾何型別預覽（見 inferSingle）。
     */
    static bool canPair(const GeomRef& a, const GeomRef& b, const Sketch* sketch);

    /**
     * @brief 兩個幾何 → 唯一的雙幾何標註型別（無選單版第 1 節表格）。
     *
     * 除了「點 + 點」需要滑鼠位置決定 水平/垂直/對齊 之外，其餘組合的
     * 型別由幾何本身唯一決定，mousePt 不影響結果（見表格中「無歧義」
     * 標記的列）。
     *
     * 回傳 std::nullopt 表示 (a, b) 不是合法配對（見 canPair）。
     */
    static std::optional<Inference> inferPair(const GeomRef& a, const GeomRef& b,
                                               const Sketch* sketch, const QVector2D& mousePt);

private:
    static std::optional<Inference> inferPointLike(const GeomRef& r, const Sketch* sketch,
                                                     const QVector2D& mousePt);
};

} // namespace aicad::cad
