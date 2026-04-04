/**
 * @file DrawingSheetWidget.h
 * @brief 圖紙預覽與編輯 Widget
 * @author AICAD Team
 * @date 2025-01-08
 *
 * 功能：
 *   - 顯示 DrawingSheet 的 2D 預覽（模擬圖紙白底）
 *   - 縮放、平移（Ctrl+滾輪縮放、中鍵拖曳）
 *   - 點選視圖選取 / 拖曳移動視圖位置
 *   - 右鍵選單（視圖屬性、刪除、對齊…）
 *   - 嵌入到 DrawingSheetDialog 或主視窗的 Tab 中
 */

#pragma once

#include "DrawingSheet.h"
#include "DrawingViewRenderer.h"

#include <QWidget>
#include <QMap>
#include <QUuid>
#include <QPointF>
#include <QTransform>

class QPainter;
class QMenu;
class QScrollArea;
class QRubberBand;

namespace aicad {
namespace drawing {

/**
 * @brief 圖紙預覽 Widget
 *
 * 使用說明：
 * @code
 *   auto* widget = new DrawingSheetWidget(this);
 *   widget->setSheet(mySheet);
 *   // widget 會自動觸發各視圖的非同步渲染
 * @endcode
 *
 * 座標系說明：
 *   - 圖紙座標（mm）：以圖框左上角為原點，X 向右，Y 向下
 *   - 螢幕座標（px）：Widget 像素座標
 *   - m_transform：從圖紙座標到螢幕座標的 QTransform
 */
class DrawingSheetWidget : public QWidget {
    Q_OBJECT

public:
    explicit DrawingSheetWidget(QWidget* parent = nullptr);
    ~DrawingSheetWidget() override;

    // ── 資料綁定 ──────────────────────────────────────────────────────────
    void setSheet(DrawingSheet* sheet);
    DrawingSheet* sheet() const { return m_sheet; }

    // ── 縮放控制 ──────────────────────────────────────────────────────────
    double zoom() const { return m_zoom; }
    void setZoom(double zoom);
    void fitToWindow();
    void zoomIn()  { setZoom(m_zoom * 1.25); }
    void zoomOut() { setZoom(m_zoom / 1.25); }

    // ── 顯示選項 ──────────────────────────────────────────────────────────
    bool isGridVisible() const { return m_showGrid; }
    void setGridVisible(bool visible);

    bool isRulersVisible() const { return m_showRulers; }
    void setRulersVisible(bool visible);

    // ── 選取 ──────────────────────────────────────────────────────────────
    DrawingView* selectedView() const { return m_selectedView; }
    void clearSelection();

Q_SIGNALS:
    void viewSelected(DrawingView* view);          // 使用者點選視圖
    void viewDoubleClicked(DrawingView* view);     // 雙擊 → 開啟屬性對話框
    void viewMoved(DrawingView* view, const QPointF& newPos);
    void zoomChanged(double zoom);
    void renderingComplete();                       // 所有視圖渲染完成

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private Q_SLOTS:
    void onViewRendered(const QUuid& viewId, const ViewRenderResult& result);
    void onSheetConfigChanged();
    void onSheetViewAdded(DrawingView* view);
    void onSheetViewRemoved(const QUuid& viewId);
    void onRebuildAllViews();

    void triggerRender(DrawingView* view);
    void triggerRenderAll();

private:
    // ── 座標轉換 ──────────────────────────────────────────────────────────
    void updateTransform();
    QPointF sheetToScreen(const QPointF& sheetPt) const;
    QPointF screenToSheet(const QPointF& screenPt) const;

    // ── 繪圖 ──────────────────────────────────────────────────────────────
    void paintBackground(QPainter& p);
    void paintSheet(QPainter& p);
    void paintGrid(QPainter& p);
    void paintRulers(QPainter& p);
    void paintViewBoundingBox(QPainter& p, DrawingView* view, bool selected);
    void paintLoadingIndicator(QPainter& p, DrawingView* view);

    // ── Hit test ──────────────────────────────────────────────────────────
    DrawingView* hitTestView(const QPointF& screenPt) const;

    // ── 右鍵選單 ──────────────────────────────────────────────────────────
    void showViewContextMenu(DrawingView* view, const QPoint& globalPos);
    void showSheetContextMenu(const QPoint& globalPos);

    // ── 成員 ──────────────────────────────────────────────────────────────
    DrawingSheet*          m_sheet         = nullptr;
    DrawingViewRenderer*   m_renderer      = nullptr;
    DrawingSheetPainter    m_painter;

    // 渲染快取
    QMap<QUuid, ViewRenderResult> m_renderCache;
    QSet<QUuid>                    m_pendingRenders;

