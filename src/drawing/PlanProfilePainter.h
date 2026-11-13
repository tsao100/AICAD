/**
 * @file PlanProfilePainter.h
 * @brief 平縱面圖（Plan & Profile）圖層繪製：平面帶狀圖＋縱斷面＋資料表
 *
 * 在 mm 座標的 QPainter 上，依 DrawingSheet::planProfile() 的設定與里程範圍，
 * 直接以 TrackCenterLine 的幾何資料繪出。預覽、PDF、SVG 共用此函式。
 */
#pragma once

#include "PlanProfileSettings.h"

#include <QString>
#include <QSizeF>

class QPainter;

namespace aicad {
namespace railway { class TrackCenterLine; }
namespace cad { class Document; }
namespace drawing {

class DrawingSheet;

/** 線形可繪製的里程範圍（取水平線形關鍵點；沒有水平線形時取縱斷面）。 */
bool planProfileChainageRange(const railway::TrackCenterLine* tcl, double& start, double& end);

/** 依設定格式化里程（K1+234.00 或純數字）。 */
QString planProfileFormatChainage(const railway::TrackCenterLine* tcl,
                                  const PlanProfileSettings& s, double ch);

/** 圖目錄 Remark 用的里程：0K+000 格式，小數位數取 s.remarkDecimals（預設 0＝整數）。 */
QString planProfileFormatRemarkChainage(const railway::TrackCenterLine* tcl,
                                        const PlanProfileSettings& s, double ch);

/** 線形資料表的資料列數（與繪製使用同一份資料；kind 1＝平面、2＝縱面） */
int planProfileTableRowCount(const railway::TrackCenterLine* tcl, int kind, const PlanProfileSettings& s);

/** 線形資料表每頁可容納的資料列數（依圖幅） */
/** 平面／縱面資料表（kind 1／2）依序接續列出 ids 各線形後所需頁數（線形間以標題列分隔，標題列不孤立在頁底）。 */
int planProfileTablePageCount(cad::Document* doc, int kind, const QStringList& ids,
                              const PlanProfileSettings& s, const QSizeF& paper);

int planProfileTableRowsPerPage(const QSizeF& paper, const PlanProfileSettings& s);

/** 機廠矩形視圖的 X 軸方向（模型座標的數學角 [rad]，逆時針）：對齊線形時為該處切線方向，再加轉角。 */
double planProfileDepotAngle(cad::Document* doc, const DepotView& v);

/** 機廠平面圖：指定比例 1:scale 時，一張圖紙最大可涵蓋的矩形視圖大小（實地 m，寬×高）。 */
QSizeF planProfileDepotMaxSize(const QSizeF& paper, const PlanProfileSettings& s, double scale);

/** 說明文字：依圖紙大小列出常用比例（1:200、1:250、1:500、1:1000、1:2000）下矩形視圖最大可到多大。 */
QString planProfileDepotSizeHint(const PlanProfileSettings& s);

/** 最小的標準比例分母（100…5000）使 w×h（m）放得進一張圖紙；放不下回傳 0。 */
double planProfileDepotFitScale(const PlanProfileSettings& s, double w, double h);

/** 繪製本圖紙的平縱面圖層（layer 未啟用或找不到線形時只畫提示文字）。 */
void paintPlanProfileLayer(QPainter& painter, const DrawingSheet& sheet);

} // namespace drawing
} // namespace aicad
