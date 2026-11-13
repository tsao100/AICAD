/**
 * @file DrawingBookActions.h
 * @brief Feature Browser「Drawing Sheets」節點（圖冊／封面／圖目錄／圖）的右鍵動作
 */
#pragma once

#include <QString>

class QWidget;

namespace aicad {
namespace cad { class Document; }
namespace command {

/**
 * @param action  newbook | open | bookSettings | regen | index | rename | deleteBook |
 *                exportPdf | sheetSettings | clearOverride | regenSheet | deleteSheet
 * @param itemId  樹節點 id："book_<id>" / "dsheet_<uuid>" / "__drawings_folder__"
 */
void runDrawingBookAction(cad::Document* doc, QWidget* parent,
                          const QString& action, const QString& itemId);   // 另有 depotAdd|depotEdit|depotDelete|depotShow|depotHide|depotShowAll|depotHideAll

/** 依 Document::shownDepotViews() 重建 View 中的機廠矩形視圖外框（新增／修改／刪除視圖後呼叫）。 */
void refreshDepotOverlays(cad::Document* doc);

} // namespace command
} // namespace aicad
