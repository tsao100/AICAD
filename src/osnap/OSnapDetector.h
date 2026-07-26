/**
 * @file OSnapDetector.h
 * @brief Object Snap 偵測核心引擎
 *
 * 架構說明：
 *  偵測管線（每次 mouse move 呼叫）：
 *   1. collectCandidateShapes()  → 從 AIS_InteractiveContext 取得附近 shape
 *   2. detectOnShape()          → 對每個 shape 執行各類型偵測
 *   3. filterAndSort()          → 依優先順序 + 螢幕距離排序
 *   4. 回傳最佳 SnapCandidate
 *
 *  偵測算法：
 *   Endpoint/Node : 遍歷所有 TopoDS_Vertex
 *   Midpoint      : BRep_Tool::Curve → 參數中點
 *   Center        : 辨識 Geom_Circle / Geom_Ellipse → 取圓心
 *   Quadrant      : Circle 0°/90°/180°/270°
 *   Intersection  : BRepExtrema_DistShapeShape 兩邊
 *   Perpendicular : GeomAPI_ProjectPointOnCurve → 垂足
 *   Tangent       : 從輸入參考點對圓求切點
 *   Nearest       : BRepExtrema_DistShapeShape point-to-edge
 *
 * @author AICAD Team
 * @date 2025-01-08
 */

#pragma once

#include "OSnapTypes.h"
#include "cad/Sketch.h"

#include <AIS_InteractiveContext.hxx>
#include <V3d_View.hxx>
#include <TopoDS_Shape.hxx>

#include <QObject>
#include <QVector>
#include <QVector3D>
#include <QVector2D>
#include <optional>

namespace aicad {
namespace cad  { class Plane; }
namespace railway { class AlignmentDocument; class HorizontalAlignment; }  // ← forward declarations
namespace osnap {

/**
 * @class OSnapDetector
 * @brief 純演算法類別，不持有 Qt parent，不含 UI 邏輯
 *
 * 使用方式：
 * @code
 *   OSnapDetector detector;
 *   detector.setSettings(settings);
 *   detector.setActivePlane(sketchPlane);  // 僅 2D 模式需要
 *
 *   // 在 mouse move handler 中：
 *   auto best = detector.detect(context, view, mouseX, mouseY);
 *   if (best.has_value()) {
 *       indicator->setCandidate(*best);
 *   }
 * @endcode
 */
class OSnapDetector
{
public:
    OSnapDetector();
    ~OSnapDetector() = default;

    // ── 設定 ──────────────────────────────────────────────────────────────────
    void setSettings(const OSnapSettings& s)   { m_settings = s; }
    const OSnapSettings& settings() const      { return m_settings; }
    OSnapSettings& settings()                  { return m_settings; }

    /// 設定草圖平面（2D 模式），nullptr 表示 3D 世界模式
    void setActivePlane(cad::Plane* plane)     { m_activePlane = plane; }
    cad::Plane* activePlane() const            { return m_activePlane; }

    void setActiveSketch(cad::Sketch* sketch) { m_activeSketch = sketch; }
    cad::Sketch* activeSketch() const         { return m_activeSketch; }

    /**
     * @brief 設定「目前作用中」的單一 AlignmentDocument（相容舊呼叫端）。
     *        內部等同 setAlignmentDocuments({doc})——會整組取代掉原本的清單，
     *        因此若場景中同時有其他可視的 AlignmentDocument 需要被 OSnap 到，
     *        請改呼叫下面的 setAlignmentDocuments()。
     * @param doc  可為 nullptr（停用 Alignment Snap）
     */
    void setAlignmentDocument(railway::AlignmentDocument* doc) {
        m_alignmentDocs.clear();
        if (doc) m_alignmentDocs.append(doc);
    }
    railway::AlignmentDocument* alignmentDocument() const {
        return m_alignmentDocs.isEmpty() ? nullptr : m_alignmentDocs.first();
    }

