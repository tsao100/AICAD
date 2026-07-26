/**
 * @file DrawingSheetWidget.cpp
 * @brief 圖紙預覽與互動 Widget 實作
 * @author AICAD Team
 * @date 2025-01-08
 *
 * 座標系：
 *   - 圖紙座標（mm）：原點在圖紙左上角，X 向右，Y 向下
 *   - 螢幕座標（px）：Qt Widget 的像素座標
 *   - m_zoom：px / mm（初始值使圖紙恰好填滿 Widget）
 *   - m_panOffset：圖紙左上角在螢幕上的位置（px）
 *
 * 轉換：
 *   screenPt = sheetPt * m_zoom + m_panOffset
 *   sheetPt  = (screenPt - m_panOffset) / m_zoom
 */

#include "cad/Feature.h"
#include "DrawingSheetWidget.h"
#include "DrawingSheet.h"
#include "DrawingViewRenderer.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QApplication>
#include <QCursor>
#include <QTimer>
#include <QResizeEvent>
#include <QDebug>
#include <QtMath>

namespace aicad {
namespace drawing {

// ─────────────────────────────────────────────────────────────────────────────
// 建構 / 解構
// ─────────────────────────────────────────────────────────────────────────────

DrawingSheetWidget::DrawingSheetWidget(QWidget* parent)
    : QWidget(parent)
    , m_renderer(new DrawingViewRenderer(this))
{
    setBackgroundRole(QPalette::Dark);
    setAutoFillBackground(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // 連接渲染器的非同步結果
    connect(m_renderer, &DrawingViewRenderer::rendered,
            this, &DrawingSheetWidget::onViewRendered);
    connect(m_renderer, &DrawingViewRenderer::renderError,
            this, [this](const QUuid& id, const QString& msg) {
                qWarning() << "[DrawingSheetWidget] Render error for view"
                           << id << ":" << msg;
                m_pendingRenders.remove(id);
                update();
            });

    qDebug() << "[DrawingSheetWidget] Created";
}

DrawingSheetWidget::~DrawingSheetWidget()
{
    qDebug() << "[DrawingSheetWidget] Destroyed";
}

// ─────────────────────────────────────────────────────────────────────────────
// 資料綁定
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetWidget::setSheet(DrawingSheet* sheet)
{
    if (m_sheet == sheet) return;

    // 斷開舊連接
    if (m_sheet) {
        disconnect(m_sheet, nullptr, this, nullptr);
    }

    m_sheet = sheet;
    m_renderCache.clear();
    m_pendingRenders.clear();
    m_selectedView = nullptr;
    m_draggingView = nullptr;

    if (m_sheet) {
        connect(m_sheet, &DrawingSheet::configChanged,
                this, &DrawingSheetWidget::onSheetConfigChanged);
        connect(m_sheet, &DrawingSheet::viewAdded,
                this, &DrawingSheetWidget::onSheetViewAdded);
        connect(m_sheet, &DrawingSheet::viewRemoved,
                this, &DrawingSheetWidget::onSheetViewRemoved);
        connect(m_sheet, &DrawingSheet::rebuildAllViews,
                this, &DrawingSheetWidget::onRebuildAllViews);

        // 延遲一幀再 fit + 觸發渲染，確保 widget 已完成 resize
        QTimer::singleShot(0, this, [this]() {
            fitToWindow();
            triggerRenderAll();
        });
    }

    update();
}

// ─────────────────────────────────────────────────────────────────────────────
// 縮放與顯示
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetWidget::setZoom(double zoom)
{
    zoom = qBound(0.05, zoom, 20.0);
    if (qFuzzyCompare(m_zoom, zoom)) return;

    m_zoom = zoom;
    updateTransform();
    update();
    Q_EMIT zoomChanged(zoom);
}

void DrawingSheetWidget::fitToWindow()
{
    if (!m_sheet) return;

    QSizeF sheetSz = m_sheet->config().paperSizeMM();
    double wZoom   = (width()  - 40) / sheetSz.width();
    double hZoom   = (height() - 40) / sheetSz.height();
    double fz      = qMin(wZoom, hZoom);

    m_zoom      = qMax(0.05, fz);
    m_panOffset = QPointF(
        (width()  - sheetSz.width()  * m_zoom) / 2.0,
        (height() - sheetSz.height() * m_zoom) / 2.0
        );

    updateTransform();
    update();
    Q_EMIT zoomChanged(m_zoom);
}

void DrawingSheetWidget::setGridVisible(bool visible)
{
    if (m_showGrid == visible) return;
    m_showGrid = visible;
    update();
}

void DrawingSheetWidget::setRulersVisible(bool visible)
{
    if (m_showRulers == visible) return;
    m_showRulers = visible;
    update();
}

void DrawingSheetWidget::clearSelection()
{
    if (!m_selectedView) return;
    m_selectedView = nullptr;
    Q_EMIT viewSelected(nullptr);
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
// 座標轉換
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetWidget::updateTransform()
{
    m_transform = QTransform()
                      .translate(m_panOffset.x(), m_panOffset.y())
                      .scale(m_zoom, m_zoom);
}

QPointF DrawingSheetWidget::sheetToScreen(const QPointF& sheetPt) const
{
    return m_transform.map(sheetPt);
}

QPointF DrawingSheetWidget::screenToSheet(const QPointF& screenPt) const
{
    bool ok = false;
    QTransform inv = m_transform.inverted(&ok);
    if (!ok) return QPointF();
    return inv.map(screenPt);
}

// ─────────────────────────────────────────────────────────────────────────────
// 事件處理
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetWidget::resizeEvent(QResizeEvent* /*event*/)
{
    // 第一次 resize 時 fit
    if (m_sheet && qFuzzyIsNull(m_zoom - 1.0) &&
        qFuzzyIsNull(m_panOffset.x()) && qFuzzyIsNull(m_panOffset.y()))
    {
        fitToWindow();
    }
}

void DrawingSheetWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus();

    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton &&
         event->modifiers() & Qt::AltModifier))
    {
        // 中鍵 / Alt+左鍵 → Pan
        m_isPanning      = true;
        m_panStartScreen = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        QPointF sheetPt = screenToSheet(event->pos());
        DrawingView* hit = hitTestView(event->pos());

        if (hit) {
            // 選取視圖
            m_selectedView = hit;
            Q_EMIT viewSelected(hit);

            // 開始拖曳
            m_draggingView   = hit;
            m_dragStartPos   = sheetPt;
            m_viewDragOffset = sheetPt - hit->position();
            setCursor(Qt::SizeAllCursor);

        } else {
            // 點選空白處 → 清除選取
            clearSelection();
        }

        update();
        event->accept();
    }
}

void DrawingSheetWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPanning) {
        QPoint delta = event->pos() - m_panStartScreen;
        m_panOffset += QPointF(delta);
        m_panStartScreen = event->pos();
        updateTransform();
        update();
        event->accept();
        return;
    }

    if (m_draggingView && (event->buttons() & Qt::LeftButton)) {
        QPointF sheetPt  = screenToSheet(event->pos());
        QPointF newPos   = sheetPt - m_viewDragOffset;

        // 格點吸附（5 mm）
        if (event->modifiers() & Qt::ControlModifier) {
            constexpr double kSnap = 5.0;
            newPos.setX(qRound(newPos.x() / kSnap) * kSnap);
            newPos.setY(qRound(newPos.y() / kSnap) * kSnap);
        }

        m_draggingView->setPosition(newPos);
        Q_EMIT viewMoved(m_draggingView, newPos);
        update();
        event->accept();
        return;
    }

    // 更新游標形狀
    DrawingView* hit = hitTestView(event->pos());
    setCursor(hit ? Qt::SizeAllCursor : Qt::ArrowCursor);
}

void DrawingSheetWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton ||
        (m_isPanning && event->button() == Qt::LeftButton))
    {
        m_isPanning = false;
        unsetCursor();
    }

    if (m_draggingView) {
        m_draggingView = nullptr;
        unsetCursor();
    }
}

void DrawingSheetWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        DrawingView* hit = hitTestView(event->pos());
        if (hit) {
            Q_EMIT viewDoubleClicked(hit);
        }
    }
}

void DrawingSheetWidget::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) return;

    // Ctrl + 滾輪 → 縮放（以游標位置為中心）
    if (event->modifiers() & Qt::ControlModifier) {
        const double factor  = (delta > 0) ? 1.15 : (1.0 / 1.15);
        const QPointF cursor = event->position();

        // 縮放前游標所指的圖紙座標
        QPointF sheetBefore = screenToSheet(cursor.toPoint());

        m_zoom = qBound(0.05, m_zoom * factor, 20.0);

        // 調整 panOffset 使圖紙座標不動
        m_panOffset = cursor - QPointF(sheetBefore.x() * m_zoom,
                                       sheetBefore.y() * m_zoom);
        updateTransform();
        update();
        Q_EMIT zoomChanged(m_zoom);
        event->accept();
    } else {
        // 無修飾鍵 → 垂直捲動
        m_panOffset.ry() += (delta > 0 ? 30 : -30);
        updateTransform();
        update();
    }
}

