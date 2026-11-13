/**
 * @file DrawingViewRenderer.h
 * @brief 圖面視圖渲染器 — 將 3D OCCT Shape 投影為 2D 工程圖線條
 * @author AICAD Team
 * @date 2025-01-08
 *
 * 實作分層：
 *
 *   DrawingView（資料）
 *       │
 *       ▼
 *   DrawingViewRenderer（幾何計算）
 *       │  使用 OCCT HLRBRep / HLRAlgo
 *       │  輸出 TopoDS_Compound（可見線 + 隱藏線）
 *       │
 *       ▼
 *   DrawingSheetPainter（繪圖）
 *       │  將 Shape 投影到 QPainter / SVG / PDF
 *       │
 *       ▼
 *   DrawingSheetWidget（顯示）或 exportPdf/exportSvg
 *
 * ── 目前狀態：框架佔位，TODO 標記實際演算法位置 ──
 */

#pragma once

#include <QObject>
#include <QPainter>

// OCCT forward declarations
#include <TopoDS_Shape.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>

#include "DrawingSheet.h"

namespace aicad {
namespace drawing {

class DrawingView;

// ─────────────────────────────────────────────────────────────────────────────
// 渲染結果
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 一個視圖的 HLR 渲染結果
 *
 * visibleEdges  ：可見輪廓線（實線）
 * hiddenEdges   ：隱藏線（虛線）
 * sectionFaces  ：剖切面外框（已投影；Section view 時有效，hatching 於繪製時裁切產生）
 * centerLines   ：中心線（需後處理辨識對稱軸）
 * boundingBox   ：視圖在圖紙座標系的邊界框（mm）
 */
struct ViewRenderResult {
    TopoDS_Compound visibleEdges;
    TopoDS_Compound hiddenEdges;
    TopoDS_Compound sectionFaces;   // 剖切面的 Wire（投影座標），painter 以其為區域畫 45° 填充線
    TopoDS_Compound centerLines;
    QSizeF          boundingBox;    // 投影後實際大小（模型單位，未套比例）
    QRectF          projectedBounds; // 投影平面範圍（模型單位，Y 向上；paintView 以其中心置中）
    bool            isValid = false;
    QString         errorMessage;
};

// ─────────────────────────────────────────────────────────────────────────────
// DrawingViewRenderer
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 圖面視圖渲染器（HLR + 投影）
 *
 * 負責：
 *   1. 根據 ViewType 建立投影軸（gp_Ax2）
 *   2. 執行 OCCT HLRBRep_Algo 隱藏線消除
 *   3. 輸出可見邊、隱藏邊、中心線、剖面線的 TopoDS_Compound
 *
 * 這個類別純粹做幾何計算，不涉及 Qt 繪圖。
 */
class DrawingViewRenderer : public QObject {
    Q_OBJECT

    // RenderTask 定義於 DrawingViewRenderer.cpp，需存取 render_internal()
    friend class RenderTask;

public:
    explicit DrawingViewRenderer(QObject* parent = nullptr);
    ~DrawingViewRenderer() override;

    /**
     * @brief 執行渲染
     * @param view   目標視圖（含 ViewType、scale、section params…）
     * @param shape  來源 3D 形體（通常是根特徵的 TopoDS_Shape）
     * @return 渲染結果
     *
     * 此函式可能耗時（HLR 複雜度 O(n²)），建議在背景執行緒呼叫。
     */
    ViewRenderResult render(const DrawingView* view, const TopoDS_Shape& shape);

    /**
     * @brief 非同步渲染（在 QThreadPool 中執行，完成後 emit rendered）
     */
    void renderAsync(const DrawingView* view, const TopoDS_Shape& shape);

    /**
     * @brief 自動 Fit 比例：把投影範圍縮放到視圖配置框（size）的 90% 內，
     *        並取 ≤ 的工程標準比例（1、2、5 × 10^n）。Widget 預覽與匯出共用。
     */
    static double computeFitScale(const DrawingView* view, const QRectF& projectedBounds);

    /**
     * @brief 同步渲染整張圖紙的所有視圖（供 PDF/SVG 匯出使用），並對
     *        autoFit 的視圖套用 Fit 比例，使輸出與預覽一致。
     */
    static QMap<QUuid, ViewRenderResult> renderSheetSync(DrawingSheet& sheet);

Q_SIGNALS:
    void rendered(const QUuid& viewId, const ViewRenderResult& result);
    void renderError(const QUuid& viewId, const QString& message);
    void renderProgress(const QUuid& viewId, int percent);

private Q_SLOTS:
    /** 非同步任務完成後，由 QMetaObject::invokeMethod 回到主執行緒呼叫 */
    void onRenderTaskFinished(const QUuid& viewId, const ViewRenderResult& result);

private:
    /**
     * @brief 核心渲染邏輯（可在背景執行緒呼叫）
     *
     * render() 與 RenderTask::run() 均委派至此函式。
     * 所有 OCCT 操作集中於此，確保執行緒安全。
     */
    ViewRenderResult render_internal(const QUuid&         viewId,
                                     ViewType             viewType,
                                     double               scale,
                                     bool                 showHidden,
                                     bool                 showCenter,
                                     const SectionParams& sectionParams,
                                     const DetailParams&  detailParams,
                                     const TopoDS_Shape&  shape);