    /**
     * @brief 設定「所有目前可作為 Alignment Snap 來源」的 AlignmentDocument
     *        清單——不侷限於正在編輯的那一個。detectAlignmentSnap() 會逐一
     *        掃描清單中每個文件的 PI/TS/SC/CS/ST/中點/垂足，讓 FC/AS 等命令
     *        取點時，也能吃到「其他」alignment（例如同一份圖面上另一條
     *        TCL）上的鎖點，而不是只有目前正在編輯的那條。
     * @param docs 可包含重複／nullptr（內部會忽略 nullptr），傳空清單停用。
     */
    void setAlignmentDocuments(const QVector<railway::AlignmentDocument*>& docs) {
        m_alignmentDocs.clear();
        for (railway::AlignmentDocument* d : docs) {
            if (d && !m_alignmentDocs.contains(d)) m_alignmentDocs.append(d);
        }
    }
    const QVector<railway::AlignmentDocument*>& alignmentDocuments() const {
        return m_alignmentDocs;
    }

    /**
     * @brief 設定「所有目前可視」的 HorizontalAlignment（每條 TCL 實際渲染
     *        用的那份**持久化**資料——`AlignmentRenderer::setHorizontalAlignment(
     *        tcl->horizontal())`——而不是 AlignmentDocument 這個「編輯階段」
     *        的包裝物件）。
     *
     *        先前只把 AlignmentDocument 接進 OSnap，但 AlignmentDocument 只有
     *        「使用者曾經打開編輯」過的 TCL 才會建立（見 UIManager 的
     *        get-or-create 流程）；場景中其他只是單純顯示、從未被編輯過的
     *        TCL 完全沒有對應的 AlignmentDocument，因此永遠不在搜尋範圍內。
     *        這裡改用每條 TCL 一定都有的 tcl->horizontal()，讓 TC/中點/垂足
     *        這三種 snap 對「任何可視 alignment」都能生效，不受是否曾經
     *        打開編輯的限制。
     *
     *        AlignmentPI（PI 交點）因為需要 EditableElement 的 startPI/endPI，
     *        仍只能透過 setAlignmentDocuments() 對「目前有編輯階段」的
     *        alignment 提供。
     */
    void setHorizontalAlignments(const QVector<railway::HorizontalAlignment*>& haligns) {
        m_horizontalAlignments.clear();
        for (railway::HorizontalAlignment* h : haligns) {
            if (h && !m_horizontalAlignments.contains(h)) m_horizontalAlignments.append(h);
        }
    }
    const QVector<railway::HorizontalAlignment*>& horizontalAlignments() const {
        return m_horizontalAlignments;
    }

    /// 設定「上一個輸入點」（用於 Perpendicular / Tangent / Parallel 計算）
    void setLastInputPoint(const gp_Pnt& pt)   { m_lastInputPoint = pt; m_hasLastPoint = true; }
    void clearLastInputPoint()                 { m_hasLastPoint = false; }

    // ── 主偵測接口 ────────────────────────────────────────────────────────────
    /**
     * @brief 對螢幕座標 (mouseX, mouseY) 執行 snap 偵測
     * @return 最佳 SnapCandidate，若無則 nullopt
     */
    std::optional<SnapCandidate> detect(
        const Handle(AIS_InteractiveContext)& context,
        const Handle(V3d_View)&              view,
        int mouseX, int mouseY);

    /// 取得上次偵測的所有候選（除錯用）
    const QVector<SnapCandidate>& lastCandidates() const { return m_lastCandidates; }

    void addExcludedObject(const Handle(AIS_InteractiveObject)& obj);
    void removeExcludedObject(const Handle(AIS_InteractiveObject)& obj);
    void clearExcludedObjects(){ m_excludedObjects.clear(); }

