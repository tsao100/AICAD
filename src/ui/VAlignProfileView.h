/**
 * @file VAlignProfileView.h
 * @brief Custom widget that draws the vertical-alignment profile and the
 *        horizontal-alignment element strip at the bottom.
 *
 * Coordinate system
 * ─────────────────
 *   World:   chainage [m] on X axis, elevation [m] on Y axis.
 *   Widget:  standard pixel coordinates (Y grows downward).
 *
 * Regions
 * ───────
 *   Profile area:   [marginLeft, marginTop]  →  [W-marginRight, profileBottom]
 *   Strip area:     [marginLeft, stripTop]   →  [W-marginRight, H-marginBottom]
 *   Between them:   a thin divider band.
 *
 * Editing tools (set via setTool())
 * ──────────────────────────────────
 *   Select   – click/drag VIP diamonds
 *   AddVip   – click in profile area to insert a new VIP
 *   DelVip   – click near a VIP to delete it
 *
 * @author AICAD Team
 * @date   2025-01
 */
#pragma once

#include <QWidget>
#include <QVector>
#include <QPointF>
#include <QRectF>
#include <QFont>
#include <QColor>
#include <QPen>
#include "VAlignTheme.h"

namespace aicad {
namespace railway {
struct VerticalAlignmentPoint;
struct AlignmentPoint;
}

namespace ui {

// ─────────────────────────────────────────────────────────────────────────────
//  VIP data used internally by the view
// ─────────────────────────────────────────────────────────────────────────────

struct Vip {
    int    id  = 0;
    double ch  = 0.0;   ///< Chainage [m]
    double el  = 0.0;   ///< Elevation [m]
    double lvc = 0.0;   ///< Vertical curve length [m] (centred on VIP)
};

// ─────────────────────────────────────────────────────────────────────────────
//  Computed vertical-curve record (derived from Vip + neighbours)
// ─────────────────────────────────────────────────────────────────────────────

struct VcData {
    double s1       = 0.0;  ///< Grade in  [m/m]
    double s2       = 0.0;  ///< Grade out [m/m]
    double L        = 0.0;  ///< VC length [m]
    double csStart  = 0.0;  ///< Chainage at VC start
    double csEnd    = 0.0;  ///< Chainage at VC end
    double elStart  = 0.0;  ///< Elevation at VC start
    double K        = 0.0;  ///< Rate-of-grade-change (K = L / |Δg%|)
    bool   isSag    = true; ///< true=SAG, false=CREST
    int    vipIdx   = -1;   ///< Index of the VIP at the centre of this VC
};

// ─────────────────────────────────────────────────────────────────────────────
//  Horizontal element type (for the strip)
// ─────────────────────────────────────────────────────────────────────────────

enum class HElemType { Tangent, Circular, Spiral };

struct HElem {
    HElemType type     = HElemType::Tangent;
    double    ch0      = 0.0;
    double    ch1      = 0.0;
    double    radius   = 0.0;  ///< 0 on tangents
    QString   label;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Plan-deviation sample (for the trace line inside the strip)
// ─────────────────────────────────────────────────────────────────────────────

struct PlanPoint {
    double ch  = 0.0;   ///< Chainage [m]
    double dev = 0.0;   ///< Lateral deviation in the plan frame [m]
};

// ─────────────────────────────────────────────────────────────────────────────
//  View
// ─────────────────────────────────────────────────────────────────────────────

class VAlignProfileView : public QWidget
{
    Q_OBJECT

public:
    enum class Tool { Select, AddVip, DeleteVip };

    explicit VAlignProfileView(QWidget* parent = nullptr);
    ~VAlignProfileView() override = default;

    // ── Data loading ─────────────────────────────────────────────────────────

    /** Replace the VIP list.  Emits vipListChanged() after updating. */
    void setVips(const QVector<Vip>& vips);
    const QVector<Vip>& vips() const { return m_vips; }

