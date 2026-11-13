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
#include "PlanProfilePainter.h"
#include "DrawingSheet.h"
#include "cad/Feature.h"
#include <cmath>
#include <algorithm>
#include <initializer_list>
#include <TopoDS_Wire.hxx>

// ── Qt ────────────────────────────────────────────────────────────────────────
#include <QThreadPool>
#include <QRunnable>
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <QtMath>
#include <QSet>

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
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <gp_Trsf.hxx>
#include <gp_Ax3.hxx>
#include <gp_Cylinder.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepTools_WireExplorer.hxx>
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

        // 以 functor 形式 queued 回主執行緒（不依賴 metatype 字串解析）
        DrawingViewRenderer* r  = m_renderer;
        const QUuid          id = m_viewId;
        QMetaObject::invokeMethod(r, [r, id, result]() {
            r->onRenderTaskFinished(id, result);
        }, Qt::QueuedConnection);
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
    qRegisterMetaType<ViewRenderResult>("ViewRenderResult");
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

double DrawingViewRenderer::computeFitScale(const DrawingView* view,
                                            const QRectF& b)
{
    auto niceFloor = [](double raw) {
        if (!(raw > 0.0)) return 1.0;
        const double base = std::pow(10.0, std::floor(std::log10(raw)));
        const double m = raw / base;
        return ((m >= 5.0) ? 5.0 : (m >= 2.0) ? 2.0 : 1.0) * base;
    };
    constexpr double kFill = 0.9;
    const double sx = (b.width()  > 1e-9) ? view->size().width()  * kFill / b.width()  : 1e30;
    const double sy = (b.height() > 1e-9) ? view->size().height() * kFill / b.height() : 1e30;
    const double raw = qMin(sx, sy);
    return (raw >= 1e30) ? 1.0 : niceFloor(raw);
}

QMap<QUuid, ViewRenderResult> DrawingViewRenderer::renderSheetSync(DrawingSheet& sheet)
{
    QMap<QUuid, ViewRenderResult> results;
    DrawingViewRenderer renderer;
    for (DrawingView* v : sheet.views()) {
        if (!v || !v->sourceFeature()) continue;
        const TopoDS_Shape shape = v->sourceFeature()->shape();
        if (shape.IsNull()) continue;
        ViewRenderResult r = renderer.render(v, shape);
        if (!r.isValid) {
            qWarning() << "[DrawingViewRenderer] sync render failed for" << v->label()
                       << ":" << r.errorMessage;
            continue;
        }
        if (v->autoFit())
            v->applyFitScale(computeFitScale(v, r.projectedBounds));
        results.insert(v->id(), r);
    }
    return results;
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
    const DetailParams&   detailParams,
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
        gp_Ax2 projAxis;
        if (viewType == ViewType::Section)
            projAxis = buildSectionAxis(sectionParams);
        else if (viewType == ViewType::Detail)
            projAxis = buildProjectionAxis(detailParams.sourceType);   // 放大來源投影的一部分
        else
            projAxis = buildProjectionAxis(viewType);

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

        // ── Step 4: 剖切面（hatching 區域；實際填充線於繪製時依比例裁切）───────
        if (viewType == ViewType::Section)
            result.sectionFaces = extractSectionFaces(workShape, projAxis, sectionParams);

        // ── Step 5: 計算投影範圍 ──────────────────────────────────────────────
        Bnd_Box bbox;
        BRepBndLib::Add(visEdges, bbox);
        if (showHidden && !hidEdges.IsNull())
            BRepBndLib::Add(hidEdges, bbox);
        if (bbox.IsVoid()) {
            // HLR 沒有產生任何邊（形體為空、或只含無法投影的幾何）→ 當作失敗，
            // 讓 Widget 的 renderError 路徑印出原因，而不是靜默畫出空白。
            result.errorMessage = "HLR produced no edges (empty projection)";
            return result;
        }
        bbox.SetGap(0.0);
        double xmin, ymin, zmin, xmax, ymax, zmax;
        bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);

        if (viewType == ViewType::Detail) {
            // 局部放大：範圍就是放大圓（繪製時以圓裁切，並置中於視圖中心）
            const double r = qMax(detailParams.radius, 1e-6);
            result.projectedBounds = QRectF(detailParams.center.x() - r,
                                            detailParams.center.y() - r, 2 * r, 2 * r);
            result.boundingBox = QSizeF(2 * r, 2 * r);
        } else {
            result.boundingBox     = QSizeF(xmax - xmin, ymax - ymin);
            result.projectedBounds = QRectF(QPointF(xmin, ymin), QPointF(xmax, ymax));
        }

        // ── Step 6: 中心線辨識（需要範圍以過濾遠離形體的大半徑圓弧面）────────
        if (showCenter)
            result.centerLines = detectCenterLines(workShape, projAxis, result.projectedBounds);

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
        // 切割平面：法向 N，通過 N * offset
        const gp_Pnt planeOrigin(planeNormal.X() * params.offset,
                                 planeNormal.Y() * params.offset,
                                 planeNormal.Z() * params.offset);
        gp_Pln cuttingPlane(planeOrigin, planeNormal);

        // 半空間（法向「正」側）被移除 → 保留負側；觀看者位於正側（見 buildSectionAxis）
        TopoDS_Face planeFace = BRepBuilderAPI_MakeFace(cuttingPlane).Face();
        gp_Pnt refPoint(planeNormal.X() * (params.offset + 1.0),
                        planeNormal.Y() * (params.offset + 1.0),
                        planeNormal.Z() * (params.offset + 1.0));
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
// 剖面：法向 / 投影軸
// ─────────────────────────────────────────────────────────────────────────────

