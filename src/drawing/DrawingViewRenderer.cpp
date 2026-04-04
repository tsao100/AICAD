/**
 * @file DrawingViewRenderer.cpp
 * @brief 圖面視圖渲染器實作 — OCCT HLR 投影 + QPainter 繪圖
 * @author AICAD Team
 * @date 2025-01-08
 *
 * OCCT HLR 流程：
 *   1. HLRBRep_Algo  ← 精確 B-Rep 隱藏線演算法（慢、精確）
 *   2. HLRBRep_HLRToShape ← 從演算法結果抽取線段 Compound
 *   3. HLRAlgo_Projector ← 定義投影方向（正交 / 透視）
 *
 * 剖面視圖：先用 BRepAlgoAPI_Cut 切割，再對切割結果跑 HLR；
 *           剖面填充線用 BRepOffsetAPI_MakeOffset + 平行線法。
 */

#include "DrawingViewRenderer.h"
#include "DrawingSheet.h"

// ── Qt ────────────────────────────────────────────────────────────────────────
#include <QThreadPool>
#include <QRunnable>
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <QtMath>

// ── OCCT: HLR ─────────────────────────────────────────────────────────────────
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <HLRAlgo_Projector.hxx>

// ── OCCT: Boolean / Cut ───────────────────────────────────────────────────────
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>

// ── OCCT: Topology ────────────────────────────────────────────────────────────
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp_Explorer.hxx>

// ── OCCT: Geometry ────────────────────────────────────────────────────────────
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Pln.hxx>
#include <gp_Ax1.hxx>
#include <Geom_Plane.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <GCPnts_UniformAbscissa.hxx>

// ── OCCT: Curve tessellation ──────────────────────────────────────────────────
#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_TangentialDeflection.hxx>

// ── Standard ──────────────────────────────────────────────────────────────────
#include <Standard_Failure.hxx>

namespace aicad {
namespace drawing {

// ─────────────────────────────────────────────────────────────────────────────
// 非同步渲染任務（QRunnable）
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 在背景執行緒中執行 HLR 渲染的任務
 *
 * 完成後透過 Qt::QueuedConnection 將結果回傳給主執行緒的 DrawingViewRenderer。
 * 注意：TopoDS_Shape 與 ViewRenderResult 包含 OCCT Handle（引用計數），
 * 跨執行緒傳遞時必須確保 shape 不被主執行緒同時修改。
 */
class RenderTask : public QRunnable {
public:
    RenderTask(DrawingViewRenderer* renderer,
               const DrawingView*   view,
               const TopoDS_Shape&  shape)
        : m_renderer(renderer)
        , m_viewId(view->id())
        , m_viewType(view->type())
        , m_scale(view->scale())
        , m_showHidden(view->showHiddenLines())
        , m_showCenter(view->showCenterLines())
        , m_sectionParams(view->sectionParams())
        , m_detailParams(view->detailParams())
        , m_shape(shape)
    {
        setAutoDelete(true);
    }

