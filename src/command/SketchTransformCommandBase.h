/**
 * @file SketchTransformCommandBase.h
 * @brief MOVE/COPY 共用的「選取物件 → 取基準點 → 取第二點 → 套用變換」
 *        狀態機基底類別（Phase 1，見 AICAD_SketchEdit_Advanced_Commands_Plan.md）。
 *
 * 互動流程與訂閱架構比照 LeaderNoteCommand / EraseCommand 既有的現代模式
 * （直接操作 Sketch/CadView，而非 LineCommand 那種較舊的
 * "command.request-*" 間接事件轉發風格）：
 *
 *   選取物件（SketchSelectionPicker::PickMultiple）
 *     → 指定基準點（GetPoint 模式 + POINT_ACQUIRED）
 *     → 指定第二點（同上，算出位移向量）
 *     → commit()（子類別實作：套用 Transform2D 並印出結果）
 *
 * 只抽出 MOVE/COPY 實際共用、完全一致的部分。ROTATE 需要額外的角度輸入、
 * MIRROR 需要鏡射軸兩點而非位移向量，兩者的取值流程與本基底有出入，
 * 留待 Phase 2 視實際需要決定是否擴充或另外處理，避免在還沒寫 ROTATE/
 * MIRROR 之前就過度預測、抽象過頭。
 */
#pragma once

#include "Command.h"
#include "SketchSelectionPicker.h"
#include "../cad/sketch/SketchGeomTransformUtil.h"

#include <QVector2D>
#include <QStringList>
#include <QVariant>
#include <QMetaObject>

namespace aicad {
namespace cad { class Sketch; }

namespace command {

class SketchTransformCommandBase : public Command {
    Q_OBJECT

public:
    bool isInteractive() const override { return true; }

protected:
    SketchTransformCommandBase(const QString& name, const QString& description);

    CommandResult execute(const CommandContext& ctx) override;

    // ── 子類別可覆寫的提示文字 ─────────────────────────────────────────
    virtual QString selectPrompt()      const { return QStringLiteral("Select objects, then press Enter:"); }
    virtual QString basePointPrompt()   const { return QStringLiteral("Specify base point:"); }
    virtual QString secondPointPrompt() const { return QStringLiteral("Specify second point (displacement target):"); }

    /**
     * @brief 套用最終變換並印出結果訊息（子類別必須實作）。
     *
     * MOVE 通常呼叫 cad::transform::applyToSelection()；
     * COPY 通常呼叫 cad::transform::cloneAndTransform()。
     * 呼叫時機保證 sketch 非 nullptr、selection 非空。
     */
    virtual void commit(cad::Sketch* sketch, const QStringList& selection,
                        const cad::transform::Transform2D& xf) = 0;

    /**
     * @brief 子類別是否要在「等待第二點」階段即時預覽被選取幾何的搬移
     *        結果（而不是只有基準點→游標的橡皮筋參考線）。
     *
     * 預設 false：ROTATE/MIRROR/STRETCH 等既有行為完全不變，只有
     * MoveCommand／CopyCommand 覆寫回傳 true。見 .cpp 內 armLivePreview()
     * 等函式的說明——實作上比照 SketchGripProvider 的「拖曳中只搬點
     * （輕量）、放開才求解（solveConstraints）」分工。
     */
    virtual bool livePreviewEnabled() const { return false; }

    /**
     * @brief livePreviewEnabled()==true 時，取得「進入等待第二點階段時，
     *        要被即時拖曳預覽的目標幾何」uuid 清單。
     *
     * 預設回傳 m_selection 本身（MOVE 的語意：直接預覽被選取的原幾何，
     * 不需要另外準備）。
     *
     * COPY 覆寫：這裡不能直接預覽原幾何（原物件在 COPY 中不該被動到），
     * 而是要立即以零位移呼叫 cad::transform::cloneAndTransform() 建立一份
     * 「疊在原物件正上方」的複製品，回傳新複製出來的 uuid 清單——之後每
     * 幀真正被增量搬移預覽的是這份複製品，原選取範圍完全不受影響。
     *
     * 呼叫時機保證 sketch 非 nullptr。
     */
    virtual QStringList armLivePreviewTargets(cad::Sketch* sketch) { Q_UNUSED(sketch); return m_selection; }

    /**
     * @brief revertLivePreview() 已經把 armLivePreviewTargets() 回傳的目標
     *        歸零位移之後的收尾掛鉤，無論是「確認後準備呼叫 commit()」或
     *        「取消」都會呼叫到。
     *
     * 預設 no-op：MOVE 的目標就是原選取範圍本身，本來就該繼續留著，不用
     * 額外處理。
     *
     * COPY 覆寫：targets 是預覽用、疊在原物件正上方的「暫時複製品」——
     * 確認時即將由 commit() 呼叫 cloneAndTransform() 建立正式的最終複製，
     * 取消時則整個操作作廢——兩種情況都必須把這份預覽複製品整個刪掉，
     * 否則會留下重複的幾何（確認時）或不該存在的殘留幾何（取消時）。
     * Sketch::removeGeometry() 會一併清掉複製品身上（含複製約束時新增
     * 出來的）約束/標註，不需要另外處理。
     *
     * 呼叫時機保證 sketch 非 nullptr；targets 可能為空（此時應為 no-op）。
     */
    virtual void teardownLivePreviewTargets(cad::Sketch* sketch, const QStringList& targets)
    { Q_UNUSED(sketch); Q_UNUSED(targets); }

    /// 供子類別在覆寫 armLivePreviewTargets() 時讀取目前的選取範圍
    /// （m_selection 本身是 private，避免子類別意外修改它）。
    const QStringList& selection() const { return m_selection; }

    cad::Sketch* activeSketch() const;

private:
    enum class State { Idle, WaitBasePoint, WaitSecondPoint };

    void beginSelection(cad::Sketch* sketch);
    void beginBasePointStage();

    void subscribePointAcquired();
    void subscribeCancelled();
    void unsubscribeAll();

    void onSelectionConfirmed(const QStringList& uuids);
    void onSelectionCancelled();
    void onPointAcquired(const QVariant& payload);
    void onCancelled(const QVariant&);

    void cleanup();

    // ── 即時搬移預覽（livePreviewEnabled() == true 時使用）──────────────
    void armLivePreview();
    void updateLivePreviewTo(const QVector2D& cursorPt);
    void revertLivePreview();

    State                  m_state = State::Idle;
    QStringList             m_selection;
    QVector2D               m_basePoint;
    SketchSelectionPicker*  m_picker = nullptr;

    QMetaObject::Connection m_livePreviewConn;
    QVector2D               m_liveDelta;   ///< 目前已「輕量套用」在真實幾何上的總位移
    QStringList             m_livePreviewTargets;  ///< armLivePreviewTargets() 的回傳快取
};

} // namespace command
} // namespace aicad