    // 視圖狀態
    double     m_zoom         = 1.0;
    QPointF    m_panOffset;           // 螢幕偏移（px）
    QTransform m_transform;

    // 互動狀態
    DrawingView* m_selectedView    = nullptr;
    DrawingView* m_draggingView    = nullptr;
    QPointF      m_dragStartPos;     // 拖曳起始（圖紙座標）
    QPointF      m_viewDragOffset;  // 拖曳偏移

    bool    m_isPanning      = false;
    QPoint  m_panStartScreen;

    // 顯示選項
    bool m_showGrid    = true;
    bool m_showRulers  = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// DrawingSheetDialog — 圖紙設定總對話框
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 圖紙設定主對話框
 *
 * 包含的 Tab：
 *   Tab 1 – 預覽（DrawingSheetWidget）
 *   Tab 2 – 圖紙設定（紙張大小、方向、比例、投影法、圖框…）
 *   Tab 3 – 標題欄（所有 TitleBlock 欄位）
 *   Tab 4 – 視圖管理（新增、移除、調整各視圖參數）
 *   Tab 5 – 零件表（啟用/停用、欄位選擇、手動編輯）
 *   Tab 6 – 進版說明（RevisionTable 條目管理）
 *   Tab 7 – 圖目錄（DrawingIndex 條目管理）
 *   Tab 8 – 圖例 & 說明（Legend、NoteBlock）
 *
 * 使用方式：
 * @code
 *   DrawingSheetDialog dlg(sheet, this);
 *   if (dlg.exec() == QDialog::Accepted) {
 *       // sheet 已被 in-place 更新
 *       sheet->save();
 *   }
 * @endcode
 */
class DrawingSheetDialog : public QDialog {
    Q_OBJECT

public:
    explicit DrawingSheetDialog(DrawingSheet* sheet, QWidget* parent = nullptr);
    ~DrawingSheetDialog() override;

private Q_SLOTS:
    void onApply();
    void onOk();
    void onCancel();
    void onExportPdf();
    void onExportSvg();

    // Tab 切換時同步資料
    void onSheetConfigEdited();
    void onTitleBlockEdited();

private:
    void setupUi();
    void setupPreviewTab();
    void setupSheetConfigTab();
    void setupTitleBlockTab();
    void setupViewsTab();
    void setupPartsListTab();
    void setupRevisionTab();
    void setupDrawingIndexTab();
    void setupLegendTab();

    void loadFromSheet();    // sheet → UI
    void applyToSheet();     // UI → sheet

    DrawingSheet*       m_sheet  = nullptr;
    DrawingSheetWidget* m_preview = nullptr;

    // Tab widgets（宣告後在 setupUi() 中建立）
    class QTabWidget* m_tabs = nullptr;

    // Config tab widgets
    class QComboBox* m_paperSizeCombo     = nullptr;
    class QComboBox* m_orientationCombo   = nullptr;
    class QComboBox* m_unitCombo          = nullptr;
    class QComboBox* m_frameTemplateCombo = nullptr;
    class QComboBox* m_projectionCombo    = nullptr;
    class QDoubleSpinBox* m_defaultScaleSpin = nullptr;

    // Title block tab widgets
    class QLineEdit* m_drawingNumberEdit  = nullptr;
    class QLineEdit* m_drawingTitleEdit   = nullptr;
    class QLineEdit* m_sheetNumberEdit    = nullptr;
    class QLineEdit* m_revisionEdit       = nullptr;
    class QLineEdit* m_companyNameEdit    = nullptr;
    class QLineEdit* m_projectNameEdit    = nullptr;
    class QLineEdit* m_partNumberEdit     = nullptr;
    class QLineEdit* m_designedByEdit     = nullptr;
    class QLineEdit* m_checkedByEdit      = nullptr;
    class QLineEdit* m_approvedByEdit     = nullptr;
    class QLineEdit* m_materialEdit       = nullptr;
    class QLineEdit* m_toleranceEdit      = nullptr;

    // Views tab
    class QListWidget* m_viewList       = nullptr;
    class QPushButton* m_addViewBtn     = nullptr;
    class QPushButton* m_removeViewBtn  = nullptr;
    class QPushButton* m_viewPropsBtn   = nullptr;

    // Parts list tab
    class QCheckBox*   m_partsListCheck = nullptr;
    class QTableWidget* m_partsListTable = nullptr;

    // Revision tab
    class QCheckBox*   m_revisionCheck  = nullptr;
    class QTableWidget* m_revisionTable = nullptr;
    class QPushButton* m_addRevBtn      = nullptr;
    class QPushButton* m_removeRevBtn   = nullptr;
};

} // namespace drawing
} // namespace aicad