    void run() override {
        ViewRenderResult result = m_renderer->render_internal(
            m_viewId, m_viewType, m_scale,
            m_showHidden, m_showCenter,
            m_sectionParams, m_detailParams,
            m_shape);

        // 透過 invokeMethod 回到主執行緒
        QMetaObject::invokeMethod(m_renderer, "onRenderTaskFinished",
                                  Qt::QueuedConnection,
                                  Q_ARG(QUuid, m_viewId),
                                  Q_ARG(ViewRenderResult, result));
    }

private:
    DrawingViewRenderer* m_renderer;
    QUuid                m_viewId;
    ViewType             m_viewType;
    double               m_scale;
    bool                 m_showHidden;
    bool                 m_showCenter;
    SectionParams        m_sectionParams;
    DetailParams         m_detailParams;
    TopoDS_Shape         m_shape;
};

// ─────────────────────────────────────────────────────────────────────────────
// DrawingViewRenderer
// ─────────────────────────────────────────────────────────────────────────────

DrawingViewRenderer::DrawingViewRenderer(QObject* parent)
    : QObject(parent)
{
    qDebug() << "[DrawingViewRenderer] Created";
}

DrawingViewRenderer::~DrawingViewRenderer() = default;

// ── 公開 API ──────────────────────────────────────────────────────────────────

ViewRenderResult DrawingViewRenderer::render(const DrawingView* view,
                                             const TopoDS_Shape& shape)
{
    if (!view) {
        ViewRenderResult r;
        r.errorMessage = "view is null";
        return r;
    }
    if (shape.IsNull()) {
        ViewRenderResult r;
        r.errorMessage = "source shape is null";
        return r;
    }

    return render_internal(view->id(), view->type(), view->scale(),
                           view->showHiddenLines(), view->showCenterLines(),
                           view->sectionParams(), view->detailParams(),
                           shape);
}

void DrawingViewRenderer::renderAsync(const DrawingView* view,
                                      const TopoDS_Shape& shape)
{
    if (!view || shape.IsNull()) {
        Q_EMIT renderError(view ? view->id() : QUuid(),
                           "Invalid view or shape");
        return;
    }

    auto* task = new RenderTask(this, view, shape);
    QThreadPool::globalInstance()->start(task);
    qDebug() << "[DrawingViewRenderer] Async render queued for view" << view->label();
}

// ── 內部渲染核心 ──────────────────────────────────────────────────────────────

ViewRenderResult DrawingViewRenderer::render_internal(
    const QUuid&          viewId,
    ViewType              viewType,
    double                /*scale*/,
    bool                  showHidden,
    bool                  showCenter,
    const SectionParams&  sectionParams,
    const DetailParams&   /*detailParams*/,
    const TopoDS_Shape&   shape)
{
    ViewRenderResult result;

    try {
        // ── Step 1: 剖面切割（Section view 才執行）──────────────────────────
        TopoDS_Shape workShape = shape;
        if (viewType == ViewType::Section) {
            workShape = applySectionCut(shape, sectionParams);
            if (workShape.IsNull()) {
                result.errorMessage = "Section cut produced null shape";
                return result;
            }
        }

        // ── Step 2: 建立投影軸 ────────────────────────────────────────────────
        gp_Ax2 projAxis = buildProjectionAxis(viewType);

        // ── Step 3: 執行 HLR ─────────────────────────────────────────────────
        TopoDS_Compound visEdges, hidEdges;
        if (!runHLR(workShape, projAxis, visEdges, hidEdges)) {
            result.errorMessage = "HLR algorithm failed";
            return result;
        }

        result.visibleEdges = visEdges;

        if (showHidden) {
            result.hiddenEdges = hidEdges;
        }

        // ── Step 4: 中心線辨識 ────────────────────────────────────────────────
        if (showCenter) {
            result.centerLines = detectCenterLines(workShape, projAxis);
        }

        // ── Step 5: 剖面填充線 ────────────────────────────────────────────────
        if (viewType == ViewType::Section) {
            // 取剖切截面（Compound of faces on cutting plane）
            // 簡化：對 workShape 的所有 Face 產生填充線
            result.sectionHatch = generateHatching(workShape);
        }

        // ── Step 6: 計算邊界框 ────────────────────────────────────────────────
        Bnd_Box bbox;
        BRepBndLib::Add(visEdges, bbox);
        if (!bbox.IsVoid()) {
            double xmin, ymin, zmin, xmax, ymax, zmax;
            bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
            result.boundingBox = QSizeF(xmax - xmin, ymax - ymin);
        } else {
            result.boundingBox = QSizeF(100, 100);
        }

        result.isValid = true;
        qDebug() << "[DrawingViewRenderer] Render done for view"
                 << (int)viewType
                 << "bbox:" << result.boundingBox;

    } catch (const Standard_Failure& e) {
        result.errorMessage = QString("OCCT: %1").arg(e.GetMessageString());
        qCritical() << "[DrawingViewRenderer]" << result.errorMessage;
    } catch (...) {
        result.errorMessage = "Unknown error in render_internal";
        qCritical() << "[DrawingViewRenderer]" << result.errorMessage;
    }

    Q_UNUSED(viewId)
    return result;
}

// ── 槽：非同步任務完成後回到主執行緒 ─────────────────────────────────────────

void DrawingViewRenderer::onRenderTaskFinished(const QUuid& viewId,
                                               const ViewRenderResult& result)
{
    if (result.isValid) {
        Q_EMIT rendered(viewId, result);
    } else {
        Q_EMIT renderError(viewId, result.errorMessage);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// buildProjectionAxis — 標準六視圖 + 等角投影軸
// ─────────────────────────────────────────────────────────────────────────────

gp_Ax2 DrawingViewRenderer::buildProjectionAxis(ViewType type) const
{
    // OCCT 慣例：gp_Ax2(origin, normal, xDir)
    // 正交投影：沿 normal 方向看進去（normal 指向觀察者）
    switch (type) {
    case ViewType::Front:
        // 從 +Y 方向看（-Y 為觀察方向），X 向右，Z 向上
        return gp_Ax2(gp::Origin(), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));

    case ViewType::Back:
        return gp_Ax2(gp::Origin(), gp_Dir(0,  1, 0), gp_Dir(-1, 0, 0));

    case ViewType::Top:
        // 從 +Z 看下
        return gp_Ax2(gp::Origin(), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));

    case ViewType::Bottom:
        return gp_Ax2(gp::Origin(), gp_Dir(0, 0, -1), gp_Dir(1, 0, 0));

    case ViewType::Right:
        // 從 +X 看左
        return gp_Ax2(gp::Origin(), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));

    case ViewType::Left:
        return gp_Ax2(gp::Origin(), gp_Dir(-1, 0, 0), gp_Dir(0, -1, 0));

    case ViewType::Isometric: {
        // 標準等角：從 (1, 1, 1) 方向看，X 方向取 (−1, 1, 0)
        gp_Dir viewDir(-1, -1, -1);
        gp_Dir xDir(-1, 1, 0);
        return gp_Ax2(gp::Origin(), viewDir, xDir);
    }

    case ViewType::Section:
        // Section 預設和 Front 相同；實際由 applySectionCut 決定切面
        return gp_Ax2(gp::Origin(), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));

