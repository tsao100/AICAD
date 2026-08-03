/**
 * @file MirrorCommand.h
 * @brief MIRROR — 以兩點定義的鏡射軸鏡射選取的草圖幾何（別名 MI，已於
 *        CommandAlias 內建立）。
 *
 * 互動流程：
 *   選取物件（PickMultiple）→ 指定鏡射線第一點（GetPoint）→ 指定鏡射線
 *   第二點（GetPoint）→ 是否刪除原物件 [Yes/No] <No>（YesNo 輸入）→ 完成。
 *
 *   - 選 No（預設）：等同鏡射複製，原物件保持不動（cloneAndTransform）。
 *   - 選 Yes：原地鏡射，原物件被鏡射覆蓋（applyToSelection）。
 *
 * 橢圓鏡射為近似（僅角度欄位反射，非正圓橢圓的鏡射手性無法用單一角度
 * 完整表達），詳見 SketchGeomTransformUtil.h 的 Transform2D::applyAngle()
 * 說明與實作計畫 §3.4。
 *
 * 與 MOVE/COPY/ROTATE 都不同（軸線兩點 + Yes/No 選項），流程結構不同，
 * 因此不繼承 SketchTransformCommandBase，改為獨立實作。
 */
#pragma once

#include "Command.h"
#include "SketchSelectionPicker.h"

#include <QVector2D>
#include <QStringList>
#include <QVariant>

namespace aicad {
namespace cad { class Sketch; }

namespace command {

class MirrorCommand : public Command {
    Q_OBJECT
public:
    MirrorCommand();

    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    enum class State { Idle, WaitAxisPoint1, WaitAxisPoint2, WaitEraseOption };

    cad::Sketch* activeSketch() const;

    void beginSelection(cad::Sketch* sketch);
    void beginAxisPoint1Stage();
    void beginEraseOptionStage();

    void subscribePointAcquired();
    void subscribeYesNoInput();
    void subscribeCancelled();
    void unsubscribeAll();

    void onSelectionConfirmed(const QStringList& uuids);
    void onSelectionCancelled();
    void onPointAcquired(const QVariant& payload);
    void onYesNoInput(const QVariant& payload);
    void onCancelled(const QVariant&);

    void cleanup();

    State                  m_state = State::Idle;
    QStringList             m_selection;
    QVector2D               m_axisP0;
    QVector2D               m_axisP1;
    SketchSelectionPicker*  m_picker = nullptr;
};

} // namespace command
} // namespace aicad