void DrawingSheetWidget::contextMenuEvent(QContextMenuEvent* event)
{
    DrawingView* hit = hitTestView(event->pos());
    if (hit) {
        showViewContextMenu(hit, event->globalPos());
    } else {
        showSheetContextMenu(event->globalPos());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 繪圖
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // 背景（深灰，模擬桌面）
    paintBackground(p);

    if (!m_sheet) {
        p.setPen(Qt::gray);
        p.setFont(QFont("Arial", 14));
        p.drawText(rect(), Qt::AlignCenter, "No sheet loaded");
        return;
    }

    // ── 圖紙陰影 ──────────────────────────────────────────────────────────
    QSizeF sheetSz = m_sheet->config().paperSizeMM();
    QRectF sheetRect(sheetToScreen(QPointF(0, 0)),
                     QSizeF(sheetSz.width()  * m_zoom,
                            sheetSz.height() * m_zoom));

    // 陰影
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 60));
    p.drawRect(sheetRect.translated(4, 4));

    // 白色圖紙底
    p.setBrush(Qt::white);
    p.setPen(Qt::NoPen);
    p.drawRect(sheetRect);

    // ── 主要繪圖（mm 座標系）──────────────────────────────────────────────
    p.save();
    p.setTransform(m_transform);

    // 讓 DrawingSheetPainter 繪製圖框、標題欄、視圖等
    m_painter.paintSheet(p, *m_sheet, m_renderCache);

    // 視圖邊界框（佔位 / 選取高亮）
    for (DrawingView* view : m_sheet->views()) {
        bool sel = (view == m_selectedView);
        bool loading = m_pendingRenders.contains(view->id());

        paintViewBoundingBox(p, view, sel);

        if (loading)
            paintLoadingIndicator(p, view);
    }

    // 格點
    if (m_showGrid)
        paintGrid(p);

    p.restore();

    // ── 尺標（在螢幕座標畫，不隨圖紙縮放而變形）──────────────────────────
    if (m_showRulers)
        paintRulers(p);

    // ── 狀態提示（右下角）────────────────────────────────────────────────
    if (!m_pendingRenders.isEmpty()) {
        p.setFont(QFont("Arial", 9));
        p.setPen(QColor(255, 200, 0));
        p.drawText(rect().adjusted(0, 0, -8, -8),
                   Qt::AlignRight | Qt::AlignBottom,
                   QString("Rendering %1 view(s)…").arg(m_pendingRenders.size()));
    }
}

void DrawingSheetWidget::paintBackground(QPainter& p)
{
    p.fillRect(rect(), QColor(68, 68, 68));

    // 細點格（輔助對齊感）
    p.setPen(QColor(80, 80, 80));
    for (int x = 0; x < width(); x += 20)
        for (int y = 0; y < height(); y += 20)
            p.drawPoint(x, y);
}

void DrawingSheetWidget::paintGrid(QPainter& p)
{
    if (!m_sheet) return;

    // 在圖紙座標系下繪製 5mm 格點
    QSizeF sz = m_sheet->config().paperSizeMM();
    constexpr double kGridStep = 5.0;

    QPen gridPen(QColor(200, 200, 200, 80), 0);
    gridPen.setCosmetic(true);
    p.setPen(gridPen);

    for (double x = 0; x <= sz.width(); x += kGridStep)
        p.drawLine(QPointF(x, 0), QPointF(x, sz.height()));
    for (double y = 0; y <= sz.height(); y += kGridStep)
        p.drawLine(QPointF(0, y), QPointF(sz.width(), y));
}

