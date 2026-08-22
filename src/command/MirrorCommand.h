/**
 * @file MirrorCommand.h
 * @brief MIRROR — 以鏡射軸鏡射選取的草圖幾何（別名 MI，已於 CommandAlias
 *        內建立）。
 *
 * 互動流程：
 *   選取物件（PickMultiple）→ 指定鏡射軸（GetPoint，見下）→ 是否刪除
 *   原物件 [Yes/No] <No>（YesNo 輸入）→ 完成。
 *
 *   - 選 No（預設）：等同鏡射複製，原物件保持不動（cloneAndTransform）。
 *   - 選 Yes：原地鏡射，原物件被鏡射覆蓋（applyToSelection）。
 *
 * 鏡射軸指定支援兩種方式（比照多數 CAD 工具的 MIRROR 慣例）：
 *   (a) 點兩點：第一點決定後，第二點等待中滑鼠移動即時預覽鏡射結果
 *       （軸＝第一點→目前游標）；
 *   (b) 直接選一條既有線段：在等待第一點的階段，若游標「整條」懸停在
 *       一條線上（而非吸附到該線的端點等特定子元素——那仍視為方式 (a)
 *       的一個點），即時預覽以該線兩端點為鏡射軸的結果；點下去就直接
 *       以該線定軸，不需要再指定第二點。
 *
 * 兩種方式最終都會呼叫 rb::disarm() 讓軸線橡皮筋消失，接著進入等待
 * Yes/No 的階段——但選取物件的鏡射預覽（見下方「即時預覽」）會持續
 * 顯示，直到使用者按 y/n/Enter/滑鼠右鍵完成決定為止。
 *
 * 滑鼠右鍵在等待 Yes/No 輸入時＝送出目前命令列輸入框內容（等同 Enter）：
 * 什麼都沒打直接右鍵＝送出空字串＝套用預設值 No（不刪除原物件）；先打
 * "y" 再右鍵＝送出 "y"＝Yes（刪除原物件）。這是 CadView::
 * tryConfirmYesNoViaRightClick() 的通用機制，不需要 MirrorCommand 自己
 * 處理滑鼠事件。
 *
 * ── 即時預覽 ──────────────────────────────────────────────────────
 * 比照 COPY 的做法（見 CopyCommand.h）：一開始就用「零位移」的
 * cloneAndTransform() 建立一份與原物件完全重疊的預覽複製品（約束一併
 * 複製，只需要複製一次），之後不論是「等待第二點滑鼠移動」還是「hover
 * 到候選線段」，每一幀都只用輕量 applyToSelection()（solveAfter=false）
 * 重新定位這份既有的複製品，不重新呼叫 cloneAndTransform()。鏡射變換是
 * 自反的（同一個鏡射軸連續套用兩次＝還原成原位置），因此「把複製品從
 * 上一幀的鏡射位置移回原位、再套用這一幀的新鏡射位置」只需要重複呼叫
 * mirror(上一幀軸) 再呼叫 mirror(這一幀軸)，不需要額外的反變換函式。
 *
 * 這份預覽複製品純粹是視覺回饋：軸確定、甚至等待 Yes/No 期間都只是
 * 「顯示著」，不會被當作最終結果沿用——按下 y/n 決定的當下，
 * onYesNoInput() 會先把它整個刪除，再用最終軸線重新走一次正式的
 * applyToSelection()（Yes）或 cloneAndTransform()（No），比照 COPY
 * commit() 的既有原則（避免預覽期間的中間狀態與正式結果產生落差）。
 *
 * 橢圓鏡射為近似（僅角度欄位反射，非正圓橢圓的鏡射手性無法用單一角度
 * 完整表達），詳見 SketchGeomTransformUtil.h 的 Transform2D::applyAngle()
 * 說明與實作計畫 §3.4。
 *
 * 與 MOVE/COPY/ROTATE 都不同（軸線可用兩點或既有線段定義 + Yes/No
 * 選項），流程結構不同，因此不繼承 SketchTransformCommandBase，改為
 * 獨立實作。
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

    /// 軸確定（不論來自兩點還是既有線段）後共用的收尾：關掉軸線橡皮筋
    /// 預覽、取消 hover／滑鼠移動訂閱，把最終軸寫入 m_axisP0/m_axisP1，
    /// 把預覽複製品定位到最終位置，進入 WaitEraseOption。
    void finalizeAxis(const QVector2D& p0, const QVector2D& p1);

    void cleanup();

    // ── 即時預覽（hover 到線 / 等第二點滑鼠移動 / 軸確定後維持顯示）───
    void subscribeHover();     ///< WaitAxisPoint1：hover 到既有線段的預覽
    void unsubscribeHover();
    void onHover(const QVariant& payload);

    void subscribeLivePreviewMove();   ///< WaitAxisPoint2：滑鼠移動的預覽
    void unsubscribeLivePreviewMove();

    /// 若尚未建立則建立一份與原物件重疊的預覽複製品（含約束複製，僅
    /// 建立一次）。
    void armPreviewClone();
    /// 用鏡射變換是自反的特性，把預覽複製品從上一幀軸線移回原位、再套
    /// 用這一幀的新軸線。p0==p1（退化軸）時整幀跳過、維持上一幀畫面。
    void updatePreviewClone(const QVector2D& p0, const QVector2D& p1);
    /// 整個刪除預覽複製品（連同一併複製的約束）。取消或正式送出（無論
    /// Yes/No）前都會呼叫。
    void teardownPreviewClone();

    /// 若 uuid 對應一條 Line 幾何，回傳其目前兩端點座標（Sketch::point()
    /// 目前實際位置，非幾何物件內部可能過期的 start/end 快取欄位）。
    bool lineAxisFromUuid(const QString& uuid, QVector2D& p0, QVector2D& p1) const;

    State                  m_state = State::Idle;
    QStringList             m_selection;
    QVector2D               m_axisP0;
    QVector2D               m_axisP1;
    SketchSelectionPicker*  m_picker = nullptr;

    QMetaObject::Connection m_hoverConn;
    QMetaObject::Connection m_livePreviewConn;

    QStringList              m_previewClone;    ///< 預覽複製品 uuid（空＝尚未建立）
    bool                     m_previewHasAxis = false;
    QVector2D                m_previewAxisP0;   ///< 預覽複製品目前所在位置對應的鏡射軸
    QVector2D                m_previewAxisP1;
};

} // namespace command
} // namespace aicad