    void setExcludedPoint(const gp_Pnt& pt, double tolerance = 0.5) {
        m_hasExcludedPoint  = true;
        m_excludedPoint     = pt;
        m_excludedPointTol  = tolerance;
    }
    void clearExcludedPoint() { m_hasExcludedPoint = false; }

private:
    // ── 內部偵測管線 ──────────────────────────────────────────────────────────
    void collectCandidateShapes(
        const Handle(AIS_InteractiveContext)& context,
        const Handle(V3d_View)& view,
        int mouseX, int mouseY,
        QVector<TopoDS_Shape>& shapes,
        QVector<Handle(AIS_InteractiveObject)>& aisObjects);

    void detectOnShape(
        const Handle(V3d_View)& view,
        const TopoDS_Shape& shape,
        const Handle(AIS_InteractiveObject)& aisObj,
        const gp_Pnt& mouseWorldPt,
        int mouseX, int mouseY,
        QVector<SnapCandidate>& candidates);

    // ── 各類型偵測子程式 ──────────────────────────────────────────────────────
    void detectEndpoints   (const TopoDS_Shape&, const Handle(AIS_InteractiveObject)&,
                         const gp_Pnt& mousePt, const Handle(V3d_View)&,
                         int mouseX, int mouseY,         // ✅ 新增
                         QVector<SnapCandidate>&);

    void detectMidpoints   (const TopoDS_Shape&, const Handle(AIS_InteractiveObject)&,
                         const gp_Pnt& mousePt, const Handle(V3d_View)&,
                         int mouseX, int mouseY,         // ✅ 新增
                         QVector<SnapCandidate>&);

    void detectCenters     (const TopoDS_Shape&, const Handle(AIS_InteractiveObject)&,
                       const gp_Pnt& mousePt, const Handle(V3d_View)&,
                       int mouseX, int mouseY,         // ✅ 新增
                       QVector<SnapCandidate>&);

    void detectQuadrants   (const TopoDS_Shape&, const Handle(AIS_InteractiveObject)&,
                         const gp_Pnt& mousePt, const Handle(V3d_View)&,
                         int mouseX, int mouseY,         // ✅ 新增
                         QVector<SnapCandidate>&);

    void detectIntersections(const QVector<TopoDS_Shape>& shapes,
                             const QVector<Handle(AIS_InteractiveObject)>& aisObjects,
                             const Handle(V3d_View)&,
                             const gp_Pnt& mousePt,
                             int mouseX, int mouseY,
                             QVector<SnapCandidate>&);

    void detectPerpendicular(const TopoDS_Shape&, const Handle(AIS_InteractiveObject)&,
                             const gp_Pnt& mousePt, const Handle(V3d_View)&,
                             int mouseX, int mouseY,         // ✅ 新增
                             QVector<SnapCandidate>&);

    void detectTangent     (const TopoDS_Shape&, const Handle(AIS_InteractiveObject)&,
                       const gp_Pnt& mousePt, const Handle(V3d_View)&,
                       int mouseX, int mouseY,         // ✅ 新增
                       QVector<SnapCandidate>&);

    void detectNearest     (const TopoDS_Shape&, const Handle(AIS_InteractiveObject)&,
                       const gp_Pnt& mousePt, const Handle(V3d_View)&,
                       int mouseX, int mouseY,
                       QVector<SnapCandidate>&);

    void detectGridPoint   (const Handle(V3d_View)&,
                         const gp_Pnt& mousePt,
                         int mouseX, int mouseY,
                         QVector<SnapCandidate>&);

    void detectExtension(const TopoDS_Shape&, const Handle(AIS_InteractiveObject)&,
                         const gp_Pnt& mousePt, const Handle(V3d_View)&,
                         int mouseX, int mouseY, QVector<SnapCandidate>&);

    void detectParallel(
        const TopoDS_Shape& shape,
        const Handle(AIS_InteractiveObject)& aisObj,
        const gp_Pnt& mousePt,
        const Handle(V3d_View)& view,
        int mouseX, int mouseY,
        QVector<SnapCandidate>& out);