void DrawingSheetWidget::paintRulers(QPainter& p)
{
    // 尺標在螢幕座標（px），但刻度值是 mm
    constexpr int kRulerW = 20;   // 尺標寬度（px）

    // 水平尺標
    p.fillRect(QRect(kRulerW, 0, width() - kRulerW, kRulerW),
               QColor(50, 50, 50));
    // 垂直尺標
    p.fillRect(QRect(0, kRulerW, kRulerW, height() - kRulerW),
               QColor(50, 50, 50));
    // 左上角空格
    p.fillRect(QRect(0, 0, kRulerW, kRulerW), QColor(40, 40, 40));

    // 計算刻度間距（螢幕上每個刻度至少 40px，刻度值為 mm）
    double mmPerTick = 1.0;
    while (mmPerTick * m_zoom < 40.0) mmPerTick *= 5;

    p.setPen(QColor(180, 180, 180));
    p.setFont(QFont("Arial", 7));

    // 水平刻度
    {
        // 找出螢幕左端對應的 mm 值
        double startMm = (kRulerW - m_panOffset.x()) / m_zoom;
        double firstTick = qCeil(startMm / mmPerTick) * mmPerTick;
        for (double mm = firstTick; mm * m_zoom + m_panOffset.x() < width(); mm += mmPerTick) {
            int sx = qRound(mm * m_zoom + m_panOffset.x());
            p.drawLine(sx, kRulerW - 6, sx, kRulerW);
            p.drawText(sx + 1, kRulerW - 7, QString::number((int)mm));
        }
    }

    // 垂直刻度
    {
        double startMm = (kRulerW - m_panOffset.y()) / m_zoom;
        double firstTick = qCeil(startMm / mmPerTick) * mmPerTick;
        for (double mm = firstTick; mm * m_zoom + m_panOffset.y() < height(); mm += mmPerTick) {
            int sy = qRound(mm * m_zoom + m_panOffset.y());
            p.drawLine(kRulerW - 6, sy, kRulerW, sy);

            // 旋轉文字
            p.save();
            p.translate(kRulerW - 7, sy - 1);
            p.rotate(-90);
            p.drawText(0, 0, QString::number((int)mm));
            p.restore();
        }
    }
}

void DrawingSheetWidget::paintViewBoundingBox(QPainter& p,
                                              DrawingView* view,
                                              bool selected)
{
    if (!view) return;

    // 若已有渲染結果，不需畫佔位框（paintView 已畫）
    if (m_renderCache.contains(view->id()) &&
        m_renderCache[view->id()].isValid)
        return;

    QRectF rect = view->boundingRect();

    // 佔位框（淺藍虛線）
    QPen pen(selected ? QColor(0, 120, 255) : QColor(150, 180, 220), 0);
    pen.setCosmetic(true);
    pen.setStyle(Qt::DashLine);
    p.setPen(pen);
    p.setBrush(selected ? QColor(0, 120, 255, 20) : QColor(200, 220, 255, 15));
    p.drawRect(rect);

    // 視圖標籤
    p.setPen(selected ? QColor(0, 80, 200) : QColor(100, 140, 200));
    p.setFont(QFont("Arial", 3));
    p.drawText(rect, Qt::AlignCenter, view->label());

    // 選取時的四角控點
    if (selected) {
        QPen handlePen(QColor(0, 120, 255), 0);
        handlePen.setCosmetic(true);
        p.setPen(handlePen);
        p.setBrush(Qt::white);
        const double hs = 4.0 / m_zoom;   // 控點大小（mm，固定螢幕 4px）
        for (QPointF corner : { rect.topLeft(), rect.topRight(),
                               rect.bottomLeft(), rect.bottomRight() })
        {
            p.drawRect(QRectF(corner - QPointF(hs / 2, hs / 2), QSizeF(hs, hs)));
        }
    }
}

