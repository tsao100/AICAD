/**
 * @file ScaleCommand.h
 * @brief SCALE — 以基準點為中心等比縮放選取的草圖幾何（別名 SC）。
 *
 * 互動流程：
 *   選取物件（PickMultiple）→ 指定基準點（GetPoint）→ 指定縮放倍率
 *   （可以拖曳滑鼠，倍率＝從基準點到游標的距離，拖曳期間即時預覽被縮放
 *   後的幾何；也可以直接輸入倍率數字——兩種輸入方式同時有效，先到者為
 *   準）→ 完成。
 *
 * 與 ROTATE 結構相同（不繼承 SketchTransformCommandBase：最後一步同時
 * 接受「取點」與「數字輸入」，流程結構與 MOVE/COPY 不同），額外加上滑鼠
 * 移動時的即時預覽（比照 SketchTransformCommandBase 內 MOVE/COPY 的
 * livePreview 機制，但套用的是 Transform2D::scale() 而非 translation()）。
 * 選取階段仍然複用 SketchSelectionPicker。
 *
 * 倍率取值方式：直接採用「游標到基準點的距離」本身當作倍率（不是相對某個
 * 參考距離的比例）——比照本專案 ROTATE 直接用方位角當旋轉角、不另外取
 * 參考角的作法，也是 AutoCAD SCALE 指令在未使用 Reference 子選項時的
 * 慣例。之所以不用「初始游標距離」當參考基準（例如 Blender 的 S 鍵手感），
 * 是因為這裡的基準點就是使用者剛點下去的游標位置，兩者幾乎重合，距離會
 * 趨近 0，拿來當分母極不穩定。
 */
#pragma once

#include "Command.h"
#include "SketchSelectionPicker.h"

#include <QVector2D>
#include <QStringList>
#include <QVariant>
#include <QMetaObject>

namespace aicad {
namespace cad { class Sketch; }
namespace view { class RubberBand; }

namespace command {

class ScaleCommand : public Command {
    Q_OBJECT
public:
    ScaleCommand();

    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    enum class State { Idle, WaitBasePoint, WaitFactor };

    cad::Sketch* activeSketch() const;

    void beginSelection(cad::Sketch* sketch);
    void beginBasePointStage();
    void beginFactorStage();

    void subscribePointAcquired();
    void subscribeNumberInput();
    void subscribeCancelled();
    void unsubscribeAll();

    void onSelectionConfirmed(const QStringList& uuids);
    void onSelectionCancelled();
    void onPointAcquired(const QVariant& payload);
    void onNumberInput(const QVariant& payload);
    void onCancelled(const QVariant&);

    /// 基準點到 pt 的距離，即目前這個點所代表的縮放倍率（見檔頭說明）。
    double factorAtPoint(const QVector2D& pt) const;

    /// 套用縮放（不論倍率是用滑鼠拖曳還是數字輸入取得，都走這條路徑）。
    /// 呼叫前會先 revertLivePreview()，確保實際送出的是「原始幾何 →
    /// 最終倍率」的單一完整變換，不會疊加預覽期間的中間態。
    void applyScale(double factor);

    void cleanup();

    // ── 即時縮放預覽（拖曳中每一幀只搬幾何本身，不跑約束求解器；比照
    //    SketchTransformCommandBase / SketchGripProvider 的既有分工）──────
    void armLivePreview();
    void updateLivePreviewTo(const QVector2D& cursorPt);
    void revertLivePreview();

    State                   m_state = State::Idle;
    QStringList              m_selection;
    QVector2D                m_basePoint;
    SketchSelectionPicker*   m_picker = nullptr;

    QMetaObject::Connection m_livePreviewConn;
    double                   m_liveFactor = 1.0;  ///< 目前已「輕量套用」在真實幾何上的累積倍率
};

} // namespace command
} // namespace aicad