    /**
     * @brief 對「所有」已設定的 AlignmentDocument（見 setAlignmentDocuments()）
     *        與「所有」已設定的 HorizontalAlignment（見
     *        setHorizontalAlignments()）逐一執行 Snap 偵測。
     *
     *  AlignmentPI（PI 交點，需要 EditableElement 的 startPI/endPI）只能對
     *  「有編輯階段」的 AlignmentDocument 偵測；AlignmentTC／Mid／Perp
     *  （皆只需 rawPoints()）則對「所有可視」的 HorizontalAlignment 偵測，
     *  不論該 TCL 是否曾經被打開編輯過。
     */
    void detectAlignmentSnap(
        const Handle(V3d_View)& view,
        const gp_Pnt& mousePt,
        int mouseX, int mouseY,
        QVector<SnapCandidate>& out);

    /// detectAlignmentSnap() 對單一 AlignmentDocument 的 PI 偵測（供上面迴圈呼叫）。
    void detectAlignmentPIForDoc(
        railway::AlignmentDocument* doc,
        const Handle(V3d_View)& view,
        int mouseX, int mouseY,
        QVector<SnapCandidate>& out);

    /**
     * @brief detectAlignmentSnap() 對單一 HorizontalAlignment 的
     *        TC／Mid／Perp 偵測（供上面迴圈呼叫）：
     *   1. AlignmentTC   — 從 rawPoints() 過濾 tsc 為 "TC"/"CS"/"ST"/"TS" 的切點
     *   2. AlignmentMid  — 每個元素起終點的中點
     *   3. AlignmentPerp — 游標至相鄰 rawPoints 連線段的最近垂足
     */
    void detectAlignmentSnapForHAlign(
        const railway::HorizontalAlignment* halign,
        const Handle(V3d_View)& view,
        const gp_Pnt& mousePt,
        int mouseX, int mouseY,
        QVector<SnapCandidate>& out);

    // ── 工具函數 ──────────────────────────────────────────────────────────────

    /// 世界座標 → 螢幕座標
    bool worldToScreen(const Handle(V3d_View)& view,
                       const gp_Pnt& worldPt,
                       double& screenX, double& screenY);

    /// 螢幕距離（像素）
    double screenDistance(const Handle(V3d_View)& view,
                          const gp_Pnt& pt,
                          int mouseX, int mouseY);

    /// 射線與平面交點（用於網格 snap）
    bool rayPlaneIntersect(const Handle(V3d_View)& view,
                           int mouseX, int mouseY,
                           const gp_Pln& plane,
                           gp_Pnt& result);

    /// 建立 SnapCandidate 輔助函數
    SnapCandidate makeCandidate(SnapType type,
                                const gp_Pnt& worldPt,
                                const Handle(AIS_InteractiveObject)& aisObj,
                                const Handle(V3d_View)& view,
                                int mouseX, int mouseY);

    /// 檢查候選點是否在平面上（2D 模式），若啟用則投影
    gp_Pnt projectToActivePlane(const gp_Pnt& pt);

    OSnapSettings              m_settings;
    cad::Plane*                m_activePlane  = nullptr;
    bool                       m_hasLastPoint = false;
    gp_Pnt                     m_lastInputPoint;
    QVector<SnapCandidate>     m_lastCandidates;
    cad::Sketch* m_activeSketch = nullptr;
    QVector<Handle(AIS_InteractiveObject)> m_excludedObjects;
    QVector<railway::AlignmentDocument*> m_alignmentDocs;  ///< 啟用 Alignment PI Snap 的所有編輯階段來源
    QVector<railway::HorizontalAlignment*> m_horizontalAlignments;  ///< 啟用 Alignment TC/Mid/Perp Snap 的所有可視來源（不限編輯階段）

    bool   m_hasExcludedPoint = false;
    gp_Pnt m_excludedPoint;
    double m_excludedPointTol = 0.5;

    static constexpr double    kMinEdgeLength = 1e-7;
};

} // namespace osnap
} // namespace aicad