    /** 根據 ViewType 建立 OCCT 投影座標系 */
    gp_Ax2 buildProjectionAxis(ViewType type) const;

    /** 執行 HLRBRep_Algo，輸出可見邊與隱藏邊 */
    bool runHLR(const TopoDS_Shape& shape,
                const gp_Ax2& axis,
                TopoDS_Compound& outVisible,
                TopoDS_Compound& outHidden);

    /** 從剖面參數建立切割平面，並裁切形體 */
    TopoDS_Shape applySectionCut(const TopoDS_Shape& shape,
                                 const SectionParams& params);

    /** 剖面視圖的投影軸：從切割平面法向正側往負側看 */
    gp_Ax2 buildSectionAxis(const SectionParams& params) const;

    /** 辨識圓柱面並產生中心線（只收 ≥~170° 的圓柱面、且位於 bounds 內者） */
    TopoDS_Compound detectCenterLines(const TopoDS_Shape& shape,
                                      const gp_Ax2& axis,
                                      const QRectF& projectedBounds);

    /** 取出位於切割平面上的平面（剖切面），轉到視圖座標後回傳其 Wire */
    TopoDS_Compound extractSectionFaces(const TopoDS_Shape& shape,
                                        const gp_Ax2& axis,
                                        const SectionParams& params);
};

// ─────────────────────────────────────────────────────────────────────────────
// DrawingSheetPainter
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 圖面繪圖器 — 將 ViewRenderResult 畫到 QPainter
 *
 * 支援三種輸出目標：
 *   - QWidget (DrawingSheetWidget) — 螢幕預覽
 *   - QPrinter — PDF 輸出
 *   - QSvgGenerator — SVG 輸出
 */
class DrawingSheetPainter {
public:
    /** 圖目錄表格：座標（圖紙 mm）落在第幾筆資料列（0 起算）；不在資料列上回傳 -1。 */
    static int drawingIndexRowAt(const DrawingIndex* index, const QPointF& sheetPt);

    explicit DrawingSheetPainter();

    /**
     * @brief 繪製整張圖紙
     * @param painter 已設定座標系的 QPainter（座標單位：mm 或像素，由 transform 決定）
     * @param sheet   圖紙資料
     * @param results 各視圖的渲染結果（key = view id）
     */
    void paintSheet(QPainter& painter,
                    const DrawingSheet& sheet,
                    const QMap<QUuid, ViewRenderResult>& results);

private:
    // 目前圖紙的線寬（paintSheet 開頭由 SheetConfig 設定）
    double m_lwThin   = 0.25;
    double m_lwMedium = 0.5;
    double m_lwThick  = 0.7;

    void paintFrame(QPainter& painter, const DrawingSheet& sheet);
    void paintTitleBlock(QPainter& painter, const TitleBlock& tb, const QSizeF& sheetSize);
    void paintCover(QPainter& painter, const DrawingSheet& sheet);
    void paintView(QPainter& painter, const DrawingView* view, const ViewRenderResult& result);
    void paintPartsList(QPainter& painter, const PartsListTable* table);
    void paintRevisionTable(QPainter& painter, const RevisionTable* table);
    void paintDrawingIndex(QPainter& painter, const DrawingIndex* index);
    void paintLegend(QPainter& painter, const Legend* legend);
    void paintNoteBlock(QPainter& painter, const NoteBlock& note);

    /** 剖面填充線：以剖切面 Wire 為區域，畫 45° 平行線（間距 mm，與比例無關） */
    void paintHatching(QPainter& painter, const TopoDS_Compound& wires,
                       const QPointF& offset, double scale);

    /** 視圖外框尺寸（總寬、總高），sizeMM 為內容在圖紙上的大小，modelSize 為模型單位 */
    void paintOverallDimensions(QPainter& painter, const QSizeF& sizeMM, const QSizeF& modelSize);

    /** 剖切線（含箭頭與代號）畫在宿主視圖上；局部放大範圍圈畫在來源視圖上 */
    void paintViewAnnotations(QPainter& painter, const DrawingSheet& sheet,
                              const QMap<QUuid, ViewRenderResult>& results);

    /** 將 OCCT 邊集合畫成 QPainterPath */
    void paintEdges(QPainter& painter, const TopoDS_Compound& edges,
                    const QPen& pen, const QPointF& offset, double scale);
};

} // namespace drawing
} // namespace aicad

// 跨執行緒 queued 傳遞需要的 metatype（沒有註冊時 invokeMethod 會靜默丟棄結果）
Q_DECLARE_METATYPE(aicad::drawing::ViewRenderResult)
