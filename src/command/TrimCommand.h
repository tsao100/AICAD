/**
 * @file TrimCommand.h
 * @brief TRIM — 以剪切邊裁切草圖幾何（別名 TR，已於 CommandAlias 內建立）。
 *
 * 互動流程：
 *   選取剪切邊（PickMultiple，Enter 空選＝以全部幾何作為剪切邊）→
 *   逐次點選要裁切的物件（每次點擊立即裁切，不需要按 Enter；點擊位置
 *   決定刪除哪一段）→ Enter 或 Esc 結束命令。
 *
 * 與 MOVE/COPY/ROTATE/MIRROR/STRETCH 不同，TRIM 是「持續點選、每次點擊
 * 立即生效」的迴圈式命令（比照 AutoCAD 的 TRIM 互動方式），因此不走
 * SketchSelectionPicker 的 PickSingle/PickMultiple 模式，改為直接訂閱
 * GEOM_PICKED 取得「幾何 UUID ＋ 點擊座標」（SketchSelectionPicker 只轉發
 * 幾何 UUID，沒有點擊座標，無法拿來判斷要刪除哪一段）。
 *
 * 實際的幾何運算（交點、裁切、圓退化為弧）由 TrimExtendHelper 負責，本
 * 命令只負責互動流程與訊息輸出。MVP 範圍限制見 TrimExtendHelper.h。
 *
 * hover 即時預覽：PickingSegment 階段訂閱 GEOM_HOVER（GetGeom 模式下
 * mouseMoveEvent 既有機制），滑鼠 hover 到某個幾何時即時算出「這次點
 * 下去會被刪除的那一段」並畫成橡皮筋多段線疊層，離開有效目標時收起。
 * 純視覺預覽，不修改 sketch，見 TrimExtendHelper::previewTrimAt()。
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

class TrimCommand : public Command {
    Q_OBJECT
public:
    TrimCommand();

    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    enum class State { Idle, SelectingCuttingEdges, PickingSegment };

    cad::Sketch* activeSketch() const;

    void beginSelectCuttingStage(cad::Sketch* sketch);
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

    // ── hover 即時預覽（PickingSegment 階段）─────────────────────────
    // 每次滑鼠移動 hover 到不同幾何時，用 TrimExtendHelper::previewTrimAt()
    // 算出「這次點下去會被刪除的那一段」，畫成橡皮筋多段線疊層
    // （rb::showPolylinePreview()）；hover 離開有效目標時收起（rb::disarm()）。
    // 純視覺預覽，不修改 sketch，也不影響按下滑鼠後的實際 trimAt() 邏輯
    // （兩者共用同一套核心運算，見 TrimExtendHelper.h 說明）。
    void subscribeHover();
    void onHover(const QVariant& payload);

    State                   m_state      = State::Idle;
    QStringList              m_cuttingEdges;
    int                      m_trimCount  = 0;
    SketchSelectionPicker*   m_picker     = nullptr;
};

} // namespace command
} // namespace aicad