void DrawingSheetWidget::paintLoadingIndicator(QPainter& p, DrawingView* view)
{
    if (!view) return;

    QRectF rect = view->boundingRect();
    QPointF center = rect.center();

    // 旋轉動畫（每 50ms update 一次）
    static int angle = 0;
    angle = (angle + 10) % 360;

    p.save();
    p.translate(center);
    p.rotate(angle);

    QPen spinPen(QColor(0, 120, 255, 180), 0.8 / m_zoom);
    spinPen.setCosmetic(false);
    spinPen.setCapStyle(Qt::RoundCap);
    p.setPen(spinPen);

    const double r = qMin(rect.width(), rect.height()) * 0.12;
    p.drawArc(QRectF(-r, -r, r * 2, r * 2), 0, 270 * 16);

    p.restore();

    // 持續重繪（動畫）
    QTimer::singleShot(50, this, [this]() {
        if (!m_pendingRenders.isEmpty()) update();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit test
// ─────────────────────────────────────────────────────────────────────────────

DrawingView* DrawingSheetWidget::hitTestView(const QPointF& screenPt) const
{
    if (!m_sheet) return nullptr;

    QPointF sheetPt = screenToSheet(screenPt);

    // 從後往前測試（最上層視圖優先）
    const auto& views = m_sheet->views();
    for (int i = views.size() - 1; i >= 0; --i) {
        DrawingView* v = views[i];
        if (v && v->boundingRect().contains(sheetPt))
            return v;
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// 右鍵選單
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetWidget::showViewContextMenu(DrawingView* view,
                                             const QPoint& globalPos)
{
    QMenu menu(this);

    // 標題（不可點選）
    auto* titleAction = new QAction(view->label(), &menu);
    titleAction->setEnabled(false);
    QFont titleFont = titleAction->font();
    titleFont.setBold(true);
    titleAction->setFont(titleFont);
    menu.addAction(titleAction);
    menu.addSeparator();

    // 視圖屬性（稍後連到 ViewPropertiesDialog）
    menu.addAction("Properties…", [this, view]() {
        Q_EMIT viewDoubleClicked(view);
    });

    menu.addSeparator();

    // 重新渲染
    menu.addAction("Rebuild View", [this, view]() {
        triggerRender(view);
    });

    // 顯示/隱藏隱藏線
    auto* hiddenAct = menu.addAction("Show Hidden Lines");
    hiddenAct->setCheckable(true);
    hiddenAct->setChecked(view->showHiddenLines());
    connect(hiddenAct, &QAction::toggled, [this, view](bool checked) {
        view->setShowHiddenLines(checked);
        triggerRender(view);
    });

    // 顯示/隱藏中心線
    auto* centerAct = menu.addAction("Show Center Lines");
    centerAct->setCheckable(true);
    centerAct->setChecked(view->showCenterLines());
    connect(centerAct, &QAction::toggled, [this, view](bool checked) {
        view->setShowCenterLines(checked);
        triggerRender(view);
    });

    menu.addSeparator();

    // 對齊功能
    QMenu* alignMenu = menu.addMenu("Align");
    alignMenu->addAction("Align Left",   [this, view]() {
        view->setPosition(QPointF(10, view->position().y()));
        Q_EMIT viewMoved(view, view->position());
        update();
    });
    alignMenu->addAction("Align Top",    [this, view]() {
        view->setPosition(QPointF(view->position().x(), 10));
        Q_EMIT viewMoved(view, view->position());
        update();
    });
    alignMenu->addAction("Center on Sheet", [this, view]() {
        if (!m_sheet) return;
        QSizeF sz = m_sheet->config().paperSizeMM();
        QPointF newPos(
            (sz.width()  - view->size().width())  / 2,
            (sz.height() - view->size().height()) / 2
            );
        view->setPosition(newPos);
        Q_EMIT viewMoved(view, newPos);
        update();
    });

    menu.addSeparator();

    // 刪除視圖
    menu.addAction("Delete View", [this, view]() {
        if (!m_sheet) return;
        int ret = QMessageBox::question(this, "Delete View",
                                        QString("Delete view '%1'?").arg(view->label()),
                                        QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            m_sheet->removeView(view->id());
            m_selectedView = nullptr;
            update();
        }
    });

    menu.exec(globalPos);
}

void DrawingSheetWidget::showSheetContextMenu(const QPoint& globalPos)
{
    if (!m_sheet) return;

    QMenu menu(this);
    menu.addAction("Fit to Window", this, &DrawingSheetWidget::fitToWindow);
    menu.addAction("Zoom In",       this, &DrawingSheetWidget::zoomIn);
    menu.addAction("Zoom Out",      this, &DrawingSheetWidget::zoomOut);
    menu.addSeparator();

    // 新增視圖子選單
    QMenu* addViewMenu = menu.addMenu("Add View");
    auto addViewAct = [&](const QString& name, ViewType type) {
        addViewMenu->addAction(name, [this, type]() {
            if (!m_sheet) return;
            QSizeF sz = m_sheet->config().paperSizeMM();
            // 預設位置：圖紙中心附近
            QPointF pos(sz.width() / 2 - 40, sz.height() / 2 - 40);
            DrawingView* view = m_sheet->addView(type, pos);
            m_selectedView = view;
            Q_EMIT viewSelected(view);
            triggerRender(view);
            update();
        });
    };

    addViewAct("Front View",     ViewType::Front);
    addViewAct("Top View",       ViewType::Top);
    addViewAct("Right View",     ViewType::Right);
    addViewAct("Left View",      ViewType::Left);
    addViewAct("Isometric View", ViewType::Isometric);
    addViewAct("Section View",   ViewType::Section);
    addViewAct("Detail View",    ViewType::Detail);

    menu.addSeparator();

    // 格點 / 尺標開關
    auto* gridAct = menu.addAction("Show Grid");
    gridAct->setCheckable(true);
    gridAct->setChecked(m_showGrid);
    connect(gridAct, &QAction::toggled, this, &DrawingSheetWidget::setGridVisible);

    auto* rulerAct = menu.addAction("Show Rulers");
    rulerAct->setCheckable(true);
    rulerAct->setChecked(m_showRulers);
    connect(rulerAct, &QAction::toggled, this, &DrawingSheetWidget::setRulersVisible);

    menu.addSeparator();
    menu.addAction("Rebuild All Views", this, &DrawingSheetWidget::triggerRenderAll);

    menu.exec(globalPos);
}

// ─────────────────────────────────────────────────────────────────────────────
// 渲染觸發 / 快取管理
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetWidget::triggerRender(DrawingView* view)
{
    if (!view || !m_sheet) return;

    // 需要有來源形體才能渲染
    cad::Feature* feature = view->sourceFeature();
    if (!feature) {
        qDebug() << "[DrawingSheetWidget] View" << view->label()
                 << "has no source feature — skipping render";
        return;
    }

    const TopoDS_Shape& shape = feature->shape();
    if (shape.IsNull()) {
        qDebug() << "[DrawingSheetWidget] View" << view->label()
                 << "source feature shape is null";
        return;
    }

    m_pendingRenders.insert(view->id());
    m_renderCache.remove(view->id());
    m_renderer->renderAsync(view, shape);
    update();

    qDebug() << "[DrawingSheetWidget] Render triggered for" << view->label();
}

void DrawingSheetWidget::triggerRenderAll()
{
    if (!m_sheet) return;
    for (DrawingView* view : m_sheet->views())
        triggerRender(view);
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots
// ─────────────────────────────────────────────────────────────────────────────

void DrawingSheetWidget::onViewRendered(const QUuid& viewId,
                                        const ViewRenderResult& result)
{
    m_pendingRenders.remove(viewId);
    m_renderCache[viewId] = result;
    update();

    if (m_pendingRenders.isEmpty())
        Q_EMIT renderingComplete();

    qDebug() << "[DrawingSheetWidget] View rendered:" << viewId
             << "pending:" << m_pendingRenders.size();
}

void DrawingSheetWidget::onSheetConfigChanged()
{
    // 圖紙設定改變（紙張大小等）→ 重新 fit
    fitToWindow();
    update();
}

void DrawingSheetWidget::onSheetViewAdded(DrawingView* view)
{
    // 新視圖加入後連接其 rebuildRequested 信號
    connect(view, &DrawingView::rebuildRequested,
            this, [this, view]() { triggerRender(view); });

    // 若視圖已有來源特徵，立即觸發渲染
    if (view->sourceFeature())
        triggerRender(view);

    update();
}

void DrawingSheetWidget::onSheetViewRemoved(const QUuid& viewId)
{
    m_renderCache.remove(viewId);
    m_pendingRenders.remove(viewId);
    if (m_selectedView && m_selectedView->id() == viewId)
        m_selectedView = nullptr;
    update();
}

void DrawingSheetWidget::onRebuildAllViews()
{
    triggerRenderAll();
}

} // namespace drawing
} // namespace aicad