    case ViewType::Detail:
    case ViewType::Auxiliary:
    case ViewType::Custom:
    default:
        return gp_Ax2(gp::Origin(), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// runHLR — 精確隱藏線消除
// ─────────────────────────────────────────────────────────────────────────────

bool DrawingViewRenderer::runHLR(const TopoDS_Shape&  shape,
                                 const gp_Ax2&        axis,
                                 TopoDS_Compound&     outVisible,
                                 TopoDS_Compound&     outHidden)
{
    try {
        // ── 建立投影器（正交投影）─────────────────────────────────────────────
        // HLRAlgo_Projector(gp_Ax2) 使用正交投影
        HLRAlgo_Projector projector(axis);

        // ── 建立 HLR 演算法並加入 shape ──────────────────────────────────────
        Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
        algo->Add(shape);
        algo->Projector(projector);

        // ── 執行隱藏線計算 ────────────────────────────────────────────────────
        algo->Update();
        algo->Hide();

        // ── 抽取可見邊與隱藏邊 ───────────────────────────────────────────────
        HLRBRep_HLRToShape extractor(algo);

        BRep_Builder builder;

        // 可見輪廓線 + 可見稜線
        builder.MakeCompound(outVisible);
        TopoDS_Shape visOutline  = extractor.OutLineVCompound();
        TopoDS_Shape visRgline   = extractor.VCompound();
        if (!visOutline.IsNull()) builder.Add(outVisible, visOutline);
        if (!visRgline.IsNull())  builder.Add(outVisible, visRgline);

        // 隱藏輪廓線 + 隱藏稜線
        builder.MakeCompound(outHidden);
        TopoDS_Shape hidOutline  = extractor.OutLineHCompound();
        TopoDS_Shape hidRgline   = extractor.HCompound();
        if (!hidOutline.IsNull()) builder.Add(outHidden, hidOutline);
        if (!hidRgline.IsNull())  builder.Add(outHidden, hidRgline);

        return true;

    } catch (const Standard_Failure& e) {
        qWarning() << "[DrawingViewRenderer] HLR failed:"
                   << e.GetMessageString();
        return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// applySectionCut — 以平面裁切形體
// ─────────────────────────────────────────────────────────────────────────────

TopoDS_Shape DrawingViewRenderer::applySectionCut(const TopoDS_Shape& shape,
                                                  const SectionParams& params)
{
    try {
        // 由剖切線方向決定切割平面法向量
        // 水平剖切 → 以 XZ 平面（法向 Y）切割
        // 垂直剖切 → 以 YZ 平面（法向 X）切割
        gp_Dir planeNormal;
        switch (params.direction) {
        case SectionDirection::Vertical:
            planeNormal = gp_Dir(1, 0, 0);
            break;
        case SectionDirection::Diagonal:
            planeNormal = gp_Dir(1, 1, 0);
            break;
        case SectionDirection::Horizontal:
        default:
            planeNormal = gp_Dir(0, 1, 0);
            break;
        }

        // 切割平面通過剖切線起點（2D 圖紙座標轉 3D 模型座標）
        // 這裡做簡化：切割平面通過原點，僅依方向決定
        // 完整實作需要將 SectionParams 的 cutPlaneStart 反投影回 3D
        gp_Pln cuttingPlane(gp::Origin(), planeNormal);

        // 建立無限大的半空間（保留法向量正方向的一側）
        TopoDS_Face planeFace = BRepBuilderAPI_MakeFace(cuttingPlane).Face();
        gp_Pnt refPoint(planeNormal.X() * 1000,
                        planeNormal.Y() * 1000,
                        planeNormal.Z() * 1000);
        BRepPrimAPI_MakeHalfSpace halfSpace(planeFace, refPoint);
        TopoDS_Solid cutter = halfSpace.Solid();

        // 執行布林差集
        BRepAlgoAPI_Cut cutOp(shape, cutter);
        cutOp.Build();

        if (!cutOp.IsDone() || cutOp.Shape().IsNull()) {
            qWarning() << "[DrawingViewRenderer] Section cut failed";
            return shape;  // fallback: 傳回原始形體
        }

        qDebug() << "[DrawingViewRenderer] Section cut applied";
        return cutOp.Shape();

    } catch (const Standard_Failure& e) {
        qWarning() << "[DrawingViewRenderer] applySectionCut exception:"
                   << e.GetMessageString();
        return shape;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// detectCenterLines — 辨識圓柱面並產生中心線
// ─────────────────────────────────────────────────────────────────────────────

TopoDS_Compound DrawingViewRenderer::detectCenterLines(const TopoDS_Shape& shape,
                                                       const gp_Ax2&       axis)
{
    // 中心線辨識策略：
    //   遍歷所有 Face → 若為圓柱面（Geom_CylindricalSurface）
    //   → 取其軸線 → 投影到視圖平面 → 建立中心線邊
    //
    // 目前回傳空 Compound（避免對複雜形體產生誤判），
    // 完整實作將在 Phase 6 加入。

    BRep_Builder builder;
    TopoDS_Compound centerLines;
    builder.MakeCompound(centerLines);

    Q_UNUSED(shape)
    Q_UNUSED(axis)

    // TODO Phase 6：
    // TopExp_Explorer faceExp(shape, TopAbs_FACE);
    // for (; faceExp.More(); faceExp.Next()) {
    //     const TopoDS_Face& face = TopoDS::Face(faceExp.Current());
    //     Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
    //     if (surf->DynamicType() == STANDARD_TYPE(Geom_CylindricalSurface)) {
    //         // 取出軸線，投影到 axis 平面，建立 BRepBuilderAPI_MakeEdge
    //     }
    // }

    return centerLines;
}

// ─────────────────────────────────────────────────────────────────────────────
// generateHatching — 剖面填充線
// ─────────────────────────────────────────────────────────────────────────────

TopoDS_Compound DrawingViewRenderer::generateHatching(const TopoDS_Shape& sectionShape,
                                                      double spacing,
                                                      double angle)
{
    // 填充線策略：
    //   取 sectionShape 的 Bnd_Box
    //   沿 angle 方向等距產生線段，與 sectionShape 的外輪廓求交
    //   得到在截面內部的線段片段
    //
    // 目前回傳空 Compound，Phase 4 完整實作。

    BRep_Builder builder;
    TopoDS_Compound hatch;
    builder.MakeCompound(hatch);

    Q_UNUSED(sectionShape)
    Q_UNUSED(spacing)
    Q_UNUSED(angle)

    // TODO Phase 4：
    // Bnd_Box box;
    // BRepBndLib::Add(sectionShape, box);
    // double xmin, ymin, zmin, xmax, ymax, zmax;
    // box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    // double rad = qDegreesToRadians(angle);
    // double dy = spacing / qCos(rad);
    // for (double y = ymin; y <= ymax; y += dy) {
    //     gp_Pnt p1(xmin - 10, y, 0);
    //     gp_Pnt p2(xmax + 10, y, 0);
    //     // TODO: intersect line with sectionShape boundary
    //     TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(p1, p2).Edge();
    //     builder.Add(hatch, edge);
    // }

    return hatch;
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawingSheetPainter
// ─────────────────────────────────────────────────────────────────────────────

DrawingSheetPainter::DrawingSheetPainter() = default;

// ── 座標轉換常數 ──────────────────────────────────────────────────────────────
// 圖紙座標單位：mm；QPainter 座標單位：pt（1 pt ≈ 1/72 inch ≈ 0.3528 mm）
// 我們的 paintSheet 假設 QPainter 已設定 QTransform，1 unit = 1 mm（由呼叫端設定）

static constexpr double kMmToUnit = 1.0;   // 在 mm 座標系下 1 mm = 1 unit

// ── 主入口 ────────────────────────────────────────────────────────────────────

void DrawingSheetPainter::paintSheet(QPainter& painter,
                                     const DrawingSheet& sheet,
                                     const QMap<QUuid, ViewRenderResult>& results)
{
    painter.save();

    // 白色圖紙底
    QSizeF sz = sheet.config().paperSizeMM();
    painter.fillRect(QRectF(QPointF(0, 0), sz), Qt::white);

    // 1. 圖框
    paintFrame(painter, sheet);

    // 2. 標題欄
    if (sheet.config().frameTemplate != FrameTemplate::None) {
        paintTitleBlock(painter, sheet.titleBlock(), sz);
    }

    // 3. 各視圖
    for (const DrawingView* view : sheet.views()) {
        if (!view) continue;
        auto it = results.find(view->id());
        if (it != results.end() && it.value().isValid) {
            paintView(painter, view, it.value());
        }
        // 若尚未渲染完成，視圖框由 DrawingSheetWidget 自行畫
    }

    // 4. 可選表格
    if (sheet.isPartsListEnabled() && sheet.partsList())
        paintPartsList(painter, sheet.partsList());
    if (sheet.isRevisionTableEnabled() && sheet.revisionTable())
        paintRevisionTable(painter, sheet.revisionTable());
    if (sheet.isDrawingIndexEnabled() && sheet.drawingIndex())
        paintDrawingIndex(painter, sheet.drawingIndex());
    if (sheet.isLegendEnabled() && sheet.legend())
        paintLegend(painter, sheet.legend());

    // 5. 說明文字
    for (const NoteBlock& note : sheet.noteBlocks())
        paintNoteBlock(painter, note);

    painter.restore();
}

// ── 圖框 ──────────────────────────────────────────────────────────────────────

void DrawingSheetPainter::paintFrame(QPainter& painter,
                                     const DrawingSheet& sheet)
{
    if (sheet.config().frameTemplate == FrameTemplate::None) return;

    QSizeF sz = sheet.config().paperSizeMM();
    double lw = sheet.config().lineWidthThick;

    // 外框（距紙邊 5 mm）
    QPen outerPen(Qt::black, lw);
    outerPen.setCosmetic(false);
    painter.setPen(outerPen);

    constexpr double kOuter = 5.0;
    constexpr double kInner = 10.0;
    painter.drawRect(QRectF(kOuter, kOuter,
                            sz.width()  - kOuter * 2,
                            sz.height() - kOuter * 2));

    // 內框（距紙邊 10 mm，提供圖面區域邊界）
    QPen innerPen(Qt::black, sheet.config().lineWidthMedium);
    innerPen.setCosmetic(false);
    painter.setPen(innerPen);
    painter.drawRect(QRectF(kInner, kInner,
                            sz.width()  - kInner * 2,
                            sz.height() - kInner * 2));

    // 分區標記（A–F 行，1–8 列，Standard 以上才畫）
    if (sheet.config().frameTemplate == FrameTemplate::Standard ||
        sheet.config().frameTemplate == FrameTemplate::ISO7200)
    {
        QPen markPen(Qt::black, sheet.config().lineWidthThin);
        markPen.setCosmetic(false);
        painter.setPen(markPen);
        painter.setFont(QFont("Arial", 3));

        const double drawW = sz.width()  - kInner * 2;
        const double drawH = sz.height() - kInner * 2;
        const int    cols  = 8;
        const int    rows  = 6;
        const double cw    = drawW / cols;
        const double rh    = drawH / rows;

        // 垂直分割線（左右側）
        for (int c = 1; c < cols; ++c) {
            double x = kInner + c * cw;
            painter.drawLine(QPointF(x, kOuter), QPointF(x, kInner));
            painter.drawLine(QPointF(x, sz.height() - kInner),
                             QPointF(x, sz.height() - kOuter));
        }
        // 水平分割線（上下側）
        for (int r = 1; r < rows; ++r) {
            double y = kInner + r * rh;
            painter.drawLine(QPointF(kOuter, y), QPointF(kInner, y));
            painter.drawLine(QPointF(sz.width() - kInner, y),
                             QPointF(sz.width() - kOuter, y));
        }
        // 數字標記（上下）
        for (int c = 0; c < cols; ++c) {
            QString num = QString::number(c + 1);
            double cx = kInner + (c + 0.5) * cw - 1.5;
            painter.drawText(QPointF(cx, kOuter + 3), num);
            painter.drawText(QPointF(cx, sz.height() - kOuter + 1), num);
        }
        // 字母標記（左右）
        for (int r = 0; r < rows; ++r) {
            QString ch = QString(QChar('A' + r));
            double ry = kInner + (r + 0.5) * rh + 1.0;
            painter.drawText(QPointF(kOuter + 1, ry), ch);
            painter.drawText(QPointF(sz.width() - kOuter - 3.5, ry), ch);
        }
    }
}

// ── 標題欄（Standard / ISO7200 版型）────────────────────────────────────────

void DrawingSheetPainter::paintTitleBlock(QPainter& painter,
                                          const TitleBlock& tb,
                                          const QSizeF& sheetSize)
{
    // 標題欄位於圖紙右下角；Standard 版型高度 55 mm，寬度 180 mm
    constexpr double kW = 180.0;
    constexpr double kH = 55.0;
    constexpr double kInner = 10.0;

    double x0 = sheetSize.width()  - kInner - kW;
    double y0 = sheetSize.height() - kInner - kH;

    QPen borderPen(Qt::black, 0.5);
    borderPen.setCosmetic(false);
    painter.setPen(borderPen);

    // 外框
    painter.drawRect(QRectF(x0, y0, kW, kH));

    // ── 列配置（從上到下）──────────────────────────────────────────────────
    // Row 0: 公司名稱                                (h=12)
    // Row 1: 圖名                                   (h=10)
    // Row 2: 圖號 | 版次 | 頁次                    (h=8)
    // Row 3: 設計/審查/核准 | 日期                  (h=8)
    // Row 4: 材料 | 公差 | 比例 | 投影符號           (h=8)
    // Row 5: 角度公差 | 表面粗糙度                  (h=9)

    struct Row { double y; double h; };
    const QVector<Row> rows = {
                               { y0,      12 },
                               { y0 + 12, 10 },
                               { y0 + 22,  8 },
                               { y0 + 30,  8 },
                               { y0 + 38,  8 },
                               { y0 + 46,  9 },
                               };

    QPen thinPen(Qt::black, 0.25);
    thinPen.setCosmetic(false);
    painter.setPen(thinPen);

    // 水平分割線
    for (const Row& r : rows)
        painter.drawLine(QPointF(x0, r.y), QPointF(x0 + kW, r.y));

    // ── Row 0：公司名稱 ────────────────────────────────────────────────────
    painter.setFont(QFont("Arial", 5, QFont::Bold));
    painter.setPen(Qt::black);
    if (!tb.companyName.isEmpty()) {
        painter.drawText(
            QRectF(x0 + 2, rows[0].y + 2, kW - 4, rows[0].h - 4),
            Qt::AlignCenter, tb.companyName);
    }

    // ── Row 1：圖名 ───────────────────────────────────────────────────────
    painter.setFont(QFont("Arial", 4.5, QFont::Bold));
    painter.drawText(
        QRectF(x0 + 2, rows[1].y + 1, kW - 4, rows[1].h - 2),
        Qt::AlignCenter, tb.drawingTitle);

    // ── Row 2：圖號 | 版次 | 頁次 ─────────────────────────────────────────
    painter.setFont(QFont("Arial", 3));
    double col2w = kW / 3.0;

    auto drawLabelValue = [&](double cx, double cy, double cw, double ch,
                              const QString& label, const QString& value) {
        painter.setFont(QFont("Arial", 2.5));
        painter.setPen(Qt::darkGray);
        painter.drawText(QRectF(cx + 1, cy + 1, cw - 2, ch * 0.4),
                         Qt::AlignLeft | Qt::AlignTop, label);
        painter.setFont(QFont("Arial", 3.5));
        painter.setPen(Qt::black);
        painter.drawText(QRectF(cx + 1, cy + ch * 0.4, cw - 2, ch * 0.6),
                         Qt::AlignLeft | Qt::AlignVCenter, value);
    };

    // 垂直分割線 Row2
    painter.setPen(thinPen);
    painter.drawLine(QPointF(x0 + col2w, rows[2].y),
                     QPointF(x0 + col2w, rows[2].y + rows[2].h));
    painter.drawLine(QPointF(x0 + col2w * 2, rows[2].y),
                     QPointF(x0 + col2w * 2, rows[2].y + rows[2].h));

    drawLabelValue(x0,           rows[2].y, col2w, rows[2].h, "DWG NO.", tb.drawingNumber);
    drawLabelValue(x0 + col2w,   rows[2].y, col2w, rows[2].h, "REV",     tb.revision);
    drawLabelValue(x0 + col2w*2, rows[2].y, col2w, rows[2].h, "SHEET",   tb.sheetNumber);

    // ── Row 3：設計/審查/核准 ─────────────────────────────────────────────
    double col3w = kW / 3.0;
    painter.setPen(thinPen);
    painter.drawLine(QPointF(x0 + col3w,   rows[3].y),
                     QPointF(x0 + col3w,   rows[3].y + rows[3].h));
    painter.drawLine(QPointF(x0 + col3w*2, rows[3].y),
                     QPointF(x0 + col3w*2, rows[3].y + rows[3].h));

    drawLabelValue(x0,           rows[3].y, col3w, rows[3].h, "DESIGNED", tb.designedBy);
    drawLabelValue(x0 + col3w,   rows[3].y, col3w, rows[3].h, "CHECKED",  tb.checkedBy);
    drawLabelValue(x0 + col3w*2, rows[3].y, col3w, rows[3].h, "APPROVED", tb.approvedBy);

    // ── Row 4：材料 | 公差 | 比例 ─────────────────────────────────────────
    double col4a = kW * 0.35;
    double col4b = kW * 0.30;
    double col4c = kW * 0.35;
    painter.setPen(thinPen);
    painter.drawLine(QPointF(x0 + col4a, rows[4].y),
                     QPointF(x0 + col4a, rows[4].y + rows[4].h));
    painter.drawLine(QPointF(x0 + col4a + col4b, rows[4].y),
                     QPointF(x0 + col4a + col4b, rows[4].y + rows[4].h));

    drawLabelValue(x0,                  rows[4].y, col4a, rows[4].h, "MATERIAL",  tb.material);
    drawLabelValue(x0 + col4a,          rows[4].y, col4b, rows[4].h, "TOLERANCE", tb.tolerance);
    drawLabelValue(x0 + col4a + col4b,  rows[4].y, col4c, rows[4].h, "SCALE",     tb.projectName);

    // ── Row 5：日期 | 料號 ─────────────────────────────────────────────────
    double col5w = kW / 2.0;
    painter.setPen(thinPen);
    painter.drawLine(QPointF(x0 + col5w, rows[5].y),
                     QPointF(x0 + col5w, rows[5].y + rows[5].h));

    drawLabelValue(x0,         rows[5].y, col5w, rows[5].h, "DATE",    tb.designDate);
    drawLabelValue(x0 + col5w, rows[5].y, col5w, rows[5].h, "PART NO", tb.partNumber);
}

// ── 視圖 ──────────────────────────────────────────────────────────────────────

void DrawingSheetPainter::paintView(QPainter& painter,
                                    const DrawingView* view,
                                    const ViewRenderResult& result)
{
    if (!view || !result.isValid) return;

    painter.save();
    painter.translate(view->position());

    const double sc = view->scale();

    // 可見線（實線，中等線寬）
    QPen visPen(Qt::black, 0.5);
    visPen.setCosmetic(false);
    visPen.setCapStyle(Qt::RoundCap);
    visPen.setJoinStyle(Qt::RoundJoin);
    paintEdges(painter, result.visibleEdges, visPen, QPointF(0, 0), sc);

    // 隱藏線（虛線，細線）
    if (view->showHiddenLines() && !result.hiddenEdges.IsNull()) {
        QPen hidPen(QColor(80, 80, 80), 0.25);
        hidPen.setCosmetic(false);
        hidPen.setStyle(Qt::DashLine);
        paintEdges(painter, result.hiddenEdges, hidPen, QPointF(0, 0), sc);
    }

    // 中心線（點劃線，細線，青色）
    if (view->showCenterLines() && !result.centerLines.IsNull()) {
        QPen clPen(QColor(0, 150, 150), 0.2);
        clPen.setCosmetic(false);
        clPen.setStyle(Qt::DashDotLine);
        paintEdges(painter, result.centerLines, clPen, QPointF(0, 0), sc);
    }

    // 剖面填充線（細實線，灰色）
    if (!result.sectionHatch.IsNull()) {
        QPen hatchPen(QColor(100, 100, 100), 0.2);
        hatchPen.setCosmetic(false);
        paintEdges(painter, result.sectionHatch, hatchPen, QPointF(0, 0), sc);
    }

    // 視圖標籤（置中在視圖下方）
    if (view->showViewLabel()) {
        QSizeF sz = result.boundingBox * sc;
        painter.setFont(QFont("Arial", 3.5));
        painter.setPen(Qt::black);
        painter.drawText(
            QRectF(-sz.width() / 2, sz.height() / 2 + 2, sz.width(), 6),
            Qt::AlignCenter, view->label());
    }

    // 比例標籤
    if (view->showScaleLabel()) {
        QSizeF sz = result.boundingBox * sc;
        QString scaleStr = QString("SCALE %1:%2")
                               .arg(view->scale() >= 1.0 ? 1 : (int)(1.0 / view->scale()))
                               .arg(view->scale() >= 1.0 ? (int)view->scale() : 1);
        painter.setFont(QFont("Arial", 2.5));
        painter.setPen(Qt::darkGray);
        painter.drawText(
            QRectF(-sz.width() / 2, sz.height() / 2 + 6, sz.width(), 4),
            Qt::AlignCenter, scaleStr);
    }

    painter.restore();
}

// ── OCCT 邊線 → QPainterPath 繪製 ────────────────────────────────────────────

void DrawingSheetPainter::paintEdges(QPainter& painter,
                                     const TopoDS_Compound& edges,
                                     const QPen& pen,
                                     const QPointF& offset,
                                     double scale)
{
    if (edges.IsNull()) return;

    painter.setPen(pen);

    TopExp_Explorer edgeExp(edges, TopAbs_EDGE);
    for (; edgeExp.More(); edgeExp.Next()) {
        const TopoDS_Edge& edge = TopoDS::Edge(edgeExp.Current());
        if (edge.IsNull()) continue;

        try {
            BRepAdaptor_Curve curve(edge);

            // 曲線離散（角度偏差 0.5°，線性偏差 0.01 mm）
            GCPnts_TangentialDeflection disc(curve,
                                             qDegreesToRadians(0.5),
                                             0.01);
            const int npts = disc.NbPoints();
            if (npts < 2) continue;

            QPainterPath path;
            for (int i = 1; i <= npts; ++i) {
                gp_Pnt p = disc.Value(i);
                // HLR 輸出的座標在 XOY 平面（Z ≈ 0）
                // X → 右，Y → 上（需翻轉 Y 以符合螢幕座標）
                QPointF pt(p.X() * scale + offset.x(),
                           -p.Y() * scale + offset.y());
                if (i == 1) path.moveTo(pt);
                else        path.lineTo(pt);
            }
            painter.drawPath(path);

        } catch (const Standard_Failure&) {
            // 跳過有問題的邊
        }
    }
}

// ── 零件表 ────────────────────────────────────────────────────────────────────

void DrawingSheetPainter::paintPartsList(QPainter& painter,
                                         const PartsListTable* table)
{
    if (!table) return;

    const PartsListConfig& cfg = table->config();
    const QPointF origin = cfg.position;

    // 欄位寬度映射（mm）
    static const QMap<QString, double> colWidths = {
                                                    {"itemNo",      10},
                                                    {"partNumber",  30},
                                                    {"description", 55},
                                                    {"material",    25},
                                                    {"quantity",    12},
                                                    {"unit",        10},
                                                    {"revision",    10},
                                                    {"remark",      28},
                                                    };

    // 計算總寬度
    double totalW = 0;
    for (const QString& col : cfg.visibleColumns)
        totalW += colWidths.value(col, 20);

    // 欄位標題名稱
    static const QMap<QString, QString> colLabels = {
                                                     {"itemNo",      "NO."},
                                                     {"partNumber",  "PART NUMBER"},
                                                     {"description", "DESCRIPTION"},
                                                     {"material",    "MATERIAL"},
                                                     {"quantity",    "QTY"},
                                                     {"unit",        "UNIT"},
                                                     {"revision",    "REV"},
                                                     {"remark",      "REMARK"},
                                                     };

    QPen borderPen(Qt::black, 0.35);
    borderPen.setCosmetic(false);
    painter.setPen(borderPen);

    // Header 行
    double y = origin.y();
    double x = origin.x();

    painter.fillRect(QRectF(x, y, totalW, cfg.headerHeight),
                     QColor(220, 220, 220));
    painter.drawRect(QRectF(x, y, totalW, cfg.headerHeight));

    painter.setFont(QFont("Arial", 3, QFont::Bold));
    painter.setPen(Qt::black);
    double cx = x;
    for (const QString& col : cfg.visibleColumns) {
        double cw = colWidths.value(col, 20);
        painter.drawText(QRectF(cx + 1, y + 1, cw - 2, cfg.headerHeight - 2),
                         Qt::AlignCenter, colLabels.value(col, col));
        painter.setPen(borderPen);
        painter.drawLine(QPointF(cx + cw, y),
                         QPointF(cx + cw, y + cfg.headerHeight));
        painter.setPen(Qt::black);
        cx += cw;
    }

    // 資料行（從下往上排，Item 1 在最下方）
    y += cfg.headerHeight;
    painter.setFont(QFont("Arial", 3));
    for (const PartsListRow& row : table->rows()) {
        cx = x;
        painter.setPen(borderPen);
        painter.drawRect(QRectF(x, y, totalW, cfg.rowHeight));

        painter.setPen(Qt::black);
        QMap<QString, QString> rowData = {
                                          {"itemNo",      QString::number(row.itemNo)},
                                          {"partNumber",  row.partNumber},
                                          {"description", row.description},
                                          {"material",    row.material},
                                          {"quantity",    QString::number(row.quantity)},
                                          {"unit",        row.unit},
                                          {"revision",    row.revision},
                                          {"remark",      row.remark},
                                          };
        for (const QString& col : cfg.visibleColumns) {
            double cw = colWidths.value(col, 20);
            painter.drawText(QRectF(cx + 1, y + 1, cw - 2, cfg.rowHeight - 2),
                             Qt::AlignVCenter | Qt::AlignLeft,
                             rowData.value(col));
            painter.setPen(borderPen);
            painter.drawLine(QPointF(cx + cw, y),
                             QPointF(cx + cw, y + cfg.rowHeight));
            painter.setPen(Qt::black);
            cx += cw;
        }
        y += cfg.rowHeight;
    }
}

// ── 進版表 ────────────────────────────────────────────────────────────────────

void DrawingSheetPainter::paintRevisionTable(QPainter& painter,
                                             const RevisionTable* table)
{
    if (!table) return;

    const QPointF origin = table->position();
    constexpr double kRowH = 8.0;
    constexpr double kHdrH = 9.0;
    constexpr double kW    = 110.0;

    // 欄位
    struct Col { QString label; double w; };
    const QVector<Col> cols = {
        {"REV", 10}, {"DATE", 22}, {"DESCRIPTION", 45},
        {"BY", 16}, {"APPR", 17}
    };

    QPen borderPen(Qt::black, 0.35);
    borderPen.setCosmetic(false);

    // Header
    double x0 = origin.x(), y0 = origin.y();
    painter.fillRect(QRectF(x0, y0, kW, kHdrH), QColor(220, 220, 220));
    painter.setPen(borderPen);
    painter.drawRect(QRectF(x0, y0, kW, kHdrH));

    painter.setFont(QFont("Arial", 3, QFont::Bold));
    painter.setPen(Qt::black);
    double cx = x0;
    for (const Col& c : cols) {
        painter.drawText(QRectF(cx + 1, y0 + 1, c.w - 2, kHdrH - 2),
                         Qt::AlignCenter, c.label);
        painter.setPen(borderPen);
        painter.drawLine(QPointF(cx + c.w, y0),
                         QPointF(cx + c.w, y0 + kHdrH));
        painter.setPen(Qt::black);
        cx += c.w;
    }

    // 資料行
    double y = y0 + kHdrH;
    painter.setFont(QFont("Arial", 3));
    for (const RevisionEntry& e : table->entries()) {
        cx = x0;
        painter.setPen(borderPen);
        painter.drawRect(QRectF(x0, y, kW, kRowH));
        painter.setPen(Qt::black);

        QStringList vals = { e.revision, e.date, e.description, e.changedBy, e.approvedBy };
        for (int i = 0; i < cols.size(); ++i) {
            double cw = cols[i].w;
            painter.drawText(QRectF(cx + 1, y + 1, cw - 2, kRowH - 2),
                             Qt::AlignVCenter | Qt::AlignLeft,
                             i < vals.size() ? vals[i] : "");
            painter.setPen(borderPen);
            painter.drawLine(QPointF(cx + cw, y), QPointF(cx + cw, y + kRowH));
            painter.setPen(Qt::black);
            cx += cw;
        }
        y += kRowH;
    }
}

// ── 圖目錄 ────────────────────────────────────────────────────────────────────

void DrawingSheetPainter::paintDrawingIndex(QPainter& painter,
                                            const DrawingIndex* idx)
{
    if (!idx) return;

    const QPointF origin = idx->position();
    constexpr double kRowH = 8.0;
    constexpr double kHdrH = 10.0;
    constexpr double kW    = 160.0;

    struct Col { QString label; double w; };
    const QVector<Col> cols = {
        {"SHEET", 15}, {"DWG NO.", 35}, {"TITLE", 65},
        {"REV", 12}, {"DATE", 20}, {"REMARK", 13}
    };

    QPen borderPen(Qt::black, 0.35);
    borderPen.setCosmetic(false);

    // 標題列
    double x0 = origin.x(), y0 = origin.y();
    painter.setFont(QFont("Arial", 4, QFont::Bold));
    painter.setPen(Qt::black);
    painter.drawText(QRectF(x0, y0 - 7, kW, 6),
                     Qt::AlignLeft | Qt::AlignVCenter, idx->title());

    // Header
    painter.fillRect(QRectF(x0, y0, kW, kHdrH), QColor(200, 220, 240));
    painter.setPen(borderPen);
    painter.drawRect(QRectF(x0, y0, kW, kHdrH));
    painter.setFont(QFont("Arial", 3, QFont::Bold));
    painter.setPen(Qt::black);

    double cx = x0;
    for (const Col& c : cols) {
        painter.drawText(QRectF(cx + 1, y0 + 1, c.w - 2, kHdrH - 2),
                         Qt::AlignCenter, c.label);
        painter.setPen(borderPen);
        painter.drawLine(QPointF(cx + c.w, y0),
                         QPointF(cx + c.w, y0 + kHdrH));
        painter.setPen(Qt::black);
        cx += c.w;
    }

    // 資料行
    double y = y0 + kHdrH;
    painter.setFont(QFont("Arial", 3));
    for (const DrawingIndexEntry& e : idx->entries()) {
        cx = x0;
        painter.setPen(borderPen);
        painter.drawRect(QRectF(x0, y, kW, kRowH));
        painter.setPen(Qt::black);

        QStringList vals = { e.sheetNumber, e.drawingNumber, e.title,
                            e.revision, e.date, e.remark };
        for (int i = 0; i < cols.size(); ++i) {
            double cw = cols[i].w;
            painter.drawText(QRectF(cx + 1, y + 1, cw - 2, kRowH - 2),
                             Qt::AlignVCenter | Qt::AlignLeft,
                             i < vals.size() ? vals[i] : "");
            painter.setPen(borderPen);
            painter.drawLine(QPointF(cx + cw, y), QPointF(cx + cw, y + kRowH));
            painter.setPen(Qt::black);
            cx += cw;
        }
        y += kRowH;
    }
}

// ── 圖例 ──────────────────────────────────────────────────────────────────────

void DrawingSheetPainter::paintLegend(QPainter& painter, const Legend* legend)
{
    if (!legend) return;

    const QPointF origin = legend->position();
    constexpr double kRowH = 7.0;
    constexpr double kHdrH = 8.0;
    constexpr double kW    = 70.0;

    QPen borderPen(Qt::black, 0.35);
    borderPen.setCosmetic(false);

    double x0 = origin.x(), y0 = origin.y();

    // 標題
    painter.setFont(QFont("Arial", 3.5, QFont::Bold));
    painter.setPen(Qt::black);
    painter.fillRect(QRectF(x0, y0, kW, kHdrH), QColor(220, 220, 220));
    painter.setPen(borderPen);
    painter.drawRect(QRectF(x0, y0, kW, kHdrH));
    painter.setPen(Qt::black);
    painter.drawText(QRectF(x0 + 1, y0 + 1, kW - 2, kHdrH - 2),
                     Qt::AlignCenter, legend->title());

    // 條目
    double y = y0 + kHdrH;
    painter.setFont(QFont("Arial", 3));
    for (const LegendEntry& e : legend->entries()) {
        painter.setPen(borderPen);
        painter.drawRect(QRectF(x0, y, kW, kRowH));
        painter.setPen(Qt::black);
        // 符號欄（左 15mm）
        painter.drawText(QRectF(x0 + 1, y + 1, 14, kRowH - 2),
                         Qt::AlignCenter, e.symbol);
        // 說明欄
        painter.drawText(QRectF(x0 + 16, y + 1, kW - 17, kRowH - 2),
                         Qt::AlignVCenter | Qt::AlignLeft, e.description);
        // 垂直分割線
        painter.drawLine(QPointF(x0 + 15, y), QPointF(x0 + 15, y + kRowH));
        y += kRowH;
    }
}

// ── 說明文字區塊 ──────────────────────────────────────────────────────────────

void DrawingSheetPainter::paintNoteBlock(QPainter& painter,
                                         const NoteBlock& note)
{
    QRectF rect(note.position, note.size);

    if (note.hasBorder) {
        QPen borderPen(Qt::black, 0.35);
        borderPen.setCosmetic(false);
        painter.setPen(borderPen);
        painter.drawRect(rect);
    }

    // 標題（若有）
    double contentY = note.position.y() + 1;
    if (!note.title.isEmpty()) {
        painter.setFont(QFont("Arial", note.fontSize * 1.1, QFont::Bold));
        painter.setPen(Qt::black);
        QRectF titleRect(note.position.x() + 1, contentY,
                         note.size.width() - 2, note.fontSize * 2);
        painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignTop, note.title);

        // 分隔線
        painter.setPen(QPen(Qt::black, 0.25));
        painter.drawLine(QPointF(note.position.x() + 1, contentY + note.fontSize * 2),
                         QPointF(note.position.x() + note.size.width() - 1,
                                 contentY + note.fontSize * 2));
        contentY += note.fontSize * 2 + 1;
    }

    // 內文
    painter.setFont(QFont("Arial", note.fontSize));
    painter.setPen(Qt::black);
    QRectF contentRect(note.position.x() + 1, contentY,
                       note.size.width() - 2,
                       note.position.y() + note.size.height() - contentY - 1);
    painter.drawText(contentRect, Qt::AlignLeft | Qt::AlignTop |
                                      Qt::TextWordWrap, note.content);
}

} // namespace drawing
} // namespace aicad