    /** Set horizontal alignment elements for the strip. */
    void setHElements(const QVector<HElem>& elems);
    const QVector<HElem>& hElements() const { return m_hElems; }

    /** Set plan-deviation trace points for the strip. */
    void setPlanTrace(const QVector<PlanPoint>& pts);

    /** Set total alignment chainage length. */
    void setChainageEnd(double ch);

    /**
     * @brief Step 19 — 標示超限坡度區段（紅色半透明底色）。
     *
     * 由 VALIGNCHECKGRADE 命令呼叫；下次 update() 後繪製。
     * 清除違規記錄請傳入空向量。
     *
     * @param violations  每筆記錄含 {起始 chainage, 終止 chainage}
     * @param maxGrade    最大容許坡度（絕對值，m/m 單位，例如 0.035 = 3.5%）
     */
    void setGradeViolations(const QVector<QPair<double,double>>& violations,
                            double maxGrade);

    // ── Tool / display ───────────────────────────────────────────────────────

    void setTool(Tool t);
    Tool tool() const { return m_tool; }

    void setShowGrid (bool v);
    void setShowGrade(bool v);
    void setShowVC   (bool v);
    void setShowKVal (bool v);

    /** Apply one of the shared colour schemes. */
    void setColorScheme(aicad::ui::ColorScheme scheme);

    // ── Selection ────────────────────────────────────────────────────────────

    /** -1 = no selection. */
    void setSelectedVip(int index);
    int  selectedVip() const { return m_selIdx; }

Q_SIGNALS:
    void vipSelected(int index);
    void vipMoved(int index, double ch, double el);
    void vipAdded(double ch, double el);
    void vipDeleteRequested(int index);
    void vipListChanged();
    void cursorMoved(double ch, double el);

protected:
    void paintEvent       (QPaintEvent*)  override;
    void mousePressEvent  (QMouseEvent*)  override;
    void mouseMoveEvent   (QMouseEvent*)  override;
    void mouseReleaseEvent(QMouseEvent*)  override;
    void leaveEvent       (QEvent*)       override;
    void resizeEvent      (QResizeEvent*) override;

private:
    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr int kML = 70;    ///< Margin left (for EL axis labels)
    static constexpr int kMR = 14;    ///< Margin right
    static constexpr int kMT = 18;    ///< Margin top
    static constexpr int kMB = 4;     ///< Margin below profile (above strip)
    static constexpr int kSH = 116;   ///< Strip height
    static constexpr int kGap= 10;    ///< Gap between profile and strip
    static constexpr int kVipR = 7;   ///< Half-size of VIP diamond

    // ── Derived layout helpers ────────────────────────────────────────────────
    int profileBottom() const { return height() - kSH - kGap - kMB; }
    int stripTop()      const { return height() - kSH; }
    int stripCY()       const { return stripTop() + kSH / 2; }
    int profileH()      const { return profileBottom() - kMT; }
    int plotW()         const { return width() - kML - kMR; }

    QRectF profileRect() const {
        return { double(kML), double(kMT), double(plotW()), double(profileH()) };
    }
    QRectF stripRect() const {
        return { double(kML), double(stripTop()), double(plotW()), double(kSH) };
    }

    // ── Coordinate transforms ─────────────────────────────────────────────────
    double  tx(double ch) const;    ///< Chainage → pixel X
    double  ty(double el) const;    ///< Elevation → pixel Y
    double  fCh(double px) const;   ///< Pixel X → chainage
    double  fEl(double py) const;   ///< Pixel Y → elevation
    double  stripDevY(double dev) const; ///< Plan deviation → strip pixel Y

    bool inProfile(const QPointF& pt) const;

    // ── Vertical alignment maths ─────────────────────────────────────────────
    QVector<double>  computeGrades()  const;
    QVector<VcData>  computeAllVCs()  const;  ///< one entry per VIP; invalid if no VC
    VcData           computeVC(int vipIdx, const QVector<double>& gs) const;

