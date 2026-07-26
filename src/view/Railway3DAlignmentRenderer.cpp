/**
 * @file Railway3DAlignmentRenderer.cpp
 * @brief Railway 資料夾節點 eyeOpen 觸發的「全部線路 3D Alignment」疊加顯示。
 */

#include "Railway3DAlignmentRenderer.h"
#include "CadView.h"

#include "railway/RailwayAlignment.h"
#include "railway/RailwayAlignmentElement.h"

#include <BRepBuilderAPI_MakePolygon.hxx>
#include <gp_Pnt.hxx>
#include <Quantity_Color.hxx>
#include <AIS_InteractiveContext.hxx>

#include <QDebug>
#include <QVector3D>
#include <algorithm>
#include <cmath>

namespace aicad {
namespace view {

namespace {

// ── 取樣間距參數 ─────────────────────────────────────────────────────────────
constexpr double kMinStep       = 1.0;   ///< [m] 最密取樣間距（小半徑曲線）
constexpr double kMaxStep       = 20.0;  ///< [m] 最疏取樣間距（直線／緩坡）
constexpr double kRadiusDivisor = 25.0;  ///< 半徑 → 取樣間距的比例常數

/**
 * @brief 依平面／豎曲線半徑決定該區段的取樣間距。
 *
 * 取兩者中較小（曲率較大）的半徑決定間距；任一方向為直線（半徑 = +∞）
 * 且另一方向也是直線時，退化為 kMaxStep。
 */
double stepForRadius(double rH, double rV)
{
    const double r = std::min(std::fabs(rH), std::fabs(rV));
    if (!std::isfinite(r))
        return kMaxStep;
    return std::clamp(r / kRadiusDivisor, kMinStep, kMaxStep);
}

} // anonymous namespace

// ============================================================================
//  Ctor / dtor
// ============================================================================

Railway3DAlignmentRenderer::Railway3DAlignmentRenderer(CadView* cadView, QObject* parent)
    : QObject(parent)
    , m_cadView(cadView)
{
}

Railway3DAlignmentRenderer::~Railway3DAlignmentRenderer()
{
    clear();
}

// ============================================================================
//  showAll() / clear()
// ============================================================================

void Railway3DAlignmentRenderer::showAll(const QList<railway::TrackCenterLine*>& tcls)
{
    // 先取消舊訂閱、移除舊折線，避免重複訂閱或殘留物件。
    for (const QMetaObject::Connection& c : m_connections)
        QObject::disconnect(c);
    m_connections.clear();
    clearOverlaysOnly();

    m_tcls = tcls;
    if (!m_cadView) return;

    // 訂閱每條線路的 dataChanged()——水平／垂直線形改變時會轉發此訊號
    // （見 TrackCenterLine 建構子），觸發後只重繪折線，不重新訂閱。
    for (railway::TrackCenterLine* tcl : m_tcls) {
        if (!tcl) continue;
        m_connections.append(
            connect(tcl, &railway::TrackCenterLine::dataChanged,
                    this, &Railway3DAlignmentRenderer::rebuildOverlays));
    }

    m_visible = true;
    rebuildOverlays();
}

void Railway3DAlignmentRenderer::clear()
{
    for (const QMetaObject::Connection& c : m_connections)
        QObject::disconnect(c);
    m_connections.clear();
    m_tcls.clear();

    const bool wasVisible = m_visible;
    clearOverlaysOnly();

    if (wasVisible && m_cadView)
        m_cadView->refreshView();

    m_visible = false;
}

// ============================================================================
//  rebuildOverlays() / clearOverlaysOnly()
// ============================================================================

void Railway3DAlignmentRenderer::rebuildOverlays()
{
    clearOverlaysOnly();
    if (!m_cadView) return;

    for (railway::TrackCenterLine* tcl : m_tcls) {
        Handle(AIS_Shape) shape = build3DPolyline(tcl);
        if (shape.IsNull()) continue;

        m_cadView->addOverlayAIS(shape, {0});

        // 登錄反查資料（geomUuid = "railway3d:<tclId>"），供：
        //   1. selectedTcls()：以 IsSelected() 檢查目前選取集合；
        //   2. 點擊取得高程的互動命令（例如 V3D）：由 POINT_ACQUIRED payload
        //      的 geomUuid 反查點擊命中哪一條線路。
        m_cadView->registerSketchGeomAIS(shape, tcl->id(),
                                          QStringLiteral("railway3d:") + tcl->id(),
                                          -1);

        m_overlays.append(shape);
        m_overlayTcl.append(tcl);
    }

    m_cadView->refreshView();
}

void Railway3DAlignmentRenderer::clearOverlaysOnly()
{
    if (m_cadView) {
        for (const auto& obj : m_overlays) {
            m_cadView->unregisterSketchGeomAIS(obj);
            m_cadView->removeOverlayAIS(obj);
        }
    }
    m_overlays.clear();
    m_overlayTcl.clear();
}

// ============================================================================
//  selectedTcls()
// ============================================================================

QList<railway::TrackCenterLine*> Railway3DAlignmentRenderer::selectedTcls() const
{
    QList<railway::TrackCenterLine*> result;
    if (!m_cadView) return result;

    Handle(AIS_InteractiveContext) ctx = m_cadView->context();
    if (ctx.IsNull()) return result;

    const int n = std::min(m_overlays.size(), m_overlayTcl.size());
    for (int i = 0; i < n; ++i) {
        if (!m_overlayTcl[i]) continue;
        if (ctx->IsSelected(m_overlays[i]) && !result.contains(m_overlayTcl[i]))
            result.append(m_overlayTcl[i]);
    }
    return result;
}

// ============================================================================
//  build3DPolyline()
// ============================================================================

Handle(AIS_Shape) Railway3DAlignmentRenderer::build3DPolyline(
    const railway::TrackCenterLine* tcl) const
{
    if (!tcl) return {};

    const railway::HorizontalAlignment* ha = tcl->horizontal();
    if (!ha || ha->isEmpty()) return {};

    const auto& rawPts = ha->rawPoints();   // 關鍵點序列（TS/SC/CS/ST…），與線形資料表同一資料來源
    if (rawPts.size() < 2) return {};

    // ── 全域里程單調遞增掃描，不做任何自訂的「關鍵點↔元素」配對 ───────────────
    //
    // 前幾次修正都是自行比對 elems[]／rawPts[] 的索引，或直接呼叫特定元素的
    // worldXY()——這些都是本類別自己另外寫的配對邏輯，一旦配對在某處錯開
    // （例如 AlignmentElementFactory::create() 因故跳過、或 elems 與
    // rawPts 的索引沒有如預期一一對應），從那一點之後的所有取樣就會用錯
    // 元素，正好對應「中段、尾段才折返」的現象。
    //
    // 改成完全不碰元素索引：取樣範圍只用整條線路的頭尾里程（頭尾點的
    // chainage，與資料表一致），中間純粹以 p += step（step 恆為正值）
    // 單調前進，並改回呼叫 TrackCenterLine::getXY()/getZ()（內部走
    // HorizontalAlignment::elementAt()／VerticalAlignment 的標準查詢路徑，
    // 全專案到處都在用、經過驗證），不再自行配對或直接操作元素指標。
    // p 本身結構上不可能倒退，folding 只可能來自世界座標查詢本身，
    // 而非本類別的取樣邏輯。
    const double pStart = rawPts.first().chainage;
    const double pEnd   = rawPts.last().chainage;
    if (pEnd - pStart < 1e-6) return {};

    QVector<gp_Pnt> pts;
    pts.reserve(256);

    auto appendStation = [&](double p) {
        const QPointF xy = tcl->getXY(p, 0.0);
        const double  z  = tcl->getZ(p);
        const gp_Pnt  np(xy.x(), xy.y(), z);
        if (pts.isEmpty() || pts.last().Distance(np) > 1e-4)  // 濾除相鄰重合點
            pts.append(np);
    };

    double p = pStart;
    appendStation(p);
    while (p < pEnd - 1e-9) {
        const double rH   = tcl->getRadius(p, /*signed_=*/false);
        const double rV   = tcl->getVerticalRadius(p);
        const double step = stepForRadius(rH, rV);
        p = std::min(p + step, pEnd);
        appendStation(p);
    }

    if (pts.size() < 2) return {};

    // ── 依序連接 3D 座標點（E, N, Z）組成折線 ────────────────────────────────
    BRepBuilderAPI_MakePolygon poly;
    for (const gp_Pnt& p : pts)
        poly.Add(p);

    if (!poly.IsDone()) {
        qWarning() << "[Railway3DAlignmentRenderer] MakePolygon failed for TCL"
                   << tcl->id();
        return {};
    }

    Handle(AIS_Shape) shape = new AIS_Shape(poly.Shape());
    // 洋紅色：與水平編輯（藍/橙/綠）及 PI 標記（黃）區隔，一眼可辨識為 3D Alignment。
    shape->SetColor(Quantity_Color(0.85, 0.1, 0.75, Quantity_TOC_RGB));
    shape->SetWidth(2.5);
    shape->SetDisplayMode(0); // AIS_WireFrame — 純線形，不需著色面
    return shape;
}

} // namespace view
} // namespace aicad
