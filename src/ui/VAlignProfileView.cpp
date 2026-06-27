/**
 * @file VAlignProfileView.cpp
 * @brief Vertical-alignment profile view implementation.
 * @author AICAD Team
 * @date   2025-01
 */

#include "VAlignProfileView.h"
#include "VAlignTheme.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QtMath>
#include <QFontDatabase>
#include <algorithm>
#include <cmath>
#include <limits>

namespace aicad {
namespace ui {

// ─────────────────────────────────────────────────────────────────────────────
//  Colour palette
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
//  Colour palette  (mutable — overwritten by setColorScheme)
// ─────────────────────────────────────────────────────────────────────────────
namespace Pal {
QColor BgProfile   ("#050c1c");
QColor BgStrip     ("#030610");
QColor BgWindow    ("#03050d");
QColor GridMajor   ("#0d1e34");
QColor GridMinor   ("#080f1c");
QColor AxisLabel   ("#1e3c5a");
QColor AxisTitle   ("#0e2440");
QColor BorderPane  ("#0c1c30");

QColor ProfileLine ("#1880f0");
QColor ProfileFill ("#1880f0");
QColor VCLine      ("#00dcc8");
QColor VCFill      ("#00dcc8");

QColor GradePos    ("#38d060");
QColor GradeNeg    ("#d84040");
QColor GradeZero   ("#4488a0");
QColor GradeDash   (14, 56, 96, 110);

QColor KvalLine    ("#5020b0");
QColor KvalText    ("#8840d8");

QColor VipFill     ("#030a18");
QColor VipBorder   ("#165080");
QColor VipSelFill  ("#1a0e00");
QColor VipSelBord  ("#ffbb20");
QColor VipSelDot   ("#ffcc40");
QColor VipDot      ("#1860a0");
QColor VipLabel    ("#1a5a78");
QColor VipLabelSel ("#ffcc40");
QColor VipDrop     ("#0c2040");
QColor VipDropSel  ("#ffaa00");

QColor CursorLine  ("#122a44");
QColor CursorRing  ("#1a5880");

QColor VcBoundary  ("#00ccb8");
QColor VcAnnText   ("#009888");

// Strip colours by element type
QColor TElemBg   ("#06101c");
QColor TElemBd   ("#14426c");
QColor TElemTx   ("#1c60c8");
QColor CElemBg   ("#03100f");
QColor CElemBd   ("#005e8a");
QColor CElemTx   ("#00a8cc");
QColor SElemBg   ("#0e0820");
QColor SElemBd   ("#3c1498");
QColor SElemTx   ("#6828d8");

QColor StripCL   ("#0c1c2c");
QColor PlanTrace ("#00c8de");
QColor PlanFill  ("#00c8de");
QColor VipTickN  ("#122840");
QColor VipTickS  ("#ffaa00");
QColor VipDotN   ("#1050a0");
QColor VipDotNBd ("#1870c0");
QColor Divider   ("#02040a");
QColor DivText   ("#0a1c2c");

// ── setPalette: map a Theme to every Pal entry ──────────────────────────────
void setPalette(const aicad::ui::Theme& th)
{
    const bool d = th.dark;

    // Backgrounds
    BgWindow  = th.bg;
    BgProfile = d ? th.bg.lighter(108) : th.bg.darker(103);
    BgStrip   = d ? th.bg.darker(120)  : th.bg.darker(106);

    // Grid / axes
    GridMajor  = th.border;
    GridMinor  = d ? th.border.darker(150) : th.border.lighter(115);
    AxisLabel  = th.textSub;
    AxisTitle  = th.textSub.darker(d ? 120 : 80);
    BorderPane = th.border;
    Divider    = d ? th.bg.darker(150) : th.border;
    DivText    = th.textSub;

    // Profile line + fill
    ProfileLine = th.accentLen.lighter(d ? 140 : 80);
    ProfileFill = ProfileLine;

    // Vertical curve
    VCLine     = th.accentVcType;
    VCFill     = th.accentVcType;
    VcBoundary = th.accentVcType;
    VcAnnText  = th.accentVcType.darker(d ? 110 : 130);

    // Grades
    GradePos  = th.accentGradePos;
    GradeNeg  = th.accentGradeNeg;
    GradeZero = th.accentGradeZero;
    GradeDash = QColor(th.accentGradeZero.red(),
                       th.accentGradeZero.green(),
                       th.accentGradeZero.blue(), 90);

    // K value
    KvalLine = th.accentKval.darker(d ? 110 : 130);
    KvalText = th.accentKval;

    // VIPs
    VipFill    = d ? th.bg.darker(130) : th.bgPanel;
    VipBorder  = th.accentLen;
    VipSelFill = d ? QColor("#1a0e00") : QColor("#fff8e0");
    VipSelBord = QColor("#ffbb20");
    VipSelDot  = QColor("#ffcc40");
    VipDot     = th.accentLen.lighter(d ? 130 : 80);
    VipLabel   = th.textSub;
    VipLabelSel= QColor("#ffcc40");
    VipDrop    = th.border;
    VipDropSel = QColor("#ffaa00");

    // Cursor
    CursorLine = th.border.lighter(d ? 140 : 80);
    CursorRing = th.accentLen;

    // Strip element types
    TElemBg = d ? th.bg.darker(115) : th.bgPanel.darker(103);
    TElemBd = th.accentHTangent.darker(d ? 110 : 130);
    TElemTx = th.accentHTangent;
    CElemBg = d ? th.bg.darker(118) : th.bgPanel.darker(103);
    CElemBd = th.accentHCircular.darker(d ? 110 : 130);
    CElemTx = th.accentHCircular;
    SElemBg = d ? th.bg.darker(120) : th.bgPanel.darker(103);
    SElemBd = th.accentHSpiral.darker(d ? 110 : 130);
    SElemTx = th.accentHSpiral;

    // Strip misc
    StripCL   = th.border;
    PlanTrace = th.accentVcType.lighter(d ? 110 : 90);
    PlanFill  = PlanTrace;
    VipTickN  = th.border.lighter(d ? 160 : 80);
    VipTickS  = QColor("#ffaa00");
    VipDotN   = th.accentLen;
    VipDotNBd = th.accentLen.lighter(d ? 130 : 85);
}
} // namespace Pal

// ─────────────────────────────────────────────────────────────────────────────
//  Construction
// ─────────────────────────────────────────────────────────────────────────────

VAlignProfileView::VAlignProfileView(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(500, 320);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);