    double elevAt(double ch, const QVector<double>& gs,
                  const QVector<VcData>& vcs) const;

    QVector<QPointF> buildProfilePoly(const QVector<double>& gs,
                                      const QVector<VcData>& vcs) const;

    QVector<QPointF> sampleVC(const VcData& vc, int n = 64) const;

    // ── Nearest VIP hit-test ──────────────────────────────────────────────────
    int nearestVip(const QPointF& pt, double thresh = 14.0) const;

    // ── Painting helpers ──────────────────────────────────────────────────────
    void drawBackground    (QPainter&) const;
    void drawGrid          (QPainter&, const QVector<double>& elTicks) const;
    void drawAxes          (QPainter&, const QVector<double>& elTicks) const;
    void drawGradeTangents (QPainter&, const QVector<double>& gs,
                           const QVector<VcData>& vcs) const;
    void drawProfileFill   (QPainter&, const QVector<QPointF>& poly) const;
    void drawProfileLine   (QPainter&, const QVector<QPointF>& poly) const;
    void drawVCArcs        (QPainter&, const QVector<VcData>& vcs) const;
    void drawVCAnnotations (QPainter&, const QVector<VcData>& vcs,
                           const QVector<double>& gs) const;
    void drawGradeLabels   (QPainter&, const QVector<double>& gs,
                         const QVector<VcData>& vcs) const;
    void drawKvalLabels    (QPainter&, const QVector<VcData>& vcs,
                        const QVector<double>& gs) const;
    void drawVipPoints     (QPainter&, const QVector<VcData>& vcs) const;
    void drawCursor        (QPainter&, const QVector<VcData>& vcs) const;

    // Strip drawing
    void drawStripBackground  (QPainter&) const;
    void drawStripElements    (QPainter&) const;
    void drawStripPlanTrace   (QPainter&) const;
    void drawStripVipTicks    (QPainter&) const;
    void drawStripCursorLine  (QPainter&) const;

    // Step 14：AddVip 預覽（Tool::AddVip 模式下，顯示虛線折線 + 坡度提示）
    void drawAddVipPreview    (QPainter& p,
                               const QVector<double>& gs,
                               const QVector<VcData>& vcs) const;

private:
    void drawGradeViolations(QPainter& p) const;

    // ── Utility ───────────────────────────────────────────────────────────────
    static QVector<double> niceTicks(double lo, double hi, double step);
    QColor elemColor(HElemType t, bool border) const;

    // ── State ─────────────────────────────────────────────────────────────────
    QVector<Vip>       m_vips;
    QVector<HElem>     m_hElems;
    QVector<PlanPoint> m_planPts;
    double             m_chEnd    = 850.0;

    Tool  m_tool      = Tool::Select;
    int   m_selIdx    = -1;   ///< Selected VIP index
    int   m_dragIdx   = -1;   ///< VIP being dragged (-1 = none)
    bool  m_showGrid  = true;
    bool  m_showGrade = true;
    bool  m_showVC    = true;
    bool  m_showKVal  = true;

    // Cursor (last mouse position in profile, chainage/elevation)
    bool   m_hasCursor = false;
    double m_curCh     = 0.0;
    double m_curEl     = 0.0;
    QPointF m_curPx;   ///< pixel position of cursor

    // Step 14：AddVip 預覽狀態
    bool   m_hasPreview  = false;
    double m_previewCh   = 0.0;
    double m_previewEl   = 0.0;

    // Step 19：Grade violation 超限標示
    QVector<QPair<double,double>> m_gradeViolations; ///< (ch_start, ch_end) pairs
    double m_violationMaxGrade = 0.0;

    // Elevation range (computed from VIPs + padding)
    double m_elBot = 8.0;
    double m_elTop = 18.0;

    void recomputeElevRange();

    // Plan trace max deviation (for strip scaling)
    double m_planMaxDev = 1.0;
    void   recomputePlanMaxDev();
};

} // namespace ui
} // namespace aicad