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

    State                  m_state = State::Idle;
    QStringList             m_selection;
    QVector2D               m_basePoint;
    SketchSelectionPicker*  m_picker = nullptr;
};

} // namespace command
} // namespace aicad