namespace {
gp_Dir sectionNormalOf(const SectionParams& p)
{
    switch (p.direction) {
    case SectionDirection::Vertical: return gp_Dir(1, 0, 0);
    case SectionDirection::Diagonal: return gp_Dir(1, 1, 0);
    case SectionDirection::Horizontal:
    default:                         return gp_Dir(0, 1, 0);
    }
}
} // namespace

gp_Ax2 DrawingViewRenderer::buildSectionAxis(const SectionParams& p) const
{
    // 保留的是法向負側（見 applySectionCut），觀看者在法向正側，視線朝向 -N，
    // 因此 gp_Ax2 的主方向（朝向觀看者）取 +N。
    switch (p.direction) {
    case SectionDirection::Vertical:   // N=+X：同「右視圖」
        return gp_Ax2(gp::Origin(), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
    case SectionDirection::Diagonal:   // N=(1,1,0)/√2，X 軸取 (-1,1,0)，Y 軸 = Z
        return gp_Ax2(gp::Origin(), gp_Dir(1, 1, 0), gp_Dir(-1, 1, 0));
    case SectionDirection::Horizontal: // N=+Y：同「後視圖」
    default:
        return gp_Ax2(gp::Origin(), gp_Dir(0, 1, 0), gp_Dir(-1, 0, 0));
    }
}

TopoDS_Compound DrawingViewRenderer::extractSectionFaces(const TopoDS_Shape& shape,
                                                         const gp_Ax2& axis,
                                                         const SectionParams& p)
{
    BRep_Builder builder;
    TopoDS_Compound out;
    builder.MakeCompound(out);

    try {
        const gp_Dir n = sectionNormalOf(p);
        gp_Trsf toView;
        toView.SetTransformation(gp_Ax3(axis));   // 全域座標 → 視圖座標（與 HLR 輸出同座標）

        for (TopExp_Explorer fe(shape, TopAbs_FACE); fe.More(); fe.Next()) {
            const TopoDS_Face face = TopoDS::Face(fe.Current());
            BRepAdaptor_Surface surf(face);
            if (surf.GetType() != GeomAbs_Plane) continue;

            const gp_Pln pl = surf.Plane();
            if (qAbs(pl.Axis().Direction().Dot(n)) < 0.9999) continue;       // 非平行於切面
            const double dist = pl.Location().XYZ().Dot(n.XYZ());
            if (qAbs(dist - p.offset) > 1e-4 * qMax(1.0, qAbs(p.offset))) continue;  // 不在切面上

            BRepBuilderAPI_Transform xf(face, toView, true);
            const TopoDS_Shape f2 = xf.Shape();
            for (TopExp_Explorer we(f2, TopAbs_WIRE); we.More(); we.Next())
                builder.Add(out, we.Current());
        }
    } catch (const Standard_Failure& e) {
        qWarning() << "[DrawingViewRenderer] extractSectionFaces:" << e.GetMessageString();
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// detectCenterLines — 辨識圓柱面並產生中心線
// ─────────────────────────────────────────────────────────────────────────────

TopoDS_Compound DrawingViewRenderer::detectCenterLines(const TopoDS_Shape& shape,
                                                       const gp_Ax2&       axis,
                                                       const QRectF&       bounds)
{
    // 策略：遍歷圓柱面 → 取軸線 → 轉到視圖座標：
    //   • 軸線與視線平行（端視，呈圓）→ 在圓心畫十字中心線
    //   • 否則 → 沿軸線畫一段中心線（兩端各外延 15%）
    // 過濾：
    //   • 圓周角 < 3 rad（~172°）的圓柱面略過（如鐵路線形拉伸出的大半徑弧面，
    //     其「軸」遠在形體之外，畫出來只是雜訊）
    //   • 中心／軸線中點不在投影範圍（外擴 5%）內者略過
    //   • 軸線端點（四捨五入後）相同者去重（圓柱常被分成兩個半面）
    BRep_Builder builder;
    TopoDS_Compound cl;
    builder.MakeCompound(cl);

    try {
        gp_Trsf toView;
        toView.SetTransformation(gp_Ax3(axis));

        const double m = 0.05 * qMax(bounds.width(), bounds.height());
        const QRectF allowed = bounds.adjusted(-m, -m, m, m);
        QSet<QString> seen;

        auto addSeg = [&](const gp_Pnt& a, const gp_Pnt& b) {
            const QString key = QString("%1,%2,%3,%4")
                                    .arg(qRound64(a.X() * 1000)).arg(qRound64(a.Y() * 1000))
                                    .arg(qRound64(b.X() * 1000)).arg(qRound64(b.Y() * 1000));
            if (seen.contains(key)) return;
            seen.insert(key);
            if (a.Distance(b) < 1e-9) return;
            builder.Add(cl, BRepBuilderAPI_MakeEdge(a, b).Edge());
        };

        for (TopExp_Explorer fe(shape, TopAbs_FACE); fe.More(); fe.Next()) {
            const TopoDS_Face face = TopoDS::Face(fe.Current());
            BRepAdaptor_Surface surf(face);
            if (surf.GetType() != GeomAbs_Cylinder) continue;

            double u1, u2, v1, v2;
            BRepTools::UVBounds(face, u1, u2, v1, v2);
            if (u2 - u1 < 3.0) continue;

            const gp_Cylinder cyl = surf.Cylinder();
            const gp_Ax1 ax = cyl.Axis();
            const double r  = cyl.Radius();

            gp_Pnt p0 = ax.Location().Translated(gp_Vec(ax.Direction()) * v1);
            gp_Pnt p1 = ax.Location().Translated(gp_Vec(ax.Direction()) * v2);
            p0.Transform(toView);
            p1.Transform(toView);
            const gp_Dir d = ax.Direction().Transformed(toView);

            if (qAbs(d.Z()) > 0.98) {
                // 端視：十字中心線
                const QPointF c(p0.X(), p0.Y());
                if (!allowed.contains(c)) continue;
                const double h = r * 1.3;
                addSeg(gp_Pnt(c.x() - h, c.y(), 0), gp_Pnt(c.x() + h, c.y(), 0));
                addSeg(gp_Pnt(c.x(), c.y() - h, 0), gp_Pnt(c.x(), c.y() + h, 0));
            } else {
                const QPointF mid((p0.X() + p1.X()) / 2, (p0.Y() + p1.Y()) / 2);
                if (!allowed.contains(mid)) continue;
                const gp_Vec v(p0, p1);
                const gp_Vec ext = v * 0.15;
                gp_Pnt a(p0.X() - ext.X(), p0.Y() - ext.Y(), 0);
                gp_Pnt b(p1.X() + ext.X(), p1.Y() + ext.Y(), 0);
                addSeg(a, b);
            }
        }
    } catch (const Standard_Failure& e) {
        qWarning() << "[DrawingViewRenderer] detectCenterLines:" << e.GetMessageString();
    }
    return cl;
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

    m_lwThin   = sheet.config().lineWidthThin;
    m_lwMedium = sheet.config().lineWidthMedium;
    m_lwThick  = sheet.config().lineWidthThick;

    // 白色圖紙底
    QSizeF sz = sheet.config().paperSizeMM();
    painter.fillRect(QRectF(QPointF(0, 0), sz), Qt::white);

    // 1. 圖框
    paintFrame(painter, sheet);

    // 2. 標題欄
    if (sheet.isCover()) {
        paintCover(painter, sheet);
    } else if (sheet.config().frameTemplate != FrameTemplate::None) {
        paintTitleBlock(painter, sheet.titleBlock(), sz);
    }

    // 2b. 平縱面圖層（Plan & Profile）
    if (sheet.planProfile().enabled || sheet.planProfile().tableKind > 0)
        paintPlanProfileLayer(painter, sheet);

    // 3. 各視圖
    for (const DrawingView* view : sheet.views()) {
        if (!view) continue;
        auto it = results.find(view->id());
        if (it != results.end() && it.value().isValid) {
            paintView(painter, view, it.value());
        }
        // 若尚未渲染完成，視圖框由 DrawingSheetWidget 自行畫
    }

    // 3b. 剖切線 / 局部放大範圍（畫在宿主視圖上）
    paintViewAnnotations(painter, sheet, results);

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

// ── 封面 ──────────────────────────────────────────────────────────────────────

void DrawingSheetPainter::paintCover(QPainter& painter, const DrawingSheet& sheet)
{
    const QSizeF sz = sheet.config().paperSizeMM();
    const TitleBlock& tb = sheet.titleBlock();
    painter.save();
    painter.setPen(Qt::black);

    auto text = [&](double y, double h, const QString& t, double pt, bool bold) {
        QFont f(QStringLiteral("Arial"));
        f.setPointSizeF(pt);
        f.setBold(bold);
        painter.setFont(f);
        painter.drawText(QRectF(20, y, sz.width() - 40, h), Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, t);
    };

    const double cy = sz.height() * 0.30;
    if (!tb.companyName.isEmpty()) text(cy - 40, 12, tb.companyName, 6.0, false);
    text(cy, 30, tb.drawingTitle.isEmpty() ? QStringLiteral("Drawing Book") : tb.drawingTitle, 14.0, true);
    const QString sub = tb.customFields.value(QStringLiteral("subtitle"));
    if (!sub.isEmpty()) text(cy + 32, 12, sub, 6.0, false);
    if (!tb.projectName.isEmpty()) text(cy + 48, 12, tb.projectName, 7.0, false);

    painter.setPen(QPen(Qt::black, 0.5));
    painter.drawLine(QPointF(sz.width() * 0.25, cy + 66), QPointF(sz.width() * 0.75, cy + 66));

    // 簽署欄
    double y = sz.height() * 0.62;
    const struct { const char* label; QString v; } rows[] = {
        { "設計 Designed", tb.designedBy }, { "校核 Checked", tb.checkedBy },
        { "核定 Approved", tb.approvedBy }, { "日期 Date", tb.designDate },
        { "版次 Revision", tb.revision },
    };
    QFont f(QStringLiteral("Arial")); f.setPointSizeF(4.5);
    painter.setFont(f);
    const double x0 = sz.width() * 0.5 - 50;
    for (const auto& r : rows) {
        painter.drawText(QRectF(x0, y, 45, 9), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(r.label));
        painter.drawText(QRectF(x0 + 47, y, 55, 9), Qt::AlignLeft | Qt::AlignVCenter, r.v);
        painter.drawLine(QPointF(x0 + 47, y + 9), QPointF(x0 + 102, y + 9));
        y += 11;
    }
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
    // 直式標題欄：位於圖面右側，寬 60 mm、高度為整個內框；上方為公司與圖名，
    // 下方為由下往上堆疊的資料格（圖號、版次／頁次、簽署、日期…）。
    constexpr double kW = 60.0;
    constexpr double kInner = 10.0;

    const double x0 = sheetSize.width() - kInner - kW;
    const double y0 = kInner;
    const double kH = sheetSize.height() - 2 * kInner;

    QPen borderPen(Qt::black, 0.5);
    borderPen.setCosmetic(false);
    QPen thinPen(Qt::black, 0.25);
    thinPen.setCosmetic(false);

    painter.save();
    painter.setPen(borderPen);
    painter.drawRect(QRectF(x0, y0, kW, kH));

    // ── 上方：公司名稱、圖名 ──────────────────────────────────────────────
    const double hCompany = 22.0, hTitle = 34.0;
    painter.setPen(thinPen);
    painter.drawLine(QPointF(x0, y0 + hCompany), QPointF(x0 + kW, y0 + hCompany));
    painter.drawLine(QPointF(x0, y0 + hCompany + hTitle), QPointF(x0 + kW, y0 + hCompany + hTitle));

    painter.setPen(Qt::black);
    painter.setFont(QFont("Arial", 4.5, QFont::Bold));
    if (!tb.companyName.isEmpty())
        painter.drawText(QRectF(x0 + 2, y0 + 1, kW - 4, hCompany - 2),
                         Qt::AlignCenter | Qt::TextWordWrap, tb.companyName);
    painter.setFont(QFont("Arial", 5, QFont::Bold));
    painter.drawText(QRectF(x0 + 2, y0 + hCompany + 1, kW - 4, hTitle - 2),
                     Qt::AlignCenter | Qt::TextWordWrap, tb.drawingTitle);

    // ── 下方資料格（由下往上）─────────────────────────────────────────────
    auto cell = [&](double x, double y, double w, double h, const QString& label, const QString& value) {
        painter.setPen(Qt::darkGray);
        painter.setFont(QFont("Arial", 2.5));
        painter.drawText(QRectF(x + 1, y + 0.8, w - 2, h * 0.35), Qt::AlignLeft | Qt::AlignTop, label);
        painter.setPen(Qt::black);
        painter.setFont(QFont("Arial", 3.5));
        painter.drawText(QRectF(x + 1, y + h * 0.35, w - 2, h * 0.65),
                         Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, value);
    };

    struct Row { QString l1, v1, l2, v2; double h; };
    const QVector<Row> rows = {      // 由上而下的順序
        { "DWG NO.",   tb.drawingNumber, "",         "",              14 },
        { "REV",       tb.revision,      "SHEET",    tb.sheetNumber,  12 },
        { "DESIGNED",  tb.designedBy,    "",         "",              12 },
        { "CHECKED",   tb.checkedBy,     "",         "",              12 },
        { "APPROVED",  tb.approvedBy,    "",         "",              12 },
        { "DATE",      tb.designDate,    "PART NO",  tb.partNumber,   12 },
        { "MATERIAL",  tb.material,      "TOLERANCE",tb.tolerance,    12 },
        { "SCALE",     tb.projectName,   "",         "",              12 },
    };
    double total = 0;
    for (const Row& r : rows) total += r.h;
    double y = y0 + kH - total;
    if (y < y0 + hCompany + hTitle) y = y0 + hCompany + hTitle;   // 圖紙太矮時壓縮上方空白
    for (const Row& r : rows) {
        painter.setPen(thinPen);
        painter.drawLine(QPointF(x0, y), QPointF(x0 + kW, y));
        if (r.l2.isEmpty()) {
            cell(x0, y, kW, r.h, r.l1, r.v1);
        } else {
            painter.drawLine(QPointF(x0 + kW / 2, y), QPointF(x0 + kW / 2, y + r.h));
            cell(x0, y, kW / 2, r.h, r.l1, r.v1);
            cell(x0 + kW / 2, y, kW / 2, r.h, r.l2, r.v2);
        }
        y += r.h;
    }
    painter.restore();
}

// ── 視圖 ──────────────────────────────────────────────────────────────────────

void DrawingSheetPainter::paintView(QPainter& painter,
                                    const DrawingView* view,
                                    const ViewRenderResult& result)
{
    if (!view || !result.isValid) return;

    painter.save();
    painter.translate(view->position());     // position = 視圖中心

    const double sc = view->scale();

    // HLR 輸出在投影平面座標（Y 向上、原點為模型原點）。把投影範圍中心對到
    // 視圖中心：x' = (X - cx) * sc，y' = -(Y - cy) * sc（螢幕 Y 向下）。
    const QPointF c = result.projectedBounds.center();
    const QPointF fitOffset(-c.x() * sc, c.y() * sc);

    const bool isDetail = (view->type() == ViewType::Detail);
    const QSizeF sz = result.boundingBox * sc;   // 內容在圖紙上的大小（mm）

    // ── 內容（Detail 以放大圓裁切）──────────────────────────────────────────
    painter.save();
    if (isDetail) {
        QPainterPath clip;
        const double r = view->detailParams().radius * sc;
        clip.addEllipse(QPointF(0, 0), r, r);
        painter.setClipPath(clip, Qt::IntersectClip);
    }

    // 剖面填充線（先畫，讓輪廓線蓋在上面）
    if (view->type() == ViewType::Section && !result.sectionFaces.IsNull())
        paintHatching(painter, result.sectionFaces, fitOffset, sc);

    // 可見線（實線，中等線寬）
    QPen visPen(Qt::black, m_lwMedium);
    visPen.setCosmetic(false);
    visPen.setCapStyle(Qt::RoundCap);
    visPen.setJoinStyle(Qt::RoundJoin);
    paintEdges(painter, result.visibleEdges, visPen, fitOffset, sc);

    // 隱藏線（虛線，細線）
    if (view->showHiddenLines() && !result.hiddenEdges.IsNull()) {
        QPen hidPen(QColor(80, 80, 80), m_lwThin);
        hidPen.setCosmetic(false);
        hidPen.setStyle(Qt::DashLine);
        paintEdges(painter, result.hiddenEdges, hidPen, fitOffset, sc);
    }

    // 中心線（點劃線，細線，青色）
    if (view->showCenterLines() && !result.centerLines.IsNull()) {
        QPen clPen(QColor(0, 150, 150), m_lwThin);
        clPen.setCosmetic(false);
        clPen.setStyle(Qt::DashDotLine);
        paintEdges(painter, result.centerLines, clPen, fitOffset, sc);
    }
    painter.restore();   // 解除 Detail 裁切

    // Detail 的放大圓外框
    if (isDetail) {
        const double r = view->detailParams().radius * sc;
        QPen cp(Qt::black, m_lwMedium);
        cp.setCosmetic(false);
        painter.setPen(cp);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(0, 0), r, r);
    }

    // ── 外框尺寸（總寬／總高）：等角、局部放大不標（投影尺寸非真長）──────────
    if (view->showDimensions() && !isDetail && view->type() != ViewType::Isometric)
        paintOverallDimensions(painter, sz, result.boundingBox);

    // 視圖標籤（置中在視圖下方）
    if (view->showViewLabel()) {
        QString text = view->label();
        if (view->type() == ViewType::Section) {
            const QString l = view->sectionParams().sectionLabel.isEmpty()
                                  ? QStringLiteral("A") : view->sectionParams().sectionLabel;
            text = QString("SECTION %1-%1").arg(l);
        } else if (isDetail) {
            const QString l = view->detailParams().label.isEmpty()
                                  ? QStringLiteral("B") : view->detailParams().label;
            text = QString("DETAIL %1").arg(l);
        }
        painter.setFont(QFont("Arial", 3.5));
        painter.setPen(Qt::black);
        painter.drawText(
            QRectF(-sz.width() / 2, sz.height() / 2 + 2, sz.width(), 6),
            Qt::AlignCenter, text);
    }

    // 比例標籤
    if (view->showScaleLabel()) {
        QString scaleStr = (view->scale() >= 1.0)
            ? QString("SCALE %1:1").arg(view->scale(), 0, 'g', 4)
            : QString("SCALE 1:%1").arg(1.0 / view->scale(), 0, 'g', 4);
        painter.setFont(QFont("Arial", 2.5));
        painter.setPen(Qt::darkGray);
        painter.drawText(
            QRectF(-sz.width() / 2, sz.height() / 2 + 6, sz.width(), 4),
            Qt::AlignCenter, scaleStr);
    }

    painter.restore();
}

// ── 剖面填充線 ────────────────────────────────────────────────────────────────

void DrawingSheetPainter::paintHatching(QPainter& painter,
                                        const TopoDS_Compound& wires,
                                        const QPointF& offset,
                                        double scale)
{
    // 以所有 Wire 組成一個區域（奇偶填充 → 內孔自動不填），再用該區域裁切
    // 一組 45° 平行線。間距以圖紙 mm 計，所以與比例無關。
    QPainterPath region;
    region.setFillRule(Qt::OddEvenFill);

    for (TopExp_Explorer we(wires, TopAbs_WIRE); we.More(); we.Next()) {
        const TopoDS_Wire wire = TopoDS::Wire(we.Current());
        bool first = true;
        try {
            for (BRepTools_WireExplorer ex(wire); ex.More(); ex.Next()) {
                const TopoDS_Edge edge = ex.Current();
                BRepAdaptor_Curve curve(edge);
                GCPnts_TangentialDeflection disc(curve, qDegreesToRadians(1.0), 0.01);
                QVector<QPointF> pts;
                for (int i = 1; i <= disc.NbPoints(); ++i) {
                    const gp_Pnt p = disc.Value(i);
                    pts.append(QPointF(p.X() * scale + offset.x(), -p.Y() * scale + offset.y()));
                }
                if (pts.size() < 2) continue;
                if (edge.Orientation() == TopAbs_REVERSED)
                    std::reverse(pts.begin(), pts.end());
                for (int i = 0; i < pts.size(); ++i) {
                    if (first) { region.moveTo(pts[i]); first = false; }
                    else if (i > 0) region.lineTo(pts[i]);
                }
            }
        } catch (const Standard_Failure&) {
            continue;
        }
        if (!first) region.closeSubpath();
    }
    if (region.isEmpty()) return;

    const QRectF br = region.boundingRect();
    painter.save();
    painter.setClipPath(region, Qt::IntersectClip);
    QPen hp(QColor(110, 110, 110), m_lwThin);
    hp.setCosmetic(false);
    painter.setPen(hp);
    constexpr double kStep = 4.0;   // 水平間距 mm（垂直間距 ≈ 2.8mm）
    for (double t = br.left() - br.height(); t <= br.right(); t += kStep)
        painter.drawLine(QPointF(t, br.bottom()), QPointF(t + br.height(), br.top()));
    painter.restore();
}

// ── 外框尺寸 ──────────────────────────────────────────────────────────────────

namespace {

QString fmtDim(double v)
{
    QString s = QString::number(v, 'f', 2);
    if (s.contains('.')) {
        while (s.endsWith('0')) s.chop(1);
        if (s.endsWith('.')) s.chop(1);
    }
    return s;
}

void drawArrowHead(QPainter& p, const QPointF& tip, const QPointF& dir /*單位向量，指向 tip 外側*/)
{
    constexpr double L = 2.5, W = 0.8;
    const QPointF n(-dir.y(), dir.x());
    QPolygonF tri;
    tri << tip << (tip - dir * L + n * W) << (tip - dir * L - n * W);
    p.save();
    p.setBrush(Qt::black);
    p.drawPolygon(tri);
    p.restore();
}

} // namespace

void DrawingSheetPainter::paintOverallDimensions(QPainter& painter,
                                                 const QSizeF& sizeMM,
                                                 const QSizeF& modelSize)
{
    // 視圖內容矩形以 (0,0) 為中心。水平尺寸放在上方、垂直尺寸放在左方，
    // 避開下方的視圖標籤與比例標籤。
    const QRectF r(-sizeMM.width() / 2, -sizeMM.height() / 2, sizeMM.width(), sizeMM.height());
    if (r.width() < 4 || r.height() < 4) return;

    constexpr double kOff = 8.0, kExt = 2.0, kOver = 1.5;
    QPen pen(Qt::black, m_lwThin);
    pen.setCosmetic(false);
    painter.save();
    painter.setPen(pen);
    painter.setFont(QFont("Arial", 3));

    // 水平（總寬）
    {
        const double y = r.top() - kOff;
        painter.drawLine(QPointF(r.left(),  r.top() - kExt), QPointF(r.left(),  y - kOver));
        painter.drawLine(QPointF(r.right(), r.top() - kExt), QPointF(r.right(), y - kOver));
        painter.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        drawArrowHead(painter, QPointF(r.left(),  y), QPointF(-1, 0));
        drawArrowHead(painter, QPointF(r.right(), y), QPointF(1, 0));
        painter.drawText(QRectF(r.left(), y - 5, r.width(), 4.5), Qt::AlignCenter,
                         fmtDim(modelSize.width()));
    }
    // 垂直（總高）
    {
        const double x = r.left() - kOff;
        painter.drawLine(QPointF(r.left() - kExt, r.top()),    QPointF(x - kOver, r.top()));
        painter.drawLine(QPointF(r.left() - kExt, r.bottom()), QPointF(x - kOver, r.bottom()));
        painter.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        drawArrowHead(painter, QPointF(x, r.top()),    QPointF(0, -1));
        drawArrowHead(painter, QPointF(x, r.bottom()), QPointF(0, 1));
        painter.save();
        painter.translate(x - 1.0, r.center().y());
        painter.rotate(-90);
        painter.drawText(QRectF(-r.height() / 2, -4.5, r.height(), 4.5), Qt::AlignCenter,
                         fmtDim(modelSize.height()));
        painter.restore();
    }
    painter.restore();
}

// ── 剖切線與局部放大標記 ──────────────────────────────────────────────────────

void DrawingSheetPainter::paintViewAnnotations(QPainter& painter,
                                               const DrawingSheet& sheet,
                                               const QMap<QUuid, ViewRenderResult>& results)
{
    auto resultOf = [&](const DrawingView* v) -> const ViewRenderResult* {
        auto it = results.constFind(v->id());
        return (it != results.constEnd() && it.value().isValid) ? &it.value() : nullptr;
    };
    // 宿主視圖投影座標 (X,Y) → 圖紙座標
    auto toSheet = [](const DrawingView* v, const ViewRenderResult& r, double X, double Y) {
        const QPointF c = r.projectedBounds.center();
        const double sc = v->scale();
        return QPointF(v->position().x() + (X - c.x()) * sc,
                       v->position().y() - (Y - c.y()) * sc);
    };
    auto findHost = [&](std::initializer_list<ViewType> types, const ViewRenderResult*& hr)
        -> const DrawingView* {
        for (ViewType t : types)
            for (const DrawingView* v : sheet.views())
                if (v && v->type() == t && (hr = resultOf(v)))
                    return v;
        hr = nullptr;
        return nullptr;
    };

    painter.save();
    for (const DrawingView* v : sheet.views()) {
        if (!v) continue;

        if (v->type() == ViewType::Section) {
            const SectionParams p = v->sectionParams();
            if (!p.showCutLine || p.direction == SectionDirection::Diagonal) continue;

            const ViewRenderResult* hr = nullptr;
            const DrawingView* host = (p.direction == SectionDirection::Horizontal)
                ? findHost({ViewType::Top}, hr)
                : findHost({ViewType::Top, ViewType::Front}, hr);
            if (!host || !hr) continue;

            const QRectF b = hr->projectedBounds;
            const double ext = 0.08 * qMax(b.width(), b.height());
            QPointF a, e, dir;
            if (p.direction == SectionDirection::Horizontal) {      // 平面 Y=offset，俯視為水平線
                a = toSheet(host, *hr, b.left()  - ext, p.offset);
                e = toSheet(host, *hr, b.right() + ext, p.offset);
                dir = QPointF(0, -1);                               // 看向 -Y，觀看者在 +Y（俯視圖上方）
            } else {                                                // 平面 X=offset
                a = toSheet(host, *hr, p.offset, b.top()    + ext);
                e = toSheet(host, *hr, p.offset, b.bottom() - ext);
                dir = QPointF(1, 0);
            }
            const QString lbl = p.sectionLabel.isEmpty() ? QStringLiteral("A") : p.sectionLabel;

            // 中段：細點劃線
            QPen thin(Qt::black, m_lwThin);
            thin.setCosmetic(false);
            thin.setStyle(Qt::DashDotLine);
            painter.setPen(thin);
            painter.drawLine(a, e);

            // 兩端：粗短線 + 箭頭 + 代號
            QPointF u = e - a;
            const double len = std::hypot(u.x(), u.y());
            if (len < 1e-6) continue;
            u /= len;
            QPen thick(Qt::black, m_lwThick);
            thick.setCosmetic(false);
            painter.setFont(QFont("Arial", 4));
            for (int k = 0; k < 2; ++k) {
                const QPointF pt = (k == 0) ? a : e;
                const QPointF inward = (k == 0) ? u : -u;
                painter.setPen(thick);
                painter.drawLine(pt, pt + inward * qMin(6.0, len / 3));
                if (p.showArrows) {
                    const QPointF tip = pt + dir * 7.0;
                    painter.drawLine(pt, tip);
                    drawArrowHead(painter, tip, dir);
                    painter.setPen(Qt::black);
                    const QPointF lp = tip + dir * 3.5;
                    painter.drawText(QRectF(lp.x() - 3, lp.y() - 3, 6, 6), Qt::AlignCenter, lbl);
                } else {
                    painter.setPen(Qt::black);
                    const QPointF lp = pt - inward * 4.0;
                    painter.drawText(QRectF(lp.x() - 3, lp.y() - 3, 6, 6), Qt::AlignCenter, lbl);
                }
            }
        }
        else if (v->type() == ViewType::Detail) {
            const DetailParams p = v->detailParams();
            const ViewRenderResult* hr = nullptr;
            const DrawingView* host = findHost({p.sourceType}, hr);
            if (!host || !hr) continue;

            const QPointF c = toSheet(host, *hr, p.center.x(), p.center.y());
            const double r = p.radius * host->scale();
            QPen cp(Qt::black, m_lwThin);
            cp.setCosmetic(false);
            cp.setStyle(Qt::DashDotLine);
            painter.setPen(cp);
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(c, r, r);

            const QString lbl = p.label.isEmpty() ? QStringLiteral("B") : p.label;
            painter.setFont(QFont("Arial", 4));
            painter.setPen(Qt::black);
            const QPointF lp = c + QPointF(r * 0.72 + 3, -r * 0.72 - 3);
            painter.drawText(QRectF(lp.x() - 3, lp.y() - 3, 6, 6), Qt::AlignCenter, lbl);
        }
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

namespace {
constexpr double kIdxRowH = 8.0;
constexpr double kIdxHdrH = 10.0;
constexpr double kIdxWidth = 26 + 38 + 66 + 14 + 24 + 62;   // 與 paintDrawingIndex 的欄寬合計一致
}

int DrawingSheetPainter::drawingIndexRowAt(const DrawingIndex* idx, const QPointF& pt)
{
    if (!idx) return -1;
    const QPointF o = idx->position();
    if (pt.x() < o.x() || pt.x() > o.x() + kIdxWidth) return -1;
    const double y = pt.y() - (o.y() + kIdxHdrH);
    if (y < 0) return -1;
    const int row = int(y / kIdxRowH);
    return (row >= 0 && row < idx->entries().size()) ? row : -1;
}

void DrawingSheetPainter::paintDrawingIndex(QPainter& painter,
                                            const DrawingIndex* idx)
{
    if (!idx) return;

    const QPointF origin = idx->position();
    constexpr double kRowH = kIdxRowH;
    constexpr double kHdrH = kIdxHdrH;

    struct Col { QString label; double w; };
    const QVector<Col> cols = {
        {"SHEET", 26}, {"DWG NO.", 38}, {"TITLE", 66},
        {"REV", 14}, {"DATE", 24}, {"REMARK", 62}
    };
    double kW = 0;
    for (const Col& c : cols) kW += c.w;

    QPen borderPen(Qt::black, 0.35);
    borderPen.setCosmetic(false);

    // 在儲存格內畫字：太寬時水平壓縮，確保不被截掉
    auto cellText = [&](const QRectF& r, const QString& text, int align) {
        if (text.isEmpty()) return;
        const double avail = r.width() - 2.0;
        const QFontMetricsF fm(painter.font(), painter.device());
        const double w = fm.boundingRect(text).width();
        painter.save();
        if (w > avail && w > 0) {
            const double k = avail / w;
            painter.translate(r.left() + 1.0, r.top());
            painter.scale(k, 1.0);
            painter.drawText(QRectF(0, 0, w + 1.0, r.height()), align, text);
        } else {
            painter.drawText(QRectF(r.left() + 1.0, r.top(), avail, r.height()), align, text);
        }
        painter.restore();
    };

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
        cellText(QRectF(cx, y0 + 1, c.w, kHdrH - 2), c.label, Qt::AlignCenter);
        painter.setPen(borderPen);
        painter.drawLine(QPointF(cx + c.w, y0), QPointF(cx + c.w, y0 + kHdrH));
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

        const QStringList vals = { e.sheetNumber, e.drawingNumber, e.title,
                                   e.revision, e.date, e.remark };
        for (int i = 0; i < cols.size(); ++i) {
            const double cw = cols[i].w;
            cellText(QRectF(cx, y + 1, cw, kRowH - 2), i < vals.size() ? vals[i] : QString(),
                     Qt::AlignVCenter | Qt::AlignLeft);
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
