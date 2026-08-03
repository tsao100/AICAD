/**
 * @file CommandRubberBandHelper.h
 * @brief 供互動編輯命令（MOVE/COPY/ROTATE/MIRROR/STRETCH）共用的橡皮筋
 *        預覽輔助函式。
 *
 * 背景：CadView::mouseMoveEvent() 原本只有在 InteractionMode::Sketching
 * 模式下才會自動把游標位置餵給 RubberBand（供 LINE 等繪圖命令即時預覽用）。
 * 這幾個互動編輯命令走的是 InteractionMode::GetPoint 模式，之前沒有這個
 * 自動跟隨機制。現在 CadView::mouseMoveEvent() 已擴充為：GetPoint 模式下
 * 只要 RubberBand 的模式被設定為非 None，就會採用與 Sketching 模式相同的
 * 「每次滑鼠移動自動呼叫 setCurrentPoint()/update()」邏輯（opt-in，不影響
 * 沒有主動設定 RubberBand 的既有 GetPoint 呼叫端）。
 *
 * 本檔案把「取得 CadView／RubberBand、設定平面、設定模式、清除」這幾個
 * 重複邏輯收斂成兩三個函式，避免 5 個命令各自複製貼上同樣的樣板碼。
 */
#pragma once

#include <QVector2D>

namespace aicad {
namespace cad { class Sketch; }

namespace command {
namespace rb {

/// 啟動一條從 anchor 出發、跟隨游標的橡皮筋預覽線。供「已取得基準點/
/// 第一個軸點，等待第二點」的階段使用（MOVE/COPY/ROTATE 的位移或角度
/// 參考線、MIRROR 的鏡射軸第二點）。
void armLinePreview(cad::Sketch* sketch, const QVector2D& anchor);

/// 啟動一個跟隨游標的矩形橡皮筋預覽（供 STRETCH 的窗選第一角點→對角點
/// 階段使用）。
void armRectPreview(cad::Sketch* sketch, const QVector2D& corner1);

/// 停用橡皮筋預覽（命令結束、取消、或階段切換時呼叫；重複呼叫安全）。
void disarm();

} // namespace rb
} // namespace command
} // namespace aicad