    // Use a monospace font for all annotations
    QFont f("Courier New", 8);
    setFont(f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Public setters
// ─────────────────────────────────────────────────────────────────────────────

void VAlignProfileView::setVips(const QVector<Vip>& vips)
{
    m_vips = vips;
    // Ensure sorted by chainage
    std::sort(m_vips.begin(), m_vips.end(),
              [](const Vip& a, const Vip& b){ return a.ch < b.ch; });
    recomputeElevRange();
    Q_EMIT vipListChanged();
    update();
}

void VAlignProfileView::setHElements(const QVector<HElem>& elems)
{
    m_hElems = elems;
    update();
}

void VAlignProfileView::setPlanTrace(const QVector<PlanPoint>& pts)
{
    m_planPts = pts;
    recomputePlanMaxDev();
    update();
}

void VAlignProfileView::setChainageEnd(double ch)
{
    m_chEnd = ch;
    update();
}

void VAlignProfileView::setTool(Tool t)
{
    m_tool = t;
    switch (t) {
    case Tool::Select:    setCursor(Qt::ArrowCursor);      break;
    case Tool::AddVip:    setCursor(Qt::CrossCursor);      break;
    case Tool::DeleteVip: setCursor(Qt::ForbiddenCursor);  break;
    }
    update();
}

void VAlignProfileView::setShowGrid (bool v) { m_showGrid  = v; update(); }
void VAlignProfileView::setShowGrade(bool v) { m_showGrade = v; update(); }
void VAlignProfileView::setShowVC   (bool v) { m_showVC    = v; update(); }
void VAlignProfileView::setShowKVal (bool v) { m_showKVal  = v; update(); }

void VAlignProfileView::setColorScheme(aicad::ui::ColorScheme scheme)
{
    Pal::setPalette(aicad::ui::makeTheme(scheme));
    update();
}

void VAlignProfileView::setSelectedVip(int index)
{
    m_selIdx = index;
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Coordinate transforms
// ─────────────────────────────────────────────────────────────────────────────

double VAlignProfileView::tx(double ch) const
{
    return kML + (ch / m_chEnd) * plotW();
}

double VAlignProfileView::ty(double el) const
{
    if (std::abs(m_elTop - m_elBot) < 1e-9) return kMT;
    return kMT + (1.0 - (el - m_elBot) / (m_elTop - m_elBot)) * profileH();
}

double VAlignProfileView::fCh(double px) const
{
    return ((px - kML) / plotW()) * m_chEnd;
}

double VAlignProfileView::fEl(double py) const
{
    if (profileH() < 1) return m_elBot;
    return m_elBot + (1.0 - (py - kMT) / profileH()) * (m_elTop - m_elBot);
}

double VAlignProfileView::stripDevY(double dev) const
{
    double scale = (kSH / 2.0 - 20.0) / std::max(m_planMaxDev, 1.0);
    return stripCY() - dev * scale;
}

bool VAlignProfileView::inProfile(const QPointF& pt) const
{
    return profileRect().contains(pt);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Vertical alignment maths
// ─────────────────────────────────────────────────────────────────────────────

QVector<double> VAlignProfileView::computeGrades() const
{
    QVector<double> gs;
    for (int i = 0; i < m_vips.size() - 1; ++i) {
        double dCh = m_vips[i+1].ch - m_vips[i].ch;
        gs.append(dCh > 1e-9 ? (m_vips[i+1].el - m_vips[i].el) / dCh : 0.0);
    }
    return gs;
}

VcData VAlignProfileView::computeVC(int idx, const QVector<double>& gs) const
{
    VcData vc;
    vc.vipIdx = idx;
    const Vip& v = m_vips[idx];
    if (v.lvc <= 0.0 || idx < 1 || idx >= m_vips.size() - 1) return vc;

    vc.s1      = gs[idx - 1];
    vc.s2      = gs[idx];
    vc.L       = v.lvc;
    vc.csStart = v.ch - vc.L / 2.0;
    vc.csEnd   = v.ch + vc.L / 2.0;
    vc.elStart = v.el - vc.s1 * (vc.L / 2.0);
    double ds  = std::abs(vc.s2 - vc.s1);
    vc.K       = ds > 1e-10 ? vc.L / (ds * 100.0) : std::numeric_limits<double>::infinity();
    vc.isSag   = vc.s2 > vc.s1;
    return vc;
}

QVector<VcData> VAlignProfileView::computeAllVCs() const
{
    QVector<VcData> vcs(m_vips.size());
    if (m_vips.size() < 2) return vcs;
    auto gs = computeGrades();
    for (int i = 1; i < m_vips.size() - 1; ++i)
        vcs[i] = computeVC(i, gs);
    return vcs;
}

QVector<QPointF> VAlignProfileView::sampleVC(const VcData& vc, int n) const
{
    QVector<QPointF> pts;
    pts.reserve(n + 1);
    for (int k = 0; k <= n; ++k) {
        double x  = (double(k) / n) * vc.L;
        double el = vc.elStart + vc.s1 * x
                    - (vc.s1 - vc.s2) * x * x / (2.0 * vc.L);
        pts.append({ tx(vc.csStart + x), ty(el) });
    }
    return pts;
}

double VAlignProfileView::elevAt(double ch,
                                 const QVector<double>& gs,
                                 const QVector<VcData>& vcs) const
{
    // Check VCs first
    for (const auto& vc : vcs) {
        if (vc.L <= 0.0) continue;
        if (ch >= vc.csStart && ch <= vc.csEnd) {
            double x = ch - vc.csStart;
            return vc.elStart + vc.s1 * x
                   - (vc.s1 - vc.s2) * x * x / (2.0 * vc.L);
        }
    }
    // Tangent grade
    for (int i = 0; i < m_vips.size() - 1; ++i) {
        if (ch >= m_vips[i].ch && ch <= m_vips[i+1].ch)
            return m_vips[i].el + gs[i] * (ch - m_vips[i].ch);
    }
    return m_vips.isEmpty() ? 0.0 : m_vips.last().el;
}

/**
 * Build the complete profile as a polyline of pixel points.
 * Tangent segments are 2-point, VCs are 65-point curves.
 */
QVector<QPointF> VAlignProfileView::buildProfilePoly(const QVector<double>& /*gs*/,
                                                     const QVector<VcData>& vcs) const
{
    if (m_vips.size() < 2) return {};

    QVector<QPointF> pts;

    // Lambda: add point only if distinct from last
    auto addPt = [&](double ch, double el) {
        QPointF p(tx(ch), ty(el));
        if (pts.isEmpty() || (pts.last() - p).manhattanLength() > 0.5)
            pts.append(p);
    };

    // Helper: effective start ch/el of a VIP (accounting for VC on the left)
    auto vcEndCh  = [&](int i) { return vcs[i].L > 0 ? vcs[i].csEnd   : m_vips[i].ch; };
    auto vcEndEl  = [&](int i) {
        if (vcs[i].L <= 0) return m_vips[i].el;
        return vcs[i].elStart + vcs[i].s1 * vcs[i].L
               - (vcs[i].s1 - vcs[i].s2) * vcs[i].L * vcs[i].L / (2.0 * vcs[i].L);
    };
    auto vcStartCh = [&](int i) { return vcs[i].L > 0 ? vcs[i].csStart : m_vips[i].ch; };
    auto vcStartEl = [&](int i) { return vcs[i].L > 0 ? vcs[i].elStart : m_vips[i].el; };

    // First point
    addPt(m_vips[0].ch, m_vips[0].el);

    for (int i = 1; i < m_vips.size(); ++i) {
        // Tangent from previous VC end to this VC start
        double tCh1 = vcEndCh(i - 1),  tEl1 = vcEndEl(i - 1);
        double tCh2 = vcStartCh(i),    tEl2 = vcStartEl(i);
        if (tCh2 > tCh1 + 1e-6) {
            addPt(tCh1, tEl1);
            addPt(tCh2, tEl2);
        }
        // VC curve
        if (vcs[i].L > 0.0) {
            const auto vcPts = sampleVC(vcs[i], 64);
            for (const QPointF& p : vcPts)
                if (pts.isEmpty() || (pts.last() - p).manhattanLength() > 0.5)
                    pts.append(p);
        }
    }

    // Final VIP
    addPt(m_vips.last().ch, m_vips.last().el);

    return pts;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: nice grid ticks
// ─────────────────────────────────────────────────────────────────────────────

QVector<double> VAlignProfileView::niceTicks(double lo, double hi, double step)
{
    QVector<double> tks;
    double v = std::ceil(lo / step) * step;
    while (v <= hi + 1e-9) {
        tks.append(std::round(v / step) * step);   // avoid float drift
        v += step;
    }
    return tks;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Elevation range
// ─────────────────────────────────────────────────────────────────────────────

void VAlignProfileView::recomputeElevRange()
{
    if (m_vips.isEmpty()) { m_elBot = 0.0; m_elTop = 10.0; return; }

    double mn = m_vips[0].el, mx = m_vips[0].el;
    for (const auto& v : m_vips) { mn = std::min(mn, v.el); mx = std::max(mx, v.el); }

    double pad = std::max((mx - mn) * 0.38, 1.8);
    m_elBot = std::floor((mn - pad) * 4.0) / 4.0;
    m_elTop = std::ceil ((mx + pad) * 4.0) / 4.0;
}

void VAlignProfileView::recomputePlanMaxDev()
{
    m_planMaxDev = 1.0;
    for (const auto& p : m_planPts)
        m_planMaxDev = std::max(m_planMaxDev, std::abs(p.dev));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Nearest VIP hit-test
// ─────────────────────────────────────────────────────────────────────────────

int VAlignProfileView::nearestVip(const QPointF& pt, double thresh) const
{
    int best = -1;
    double bestD = thresh;
    for (int i = 0; i < m_vips.size(); ++i) {
        double d = QLineF(QPointF(tx(m_vips[i].ch), ty(m_vips[i].el)), pt).length();
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Mouse events
// ─────────────────────────────────────────────────────────────────────────────

void VAlignProfileView::mousePressEvent(QMouseEvent* e)
{
    QPointF pt = e->pos();
    if (!inProfile(pt)) return;

    double ch = std::max(0.0, std::min(m_chEnd, fCh(pt.x())));
    double el = fEl(pt.y());

    if (m_tool == Tool::AddVip) {
        Q_EMIT vipAdded(ch, el);

    } else if (m_tool == Tool::DeleteVip) {
        int idx = nearestVip(pt);
        if (idx >= 0 && m_vips.size() > 2)
            Q_EMIT vipDeleteRequested(idx);

    } else {
        // Select / drag
        int idx = nearestVip(pt);
        m_selIdx  = idx;
        m_dragIdx = idx;
        Q_EMIT vipSelected(idx);
        update();
    }
}

void VAlignProfileView::mouseMoveEvent(QMouseEvent* e)
{
    QPointF pt = e->pos();

    if (inProfile(pt)) {
        m_hasCursor = true;
        m_curCh  = std::max(0.0, std::min(m_chEnd, fCh(pt.x())));
        m_curEl  = fEl(pt.y());
        m_curPx  = pt;
        Q_EMIT cursorMoved(m_curCh, m_curEl);

        // Step 14：AddVip 預覽狀態更新
        if (m_tool == Tool::AddVip) {
            m_hasPreview = true;
            m_previewCh  = m_curCh;
            m_previewEl  = m_curEl;
        } else {
            m_hasPreview = false;
        }
    } else {
        m_hasCursor  = false;
        m_hasPreview = false;
    }

    // Drag VIP
    if (m_dragIdx >= 0 && m_dragIdx < m_vips.size()) {
        double ch = std::max(0.0, std::min(m_chEnd, fCh(pt.x())));
        double py = std::max(double(kMT), std::min(double(kMT + profileH()), pt.y()));
        double el = std::round(fEl(py) * 1000.0) / 1000.0;
        ch = std::round(ch * 10.0) / 10.0;

        m_vips[m_dragIdx].ch = ch;
        m_vips[m_dragIdx].el = el;

        // Re-sort and find new index
        int id = m_vips[m_dragIdx].id;
        std::stable_sort(m_vips.begin(), m_vips.end(),
                         [](const Vip& a, const Vip& b){ return a.ch < b.ch; });
        for (int i = 0; i < m_vips.size(); ++i) {
            if (m_vips[i].id == id) { m_dragIdx = i; m_selIdx = i; break; }
        }

        recomputeElevRange();
        Q_EMIT vipMoved(m_selIdx, ch, el);
        Q_EMIT vipListChanged();
        update();
    } else {
        update();   // for cursor crosshair repaint
    }
}

void VAlignProfileView::mouseReleaseEvent(QMouseEvent*)
{
    m_dragIdx = -1;
}

void VAlignProfileView::leaveEvent(QEvent*)
{
    m_hasCursor = false;
    m_dragIdx   = -1;
    update();
}

void VAlignProfileView::resizeEvent(QResizeEvent*)
{
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
//  paintEvent – top-level orchestrator
// ─────────────────────────────────────────────────────────────────────────────

void VAlignProfileView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // Pre-compute layout-dependent quantities
    const auto gs  = computeGrades();
    const auto vcs = computeAllVCs();

    // Elevation grid ticks
    const double elRange = m_elTop - m_elBot;
    const double elStep  = elRange > 12.0 ? 2.0 : elRange > 6.0 ? 1.0 : 0.5;
    const auto   elTicks = niceTicks(m_elBot, m_elTop, elStep);

    // Chainage grid ticks
    const double chStep = m_chEnd <= 500 ? 50.0 : 100.0;
    QVector<double> chTicks;
    for (double c = 0; c <= m_chEnd + 1e-6; c += chStep) chTicks.append(c);

    // Profile polyline
    const auto poly = buildProfilePoly(gs, vcs);

    // ── Draw layers (order matters) ──────────────────────────────────────────
    drawBackground(p);
    if (m_showGrid) drawGrid(p, elTicks);
    drawAxes(p, elTicks);
    if (m_showGrade) drawGradeTangents(p, gs, vcs);
    drawProfileFill(p, poly);
    if (m_showVC) drawVCArcs(p, vcs);       // VC fills under main line
    drawProfileLine(p, poly);
    if (m_showVC)    drawVCArcs(p, vcs);    // draw arcs again ON TOP with highlight
    if (m_showVC)    drawVCAnnotations(p, vcs, gs);
    if (m_showGrade) drawGradeLabels(p, gs, vcs);
    if (m_showKVal)  drawKvalLabels(p, vcs, gs);
    drawVipPoints(p, vcs);
    if (m_hasCursor) drawCursor(p, vcs);
    if (!m_gradeViolations.isEmpty()) drawGradeViolations(p);  // Step 19
    if (m_hasPreview && m_tool == Tool::AddVip)
        drawAddVipPreview(p, gs, vcs);    // Step 14

    // ── Chainage axis ────────────────────────────────────────────────────────
    {
        QFont f = font(); f.setPointSize(7); p.setFont(f);
        const int axY = kMT + profileH();
        QPen tickPen(Pal::GridMajor); tickPen.setWidthF(1.0);
        for (double c : chTicks) {
            int x = int(tx(c));
            p.setPen(tickPen);
            p.drawLine(x, axY, x, axY + 4);
            p.setPen(Pal::AxisLabel);
            p.drawText(QRect(x - 18, axY + 6, 36, 14), Qt::AlignHCenter, QString::number(int(c)));
        }
        // "CHAINAGE (m)" label
        QFont fT = font(); fT.setPointSize(7); fT.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        p.setFont(fT);
        p.setPen(Pal::AxisTitle);
        p.drawText(QRect(kML, axY + 22, plotW(), 14), Qt::AlignHCenter, "CHAINAGE (m)");
    }

    // ── Divider between profile and strip ────────────────────────────────────
    {
        p.fillRect(0, kMT + profileH() + 2, width(), kGap - 4, Pal::Divider);
        QFont f = font(); f.setPointSize(6); f.setLetterSpacing(QFont::AbsoluteSpacing, 2);
        p.setFont(f);
        p.setPen(Pal::DivText);
        p.drawText(QRect(kML, kMT + profileH() + 2, plotW(), kGap - 2),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   "── HORIZONTAL ALIGNMENT ──────────────────────────────────────────────────────");
    }

    // ── Strip ────────────────────────────────────────────────────────────────
    drawStripBackground(p);
    drawStripElements(p);
    drawStripPlanTrace(p);
    drawStripVipTicks(p);
    if (m_hasCursor) drawStripCursorLine(p);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Drawing helpers – profile
// ─────────────────────────────────────────────────────────────────────────────

void VAlignProfileView::drawBackground(QPainter& p) const
{
    p.fillRect(rect(), Pal::BgWindow);
    p.fillRect(profileRect().toRect(), Pal::BgProfile);
    p.setPen(QPen(Pal::BorderPane, 1));
    p.drawRect(profileRect().adjusted(0, 0, -1, -1));
}

void VAlignProfileView::drawGrid(QPainter& p, const QVector<double>& elTicks) const
{
    p.save();
    p.setClipRect(profileRect());
    const double chStep = m_chEnd <= 500 ? 50.0 : 100.0;
    for (double c = 0; c <= m_chEnd + 1e-6; c += chStep) {
        bool major = (int(std::round(c)) % 200 == 0);
        p.setPen(QPen(major ? Pal::GridMajor : Pal::GridMinor, major ? 1.0 : 0.5));
        int x = int(tx(c));
        p.drawLine(x, kMT, x, kMT + profileH());
    }
    p.setPen(QPen(Pal::GridMajor, 0.5));
    for (double e : elTicks) {
        int y = int(ty(e));
        p.drawLine(kML, y, kML + plotW(), y);
    }
    p.restore();
}

void VAlignProfileView::drawAxes(QPainter& p, const QVector<double>& elTicks) const
{
    QFont f = font(); f.setPointSize(7); p.setFont(f);
    p.setPen(Pal::AxisLabel);

    // Elevation tick labels
    for (double e : elTicks) {
        int y = int(ty(e));
        p.drawText(QRect(0, y - 6, kML - 5, 14), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(e, 'f', 1));
    }

    // "ELEVATION (m)" rotated label
    p.save();
    QFont fT = font(); fT.setPointSize(7); fT.setLetterSpacing(QFont::AbsoluteSpacing, 1);
    p.setFont(fT);
    p.setPen(Pal::AxisTitle);
    int cx = kML - 46, cy = kMT + profileH() / 2;
    p.translate(cx, cy);
    p.rotate(-90);
    p.drawText(QRect(-60, -7, 120, 14), Qt::AlignHCenter, "ELEVATION (m)");
    p.restore();
}

void VAlignProfileView::drawGradeTangents(QPainter& p,
                                          const QVector<double>& gs,
                                          const QVector<VcData>& vcs) const
{
    if (m_vips.size() < 2) return;
    p.save();
    p.setClipRect(profileRect());

    QPen pen(Pal::GradeDash, 1.0, Qt::DashLine);
    pen.setDashPattern({5, 4});

    for (int i = 0; i < m_vips.size() - 1; ++i) {
        double x1 = vcs[i].L   > 0 ? vcs[i].csEnd   : m_vips[i].ch;
        double x2 = vcs[i+1].L > 0 ? vcs[i+1].csStart : m_vips[i+1].ch;
        if (x2 <= x1) continue;
        double y1 = elevAt(x1, gs, vcs);
        double y2 = elevAt(x2, gs, vcs);
        p.setPen(pen);
        p.drawLine(QPointF(tx(x1), ty(y1)), QPointF(tx(x2), ty(y2)));
    }
    p.restore();
}

void VAlignProfileView::drawProfileFill(QPainter& p, const QVector<QPointF>& poly) const
{
    if (poly.size() < 2) return;
    p.save();
    p.setClipRect(profileRect());

    QPainterPath path;
    path.moveTo(poly.first());
    for (int i = 1; i < poly.size(); ++i) path.lineTo(poly[i]);

    double bottom = kMT + profileH();
    path.lineTo(poly.last().x(), bottom);
    path.lineTo(poly.first().x(), bottom);
    path.closeSubpath();

    QColor fill(Pal::ProfileFill);
    fill.setAlpha(22);
    p.fillPath(path, fill);
    p.restore();
}

void VAlignProfileView::drawProfileLine(QPainter& p, const QVector<QPointF>& poly) const
{
    if (poly.size() < 2) return;
    p.save();
    p.setClipRect(profileRect());
    p.setPen(QPen(Pal::ProfileLine, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(poly.constData(), poly.size());
    p.restore();
}

void VAlignProfileView::drawVCArcs(QPainter& p, const QVector<VcData>& vcs) const
{
    p.save();
    p.setClipRect(profileRect());

    for (const VcData& vc : vcs) {
        if (vc.L <= 0.0) continue;

        // VC fill
        auto vcPts = sampleVC(vc, 64);
        QPainterPath fillPath;
        fillPath.moveTo(vcPts.first());
        for (int k = 1; k < vcPts.size(); ++k) fillPath.lineTo(vcPts[k]);
        fillPath.lineTo(vcPts.last().x(), kMT + profileH());
        fillPath.lineTo(vcPts.first().x(), kMT + profileH());
        fillPath.closeSubpath();
        QColor fill(Pal::VCFill); fill.setAlpha(18);
        p.fillPath(fillPath, fill);

        // VC boundary dashes
        QPen bp(Pal::VcBoundary, 0.4, Qt::DashLine);
        bp.setDashPattern({3, 5}); bp.setColor(QColor(0, 204, 184, 80));
        p.setPen(bp);
        p.drawLine(QPointF(tx(vc.csStart), kMT),
                   QPointF(tx(vc.csStart), kMT + profileH()));
        p.drawLine(QPointF(tx(vc.csEnd), kMT),
                   QPointF(tx(vc.csEnd), kMT + profileH()));

        // VC arc line
        p.setPen(QPen(Pal::VCLine, 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawPolyline(vcPts.constData(), vcPts.size());
    }
    p.restore();
}

void VAlignProfileView::drawVCAnnotations(QPainter& p,
                                          const QVector<VcData>& vcs,
                                          const QVector<double>& /*gs*/) const
{
    p.save();
    p.setClipRect(profileRect());
    QFont f = font(); f.setPointSize(7); p.setFont(f);

    for (const VcData& vc : vcs) {
        if (vc.L <= 0.0) continue;

        // Span arrow + type label at top of profile area
        double midX = tx(vc.csStart + vc.L / 2.0);
        p.setPen(QColor(0, 204, 184, 110));
        p.drawLine(QPointF(tx(vc.csStart) + 4, kMT + 7),
                   QPointF(tx(vc.csEnd) - 4,   kMT + 7));

        const QString lbl = QString("%1  L=%2 m")
                                .arg(vc.isSag ? "SAG" : "CREST")
                                .arg(vc.L, 0, 'f', 0);
        p.setPen(Pal::VcAnnText);
        p.drawText(QRect(int(midX) - 55, kMT + 2, 110, 12),
                   Qt::AlignHCenter, lbl);
    }
    p.restore();
}

void VAlignProfileView::drawGradeLabels(QPainter& p,
                                        const QVector<double>& gs,
                                        const QVector<VcData>& vcs) const
{
    p.save();
    p.setClipRect(profileRect());

    QFont f = font(); f.setPointSize(9); f.setBold(true); p.setFont(f);

    for (int i = 0; i < gs.size(); ++i) {
        double x1 = vcs[i].L   > 0 ? vcs[i].csEnd   : m_vips[i].ch;
        double x2 = vcs[i+1].L > 0 ? vcs[i+1].csStart : m_vips[i+1].ch;
        if (x2 <= x1 + 1.0) continue;

        double midCh = (x1 + x2) / 2.0;
        double midEl = elevAt(midCh, gs, vcs);
        int mx = int(tx(midCh));
        int my = int(ty(midEl)) - 16;

        const double gPct = gs[i] * 100.0;
        QString txt = (gPct >= 0 ? "+" : "") + QString::number(gPct, 'f', 3) + "%";
        QColor col = gs[i] > 0.0  ? Pal::GradePos
                     : gs[i] < 0.0  ? Pal::GradeNeg
                                   :                 Pal::GradeZero;

        // Badge background
        QRectF badge(mx - 23, my - 10, 50, 14);
        p.setPen(QPen(col, 0.6));
        p.setBrush(QColor(3, 8, 18, 220));
        p.drawRoundedRect(badge, 2, 2);

        // Text
        p.setPen(col);
        p.setBrush(Qt::NoBrush);
        p.drawText(badge, Qt::AlignCenter, txt);
    }
    p.restore();
}

void VAlignProfileView::drawKvalLabels(QPainter& p,
                                       const QVector<VcData>& vcs,
                                       const QVector<double>& gs) const
{
    p.save();
    p.setClipRect(profileRect());
    QFont f = font(); f.setPointSize(8); p.setFont(f);

    for (const VcData& vc : vcs) {
        if (vc.L <= 0.0) continue;
        double midCh = vc.csStart + vc.L / 2.0;
        double midEl = elevAt(midCh, gs, vcs);
        int mx = int(tx(midCh));
        int my = int(ty(midEl)) + 19;

        QString kTxt = std::isinf(vc.K)
                           ? "K = ∞"
                           : "K = " + QString::number(vc.K, 'f', 0);

        QRectF badge(mx - 27, my - 9, 54, 14);
        p.setPen(QPen(Pal::KvalLine, 0.6));
        p.setBrush(QColor(3, 8, 18, 210));
        p.drawRoundedRect(badge, 2, 2);

        p.setPen(Pal::KvalText);
        p.setBrush(Qt::NoBrush);
        p.drawText(badge, Qt::AlignCenter, kTxt);
    }
    p.restore();
}

void VAlignProfileView::drawVipPoints(QPainter& p, const QVector<VcData>& vcs) const
{
    p.save();
    p.setClipRect(profileRect());
    QFont f = font(); f.setPointSize(7); p.setFont(f);

    for (int i = 0; i < m_vips.size(); ++i) {
        const Vip& v = m_vips[i];
        const bool  sel = (i == m_selIdx);
        const int   cx = int(tx(v.ch));
        const int   cy = int(ty(v.el));

        // Drop line to baseline
        p.setPen(QPen(sel ? Pal::VipDropSel : Pal::VipDrop, sel ? 1.0 : 0.6,
                      Qt::DashLine));
        QVector<qreal> dash = {3, 5};
        p.setPen([&]{
            QPen pen(sel ? Pal::VipDropSel : Pal::VipDrop, sel ? 1.0 : 0.6, Qt::CustomDashLine);
            pen.setDashPattern(dash); return pen;
        }());
        p.drawLine(cx, cy + 7, cx, kMT + profileH());

        // Selection halo
        if (sel) {
            QColor halo(Pal::VipSelBord); halo.setAlpha(16);
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            p.drawEllipse(QPointF(cx, cy), 14, 14);
        }

        // Diamond shape
        const QPolygonF diamond = QVector<QPointF>{
            QPointF(cx,         cy - kVipR),
            QPointF(cx + kVipR, cy),
            QPointF(cx,         cy + kVipR),
            QPointF(cx - kVipR, cy),
        };
        p.setPen(QPen(sel ? Pal::VipSelBord : Pal::VipBorder, sel ? 1.8 : 1.2));
        p.setBrush(sel ? Pal::VipSelFill : Pal::VipFill);
        p.drawPolygon(diamond);

        // Centre dot
        p.setPen(Qt::NoPen);
        p.setBrush(sel ? Pal::VipSelDot : Pal::VipDot);
        p.drawEllipse(QPointF(cx, cy), 2.5, 2.5);

        // Coordinate label
        const QString lbl = QString("%1 m · %2 m")
                                .arg(v.ch, 0, 'f', 0)
                                .arg(v.el, 0, 'f', 3);
        const QRect lblRect(cx + 9, cy - 17, 76, 13);
        p.setPen(QPen(sel ? Pal::VipSelBord : Pal::VipBorder, 0.6));
        p.setBrush(QColor(2, 5, 9, 238));
        p.drawRoundedRect(lblRect, 2, 2);
        p.setPen(sel ? Pal::VipLabelSel : Pal::VipLabel);
        p.setBrush(Qt::NoBrush);
        p.drawText(lblRect, Qt::AlignCenter, lbl);

        // LVC sub-label below baseline
        if (m_showVC && vcs[i].L > 0.0) {
            p.setPen(QColor(0, 120, 112, 180));
            p.drawText(QRect(cx - 20, kMT + profileH() - 16, 40, 12),
                       Qt::AlignHCenter,
                       QString("L=%1").arg(vcs[i].L, 0, 'f', 0));
        }
    }
    p.restore();
}

void VAlignProfileView::drawCursor(QPainter& p, const QVector<VcData>& vcs) const
{
    p.save();
    p.setClipRect(profileRect());

    QPen hairPen(Pal::CursorLine, 0.5, Qt::DashLine);
    hairPen.setDashPattern({2, 4});
    p.setPen(hairPen);
    p.drawLine(QPointF(m_curPx.x(), kMT), QPointF(m_curPx.x(), kMT + profileH()));
    p.drawLine(QPointF(kML, m_curPx.y()), QPointF(kML + plotW(), m_curPx.y()));

    // Ring at intersection
    p.setPen(QPen(Pal::CursorRing, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(m_curPx, 2.8, 2.8);

    // VC type tooltip
    for (const VcData& vc : vcs) {
        if (vc.L > 0 && m_curCh >= vc.csStart && m_curCh <= vc.csEnd) {
            p.setPen(Pal::VcAnnText);
            QFont f = font(); f.setPointSize(8); p.setFont(f);
            p.drawText(int(m_curPx.x()) + 7, int(m_curPx.y()) - 5,
                       vc.isSag ? "SAG" : "CREST");
            break;
        }
    }
    p.restore();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Step 14 — AddVip 預覽
//
//  在 Tool::AddVip 模式下，游標移動時顯示：
//   1. 插入新 VIP 後的折線（前一 VIP → 游標 → 後一 VIP）：橙色虛線
//   2. 兩段的 Δg%（坡度差）標注，以及預估 K 值
// ─────────────────────────────────────────────────────────────────────────────

void VAlignProfileView::drawAddVipPreview(QPainter& p,
                                          const QVector<double>& gs,
                                          const QVector<VcData>& /*vcs*/) const
{
    if (!m_hasPreview || m_vips.size() < 2) return;

    p.save();
    p.setClipRect(profileRect());

    // ── 找出游標所在的前後 VIP ───────────────────────────────────────────────
    int idxBefore = -1;  // 最後一個 ch <= m_previewCh 的 VIP
    int idxAfter  = -1;  // 第一個 ch >  m_previewCh 的 VIP

    for (int i = 0; i < m_vips.size(); ++i) {
        if (m_vips[i].ch <= m_previewCh) idxBefore = i;
        else if (idxAfter < 0)           idxAfter  = i;
    }

    // 若游標在所有 VIP 之前或之後，不顯示預覽
    if (idxBefore < 0 || idxAfter < 0) { p.restore(); return; }

    const Vip& vBefore = m_vips[idxBefore];
    const Vip& vAfter  = m_vips[idxAfter];

    // ── 計算插入後的三段坡度 ─────────────────────────────────────────────────
    const double dch_in  = m_previewCh - vBefore.ch;
    const double dch_out = vAfter.ch   - m_previewCh;

    if (dch_in < 1e-3 || dch_out < 1e-3) { p.restore(); return; }

    const double g_in  = (m_previewEl - vBefore.el) / dch_in  * 100.0;  // %
    const double g_out = (vAfter.el   - m_previewEl) / dch_out * 100.0;  // %
    const double deltaG = std::abs(g_out - g_in);

    // 原來坡度（無新 VIP）
    const double g_orig = (idxBefore < gs.size()) ? gs[idxBefore] * 100.0 : 0.0;
    Q_UNUSED(g_orig);

    // ── 取 3 個節點的像素座標 ────────────────────────────────────────────────
    const QPointF pxBefore(tx(vBefore.ch), ty(vBefore.el));
    const QPointF pxCursor(tx(m_previewCh), ty(m_previewEl));
    const QPointF pxAfter (tx(vAfter.ch),  ty(vAfter.el));

    // ── 繪製橙色虛線折線 ─────────────────────────────────────────────────────
    QPen dashPen(QColor(255, 160, 32), 1.5, Qt::DashLine);
    dashPen.setDashPattern({4, 3});
    p.setPen(dashPen);
    p.drawLine(pxBefore, pxCursor);
    p.drawLine(pxCursor, pxAfter);

    // ── 游標點：橙色菱形 ─────────────────────────────────────────────────────
    p.setPen(QPen(QColor(255, 130, 0), 1.5));
    p.setBrush(QColor(255, 160, 32, 160));
    static const double kR = 5.0;
    QPolygonF diamond;
    diamond << QPointF(pxCursor.x(),      pxCursor.y() - kR)
            << QPointF(pxCursor.x() + kR, pxCursor.y())
            << QPointF(pxCursor.x(),      pxCursor.y() + kR)
            << QPointF(pxCursor.x() - kR, pxCursor.y());
    p.drawPolygon(diamond);

    // ── 標注文字 ─────────────────────────────────────────────────────────────
    QFont f = font();
    f.setPointSize(8);
    p.setFont(f);

    auto fmtGrade = [](double g) -> QString {
        return QString("%1%2%")
            .arg(g >= 0 ? "+" : "")
            .arg(g, 0, 'f', 3);
    };

    // 入坡標注（折線中點）
    QPointF midIn ((pxBefore.x() + pxCursor.x()) / 2.0,
                   (pxBefore.y() + pxCursor.y()) / 2.0);
    p.setPen(QColor(255, 200, 80));
    p.drawText(midIn + QPointF(4, -4), fmtGrade(g_in));

    // 出坡標注
    QPointF midOut((pxCursor.x() + pxAfter.x()) / 2.0,
                   (pxCursor.y() + pxAfter.y()) / 2.0);
    p.drawText(midOut + QPointF(4, -4), fmtGrade(g_out));

    // 游標旁：Δg 與 K 值
    const QString kStr = (deltaG > 1e-6)
        ? QString("Δg=%1%  K≈%2")
              .arg(deltaG, 0, 'f', 2)
              .arg(0.0)   // K 在無 LVC 時不顯示數值
        : QString("Δg=%1%  ─").arg(deltaG, 0, 'f', 2);

    p.setPen(QColor(255, 220, 120));
    QFont fInfo = f;
    fInfo.setPointSize(7);
    p.setFont(fInfo);
    p.drawText(int(pxCursor.x()) + 10, int(pxCursor.y()) + 14, kStr);

    p.restore();
}


QColor VAlignProfileView::elemColor(HElemType t, bool border) const
{
    switch (t) {
    case HElemType::Tangent:  return border ? Pal::TElemBd : Pal::TElemBg;
    case HElemType::Circular: return border ? Pal::CElemBd : Pal::CElemBg;
    case HElemType::Spiral:   return border ? Pal::SElemBd : Pal::SElemBg;
    }
    return Qt::black;
}

static QColor elemTextColor(HElemType t)
{
    switch (t) {
    case HElemType::Tangent:  return Pal::TElemTx;
    case HElemType::Circular: return Pal::CElemTx;
    case HElemType::Spiral:   return Pal::SElemTx;
    }
    return Pal::TElemTx;
}

void VAlignProfileView::drawStripBackground(QPainter& p) const
{
    p.fillRect(kML, stripTop(), plotW(), kSH, Pal::BgStrip);
    p.setPen(QPen(Pal::BorderPane, 1));
    p.drawRect(kML, stripTop(), plotW() - 1, kSH - 1);

    // Centreline dashes
    QPen clPen(Pal::StripCL, 0.5, Qt::DashLine);
    clPen.setDashPattern({4, 5});
    p.setPen(clPen);
    p.drawLine(kML, stripCY(), kML + plotW(), stripCY());

    // Y-axis label (rotated)
    p.save();
    QFont f = font(); f.setPointSize(6); f.setLetterSpacing(QFont::AbsoluteSpacing, 1);
    p.setFont(f);
    p.setPen(Pal::AxisTitle);
    int lx = kML - 28, ly = stripCY();
    p.translate(lx, ly);
    p.rotate(-90);
    p.drawText(QRect(-40, -7, 80, 14), Qt::AlignHCenter, "PLAN DEV.");
    p.restore();
}

void VAlignProfileView::drawStripElements(QPainter& p) const
{
    p.save();
    p.setClipRect(stripRect());
    QFont fBig = font(); fBig.setPointSize(8);  fBig.setBold(true);
    QFont fSml = font(); fSml.setPointSize(6.5);

    for (const HElem& el : m_hElems) {
        int bx = int(tx(el.ch0));
        int bw = int(tx(el.ch1)) - bx;
        int by = stripTop() + 1;
        int bh = kSH - 2;
        QRect band(bx, by, bw, bh);

        // Band fill
        p.setPen(QPen(elemColor(el.type, true), 0.6));
        p.setBrush(elemColor(el.type, false));
        p.drawRect(band);

        // Left edge accent
        p.setPen(QPen(elemColor(el.type, true), 1.4));
        p.drawLine(bx, by, bx, by + bh);

        // Highlight if cursor is over this element
        if (m_hasCursor && m_curCh >= el.ch0 && m_curCh < el.ch1) {
            QColor hl(elemTextColor(el.type)); hl.setAlpha(12);
            p.fillRect(band, hl);
        }

        // Type badge (T / C / S)
        QString typeStr = el.type == HElemType::Tangent  ? "T"
                          : el.type == HElemType::Circular ? "C" : "S";
        QRect badge(bx + 3, by + 4, 20, 12);
        QColor bd(elemColor(el.type, true)); bd.setAlpha(130);
        p.setPen(Qt::NoPen); p.setBrush(bd);
        p.drawRoundedRect(badge, 2, 2);
        p.setFont(fBig); p.setPen(elemTextColor(el.type)); p.setBrush(Qt::NoBrush);
        p.drawText(badge, Qt::AlignCenter, typeStr);

        // Radius label
        if (el.radius > 0 && bw > 44) {
            p.setFont(fSml);
            QColor tx2(elemTextColor(el.type)); tx2.setAlpha(140);
            p.setPen(tx2);
            p.drawText(bx + 5, by + 26, QString("R=%1 m").arg(el.radius, 0, 'f', 0));
        }

        // Element label (centred)
        if (bw > 80 && !el.label.isEmpty()) {
            p.setFont(fSml);
            QColor tx3(elemTextColor(el.type)); tx3.setAlpha(128);
            p.setPen(tx3);
            p.drawText(QRect(bx, by + bh - 16, bw, 12), Qt::AlignHCenter, el.label);
        }

        // Start chainage label
        p.setFont(fSml);
        p.setPen(QColor("#182e48"));
        p.drawText(bx + 3, by + bh - 4, QString::number(int(el.ch0)));
    }

    // End chainage
    if (!m_hElems.isEmpty()) {
        p.setFont(fSml);
        p.setPen(QColor("#182e48"));
        const HElem& last = m_hElems.last();
        int ex = int(tx(last.ch1));
        p.drawText(ex - 26, stripTop() + kSH - 4, QString::number(int(last.ch1)));
    }

    p.restore();
}

void VAlignProfileView::drawStripPlanTrace(QPainter& p) const
{
    if (m_planPts.size() < 2) return;
    p.save();
    p.setClipRect(stripRect());

    // Fill under the trace
    QPainterPath fillPath;
    fillPath.moveTo(tx(m_planPts[0].ch), stripDevY(m_planPts[0].dev));
    for (int k = 1; k < m_planPts.size(); ++k)
        fillPath.lineTo(tx(m_planPts[k].ch), stripDevY(m_planPts[k].dev));
    fillPath.lineTo(tx(m_planPts.last().ch), stripCY());
    fillPath.lineTo(tx(m_planPts.first().ch), stripCY());
    fillPath.closeSubpath();
    QColor fill(Pal::PlanFill); fill.setAlpha(14);
    p.fillPath(fillPath, fill);

    // Trace line
    QVector<QPointF> pts;
    pts.reserve(m_planPts.size());
    for (const auto& pp : m_planPts)
        pts.append({ tx(pp.ch), stripDevY(pp.dev) });

    p.setPen(QPen(Pal::PlanTrace, 1.8, Qt::SolidLine, Qt::RoundCap));
    p.drawPolyline(pts.constData(), pts.size());

    p.restore();
}

void VAlignProfileView::drawStripVipTicks(QPainter& p) const
{
    p.save();
    p.setClipRect(stripRect());

    for (int i = 0; i < m_vips.size(); ++i) {
        const Vip& v  = m_vips[i];
        const bool sel = (i == m_selIdx);
        int sx = int(tx(v.ch));

        // Tick line
        QPen tickPen(sel ? Pal::VipTickS : Pal::VipTickN,
                     sel ? 1.5 : 0.7, Qt::DashLine);
        tickPen.setDashPattern({3, 4});
        p.setPen(tickPen);
        p.drawLine(sx, stripTop(), sx, stripTop() + kSH);

        // Downward triangle at the top of the strip
        QPolygonF tri = QVector<QPointF>{
            QPointF(sx,     stripTop() + 3),
            QPointF(sx + 5, stripTop() + 12),
            QPointF(sx - 5, stripTop() + 12),
        };
        p.setPen(QPen(sel ? Pal::VipTickS : Pal::VipTickN, 1.0));
        p.setBrush(sel ? QColor(255, 170, 0) : QColor(15, 30, 52));
        p.drawPolygon(tri);

        // Dot at the plan-trace deviation for this VIP
        double planDev = 0.0;
        for (int k = 0; k + 1 < m_planPts.size(); ++k) {
            if (m_planPts[k].ch <= v.ch && v.ch <= m_planPts[k+1].ch) {
                double t = (v.ch - m_planPts[k].ch) /
                           (m_planPts[k+1].ch - m_planPts[k].ch);
                planDev = m_planPts[k].dev + t * (m_planPts[k+1].dev - m_planPts[k].dev);
                break;
            }
        }
        int dy = int(stripDevY(planDev));
        p.setPen(QPen(sel ? Pal::VipSelDot : Pal::VipDotNBd, 1.0));
        p.setBrush(sel ? Pal::VipDropSel : Pal::VipDotN);
        p.drawEllipse(QPointF(sx, dy), 3.0, 3.0);
    }
    p.restore();
}

void VAlignProfileView::drawStripCursorLine(QPainter& p) const
{
    p.save();
    p.setClipRect(stripRect());
    QPen hp(Pal::CursorLine, 0.5, Qt::DashLine);
    hp.setDashPattern({2, 4});
    p.setPen(hp);
    p.drawLine(QPointF(m_curPx.x(), stripTop()),
               QPointF(m_curPx.x(), stripTop() + kSH));
    p.restore();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Step 19 — Grade violations
// ─────────────────────────────────────────────────────────────────────────────

void VAlignProfileView::setGradeViolations(
    const QVector<QPair<double,double>>& violations,
    double maxGrade)
{
    m_gradeViolations    = violations;
    m_violationMaxGrade  = maxGrade;
    update();
}

void VAlignProfileView::drawGradeViolations(QPainter& p) const
{
    if (m_gradeViolations.isEmpty()) return;

    p.save();
    p.setClipRect(profileRect());

    // 半透明紅色填色
    const QColor fillCol(220, 40, 40, 55);
    const QColor borderCol(220, 40, 40, 140);

    for (const auto& seg : m_gradeViolations) {
        const double x0 = tx(seg.first);
        const double x1 = tx(seg.second);
        if (x1 <= kML || x0 >= width() - kMR) continue;

        const QRectF band(x0, kMT, x1 - x0, profileH());
        p.fillRect(band, fillCol);
        p.setPen(QPen(borderCol, 1.0, Qt::DashLine));
        p.drawLine(QPointF(x0, kMT), QPointF(x0, kMT + profileH()));
        p.drawLine(QPointF(x1, kMT), QPointF(x1, kMT + profileH()));
    }

    // 在最頂端標示限值文字
    if (m_violationMaxGrade > 0.0) {
        QFont f = font(); f.setPointSize(7); p.setFont(f);
        p.setPen(QColor(220, 80, 80, 180));
        const QString lbl = QString("Max grade: ±%1%").arg(m_violationMaxGrade * 100.0, 0, 'f', 2);
        p.drawText(kML + 4, kMT + 11, lbl);
    }

    p.restore();
}

} // namespace ui
} // namespace aicad
