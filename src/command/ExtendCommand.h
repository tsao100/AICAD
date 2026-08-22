/**
 * @file ExtendCommand.h
 * @brief EXTEND — 以邊界邊延伸草圖幾何（別名 EX，已於 CommandAlias 內建立）。
 *
 * 互動流程：
 *   選取邊界邊（PickMultiple，Enter 空選＝以全部幾何作為邊界邊）→
 *   逐次點選要延伸的物件（點擊位置決定延伸哪一端：離點擊處較近的端點）
 *   → Enter 或 Esc 結束命令。
 *
 * 架構與 TrimCommand 完全對稱（共用 TrimExtendHelper 的幾何運算），互動
 * 流程細節見 TrimCommand.h 的說明。Circle 不支援 EXTEND（沒有端點可延伸）。
 *
 * hover 即時預覽：與 TrimCommand 完全對稱，見 TrimCommand.h 的說明（改用
 * TrimExtendHelper::previewExtendAt()，畫的是「延伸後會新增的那一段」而
 * 非「會被刪除的那一段」）。
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

class ExtendCommand : public Command {
    Q_OBJECT
public:
    ExtendCommand();

    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    enum class State { Idle, SelectingBoundaryEdges, PickingSegment };

    cad::Sketch* activeSketch() const;

    void beginSelectBoundaryStage(cad::Sketch* sketch);
    void beginPickSegmentStage(cad::Sketch* sketch);

    void subscribeGeomPicked();
    void subscribeConfirm();
    void subscribeCancelled();
    void unsubscribeAll();

    void onSelectionConfirmed(const QStringList& uuids);
    void onSelectionCancelled();
    void onGeomPicked(const QVariant& payload);
    void onConfirm(const QVariant&);
    void onCancelled(const QVariant&);

    void finishLoop();
    void cleanup();

    // hover 即時預覽（PickingSegment 階段）：見 TrimCommand.h 對應方法
    // 的說明，架構完全對稱。
    void subscribeHover();
    void onHover(const QVariant& payload);

    State                   m_state         = State::Idle;
    QStringList              m_boundaryEdges;
    int                      m_extendCount   = 0;
    SketchSelectionPicker*   m_picker        = nullptr;
};

} // namespace command
} // namespace aicad
