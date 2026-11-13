/**
 * @file PlanProfilePainter.cpp
 */
#include "PlanProfilePainter.h"

#include "DrawingSheet.h"
#include "cad/Document.h"
#include "railway/RailwayAlignment.h"
#include "core/geometry/ProjectOrigin.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QFont>
#include <QFontMetricsF>
#include <QMap>
#include <QHash>
#include <QSet>
#include <QDebug>
#include <QPolygonF>
#include <QtMath>
#include <QPair>
#include <algorithm>
#include <limits>
#include <cmath>

namespace aicad {
namespace drawing {

using railway::TrackCenterLine;
using railway::AlignmentPoint;
using railway::VerticalAlignmentPoint;

// ─────────────────────────────────────────────────────────────────────────────
// 公用
// ─────────────────────────────────────────────────────────────────────────────

bool planProfileChainageRange(const TrackCenterLine* tcl, double& start, double& end)
{
    if (!tcl) return false;
    double lo = 1e300, hi = -1e300;
    if (tcl->horizontal() && !tcl->horizontal()->rawPoints().isEmpty()) {
        for (const AlignmentPoint& p : tcl->horizontal()->rawPoints()) {
            lo = std::min(lo, p.chainage);
            hi = std::max(hi, p.chainage);
        }
    } else if (tcl->vertical() && !tcl->vertical()->points().isEmpty()) {
        for (const VerticalAlignmentPoint& p : tcl->vertical()->points()) {
            lo = std::min(lo, p.chainage);
            hi = std::max(hi, p.chainage);
        }
    }
    if (!(hi > lo)) return false;
    start = lo;
    end = hi;
    return true;
}

QString planProfileFormatRemarkChainage(const TrackCenterLine* tcl,
                                        const PlanProfileSettings& s, double ch)
{
    double v = ch;
    if (s.useContChainage && tcl && tcl->horizontal() && !tcl->horizontal()->isEmpty())
        v = tcl->horizontal()->getContinuousChainage(ch);
    const int dec = std::clamp(s.remarkDecimals, 0, 4);
    const double scale = std::pow(10.0, dec);
    const double a = std::round(std::fabs(v) * scale) / scale;      // 先捨入再拆，避免 999.6 → 1000 的進位問題
    const int km = int(std::floor(a / 1000.0));
    const double m = a - km * 1000.0;
    return QStringLiteral("%1%2K+%3").arg(v < 0 && a > 0 ? QStringLiteral("-") : QString())
                                     .arg(km)
                                     .arg(m, 3 + (dec > 0 ? dec + 1 : 0), 'f', dec, QLatin1Char('0'));
}

QString planProfileFormatChainage(const TrackCenterLine* tcl,
                                  const PlanProfileSettings& s, double ch)
{
    double v = ch;
    if (s.useContChainage && tcl && tcl->horizontal() && !tcl->horizontal()->isEmpty())
        v = tcl->horizontal()->getContinuousChainage(ch);
    if (!s.kmFormat)
        return QString::number(v, 'f', 2);
    const bool neg = v < 0;
    const double a = std::fabs(v);
    const int km = static_cast<int>(std::floor(a / 1000.0));
    const double m = a - km * 1000.0;
    return QStringLiteral("%1%2%3+%4").arg(s.chainagePrefix)
                                       .arg(neg ? QStringLiteral("-") : QString())
                                       .arg(km)
                                       .arg(m, 6, 'f', 2, QLatin1Char('0'));
}

namespace {

QString azimuthText(double rad)
{
    double deg = qRadiansToDegrees(rad);
    deg = std::fmod(deg, 360.0);
    if (deg < 0) deg += 360.0;
    int d = static_cast<int>(std::floor(deg));
    double mm = (deg - d) * 60.0;
    int m = static_cast<int>(std::floor(mm));
    int sec = static_cast<int>(std::round((mm - m) * 60.0));
    if (sec >= 60) { sec -= 60; ++m; }
    if (m >= 60)   { m -= 60; ++d; }
    if (d >= 360)  d -= 360;
    return QStringLiteral("%1°%2'%3\"").arg(d).arg(m, 2, 10, QLatin1Char('0'))
                                         .arg(sec, 2, 10, QLatin1Char('0'));
}

/** 以錨點與對齊方式畫字（不依賴 QFontMetrics，避免與繪圖裝置 dpi 不一致）。 */
void drawTextAt(QPainter& p, const QPointF& pt, const QString& t, Qt::Alignment al,
                double textH)
{
    constexpr double W = 120.0;
    const double H = textH * 1.6;
    double x = pt.x(), y = pt.y();
    if (al & Qt::AlignRight)        x -= W;
    else if (al & Qt::AlignHCenter) x -= W / 2;
    if (al & Qt::AlignBottom)       y -= H;
    else if (al & Qt::AlignVCenter) y -= H / 2;
    p.drawText(QRectF(x, y, W, H), int(al), t);
}

/** 沿方向 u（單位向量）畫字；flipped＝文字須往 -u 方向延伸（右對齊於 origin）。 */
void drawAlongText(QPainter& p, const QPointF& origin, const QPointF& u, bool flipped,
                   const QString& t, double textH)
{
    constexpr double W = 120.0;
    const double H = textH * 1.6;
    p.save();
    p.translate(origin);
    p.rotate(qRadiansToDegrees(std::atan2(u.y(), u.x())));
    if (flipped) p.drawText(QRectF(-W, -H / 2, W, H), Qt::AlignRight | Qt::AlignVCenter, t);
    else         p.drawText(QRectF(0, -H / 2, W, H), Qt::AlignLeft | Qt::AlignVCenter, t);
    p.restore();
}

/** 沿角度 ang（度，螢幕座標順時針為正）畫字，置中於 mid；off＝垂直於文字的偏移（負＝文字上方）。 */
void drawParallelText(QPainter& p, const QPointF& mid, double ang, double off,
                      const QString& t, double textH)
{
    constexpr double W = 120.0;
    const double H = textH * 1.6;
    p.save();
    p.translate(mid);
    p.rotate(ang);
    p.drawText(QRectF(-W / 2, off - H / 2, W, H), Qt::AlignHCenter | Qt::AlignVCenter, t);
    p.restore();
}

/** Liang–Barsky：線段對矩形裁剪。成功回傳 true 並更新端點。 */
bool clipLine(const QRectF& r, QPointF& a, QPointF& b)
{
    double t0 = 0, t1 = 1;
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const double P[4] = { -dx, dx, -dy, dy };
    const double Q[4] = { a.x() - r.left(), r.right() - a.x(),
                          a.y() - r.top(),  r.bottom() - a.y() };
    for (int i = 0; i < 4; ++i) {
        if (std::fabs(P[i]) < 1e-12) { if (Q[i] < 0) return false; continue; }
        const double t = Q[i] / P[i];
        if (P[i] < 0) { if (t > t1) return false; t0 = std::max(t0, t); }
        else          { if (t < t0) return false; t1 = std::min(t1, t); }
    }
    const QPointF na(a.x() + t0 * dx, a.y() + t0 * dy);
    const QPointF nb(a.x() + t1 * dx, a.y() + t1 * dy);
    a = na; b = nb;
    return true;
}

double niceAtLeast(double v)
{
    static const double kNice[] = { 1, 2, 2.5, 5, 10, 20, 25, 50, 100, 200, 250, 500,
                                    1000, 2000, 2500, 5000, 10000, 20000, 50000 };
    for (double n : kNice) if (n >= v - 1e-9) return n;
    return kNice[sizeof(kNice) / sizeof(kNice[0]) - 1];
}

double niceGridStep(double minMeters)
{
    static const double kStep[] = { 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000 };
    for (double n : kStep) if (n >= minMeters - 1e-9) return n;
    return 1000;
}

// ─────────────────────────────────────────────────────────────────────────────
// 串接路徑：把一張圖紙涵蓋的若干線形片段，接成一條以「串接距離 d」為座標的路徑
// ─────────────────────────────────────────────────────────────────────────────

struct Piece {
    const TrackCenterLine* tcl = nullptr;
    double c0 = 0, c1 = 0;   ///< 線形本身的里程範圍
    double d0 = 0;           ///< 此片段在串接距離中的起點
    double len() const { return c1 - c0; }
};

struct Tick { double d, local; const TrackCenterLine* tcl; };

class ChainPath {
public:
    QVector<Piece> pieces;
    double total = 0.0;

    void add(const TrackCenterLine* t, double c0, double c1) {
        Piece p; p.tcl = t; p.c0 = c0; p.c1 = c1; p.d0 = total;
        pieces.append(p);
        total += (c1 - c0);
    }
    const Piece& at(double d) const {
        int k = 0;
        for (int i = 0; i < pieces.size(); ++i)
            if (pieces[i].d0 <= d + 1e-9) k = i;
        return pieces[k];
    }
    double localOf(const Piece& p, double d) const {
        return std::min(p.c1, std::max(p.c0, p.c0 + (d - p.d0)));
    }
    QPointF xy(double d) const    { const Piece& p = at(d); return p.tcl->getXYForCalc(localOf(p, d)); }
    double  elev(double d) const  { const Piece& p = at(d); return p.tcl->getElevationForCalc(localOf(p, d)); }
    double  az(double d) const    { const Piece& p = at(d); return p.tcl->getAzimuthForCalc(localOf(p, d)); }
    double  slope(double d) const { const Piece& p = at(d); return p.tcl->getSlopeForCalc(localOf(p, d)); }
    double  radius(double d) const{ const Piece& p = at(d); return p.tcl->getRadius(localOf(p, d)); }
    double  cant(double d) const  { const Piece& p = at(d); return p.tcl->getCant(localOf(p, d)); }
    QString fmt(double d, const PlanProfileSettings& s) const {
        const Piece& p = at(d);
        return planProfileFormatChainage(p.tcl, s, localOf(p, d));
    }
    bool contains(const TrackCenterLine* t) const {
        for (const Piece& p : pieces) if (p.tcl == t) return true;
        return false;
    }
    /** 取樣點：等間距＋每個片段的起點（讓折線在接點處精確斷開） */
    QVector<double> samples(double stepM) const {
        QVector<double> v;
        for (double d = 0; d < total; d += stepM) v.append(d);
        for (const Piece& p : pieces) v.append(p.d0);
        v.append(total);
        std::sort(v.begin(), v.end());
        QVector<double> u;
        for (double d : v) if (u.isEmpty() || d - u.last() > 1e-9) u.append(d);
        return u;
    }
    /** 各線形里程為 step 整數倍的位置 */
    QVector<Tick> ticks(double step) const {
        QVector<Tick> out;
        for (const Piece& p : pieces) {
            for (double ch = std::ceil(p.c0 / step - 1e-9) * step; ch <= p.c1 + 1e-6; ch += step) {
                const double d = p.d0 + (ch - p.c0);
                if (d > total + 1e-6) break;
                if (!out.isEmpty() && std::fabs(out.last().d - d) < 1e-6) continue;
                Tick tk; tk.d = d; tk.local = ch; tk.tcl = p.tcl;
                out.append(tk);
            }
        }
        return out;
    }
};

bool isMultiple(double v, double step)
{
    const double r = std::fmod(v + 1e-6, step);
    return r < 2e-6 || step - r < 2e-6;
}

struct VC { double pvi, entry, exit, elev, lvc, k; double pviLocal; const TrackCenterLine* tcl;
            double entryLocal, exitLocal; };

QVector<VC> collectVerticalCurves(const Piece& pc)
{
    QVector<VC> out;
    const TrackCenterLine* tcl = pc.tcl;
    if (!tcl->vertical()) return out;
    const QVector<VerticalAlignmentPoint>& pts = tcl->vertical()->points();
    const int n = pts.size();
    int i = 0;
    while (i < n) {
        if (pts[i].lvc <= 1e-6) { ++i; continue; }
        int j = i;
        while (j + 1 < n && j - i < 2 && pts[j + 1].lvc > 1e-6 &&
               std::fabs(pts[j + 1].lvc - pts[i].lvc) < 1e-6)
            ++j;
        const int mid = (j - i == 2) ? i + 1 : i;
        VC vc;
        vc.tcl = tcl;
        vc.lvc = pts[i].lvc;
        vc.pviLocal   = (j - i == 2) ? pts[mid].chainage : (pts[i].chainage + pts[j].chainage) / 2.0;
        vc.entryLocal = vc.pviLocal - vc.lvc / 2.0;
        vc.exitLocal  = vc.pviLocal + vc.lvc / 2.0;
        vc.elev = pts[mid].pviElevation != 0.0 ? pts[mid].pviElevation
                                               : tcl->getElevationForCalc(vc.pviLocal);
        vc.k = pts[mid].kValue;
        vc.pvi   = pc.d0 + (vc.pviLocal   - pc.c0);
        vc.entry = pc.d0 + (vc.entryLocal - pc.c0);
        vc.exit  = pc.d0 + (vc.exitLocal  - pc.c0);
        if (vc.exitLocal >= pc.c0 && vc.entryLocal <= pc.c1) out.append(vc);
        i = j + 1;
    }
    return out;
}

struct Ctx {
    QPainter* p = nullptr;
    const PlanProfileSettings* s = nullptr;
    const PlanProfileLayout* L = nullptr;
    ChainPath path;
    cad::Document* doc = nullptr;
    double mm = 1.0;          ///< 水平 mm/m
    double lwThin = 0.25, lwMed = 0.5;
    double th = 2.5;          ///< 字高
    QString prevNo, nextNo;   ///< 左／右接圖的圖號

    double xOf(double d) const { return L->dataLeft + d * mm; }
    void font(double size) const {
        QFont f(QStringLiteral("Arial"));
        f.setPointSizeF(size);
        p->setFont(f);
    }
    void pen(const QColor& c, double w, Qt::PenStyle st = Qt::SolidLine) const {
        QPen pn(c, w, st);
        pn.setCosmetic(false);
        p->setPen(pn);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// 平面帶狀圖
// ─────────────────────────────────────────────────────────────────────────────

void paintPlan(const Ctx& c)
{
    QPainter& p = *c.p;
    const PlanProfileSettings& s = *c.s;
    const ChainPath& path = c.path;
    const QRectF R = c.L->planRect;
    const double mm = c.mm;

    c.pen(Qt::black, c.lwMed);
    p.drawRect(R);

    const QPointF P0 = path.xy(0.0);
    const QPointF P1 = path.xy(path.total);
    QPointF d = P1 - P0;
    const double dl = std::hypot(d.x(), d.y());
    if (dl < 1e-6) {
        const double az = path.az(0.0);
        d = QPointF(std::sin(az), std::cos(az));
    } else {
        d /= dl;
    }

    const double stepM = std::max(0.25, 0.5 / mm);
    const QVector<double> ds = path.samples(stepM);
    QVector<QPointF> loc;
    loc.reserve(ds.size());
    for (double x : ds) loc.append(path.xy(x));

    double vmin = 1e300, vmax = -1e300;
    auto uv = [&](const QPointF& q) {
        const double du = q.x() - P0.x(), dv = q.y() - P0.y();
        return QPointF(du * d.x() + dv * d.y(), -du * d.y() + dv * d.x());
    };
    for (const QPointF& q : loc) {
        const double v = uv(q).y();
        vmin = std::min(vmin, v);
        vmax = std::max(vmax, v);
    }
    const double vMid = (vmin + vmax) / 2.0;
    const double yc = R.center().y();
    auto toSheet = [&](const QPointF& q) {
        const QPointF t = uv(q);
        return QPointF(c.L->dataLeft + t.x() * mm, yc - (t.y() - vMid) * mm);
    };

    p.save();
    p.setClipRect(R.adjusted(0.1, 0.1, -0.1, -0.1));

    // TM2 座標格線
    if (s.coordGrid > 0.0) {
        auto& po = core::geometry::ProjectOrigin::instance();
        double eMin = 1e300, eMax = -1e300, nMin = 1e300, nMax = -1e300;
        for (const QPointF& q : loc) {
            const QPointF g = po.toGlobal(q);
            eMin = std::min(eMin, g.x()); eMax = std::max(eMax, g.x());
            nMin = std::min(nMin, g.y()); nMax = std::max(nMax, g.y());
        }
        const double margin = std::max(R.height(), 10.0) / mm;
        eMin -= margin; eMax += margin; nMin -= margin; nMax += margin;
        const double g = s.coordGrid;
        if ((eMax - eMin) / g < 300 && (nMax - nMin) / g < 300) {
            c.font(c.th * 0.8);
            auto drawGrid = [&](bool isE, double val) {
                const QPointF g0 = isE ? po.toLocal(val, nMin) : po.toLocal(eMin, val);
                const QPointF g1 = isE ? po.toLocal(val, nMax) : po.toLocal(eMax, val);
                QPointF a = toSheet(g0), b = toSheet(g1);
                if (!clipLine(R, a, b)) return;
                c.pen(QColor(170, 170, 170), c.lwThin * 0.6, Qt::DashLine);
                p.drawLine(a, b);
                p.setPen(QColor(120, 120, 120));
                const QPointF at = (a.y() > b.y()) ? a : b;
                drawTextAt(p, at + QPointF(0.5, -0.3),
                           QStringLiteral("%1%2").arg(isE ? QLatin1Char('E') : QLatin1Char('N')).arg(val, 0, 'f', 0),
                           Qt::AlignLeft | Qt::AlignBottom, c.th * 0.8);
            };
            for (double e = std::floor(eMin / g) * g; e <= eMax; e += g) drawGrid(true, e);
            for (double n = std::floor(nMin / g) * g; n <= nMax; n += g) drawGrid(false, n);
        }
    }

    // 同框內、但不在本圖串接路徑上的其他線形
    if (s.showNeighbors && c.doc) {
        for (railway::TrackCenterLine* other : c.doc->trackCenterLines()) {
            if (!other || path.contains(other) || !other->horizontal() || other->horizontal()->isEmpty()) continue;
            double oa = 0, ob = 0;
            if (!planProfileChainageRange(other, oa, ob)) continue;
            const double ostep = std::max(stepM, 1.0);
            QPainterPath op;
            bool any = false, started = false;
            QPointF firstIn;
            for (double ch = oa; ; ch += ostep) {
                const double cc = std::min(ch, ob);
                const QPointF q = toSheet(other->getXYForCalc(cc));
                if (!started) { op.moveTo(q); started = true; } else op.lineTo(q);
                if (R.contains(q)) { if (!any) firstIn = q; any = true; }
                if (cc >= ob) break;
            }
            if (!any) continue;
            c.pen(QColor(120, 120, 120), c.lwMed, Qt::DashDotLine);
            p.setBrush(Qt::NoBrush);
            p.drawPath(op);
            c.font(c.th * 0.85);
            p.setPen(QColor(100, 100, 100));
            drawTextAt(p, firstIn + QPointF(1.0, -1.0), other->name(),
                       Qt::AlignLeft | Qt::AlignBottom, c.th * 0.85);
        }
    }

    // 中心線（接點處斷開重畫，避免不同線形間誤連）
    c.pen(Qt::black, c.lwMed * 1.2);
    {
        QPainterPath cur;
        bool open = false;
        int pi = 0;
        for (int i = 0; i < ds.size(); ++i) {
            while (pi + 1 < path.pieces.size() && ds[i] >= path.pieces[pi + 1].d0 - 1e-9) {
                if (open) { p.drawPath(cur); cur = QPainterPath(); open = false; }
                ++pi;
            }
            const QPointF q = toSheet(loc[i]);
            if (!open) { cur.moveTo(q); open = true; } else cur.lineTo(q);
        }
        if (open) p.drawPath(cur);
    }

    // 里程刻度
    auto tangentNormal = [&](double x) {
        double x0 = x, x1 = x + 0.5;
        if (x1 > path.total) { x1 = x; x0 = std::max(0.0, x - 0.5); }
        const QPointF q0 = toSheet(path.xy(x0));
        const QPointF q1 = toSheet(path.xy(x1));
        QPointF t = q1 - q0;
        const double l = std::hypot(t.x(), t.y());
        if (l < 1e-9) t = QPointF(1, 0); else t /= l;
        QPointF n(-t.y(), t.x());
        if (n.y() > 0) n = -n;
        return n;
    };
    c.font(c.th * 0.9);
    for (const Tick& tk : path.ticks(s.minorTick)) {
        const bool major = isMultiple(tk.local, s.majorTick);
        const QPointF q = toSheet(path.xy(tk.d));
        const QPointF n = tangentNormal(tk.d);
        const double len = major ? 2.4 : 1.2;
        c.pen(Qt::black, c.lwThin);
        p.drawLine(q - n * len * 0.5, q + n * len);
        if (major) {
            p.setPen(Qt::black);
            // 文字方向垂直於線形：沿法向延伸；必要時翻轉讀向避免倒立
            QPointF u = n;
            const bool flipped = (u.x() < -1e-9) || (std::fabs(u.x()) <= 1e-9 && u.y() > 0);
            if (flipped) u = -u;
            drawAlongText(p, q + n * (len + 0.8), u, flipped,
                          planProfileFormatChainage(tk.tcl, s, tk.local), c.th * 0.9);
        }
    }

    // 關鍵點
    if (s.showKeyPoints) {
        c.font(c.th * 0.8);
        int k = 0;
        for (const Piece& pc : path.pieces) {
            for (const AlignmentPoint& ap : pc.tcl->horizontal()->rawPoints()) {
                if (ap.tsc.isEmpty() || ap.chainage < pc.c0 - 1e-6 || ap.chainage > pc.c1 + 1e-6) continue;
                const double dd = pc.d0 + (ap.chainage - pc.c0);
                const QPointF q = toSheet(path.xy(dd));
                const QPointF n = tangentNormal(dd);
                c.pen(QColor(200, 0, 0), c.lwThin);
                p.drawEllipse(q, 0.7, 0.7);
                p.drawLine(q - n * 1.5, q + n * 1.5);
                p.setPen(QColor(200, 0, 0));
                drawTextAt(p, q + QPointF(0.8, 2.0 + (k % 3) * 3.0),
                           QStringLiteral("%1 %2").arg(ap.tsc,
                               planProfileFormatChainage(pc.tcl, s, ap.chainage)),
                           Qt::AlignLeft | Qt::AlignTop, c.th * 0.8);
                ++k;
            }
        }
    }

    // 曲線資料：圓弧段中點標 R / C
    if (s.showCurveData) {
        c.font(c.th * 0.85);
        for (const Piece& pc : path.pieces) {
            const QVector<AlignmentPoint>& raw = pc.tcl->horizontal()->rawPoints();
            for (int i = 0; i + 1 < raw.size(); ++i) {
                const AlignmentPoint& ap = raw[i];
                const double L = raw[i + 1].chainage - ap.chainage;
                const double R0 = ap.radius;
                if (L <= 1e-3 || !std::isfinite(R0) || std::fabs(R0) < 1.0 || std::fabs(R0) > 1e6) continue;
                const double mid = ap.chainage + L / 2.0;
                if (mid < pc.c0 || mid > pc.c1) continue;
                const double Rm = pc.tcl->getRadius(mid);
                if (!std::isfinite(Rm) || std::fabs(Rm - R0) > 1e-3 * std::fabs(R0)) continue;
                const QPointF q = toSheet(pc.tcl->getXYForCalc(mid));
                const double cant = pc.tcl->getCant(mid);
                QString txt = QStringLiteral("R=%1  Lc=%2").arg(std::fabs(R0), 0, 'f', 0).arg(L, 0, 'f', 2);
                if (std::fabs(cant) > 0.5) txt += QStringLiteral("  C=%1").arg(std::fabs(cant), 0, 'f', 0);
                p.setPen(QColor(0, 0, 160));
                drawTextAt(p, q + QPointF(0, (R0 > 0) ? 9.0 : -9.0), txt,
                           Qt::AlignHCenter | ((R0 > 0) ? Qt::AlignTop : Qt::AlignBottom), c.th * 0.85);
            }
        }
    }

    // 接圖線：通過起／訖點，垂直於該點線形方向；圖號文字沿接圖線置中
    for (int e = 0; e < 2; ++e) {
        const QString& no = e == 0 ? c.prevNo : c.nextNo;
        if (no.isEmpty()) continue;
        const double dd = e == 0 ? 0.0 : path.total;
        const QPointF Q = toSheet(path.xy(dd));
        const double az = path.az(dd);
        const double wx = std::sin(az), wy = std::cos(az);              // 世界座標切線
        const double ux = wx * d.x() + wy * d.y();
        const double vy = -wx * d.y() + wy * d.x();
        QPointF tan(ux, -vy);                                             // 圖面切線
        const double tl = std::hypot(tan.x(), tan.y());
        if (tl < 1e-9) continue;
        tan /= tl;
        const QPointF nrm(-tan.y(), tan.x());                             // 接圖線方向（垂直切線）
        QPointF a = Q - nrm * 1000.0, b = Q + nrm * 1000.0;
        if (!clipLine(R, a, b)) continue;
        c.pen(QColor(200, 0, 0), c.lwMed, Qt::DashDotLine);
        p.drawLine(a, b);
        const QPointF mid = (a + b) / 2.0;
        double ang = qRadiansToDegrees(std::atan2(nrm.y(), nrm.x()));
        if (ang > 90.0) ang -= 180.0;
        if (ang <= -90.0) ang += 180.0;
        const QPointF inside = e == 0 ? tan : -tan;                       // 朝圖內側
        const double ar = qDegreesToRadians(ang);
        const double side = (-std::sin(ar) * inside.x() + std::cos(ar) * inside.y()) >= 0 ? 1.0 : -1.0;
        c.font(c.th * 0.9);
        p.setPen(QColor(200, 0, 0));
        drawParallelText(p, mid, ang, side * c.th * 0.9,
                         QStringLiteral("接圖線 見 %1").arg(no), c.th * 0.9);
    }

    // 指北針
    if (s.showNorthArrow) {
        QPointF dir(d.y(), -d.x());
        const double l = std::hypot(dir.x(), dir.y());
        if (l > 1e-9) dir /= l;
        const QPointF ctr(R.right() - 9.0, R.top() + 10.0);
        const QPointF tip = ctr + dir * 6.0, tail = ctr - dir * 6.0;
        const QPointF side(-dir.y(), dir.x());
        c.pen(Qt::black, c.lwMed);
        p.drawLine(tail, tip);
        QPolygonF head;
        head << tip << (tip - dir * 3.0 + side * 1.4) << (tip - dir * 3.0 - side * 1.4);
        p.setBrush(Qt::black);
        p.drawPolygon(head);
        p.setBrush(Qt::NoBrush);
        c.font(c.th);
        p.setPen(Qt::black);
        drawTextAt(p, tip + dir * 1.0, QStringLiteral("N"), Qt::AlignHCenter | Qt::AlignVCenter, c.th);
    }

    p.restore();
}

// ─────────────────────────────────────────────────────────────────────────────
// 縱斷面
// ─────────────────────────────────────────────────────────────────────────────

struct ProfileInfo { double vScale = 100.0, datum = 0.0, step = 1.0; };

ProfileInfo paintProfile(const Ctx& c)
{
    QPainter& p = *c.p;
    const PlanProfileSettings& s = *c.s;
    const ChainPath& path = c.path;
    const QRectF R = c.L->profileRect;
    const double mm = c.mm;

    const double stepM = std::max(0.25, 0.5 / mm);
    const QVector<double> ds = path.samples(stepM);
    QVector<double> zs;
    zs.reserve(ds.size());
    double zmin = 1e300, zmax = -1e300;
    for (double x : ds) {
        const double z = path.elev(x);
        zs.append(z);
        zmin = std::min(zmin, z);
        zmax = std::max(zmax, z);
    }
    if (!(zmax - zmin > 1e-6)) { zmax = zmin + 1.0; }

    QVector<VC> vcs;
    for (const Piece& pc : path.pieces) vcs += collectVerticalCurves(pc);
    for (const VC& vc : vcs)
        if (vc.pvi >= 0 && vc.pvi <= path.total) { zmin = std::min(zmin, vc.elev); zmax = std::max(zmax, vc.elev); }

    ProfileInfo info;
    const double usableH = R.height() - 12.0;
    if (s.autoVScale) {
        const double need = (zmax - zmin) * 1000.0 / (0.8 * usableH);
        info.vScale = std::max(s.vScale, niceAtLeast(need));
    } else {
        info.vScale = s.vScale;
    }
    const double kz = 1000.0 / info.vScale;
    info.step = s.elevGridStep > 0 ? s.elevGridStep : niceGridStep(8.0 / kz);
    info.datum = std::floor((zmin - 0.05 * (zmax - zmin)) / info.step) * info.step;
    const double yBase = R.bottom() - 3.0;
    auto yOf = [&](double z) { return yBase - (z - info.datum) * kz; };

    c.pen(Qt::black, c.lwMed);
    p.drawRect(R);

    p.save();
    p.setClipRect(R.adjusted(0.1, 0.1, -0.1, -0.1));

    const QVector<Tick> minorTicks = path.ticks(s.minorTick);

    if (s.showGridLines) {
        c.pen(QColor(190, 190, 190), c.lwThin * 0.6);
        const double zTop = info.datum + (yBase - R.top()) / kz;
        for (double z = info.datum; z <= zTop + 1e-9; z += info.step)
            p.drawLine(QPointF(R.left(), yOf(z)), QPointF(R.right(), yOf(z)));
        for (const Tick& tk : minorTicks)
            if (isMultiple(tk.local, s.majorTick))
                p.drawLine(QPointF(c.xOf(tk.d), R.top()), QPointF(c.xOf(tk.d), R.bottom()));
    }
    c.pen(Qt::black, c.lwThin);
    for (const Tick& tk : minorTicks) {
        const bool major = isMultiple(tk.local, s.majorTick);
        p.drawLine(QPointF(c.xOf(tk.d), R.bottom()), QPointF(c.xOf(tk.d), R.bottom() - (major ? 2.5 : 1.2)));
    }

    // 設計線（片段接點處斷開）
    c.pen(Qt::black, c.lwMed * 1.4);
    {
        QPainterPath cur;
        bool open = false;
        int pi = 0;
        for (int i = 0; i < ds.size(); ++i) {
            while (pi + 1 < path.pieces.size() && ds[i] >= path.pieces[pi + 1].d0 - 1e-9) {
                if (open) { p.drawPath(cur); cur = QPainterPath(); open = false; }
                ++pi;
            }
            const QPointF q(c.xOf(ds[i]), yOf(zs[i]));
            if (!open) { cur.moveTo(q); open = true; } else cur.lineTo(q);
        }
        if (open) p.drawPath(cur);
    }
    // 接點標示
    for (int i = 1; i < path.pieces.size(); ++i) {
        const double x = c.xOf(path.pieces[i].d0);
        c.pen(QColor(120, 120, 120), c.lwThin, Qt::DashDotLine);
        p.drawLine(QPointF(x, R.top()), QPointF(x, R.bottom()));
        c.font(c.th * 0.8);
        p.setPen(QColor(100, 100, 100));
        drawTextAt(p, QPointF(x + 0.8, R.top() + 0.8),
                   QStringLiteral("%1 → %2").arg(path.pieces[i - 1].tcl->name(), path.pieces[i].tcl->name()),
                   Qt::AlignLeft | Qt::AlignTop, c.th * 0.8);
    }

    // 豎曲線
    if (s.showPVI) {
        c.font(c.th * 0.8);
        for (const VC& vc : vcs) {
            const double ze = vc.tcl->getElevationForCalc(vc.entryLocal);
            const double zx = vc.tcl->getElevationForCalc(vc.exitLocal);
            c.pen(QColor(0, 120, 0), c.lwThin, Qt::DashLine);
            QPolygonF tri;
            tri << QPointF(c.xOf(vc.entry), yOf(ze)) << QPointF(c.xOf(vc.pvi), yOf(vc.elev))
                << QPointF(c.xOf(vc.exit), yOf(zx));
            p.drawPolyline(tri);
            c.pen(QColor(0, 120, 0), c.lwThin);
            p.drawEllipse(QPointF(c.xOf(vc.pvi), yOf(vc.elev)), 0.8, 0.8);
            const double ee[2] = { vc.entry, vc.exit };
            const double ez[2] = { ze, zx };
            for (int k = 0; k < 2; ++k)
                p.drawLine(QPointF(c.xOf(ee[k]), yOf(ez[k]) - 1.5), QPointF(c.xOf(ee[k]), yOf(ez[k]) + 1.5));
            const double zMidCurve = vc.tcl->getElevationForCalc(vc.pviLocal);
            const bool crest = vc.elev > zMidCurve;
            p.setPen(QColor(0, 120, 0));
            const QPointF at(c.xOf(vc.pvi), yOf(vc.elev) + (crest ? -2.0 : 2.0));
            const QString t1 = QStringLiteral("VPI %1  EL.%2")
                                   .arg(planProfileFormatChainage(vc.tcl, s, vc.pviLocal))
                                   .arg(vc.elev, 0, 'f', s.elevDecimals);
            QString t2 = QStringLiteral("Lvc=%1").arg(vc.lvc, 0, 'f', 1);
            if (vc.k > 0) t2 += QStringLiteral("  K=%1").arg(vc.k, 0, 'f', 0);
            const double lh = c.th * 0.95;
            if (crest) {
                drawTextAt(p, at + QPointF(0, -lh), t1, Qt::AlignHCenter | Qt::AlignBottom, c.th * 0.8);
                drawTextAt(p, at, t2, Qt::AlignHCenter | Qt::AlignBottom, c.th * 0.8);
            } else {
                drawTextAt(p, at, t1, Qt::AlignHCenter | Qt::AlignTop, c.th * 0.8);
                drawTextAt(p, at + QPointF(0, lh), t2, Qt::AlignHCenter | Qt::AlignTop, c.th * 0.8);
            }
        }
    }

    // 坡度與坡長：豎曲線與片段接點之間的直坡段
    if (s.showGradeLabels) {
        QVector<QPair<double, double>> curves;
        for (const VC& vc : vcs) curves.append(qMakePair(std::max(0.0, vc.entry), std::min(path.total, vc.exit)));
        for (int i = 1; i < path.pieces.size(); ++i)
            curves.append(qMakePair(path.pieces[i].d0, path.pieces[i].d0));
        std::sort(curves.begin(), curves.end());
        QVector<QPair<double, double>> straight;
        double cursor = 0.0;
        for (const auto& cv : curves) {
            if (cv.first > cursor) straight.append(qMakePair(cursor, cv.first));
            cursor = std::max(cursor, cv.second);
        }
        if (path.total > cursor) straight.append(qMakePair(cursor, path.total));

        c.font(c.th * 0.85);
        for (const auto& iv : straight) {
            const double s0 = iv.first, s1 = iv.second;
            if (s1 - s0 < 1e-6 || (s1 - s0) * mm < 6.0) continue;
            const double mid = (s0 + s1) / 2.0;
            const double zmid = path.elev(mid);
            p.setPen(Qt::black);
            const QPointF a0(c.xOf(s0), yOf(path.elev(s0)));
            const QPointF a1(c.xOf(s1), yOf(path.elev(s1)));
            const double ang = qRadiansToDegrees(std::atan2(a1.y() - a0.y(), a1.x() - a0.x()));
            const QPointF at(c.xOf(mid), yOf(zmid));
            drawParallelText(p, at, ang, -c.th * 0.75,
                             QStringLiteral("%1%").arg(path.slope(mid), 0, 'f', 2), c.th * 0.85);
            drawParallelText(p, at, ang, c.th * 0.75,
                             QStringLiteral("L=%1").arg(s1 - s0, 0, 'f', 1), c.th * 0.85);
        }
    }
    p.restore();

    // 左側高程標籤
    c.font(c.th * 0.9);
    p.setPen(Qt::black);
    const double zTop = info.datum + (yBase - R.top()) / kz;
    const int dec = info.step < 1.0 ? 1 : 0;
    for (double z = info.datum; z <= zTop + 1e-9; z += info.step) {
        const double y = yOf(z);
        if (y < R.top() + 1.5 || y > R.bottom() - 0.5) continue;
        drawTextAt(p, QPointF(R.left() - 1.5, y), QString::number(z, 'f', dec),
                   Qt::AlignRight | Qt::AlignVCenter, c.th * 0.9);
    }
    return info;
}

// ─────────────────────────────────────────────────────────────────────────────
// 資料表
// ─────────────────────────────────────────────────────────────────────────────

void paintTable(const Ctx& c)
{
    QPainter& p = *c.p;
    const PlanProfileSettings& s = *c.s;
    const PlanProfileLayout& L = *c.L;
    const ChainPath& path = c.path;

    struct Row { QString label; int kind; };
    QVector<Row> rows;
    rows.append(Row{ QStringLiteral("里程 Chainage"), 0 });
    if (s.rowElevation) rows.append(Row{ QStringLiteral("高程 Elev. (m)"), 1 });
    if (s.rowGrade)     rows.append(Row{ QStringLiteral("坡度 Grade"), 2 });
    if (s.rowCurve)     rows.append(Row{ QStringLiteral("曲線 Curve"), 3 });
    if (s.rowAzimuth)   rows.append(Row{ QStringLiteral("方位角 Azimuth"), 4 });

    // 站點：每 10 m（各線形里程倍數）、起訖點、BVC／PVI／EVC；
    // 依優先序放置，與已放置站點太近（文字會重疊）者略過
    struct St { double d; int pri; QString tag; double elev; };
    const double kNaN = std::numeric_limits<double>::quiet_NaN();
    QVector<St> cand;
    {
        QVector<VC> vcs;
        for (const Piece& pc : path.pieces) vcs += collectVerticalCurves(pc);
        for (const VC& vc : vcs) {
            if (vc.pvi >= -1e-6 && vc.pvi <= path.total + 1e-6) cand.append(St{ vc.pvi, 0, QStringLiteral("PVI"), vc.elev });
            if (vc.entry >= -1e-6 && vc.entry <= path.total + 1e-6) cand.append(St{ vc.entry, 0, QStringLiteral("BVC"), kNaN });
            if (vc.exit >= -1e-6 && vc.exit <= path.total + 1e-6) cand.append(St{ vc.exit, 0, QStringLiteral("EVC"), kNaN });
        }
        cand.append(St{ 0.0, 1, QString(), kNaN });
        cand.append(St{ path.total, 1, QString(), kNaN });
        for (const Tick& tk : path.ticks(10.0)) cand.append(St{ tk.d, 2, QString(), kNaN });
    }
    std::stable_sort(cand.begin(), cand.end(), [](const St& x, const St& y) {
        return x.pri != y.pri ? x.pri < y.pri : x.d < y.d; });
    const double minGap = c.th * 1.15 / c.mm;       // 最小站距 [m]
    QVector<St> sts;
    for (const St& st : cand) {
        bool ok = true;
        for (const St& q : sts) if (std::fabs(q.d - st.d) < minGap) { ok = false; break; }
        if (ok) sts.append(st);
    }
    std::sort(sts.begin(), sts.end(), [](const St& x, const St& y) { return x.d < y.d; });

    const QRectF T = L.tableRect;
    // 各列頂端 y
    QVector<double> rowTop;
    {
        double y = T.top();
        for (int r = 0; r < rows.size(); ++r) {
            rowTop << y;
            y += (r < L.rowHs.size() ? L.rowHs[r] : L.rowH);
        }
        rowTop << y;
    }
    c.pen(Qt::black, c.lwMed);
    p.drawRect(T);
    c.pen(Qt::black, c.lwThin);
    p.drawLine(QPointF(L.plotLeft, T.top()), QPointF(L.plotLeft, T.bottom()));
    for (int r = 1; r < rows.size(); ++r)
        p.drawLine(QPointF(T.left(), rowTop[r]), QPointF(T.right(), rowTop[r]));

    c.font(c.th * 0.95);
    p.setPen(Qt::black);
    for (int r = 0; r < rows.size(); ++r) {
        const QRectF cell(T.left(), rowTop[r], s.leftLabelW, rowTop[r + 1] - rowTop[r]);
        p.drawText(cell.adjusted(1, 0, -0.5, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                   rows[r].label);
    }

    const double rightEdge = L.plotLeft + L.plotWidth + 5.0;
    c.font(c.th);
    for (const St& stn : sts) {
        const double st = stn.d;
        const double x = c.xOf(st);
        c.pen(stn.tag.isEmpty() ? QColor(170, 170, 170) : QColor(0, 120, 0), c.lwThin * (stn.tag.isEmpty() ? 0.6 : 1.0));
        p.drawLine(QPointF(x, T.top()), QPointF(x, T.bottom()));
        if (x - c.th * 0.6 < L.plotLeft - 1.0 || x + c.th * 0.6 > rightEdge) continue;
        for (int r = 0; r < rows.size(); ++r) {
            QString txt;
            switch (rows[r].kind) {
            case 0: txt = (stn.tag.isEmpty() ? QString() : stn.tag + QLatin1Char(' ')) + path.fmt(st, s); break;
            case 1: txt = QString::number(std::isnan(stn.elev) ? path.elev(st) : stn.elev, 'f', s.elevDecimals); break;
            case 2: txt = QStringLiteral("%1%").arg(path.slope(st), 0, 'f', 2); break;
            case 3: {
                const double R0 = path.radius(st);
                if (!std::isfinite(R0) || std::fabs(R0) > 1e6) txt = QStringLiteral("—");
                else {
                    txt = QStringLiteral("R%1%2").arg(R0 > 0 ? QLatin1Char('+') : QLatin1Char('-')).arg(std::fabs(R0), 0, 'f', 0);
                    const double cant = path.cant(st);
                    if (std::fabs(cant) > 0.5) txt += QStringLiteral(" C%1").arg(std::fabs(cant), 0, 'f', 0);
                }
                break; }
            default: txt = azimuthText(path.az(st)); break;
            }
            // 垂直書寫（由下往上讀），置中於格內
            const double rh = rowTop[r + 1] - rowTop[r];
            p.save();
            p.translate(x, rowTop[r] + rh / 2);
            p.rotate(-90);
            p.setPen(Qt::black);
            p.drawText(QRectF(-rh / 2, -c.th, rh, c.th * 2),
                       Qt::AlignHCenter | Qt::AlignVCenter, txt);
            p.restore();
        }
    }
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// 入口
// ─────────────────────────────────────────────────────────────────────────────

/** 平面圖在垂直方向（垂直於弦線）所需的實長 [m] */
static double planVExtentMeters(const ChainPath& path, double mm)
{
    const QPointF P0 = path.xy(0.0), P1 = path.xy(path.total);
    QPointF d = P1 - P0;
    const double dl = std::hypot(d.x(), d.y());
    if (dl < 1e-6) { const double az = path.az(0.0); d = QPointF(std::sin(az), std::cos(az)); }
    else d /= dl;
    double vmin = 1e300, vmax = -1e300;
    for (double x : path.samples(std::max(0.25, 0.5 / mm))) {
        const QPointF q = path.xy(x);
        const double v = -(q.x() - P0.x()) * d.y() + (q.y() - P0.y()) * d.x();
        vmin = std::min(vmin, v); vmax = std::max(vmax, v);
    }
    return vmax - vmin;
}

// ─────────────────────────────────────────────────────────────────────────────
// 線形資料表圖頁（平面／縱面）
// ─────────────────────────────────────────────────────────────────────────────

namespace {
constexpr double kDtRowH = 5.5;     // 資料列高
constexpr double kDtHdrH = 9.0;     // 表頭高
constexpr double kDtTitleH = 12.0;  // 標題區高

using RowMap = QMap<QString, QString>;

QString trimNum(double v, int dec)
{
    QString s = QString::number(v, 'f', dec);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0'))) s.chop(1);
        if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    }
    if (s == QLatin1String("-0")) s = QStringLiteral("0");
    return s;
}

/** 方位角 ddd°mm'ss.sss"（度 3 位、分 2 位補零；秒補零並依設定的小數位數） */
QString azimuthDms(double rad, int secDec)
{
    double deg = std::fmod(qRadiansToDegrees(rad), 360.0);
    if (deg < 0) deg += 360.0;
    const double scale = std::pow(10.0, secDec);
    double total = std::round(deg * 3600.0 * scale) / scale;     // 先捨入再拆，避免 60.000 進位問題
    if (total >= 360.0 * 3600.0) total -= 360.0 * 3600.0;
    const int d = int(std::floor(total / 3600.0));
    double rem = total - d * 3600.0;
    const int m = int(std::floor(rem / 60.0));
    const double sec = rem - m * 60.0;
    const int width = 2 + (secDec > 0 ? secDec + 1 : 0);
    return QStringLiteral("%1°%2'%3\"")
        .arg(d, 3, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(sec, width, 'f', secDec, QLatin1Char('0'));
}

/** 資料表用里程：不加前綴，小數位數依設定（km+m 時 m 部分補零） */
QString tableChainage(const TrackCenterLine* tcl, const PlanProfileSettings& s, double ch)
{
    double v = ch;
    if (s.useContChainage && tcl && tcl->horizontal() && !tcl->horizontal()->isEmpty())
        v = tcl->horizontal()->getContinuousChainage(ch);
    const int dec = s.tableDecimals;
    if (!s.kmFormat) return QString::number(v, 'f', dec);
    const bool neg = v < 0;
    const double a = std::fabs(v);
    const int km = int(std::floor(a / 1000.0));
    const double m = a - km * 1000.0;
    return QStringLiteral("%1%2+%3").arg(neg ? QStringLiteral("-") : QString())
                                    .arg(km)
                                    .arg(m, 3 + (dec > 0 ? dec + 1 : 0), 'f', dec, QLatin1Char('0'));
}

QString curveTypeText(const QString& ct)
{
    return ct.isEmpty() ? QStringLiteral("STRAIGHT") : ct;
}

QString radiusText(double r, const PlanProfileSettings& s)
{
    const int dec = s.tableDecimals;
    if (s.radiusSigned)
        return QStringLiteral("%1%2").arg(r >= 0 ? QStringLiteral("+") : QStringLiteral("-"))
                                     .arg(std::fabs(r), 0, 'f', dec);
    return QString::number(std::fabs(r), 'f', dec);
}

/** 平面資料列：來源與「線形資料表」對話框相同（rawPoints，並做相同的重複切線點／SS 合併）。 */
QVector<RowMap> buildHzRows(const TrackCenterLine* tcl, const PlanProfileSettings& s)
{
    QVector<RowMap> out;
    if (!tcl || !tcl->horizontal() || tcl->horizontal()->isEmpty()) return out;
    core::geometry::ProjectOrigin::ensureDefault();
    const int dec = s.tableDecimals;

    QVector<AlignmentPoint> pts;
    for (const AlignmentPoint& p : tcl->horizontal()->rawPoints()) {
        if (!pts.isEmpty() && p.tsc.size() == 2 && p.tsc[1] == QLatin1Char('T')
            && pts.last().tsc.size() == 2 && pts.last().tsc[1] == QLatin1Char('T')) {
            const double dx = p.easting - pts.last().easting, dy = p.northing - pts.last().northing;
            if (dx * dx + dy * dy < 1.0e-6) continue;
        }
        if (!pts.isEmpty() && p.tsc == QLatin1String("TS") && pts.last().tsc == QLatin1String("ST")
            && pts.last().length < 1.0e-6) {
            AlignmentPoint m = p;
            m.tsc = QStringLiteral("SS");
            pts.removeLast();
            pts.append(m);
            continue;
        }
        pts.append(p);
    }

    for (int i = 0; i < pts.size(); ++i) {
        const AlignmentPoint& p = pts[i];
        const bool last = (i == pts.size() - 1);
        RowMap r;
        r["pt"]    = p.tsc;
        r["name"]  = p.plat;
        r["chain"] = tableChainage(tcl, s, p.chainage);
        r["cont"]  = QString::number(p.contChainage, 'f', dec);
        const QPointF tm2 = core::geometry::ProjectOrigin::instance().toGlobal(p.easting, p.northing);
        r["e"]     = QString::number(tm2.x(), 'f', dec);
        r["n"]     = QString::number(tm2.y(), 'f', dec);
        r["az"]    = azimuthDms(p.azimuth, s.tableAzDecimals);
        if (!last) {
            r["len"] = p.length > 1e-9 ? QString::number(p.length, 'f', dec) : QStringLiteral("—");
            const bool arc = p.tsc == QLatin1String("SC") || p.tsc == QLatin1String("CC")
                          || p.tsc == QLatin1String("TC");
            if (arc) r["rad"] = radiusText(p.radius, s);
            // 曲線型式：圓弧段＝ARC；緩和曲線＝其型式（如 SPIRAL）；直線＝STRAIGHT
            r["curve"] = arc ? (p.curveType.isEmpty() ? QStringLiteral("ARC") : p.curveType)
                             : curveTypeText(p.curveType);
            // 合併欄：圓弧段顯示半徑值；緩和曲線顯示型式；直線顯示 STRAIGHT
            r["rc"] = arc ? radiusText(p.radius, s) : curveTypeText(p.curveType);
        }
        r["ccno"]  = p.circularCurveNo;
        r["cant"]  = trimNum(p.cant, dec);
        r["gauge"] = trimNum(p.gaugeWidening, dec);
        r["speed"] = trimNum(p.speedLimit, dec);
        r["text1"] = p.text1;
        r["text2"] = p.text2;
        r["real1"] = trimNum(p.real1, dec);
        r["real2"] = trimNum(p.real2, dec);
        out.append(r);
    }
    return out;
}

/**
 * 縱面資料列。TCL 縱斷面點的存法：起點、（每個豎曲線 3 點：BVC、PVI 中點、EVC）…、終點；
 * 中點 lvc>0（帶 PVI 高程、Lvc、K、Mo），BVC／EVC 為曲線兩端（lvc=0，坡度為進／出坡度）。
 * PVI 列的坡度取兩側坡度平均（對稱形豎曲線）；PVI 高程、Lvc、K、Mo 只填在 PVI 列。
 */
QVector<RowMap> buildVtRows(const TrackCenterLine* tcl, const PlanProfileSettings& s)
{
    QVector<RowMap> out;
    if (!tcl || !tcl->vertical() || tcl->vertical()->isEmpty()) return out;
    const QVector<VerticalAlignmentPoint>& P = tcl->vertical()->points();
    const int n = P.size();
    if (n < 2) return out;
    const int dec = s.tableDecimals;
    auto N = [](double v, int d) { return QString::number(v, 'f', d); };
    constexpr double kMinLvc = 0.001;

    auto mk = [&](const QString& label, double ch, double el, const QString& grade) {
        RowMap r;
        r["pt"] = label;
        r["chain"] = tableChainage(tcl, s, ch);
        r["_ch"] = QString::number(ch, 'f', 8);
        r["elev"] = N(el, dec);
        r["grade"] = grade;
        return r;
    };

    out.append(mk(QStringLiteral("起點"), P.first().chainage, P.first().elevation, N(P.first().grade, dec)));
    int i = 1;
    while (i < n - 1) {
        const bool triple = (i + 2 <= n - 2) && P[i + 1].lvc > kMinLvc && P[i].lvc <= kMinLvc
                            && P[i + 2].lvc <= kMinLvc;
        if (triple) {
            const VerticalAlignmentPoint& bvc = P[i];
            const VerticalAlignmentPoint& pvi = P[i + 1];
            const VerticalAlignmentPoint& evc = P[i + 2];
            out.append(mk(QStringLiteral("BVC"), bvc.chainage, bvc.elevation, N(bvc.grade, dec)));
            RowMap r = mk(QStringLiteral("PVI"), pvi.chainage, pvi.elevation,
                          N((bvc.grade + evc.grade) / 2.0, dec));       // 兩側坡度平均
            r["pvi"] = N(pvi.pviElevation, dec);
            r["lvc"] = N(pvi.lvc, dec);
            const double K = std::fabs(pvi.kValue) > 1e-9 ? pvi.kValue
                           : (std::fabs(evc.grade - bvc.grade) > 1e-9 ? pvi.lvc / (evc.grade - bvc.grade) : 0.0);
            r["k"] = N(K, dec);
            r["mo"] = N(pvi.mo, dec);
            out.append(r);
            out.append(mk(QStringLiteral("EVC"), evc.chainage, evc.elevation, N(evc.grade, dec)));
            i += 3;
        } else {
            // 純折點（沒有豎曲線）：以 VIP 呈現，並不另列「折點」
            out.append(mk(QStringLiteral("VIP"), P[i].chainage, P[i].elevation, N(P[i].grade, dec)));
            ++i;
        }
    }
    out.append(mk(QStringLiteral("終點"), P.last().chainage, P.last().elevation, N(P.last().grade, dec)));

    // 點間資料：與下一點的間距（BVC–PVI、PVI–EVC 各為 Lvc/2）
    for (int k = 0; k + 1 < out.size(); ++k)
        out[k]["dist"] = N(out[k + 1]["_ch"].toDouble() - out[k]["_ch"].toDouble(), dec);
    return out;
}

} // namespace

int planProfileTableRowsPerPage(const QSizeF& paper, const PlanProfileSettings& s)
{
    const PlanProfileLayout L = computePlanProfileLayout(paper, s);
    const int n = int(std::floor((L.area.height() - kDtTitleH - kDtHdrH) / kDtRowH - 0.5));
    return std::max(5, n);
}

int planProfileTableRowCount(const TrackCenterLine* tcl, int kind, const PlanProfileSettings& s)
{
    return kind == 1 ? buildHzRows(tcl, s).size() : buildVtRows(tcl, s).size();
}

namespace {
/** 資料表的一列：線形標題列，或該線形的一個資料點列（last＝該線形最後一點，沒有「點間資料」） */
struct TRow {
    bool    header = false;
    bool    last   = false;
    QString title;
    RowMap  map;
};

/** 依序接續各線形的資料列；每條線形前面插一列標題。 */
QVector<TRow> combinedTableRows(cad::Document* doc, int kind, const QStringList& ids, const PlanProfileSettings& s)
{
    QVector<TRow> out;
    if (!doc) return out;
    for (const QString& id : ids) {
        const TrackCenterLine* t = doc->findTrackCenterLine(id);
        if (!t) continue;
        const QVector<RowMap> rows = kind == 1 ? buildHzRows(t, s) : buildVtRows(t, s);
        if (rows.isEmpty()) continue;
        TRow h; h.header = true; h.title = t->name().isEmpty() ? t->id() : t->name();
        out.append(h);
        for (int i = 0; i < rows.size(); ++i) {
            TRow r; r.map = rows[i]; r.last = (i == rows.size() - 1);
            out.append(r);
        }
    }
    return out;
}

/** 分頁：每頁最多 perPage 列；標題列不孤立在頁底（移到下一頁開頭）。 */
QVector<QPair<int, int>> tablePageRanges(const QVector<TRow>& rows, int perPage)
{
    QVector<QPair<int, int>> out;
    int from = 0;
    while (from < rows.size()) {
        int to = std::min<int>(rows.size(), from + perPage);
        if (to < rows.size() && to - 1 > from && rows[to - 1].header) --to;
        out.append(qMakePair(from, to));
        from = to;
    }
    return out;
}
} // namespace

int planProfileTablePageCount(cad::Document* doc, int kind, const QStringList& ids,
                              const PlanProfileSettings& s, const QSizeF& paper)
{
    const QVector<TRow> rows = combinedTableRows(doc, kind, ids, s);
    return int(tablePageRanges(rows, planProfileTableRowsPerPage(paper, s)).size());
}

static QFont dtFont(double size, bool bold = false)
{
    QFont f(QStringLiteral("Arial"));
    f.setPointSizeF(size);
    f.setBold(bold);
    return f;
}

static void paintAlignmentTable(QPainter& painter, const DrawingSheet& sheet)
{
    const PlanProfileLayer& layer = sheet.planProfile();
    const PlanProfileSettings& s = layer.settings;
    const QSizeF paper = sheet.config().paperSizeMM();
    const PlanProfileLayout L = computePlanProfileLayout(paper, s);
    cad::Document* doc = sheet.sourceDocument();
    QStringList tids = layer.tableTclIds;
    if (tids.isEmpty() && !layer.tableTclId.isEmpty()) tids << layer.tableTclId;     // 舊版相容

    painter.save();
    painter.setBrush(Qt::NoBrush);     // 前面繪製可能留下填色筆刷；外框 drawRect 不可填滿而蓋掉文字
    painter.setPen(Qt::black);
    if (!doc || tids.isEmpty()) {
        painter.setFont(dtFont(4));
        painter.setPen(QColor(200, 0, 0));
        painter.drawText(L.area, Qt::AlignCenter, QStringLiteral("線形資料表：找不到線形"));
        painter.restore();
        return;
    }

    const bool hz = layer.tableKind == 1;
    const QVector<PlanProfileTableColumn> defs = planProfileTableColumnDefs(layer.tableKind);
    const QStringList ids = planProfileEffectiveTableColumns(s, layer.tableKind);
    QVector<PlanProfileTableColumn> cols;
    for (const QString& id : ids)
        for (const auto& d : defs) if (d.id == id) cols.append(d);
    const QVector<TRow> rows = combinedTableRows(doc, layer.tableKind, tids, s);

    const int perPage = planProfileTableRowsPerPage(paper, s);
    const QVector<QPair<int, int>> ranges = tablePageRanges(rows, perPage);
    int from = 0, to = 0;
    if (!ranges.isEmpty()) {
        const int pi = std::clamp(layer.tablePage - 1, 0, int(ranges.size()) - 1);
        from = ranges[pi].first; to = ranges[pi].second;
    }

    double wsum = 0;
    for (const auto& c : cols) wsum += c.width;
    const double k = wsum > 0 ? L.area.width() / wsum : 1.0;

    painter.setFont(dtFont(5, true));
    painter.setPen(Qt::black);
    painter.drawText(QRectF(L.area.left(), L.area.top(), L.area.width(), kDtTitleH - 2),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("%1 — %2%3")
                         .arg(hz ? QStringLiteral("平面線形資料表 HORIZONTAL ALIGNMENT DATA")
                                 : QStringLiteral("縱斷面線形資料表 VERTICAL ALIGNMENT DATA"),
                              QString(),
                              layer.tablePages > 1 ? QStringLiteral("  (%1/%2)").arg(layer.tablePage).arg(layer.tablePages)
                                                   : QString()));

    // 以字元數估算寬度（不依賴 QFontMetrics，避免與繪圖裝置 dpi 不一致）；
    // 太寬時縮小字級以確保不被截掉。
    auto cellText = [&](const QRectF& r, const QString& text, bool center, double base, bool bold) {
        if (text.isEmpty()) return;
        double w = 0;
        for (const QChar ch : text) w += (ch.unicode() >= 0x2E80 ? 1.35 : 0.78) * base;
        const double avail = r.width() - 1.6;
        const double sz = (w > avail && w > 0) ? std::max(base * 0.35, base * avail / w) : base;
        painter.setFont(dtFont(sz, bold));
        painter.setPen(Qt::black);
        painter.drawText(QRectF(r.left() + 0.8, r.top(), avail, r.height()),
                         center ? int(Qt::AlignCenter) : int(Qt::AlignRight | Qt::AlignVCenter), text);
    };

    if (rows.isEmpty() || cols.isEmpty()) {
        painter.setFont(dtFont(4));
        painter.setPen(QColor(200, 0, 0));
        painter.drawText(QRectF(L.area.left(), L.area.top() + kDtTitleH, L.area.width(), 10),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("（欄位 %1 個、資料列 %2 筆，沒有可顯示的資料）")
                             .arg(cols.size()).arg(rows.size()));
    }

    QPen thin(Qt::black, sheet.config().lineWidthThin);
    QPen med(Qt::black, sheet.config().lineWidthMedium);
    const double x0 = L.area.left(), y0 = L.area.top() + kDtTitleH;
    const int nRows = std::max(0, to - from);
    // 本頁之後還有資料時，最後一個點的「點間資料」要多半格才放得下
    const bool continues = to < rows.size() && to > from && !rows[to - 1].header && !rows[to - 1].last;
    const double tableH = kDtHdrH + (nRows + (continues ? 0.5 : 0.0)) * kDtRowH;
    const double bodyTop = y0 + kDtHdrH;

    double cx = x0;
    for (const auto& c : cols) {
        const double w = c.width * k;
        const QStringList ls = c.label.split(QLatin1Char('\n'));
        const double lh = (kDtHdrH - 1.0) / ls.size();
        for (int i = 0; i < ls.size(); ++i)
            cellText(QRectF(cx, y0 + 0.5 + i * lh, w, lh), ls[i], true, 2.6, true);
        cx += w;
    }
    // 資料：跟著點的欄位置於列中；點間資料（長度等）下移半格，位於該點與下一點之間
    for (int r = from; r < to; ++r) {
        const double y = bodyTop + (r - from) * kDtRowH;
        cx = x0;
        for (const auto& c : cols) {
            const double w = c.width * k;
            if (rows[r].header) { cx += w; continue; }
            if (!c.between)
                cellText(QRectF(cx, y, w, kDtRowH), rows[r].map.value(c.id), c.center, 2.8, false);
            else if (!rows[r].last)             // 每條線形最後一點沒有「下一點」，不顯示點間資料
                cellText(QRectF(cx, y + kDtRowH / 2.0, w, kDtRowH), rows[r].map.value(c.id), c.center, 2.8, false);
            cx += w;
        }
    }

    // 格線：點欄位每列一條；點間欄位的橫線下移半格
    painter.setPen(thin);
    cx = x0;
    for (const auto& c : cols) {
        const double w = c.width * k;
        if (!c.between) {
            for (int r = 0; r <= nRows; ++r) {
                const double y = bodyTop + r * kDtRowH;
                painter.drawLine(QPointF(cx, y), QPointF(cx + w, y));
            }
        } else {
            // 點間欄位：每個有「下一點」的資料列，其點間儲存格（下移半格）上下各一條橫線
            for (int r = from; r < to; ++r) {
                if (rows[r].header || rows[r].last) continue;
                const double y = bodyTop + (r - from + 0.5) * kDtRowH;
                painter.drawLine(QPointF(cx, y), QPointF(cx + w, y));
                painter.drawLine(QPointF(cx, y + kDtRowH), QPointF(cx + w, y + kDtRowH));
            }
        }
        cx += w;
    }
    cx = x0;
    for (int ci = 0; ci <= cols.size(); ++ci) {
        painter.drawLine(QPointF(cx, y0), QPointF(cx, y0 + tableH));
        if (ci < cols.size()) cx += cols[ci].width * k;
    }
    // 線形標題列：整列合併、淺灰底、靠左粗體
    for (int r = from; r < to; ++r) {
        if (!rows[r].header) continue;
        const QRectF hr(x0, bodyTop + (r - from) * kDtRowH, L.area.width(), kDtRowH);
        painter.setPen(thin);
        painter.setBrush(QColor(225, 225, 225));
        painter.drawRect(hr);
        painter.setBrush(Qt::NoBrush);
        painter.setFont(dtFont(3.2, true));
        painter.setPen(Qt::black);
        painter.drawText(hr.adjusted(1.5, 0, -1.5, 0), Qt::AlignLeft | Qt::AlignVCenter,
                         rows[r].title);
    }
    painter.setPen(med);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(x0, y0, L.area.width(), tableH));
    painter.drawLine(QPointF(x0, bodyTop), QPointF(x0 + L.area.width(), bodyTop));
    painter.restore();
}


// ─────────────────────────────────────────────────────────────────────────────
// 簡圖（軌道示意／平面簡圖／縱面簡圖）
// ─────────────────────────────────────────────────────────────────────────────

namespace {

double schEstW(const QString& text, double sz)
{
    double w = 0;
    for (const QChar ch : text) w += (ch.unicode() >= 0x2E80 ? 1.35 : 0.78) * sz;
    return w;
}

struct SchItem {
    QString id;
    const TrackCenterLine* t = nullptr;
    double a = 0, b = 0;      // 里程範圍
    double x0 = 0, x1 = 0;    // 圖上水平範圍（等長）
    int band = 0;
    double X(double ch) const { return b - a > 1e-9 ? x0 + (ch - a) / (b - a) * (x1 - x0) : x0; }
};

struct SchEl { double c0 = 0, c1 = 0; int type = 0; double R = 0, cant = 0; double l0 = 0, l1 = 0; };

QVector<AlignmentPoint> schHzPoints(const TrackCenterLine* tcl)
{
    QVector<AlignmentPoint> pts;
    if (!tcl || !tcl->horizontal() || tcl->horizontal()->isEmpty()) return pts;
    for (const AlignmentPoint& p : tcl->horizontal()->rawPoints()) {
        if (!pts.isEmpty() && p.tsc.size() == 2 && p.tsc[1] == QLatin1Char('T')
            && pts.last().tsc.size() == 2 && pts.last().tsc[1] == QLatin1Char('T')) {
            const double dx = p.easting - pts.last().easting, dy = p.northing - pts.last().northing;
            if (dx * dx + dy * dy < 1.0e-6) continue;
        }
        if (!pts.isEmpty() && p.tsc == QLatin1String("TS") && pts.last().tsc == QLatin1String("ST")
            && pts.last().length < 1.0e-6) {
            AlignmentPoint m = p;
            m.tsc = QStringLiteral("SS");
            pts.removeLast();
            pts.append(m);
            continue;
        }
        pts.append(p);
    }
    return pts;
}

struct SchRect { double l, t, r, b; };
bool schHit(const SchRect& a, const SchRect& b)
{
    return !(a.r <= b.l || b.r <= a.l || a.b <= b.t || b.b <= a.t);
}

} // namespace

static void paintSchematic(QPainter& painter, const DrawingSheet& sheet)
{
    const PlanProfileLayer& layer = sheet.planProfile();
    const PlanProfileSettings& s = layer.settings;
    const QSizeF paper = sheet.config().paperSizeMM();
    const PlanProfileLayout L = computePlanProfileLayout(paper, s);
    cad::Document* doc = sheet.sourceDocument();
    const SheetConfig& cfg = sheet.config();

    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(Qt::black);

    const int nb = layer.schemaBands.size();
    if (!doc || nb == 0) {
        painter.setFont(dtFont(4));
        painter.setPen(QColor(200, 0, 0));
        painter.drawText(L.area, Qt::AlignCenter, QStringLiteral("簡圖：沒有可繪製的線形串"));
        painter.restore();
        return;
    }

    const double fs = 2.4;
    const QPen thin(Qt::black, cfg.lineWidthThin), med(Qt::black, cfg.lineWidthMedium),
               thick(Qt::black, cfg.lineWidthThick);
    QPen guide(QColor(150, 150, 150), cfg.lineWidthThin * 0.8, Qt::DotLine);

    // 標題
    QStringList names;
    for (const SchemaBand& b : layer.schemaBands) names << b.name;
    constexpr double kTitleH = 10.0;
    painter.setFont(dtFont(5, true));
    painter.drawText(QRectF(L.area.left(), L.area.top(), L.area.width(), kTitleH - 2),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("簡圖 SCHEMATIC — %1%2").arg(names.join(QStringLiteral(" / ")),
                         layer.tablePages > 1 ? QStringLiteral("  (%1/%2)").arg(layer.tablePage).arg(layer.tablePages)
                                              : QString()));

    // 三列
    const double top = L.area.top() + kTitleH;
    const double H = L.area.height() - kTitleH;
    const double frac[6] = { 0.17, 0.19, 0.09, 0.09, 0.337, 0.09 };
    double rh[6], rt[6];
    { double y = top; for (int i = 0; i < 6; ++i) { rh[i] = H * frac[i]; rt[i] = y; y += rh[i]; } }
    static const char* kCap[6] = { "軌道簡易示意圖", "平面線形簡圖", "結構型式及軌道支承系統",
                                   "耐磨鋼軌", "縱面線形簡圖", "導電軌" };
    for (int r = 0; r < 6; ++r) {
        painter.setPen(med);
        painter.drawRect(QRectF(L.area.left(), rt[r], L.area.width(), rh[r]));
        painter.setPen(Qt::black);
        painter.setFont(dtFont(2.6, true));
        painter.drawText(QRectF(L.area.left() + 1.0, rt[r] + 0.5, 60, 4.5), Qt::AlignLeft | Qt::AlignVCenter,
                         QString::fromUtf8(kCap[r]));
    }

    // 項目配置（等長）
    const double pad = 16.0;
    const double ux0 = L.area.left() + pad;
    const double uw = L.area.width() - 2 * pad;
    QVector<SchItem> items;
    QHash<QString, int> itemOf;
    QVector<double> guideX;
    for (int b = 0; b < nb; ++b) {
        const QStringList& ids = layer.schemaBands[b].tclIds;
        QVector<SchItem> bi;
        double total = 0;
        for (const QString& id : ids) {
            SchItem it;
            it.id = id;
            it.t = doc->findTrackCenterLine(id);
            it.band = b;
            if (!it.t || !planProfileChainageRange(it.t, it.a, it.b)) continue;
            total += std::max(0.0, it.b - it.a);
            bi.append(it);
        }
        // 本頁內各項依其長度佔本串本頁總長的比例分配寬度
        double cum = 0;
        for (SchItem& it : bi) {
            const double len = std::max(0.0, it.b - it.a);
            it.x0 = ux0 + (total > 1e-9 ? cum / total : 0.0) * uw;
            cum += len;
            it.x1 = ux0 + (total > 1e-9 ? cum / total : 1.0) * uw;
            itemOf.insert(it.id, items.size());
            items.append(it);
            guideX << it.x0 << it.x1;
        }
    }
    // 項目分界導引線（貫穿各列）
    painter.setPen(guide);
    QVector<double> drawn;
    for (double x : guideX) {
        bool dup = false;
        for (double d : drawn) if (std::fabs(d - x) < 0.01) { dup = true; break; }
        if (dup) continue;
        drawn << x;
        painter.drawLine(QPointF(x, rt[0]), QPointF(x, rt[5] + rh[5]));
    }

    // ───── 第一列：軌道示意圖 ─────
    {
        const double sp = rh[0] / (nb + 1);
        auto yb = [&](int b) { return rt[0] + sp * (b + 1); };
        const double leader = std::min(6.0, sp * 0.5);
        const double stub = std::min(12.0, sp * 0.55);
        for (const SchItem& it : items) {
            const double y = yb(it.band);
            const int dir = it.band < nb / 2.0 ? -1 : 1;
            painter.setPen(thick);
            painter.drawLine(QPointF(it.x0, y), QPointF(it.x1, y));
            painter.setPen(thin);
            painter.setFont(dtFont(fs));
            const QString sa = planProfileFormatChainage(it.t, s, it.a);
            const QString sb = planProfileFormatChainage(it.t, s, it.b);
            painter.drawLine(QPointF(it.x0, y), QPointF(it.x0, y + dir * leader));
            painter.drawLine(QPointF(it.x1, y), QPointF(it.x1, y + dir * leader));
            drawTextAt(painter, QPointF(it.x0 + 0.8, y + dir * leader), sa, Qt::AlignLeft | Qt::AlignVCenter, fs);
            drawTextAt(painter, QPointF(it.x1 - 0.8, y + dir * leader), sb, Qt::AlignRight | Qt::AlignVCenter, fs);
            painter.setFont(dtFont(fs * 1.2, true));
            drawTextAt(painter, QPointF((it.x0 + it.x1) / 2, y - dir * 1.0), it.t->name(),
                       Qt::AlignHCenter | (dir < 0 ? Qt::AlignTop : Qt::AlignBottom), fs * 1.2);
        }
        painter.setFont(dtFont(fs));
        for (const SchemaLink& k : layer.schemaLinks) {
            if (!itemOf.contains(k.aTcl)) continue;
            const SchItem& A = items[itemOf[k.aTcl]];
            const double xa = A.X(k.aCh), ya = yb(A.band);
            painter.setPen(med);
            if (k.kind == 1 && itemOf.contains(k.bTcl)) {
                const SchItem& B = items[itemOf[k.bTcl]];
                const double xb = B.X(k.bCh), yb2 = yb(B.band);
                painter.drawLine(QPointF(xa, ya), QPointF(xb, yb2));
                painter.setPen(Qt::black);
                drawTextAt(painter, QPointF((xa + xb) / 2 + 1.0, (ya + yb2) / 2), k.name,
                           Qt::AlignLeft | Qt::AlignVCenter, fs);
            } else {
                const QPointF e(xa + k.dir * stub * 0.5, ya - k.side * stub);
                painter.drawLine(QPointF(xa, ya), e);
                painter.setPen(Qt::black);
                drawTextAt(painter, e + QPointF(0, k.side > 0 ? -0.6 : 0.6), k.name,
                           Qt::AlignHCenter | (k.side > 0 ? Qt::AlignBottom : Qt::AlignTop), fs);
            }
        }
    }

    // ───── 第二列：平面線形簡圖 ─────
    {
        const double subH = rh[1] / nb;
        const double amp = std::min(5.0, subH * 0.14);
        for (int b = 0; b < nb; ++b) {
            const double sTop = rt[1] + subH * b, sBot = sTop + subH, yc = sTop + subH / 2;
            painter.setPen(Qt::black);
            painter.setFont(dtFont(2.4, true));
            painter.drawText(QRectF(L.area.left() + 1.0, yc - 2.5, pad - 1.5, 5.0),
                             Qt::AlignLeft | Qt::AlignVCenter, layer.schemaBands[b].name);
            if (b > 0) { painter.setPen(guide); painter.drawLine(QPointF(L.area.left(), sTop), QPointF(L.area.right(), sTop)); }
            for (const SchItem& it : items) {
                if (it.band != b) continue;
                const QVector<AlignmentPoint> pts = schHzPoints(it.t);
                QVector<SchEl> el;
                for (int i = 0; i + 1 < pts.size(); ++i) {
                    const AlignmentPoint& p = pts[i];
                    SchEl e;
                    e.c0 = std::max(p.chainage, it.a);
                    e.c1 = std::min(pts[i + 1].chainage, it.b);
                    if (e.c1 - e.c0 < 1e-6) continue;
                    const bool arc = p.tsc == QLatin1String("SC") || p.tsc == QLatin1String("CC")
                                  || p.tsc == QLatin1String("TC");
                    const bool spi = p.tsc == QLatin1String("TS") || p.tsc == QLatin1String("CS")
                                  || p.tsc == QLatin1String("SS");
                    e.type = arc ? 1 : (spi ? 2 : 0);
                    e.R = p.radius; e.cant = std::fabs(p.cant);
                    if (arc) e.l0 = e.l1 = (p.radius > 0 ? amp : -amp);
                    el.append(e);
                }
                for (int i = 0; i < el.size(); ++i) {
                    if (el[i].type != 2) continue;
                    el[i].l0 = i > 0 ? el[i - 1].l1 : 0.0;
                    double nx = 0.0;
                    for (int j = i + 1; j < el.size(); ++j) if (el[j].type != 2) { nx = el[j].l0; break; }
                    el[i].l1 = nx;
                }
                // 中心參考線
                painter.setPen(guide);
                painter.drawLine(QPointF(it.x0, yc), QPointF(it.x1, yc));
                painter.setPen(med);
                QPolygonF poly;
                for (const SchEl& e : el) {
                    poly << QPointF(it.X(e.c0), yc + e.l0) << QPointF(it.X(e.c1), yc + e.l1);
                }
                if (poly.size() >= 2) painter.drawPolyline(poly);
                else painter.drawLine(QPointF(it.x0, yc), QPointF(it.x1, yc));

                QVector<SchRect> placed;
                painter.setPen(Qt::black);
                // 圓曲線 R、C
                for (const SchEl& e : el) {
                    if (e.type != 1) continue;
                    const QString txt = QStringLiteral("R=%1  C=%2").arg(radiusText(e.R, s), trimNum(e.cant, 1));
                    const double span = it.X(e.c1) - it.X(e.c0);
                    const double w0 = schEstW(txt, fs);
                    const double sz = (w0 > span - 1.0 && w0 > 0) ? std::max(1.2, fs * (span - 1.0) / w0) : fs;
                    const double xm = (it.X(e.c0) + it.X(e.c1)) / 2;
                    const bool below = e.l0 > 0;
                    const double ty = yc + e.l0 + (below ? 0.8 : -0.8);
                    painter.setFont(dtFont(sz));
                    drawTextAt(painter, QPointF(xm, ty), txt,
                               Qt::AlignHCenter | (below ? Qt::AlignTop : Qt::AlignBottom), sz);
                    const double w = schEstW(txt, sz), h = sz * 1.3;
                    placed.append({xm - w / 2, below ? ty : ty - h, xm + w / 2, below ? ty + h : ty});
                }
                // 線形變化點里程（垂直引線、水平文字）
                painter.setFont(dtFont(fs));
                const double lens[4] = { 5.0, 5.0, 9.0, 9.0 };
                const int dirs[4] = { -1, 1, -1, 1 };
                for (int i = 1; i < el.size(); ++i) {
                    const double c = el[i].c0;
                    if (c <= it.a + 1e-6 || c >= it.b - 1e-6) continue;
                    const double x = it.X(c), y = yc + el[i].l0;
                    const QString txt = planProfileFormatChainage(it.t, s, c);
                    const double w = schEstW(txt, fs), h = fs * 1.3;
                    int pick = -1;
                    SchRect pr{};
                    for (int q = 0; q < 4 && pick < 0; ++q) {
                        const double ye = y + dirs[q] * lens[q];
                        if (ye - h / 2 < sTop + 0.8 || ye + h / 2 > sBot - 0.8) continue;
                        const SchRect r{ x + 0.4, ye - h / 2, x + 0.8 + w, ye + h / 2 };
                        bool bad = false;
                        for (const SchRect& o : placed) if (schHit(r, o)) { bad = true; break; }
                        if (!bad) { pick = q; pr = r; }
                    }
                    if (pick < 0) {
                        for (int q = 0; q < 4; ++q) {
                            const double ye = y + dirs[q] * lens[q];
                            if (ye - h / 2 >= sTop + 0.8 && ye + h / 2 <= sBot - 0.8) {
                                pick = q; pr = SchRect{ x + 0.4, ye - h / 2, x + 0.8 + w, ye + h / 2 }; break;
                            }
                        }
                    }
                    if (pick < 0) continue;
                    placed.append(pr);
                    painter.setPen(thin);
                    painter.drawLine(QPointF(x, y), QPointF(x, y + dirs[pick] * lens[pick]));
                    painter.setPen(Qt::black);
                    drawTextAt(painter, QPointF(x + 0.8, y + dirs[pick] * lens[pick]), txt,
                               Qt::AlignLeft | Qt::AlignVCenter, fs);
                }
            }
        }
    }

    // ───── 第三列：縱面線形簡圖 ─────
    {
        const double subH = rh[4] / nb;
        constexpr double kMinLvc = 0.001;
        for (int b = 0; b < nb; ++b) {
            const double sTop = rt[4] + subH * b, sBot = sTop + subH, yc = sTop + subH / 2;
            painter.setPen(Qt::black);
            painter.setFont(dtFont(2.4, true));
            painter.drawText(QRectF(L.area.left() + 1.0, yc - 2.5, pad - 1.5, 5.0),
                             Qt::AlignLeft | Qt::AlignVCenter, layer.schemaBands[b].name);
            if (b > 0) { painter.setPen(guide); painter.drawLine(QPointF(L.area.left(), sTop), QPointF(L.area.right(), sTop)); }

            struct Vc { double bCh, bEl, bGr, pCh, pEl, pGr, eCh, eEl, eGr; };
            struct Line { const SchItem* it; QVector<QPointF> v; QVector<Vc> vcs; };   // v: (ch, elev)
            QVector<Line> lines;
            double emin = 1e18, emax = -1e18;
            auto upd = [&](double e) { emin = std::min(emin, e); emax = std::max(emax, e); };
            for (const SchItem& it : items) {
                if (it.band != b) continue;
                const TrackCenterLine* t = it.t;
                if (!t->vertical() || t->vertical()->isEmpty()) continue;
                const QVector<VerticalAlignmentPoint>& P = t->vertical()->points();
                const int n = P.size();
                if (n < 2) continue;
                Line ln; ln.it = &it;
                ln.v << QPointF(it.a, t->getElevationForCalc(it.a));
                int i = 1;
                while (i < n - 1) {
                    const bool triple = (i + 2 <= n - 2) && P[i + 1].lvc > kMinLvc && P[i].lvc <= kMinLvc
                                        && P[i + 2].lvc <= kMinLvc;
                    if (triple) {
                        const auto& bv = P[i]; const auto& pv = P[i + 1]; const auto& ev = P[i + 2];
                        if (pv.chainage > it.a && pv.chainage < it.b) {
                            ln.v << QPointF(pv.chainage, pv.pviElevation);
                            ln.vcs.append({ bv.chainage, bv.elevation, bv.grade, pv.chainage, pv.pviElevation,
                                            (bv.grade + ev.grade) / 2.0, ev.chainage, ev.elevation, ev.grade });
                        }
                        i += 3;
                    } else {
                        if (P[i].chainage > it.a && P[i].chainage < it.b) ln.v << QPointF(P[i].chainage, P[i].elevation);
                        ++i;
                    }
                }
                ln.v << QPointF(it.b, t->getElevationForCalc(it.b));
                for (const QPointF& q : ln.v) upd(q.y());
                lines.append(ln);
            }
            if (lines.isEmpty()) continue;
            const double hLine = subH * 0.12;
            auto yOf = [&](double el) {
                return emax - emin > 1e-6 ? yc + hLine / 2 - (el - emin) / (emax - emin) * hLine : yc;
            };
            for (const Line& ln : lines) {
                const SchItem& it = *ln.it;
                QPolygonF poly;
                for (const QPointF& q : ln.v) poly << QPointF(it.X(q.x()), yOf(q.y()));
                painter.setPen(med);
                painter.drawPolyline(poly);
                const double rmax = 0.5;
                for (const Vc& v : ln.vcs) {
                    const bool crest = v.bGr > v.eGr;
                    const int dir = crest ? -1 : 1;
                    const double xb = it.X(v.bCh), xp = it.X(v.pCh), xe = it.X(v.eCh);
                    const double yB = yOf(v.bEl), yP = yOf(v.pEl), yE = yOf(v.eEl);
                    painter.setBrush(Qt::black);
                    painter.setPen(Qt::NoPen);
                    painter.drawEllipse(QPointF(xb, yB), rmax, rmax);
                    painter.drawEllipse(QPointF(xe, yE), rmax, rmax);
                    painter.drawEllipse(QPointF(xp, yP), rmax, rmax);
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(thin);
                    // BVC／EVC：垂直引線、水平文字
                    auto hText = [&](double x, double y, const QString& tag, double ch, double el, double gr, bool left) {
                        const double len = 6.0;
                        double ye = y + dir * len;
                        const double lineH = fs * 1.25;
                        ye = std::max(sTop + 1.5 + lineH * 1.5, std::min(sBot - 1.5 - lineH * 1.5, ye));
                        painter.setPen(thin);
                        painter.drawLine(QPointF(x, y), QPointF(x, ye));
                        painter.setPen(Qt::black);
                        painter.setFont(dtFont(fs));
                        const QString l1 = QStringLiteral("%1 %2").arg(tag, planProfileFormatChainage(it.t, s, ch));
                        const QString l2 = QStringLiteral("EL.%1").arg(el, 0, 'f', 3);
                        const QString l3 = QStringLiteral("G=%1%2%").arg(gr >= 0 ? QStringLiteral("+") : QString())
                                                                    .arg(gr, 0, 'f', 2);
                        const Qt::Alignment al = (left ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter;
                        const double xt = left ? x - 0.8 : x + 0.8;
                        drawTextAt(painter, QPointF(xt, ye - lineH), l1, al, fs);
                        drawTextAt(painter, QPointF(xt, ye), l2, al, fs);
                        drawTextAt(painter, QPointF(xt, ye + lineH), l3, al, fs);
                    };
                    hText(xb, yB, QStringLiteral("BVC"), v.bCh, v.bEl, v.bGr, true);
                    hText(xe, yE, QStringLiteral("EVC"), v.eCh, v.eEl, v.eGr, false);
                    // PVI：垂直引線、垂直文字
                    {
                        const double leader = 2.5;
                        painter.setPen(thin);
                        painter.drawLine(QPointF(xp, yP), QPointF(xp, yP + dir * leader));
                        const QString t1 = QStringLiteral("PVI %1").arg(planProfileFormatChainage(it.t, s, v.pCh));
                        const QString t2 = QStringLiteral("EL.%1").arg(v.pEl, 0, 'f', 3);
                        const QString t3 = QStringLiteral("G=%1%2%").arg(v.pGr >= 0 ? QStringLiteral("+") : QString())
                                                                     .arg(v.pGr, 0, 'f', 2);
                        const double room = (dir < 0 ? (yP - leader - sTop) : (sBot - yP - leader)) - 1.0;
                        double mx = 0;
                        for (const QString& q : {t1, t2, t3}) mx = std::max(mx, schEstW(q, 1.0));
                        const double sz = std::max(1.1, std::min(fs, room / std::max(1.0, mx)));
                        const double lineH = sz * 1.25;
                        painter.setPen(Qt::black);
                        painter.setFont(dtFont(sz));
                        const double yo = yP + dir * leader;
                        const QString tx[3] = { t1, t2, t3 };
                        for (int k = 0; k < 3; ++k) {
                            painter.save();
                            painter.translate(xp + (k - 1) * lineH, yo);
                            painter.rotate(-90.0);
                            const double H2 = sz * 1.6;
                            if (dir < 0) painter.drawText(QRectF(0, -H2 / 2, 120, H2), Qt::AlignLeft | Qt::AlignVCenter, tx[k]);
                            else         painter.drawText(QRectF(-120, -H2 / 2, 120, H2), Qt::AlignRight | Qt::AlignVCenter, tx[k]);
                            painter.restore();
                        }
                    }
                }
            }
        }
    }
    painter.restore();
}


// ─────────────────────────────────────────────────────────────────────────────
// 機廠平面圖（矩形視圖）
// ─────────────────────────────────────────────────────────────────────────────

QSizeF planProfileDepotMaxSize(const QSizeF& paper, const PlanProfileSettings& s, double scale)
{
    const PlanProfileLayout L = computePlanProfileLayout(paper, s);
    constexpr double kTitleH = 10.0, kMargin = 6.0;
    const double aw = L.area.width() - 2 * kMargin, ah = L.area.height() - kTitleH - kMargin;
    const double m = scale / 1000.0;     // 圖上 1 mm ＝ 實地 scale/1000 m
    return QSizeF(aw * m, ah * m);
}

static QSizeF depotPaperOf(const PlanProfileSettings& s)
{
    SheetConfig cfg;
    cfg.paperSize   = static_cast<PaperSize>(s.paperSize);
    cfg.orientation = static_cast<PaperOrientation>(s.orientation);
    return cfg.paperSizeMM();
}

QString planProfileDepotSizeHint(const PlanProfileSettings& s)
{
    const QSizeF paper = depotPaperOf(s);
    QStringList lines;
    lines << QStringLiteral("圖紙 %1×%2 mm；一張圖可放的矩形視圖最大（寬×高，實地 m）：")
                 .arg(paper.width(), 0, 'f', 0).arg(paper.height(), 0, 'f', 0);
    for (double sc : { 200.0, 250.0, 500.0, 1000.0, 2000.0 }) {
        const QSizeF m = planProfileDepotMaxSize(paper, s, sc);
        lines << QStringLiteral("  1:%1 → %2 × %3 m").arg(sc, 0, 'f', 0).arg(m.width(), 0, 'f', 0).arg(m.height(), 0, 'f', 0);
    }
    lines << QStringLiteral("（比例填 0＝自動縮放配合圖紙；指定比例時超出的部分會被裁切。）");
    return lines.join(QLatin1Char('\n'));
}

double planProfileDepotFitScale(const PlanProfileSettings& s, double w, double h)
{
    const QSizeF paper = depotPaperOf(s);
    for (double sc : { 100.0, 200.0, 250.0, 500.0, 1000.0, 2000.0, 5000.0 }) {
        const QSizeF m = planProfileDepotMaxSize(paper, s, sc);
        if (w <= m.width() && h <= m.height()) return sc;
    }
    return 0.0;
}

double planProfileDepotAngle(cad::Document* doc, const DepotView& V)
{
    double th = 0.0;
    if (doc && !V.alignTcl.isEmpty()) {
        if (const TrackCenterLine* at = doc->findTrackCenterLine(V.alignTcl)) {
            double a = 0, b = 0;
            if (planProfileChainageRange(at, a, b)) {
                double ch = std::max(a, std::min(b, V.alignCh));
                double c2 = ch + 0.5;
                if (c2 > b) { c2 = ch; ch = ch - 0.5; }
                const QPointF p0 = at->getXYForCalc(ch), p1 = at->getXYForCalc(c2);
                th = std::atan2(p1.y() - p0.y(), p1.x() - p0.x());
            }
        }
    }
    return th + qDegreesToRadians(V.rotDeg);
}

static void paintDepotView(QPainter& painter, const DrawingSheet& sheet)
{
    const PlanProfileLayer& layer = sheet.planProfile();
    const DepotView& V = layer.depotView;
    const PlanProfileSettings& s = layer.settings;
    const QSizeF paper = sheet.config().paperSizeMM();
    const PlanProfileLayout L = computePlanProfileLayout(paper, s);
    cad::Document* doc = sheet.sourceDocument();
    const SheetConfig& cfg = sheet.config();

    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(Qt::black);
    if (!doc || !(V.w > 0) || !(V.h > 0)) {
        painter.setFont(dtFont(4));
        painter.setPen(QColor(200, 0, 0));
        painter.drawText(L.area, Qt::AlignCenter, QStringLiteral("機廠平面圖：視圖大小無效"));
        painter.restore();
        return;
    }
    core::geometry::ProjectOrigin::ensureDefault();
    const QPointF c = core::geometry::ProjectOrigin::instance().toLocal(V.e, V.n);

    const double th = planProfileDepotAngle(doc, V);
    const double ex = std::cos(th), ey = std::sin(th);     // 視圖 X 軸（模型座標）
    const double fx = -ey, fy = ex;                        // 視圖 Y 軸
    auto toView = [&](const QPointF& p) {
        const double dx = p.x() - c.x(), dy = p.y() - c.y();
        return QPointF(dx * ex + dy * ey, dx * fx + dy * fy);
    };

    // 圖紙上的矩形：預設配合圖紙；指定比例時依比例（超出就裁切）
    constexpr double kTitleH = 10.0, kMargin = 6.0;
    const QRectF avail(L.area.left() + kMargin, L.area.top() + kTitleH,
                       L.area.width() - 2 * kMargin, L.area.height() - kTitleH - kMargin);
    double k = std::min(avail.width() / V.w, avail.height() / V.h);       // mm / m
    if (V.scale > 0) k = 1000.0 / V.scale;
    const double fw = std::min(V.w * k, avail.width()), fh = std::min(V.h * k, avail.height());
    const QRectF frame(avail.center().x() - fw / 2, avail.center().y() - fh / 2, fw, fh);
    const double scaleDen = 1000.0 / k;
    auto toPaper = [&](const QPointF& v) {
        return QPointF(frame.center().x() + v.x() * k, frame.center().y() - v.y() * k);
    };
    const double halfW = fw / k / 2, halfH = fh / k / 2;     // 可見範圍（m）

    // 標題
    painter.setFont(dtFont(5, true));
    painter.drawText(QRectF(L.area.left(), L.area.top(), L.area.width(), kTitleH - 2),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("機廠平面圖 DEPOT PLAN — %1    1:%2")
                         .arg(V.name, QString::number(scaleDen, 'f', 0)));

    painter.setPen(QPen(Qt::black, cfg.lineWidthMedium));
    painter.drawRect(frame);

    painter.save();
    painter.setClipRect(frame);
    const QPen trackPen(Qt::black, cfg.lineWidthMedium);
    const QPen tickPen(Qt::black, cfg.lineWidthThin);
    const double margin = 5.0;
    auto inView = [&](const QPointF& v, double m) {
        return std::fabs(v.x()) <= halfW + m && std::fabs(v.y()) <= halfH + m;
    };
    const double textH = 2.4;
    for (const TrackCenterLine* t : doc->trackCenterLines()) {
        double a = 0, b = 0;
        if (!t || !planProfileChainageRange(t, a, b) || !(b - a > 1e-6)) continue;
        // 粗掃：沒有任何點靠近視圖就略過
        bool any = false;
        const double coarse = std::max(5.0, std::min(halfW, halfH) / 2);
        for (double x = a; x < b + coarse && !any; x += coarse)
            any = inView(toView(t->getXYForCalc(std::min(x, b))), coarse + margin);
        if (!any) continue;

        QVector<QPointF> inside;                // 可見範圍內的取樣點（紙面座標），供標註用
        QVector<double> insideCh;
        QPolygonF run;
        auto flush = [&]() {
            if (run.size() >= 2) { painter.setPen(trackPen); painter.drawPolyline(run); }
            run.clear();
        };
        const double step = 1.0;
        for (double x = a;; x += step) {
            const double ch = std::min(x, b);
            const QPointF v = toView(t->getXYForCalc(ch));
            if (inView(v, margin)) {
                const QPointF pp = toPaper(v);
                run << pp;
                if (inView(v, 0.0)) { inside << pp; insideCh << ch; }
            } else {
                flush();
            }
            if (ch >= b) break;
        }
        flush();
        if (inside.size() < 2) continue;

        // 里程刻度：每 50 m 一小刻度、每 100 m 標里程（圖上間距夠大時）
        const bool labelCh = k * 100.0 >= 14.0;
        painter.setFont(dtFont(textH * 0.85));
        for (double m = std::ceil(insideCh.first() / 50.0) * 50.0; m <= insideCh.last() + 1e-6; m += 50.0) {
            const QPointF p0 = toPaper(toView(t->getXYForCalc(m)));
            const QPointF p1 = toPaper(toView(t->getXYForCalc(std::min(m + 0.5, b))));
            double dx = p1.x() - p0.x(), dy = p1.y() - p0.y();
            const double len = std::hypot(dx, dy);
            if (len < 1e-9) continue;
            dx /= len; dy /= len;
            const QPointF nrm(-dy, dx);
            const bool major = std::fabs(std::fmod(m, 100.0)) < 1e-6;
            painter.setPen(tickPen);
            painter.drawLine(p0 - nrm * (major ? 1.2 : 0.7), p0 + nrm * (major ? 1.2 : 0.7));
            if (major && labelCh) {
                painter.setPen(Qt::black);
                double ang = qRadiansToDegrees(std::atan2(dy, dx));
                if (ang > 90) ang -= 180; else if (ang < -90) ang += 180;
                drawParallelText(painter, p0 + nrm * 2.6, ang, 0.0, planProfileFormatChainage(t, s, m), textH * 0.85);
            }
        }
        // 線形名稱：標在可見段中點，沿線方向
        const int mi = inside.size() / 2;
        const QPointF q0 = inside[std::max(0, mi - 1)], q1 = inside[std::min<int>(inside.size() - 1, mi + 1)];
        double ang = qRadiansToDegrees(std::atan2(q1.y() - q0.y(), q1.x() - q0.x()));
        if (ang > 90) ang -= 180; else if (ang < -90) ang += 180;
        painter.setPen(Qt::black);
        painter.setFont(dtFont(textH * 1.2, true));
        drawParallelText(painter, inside[mi], ang, -2.2, t->name(), textH * 1.2);
    }
    painter.restore();

    // ── Key Plan：標題欄內（圖名下方、資料格上方的空白處）；方向＝本頁矩形視圖對齊圖紙水平方向 ──
    {
        QVector<DepotView> all = s.depotViews;
        std::stable_sort(all.begin(), all.end(), [](const DepotView& a, const DepotView& b) { return a.order < b.order; });
        const int cur = layer.tablePage - 1;
        const double tbX = paper.width() - 10.0 - 60.0;               // 標題欄（見 paintTitleBlock）
        const double topY = 10.0 + 22.0 + 34.0 + 2.0;                  // 公司＋圖名之下
        const double botY = paper.height() - 10.0 - 98.0 - 2.0;        // 資料格之上
        const double kh = std::min(48.0, botY - topY);
        if (!all.isEmpty() && cur >= 0 && cur < all.size() && kh >= 20.0) {
            const QRectF box(tbX + 2.0, topY, 56.0, kh);
            const DepotView& cv = all[cur];
            const QPointF c0 = core::geometry::ProjectOrigin::instance().toLocal(cv.e, cv.n);
            const double a0 = planProfileDepotAngle(doc, cv);
            const double rx = std::cos(a0), ry = std::sin(a0);
            // 模型座標 → 以本頁視圖中心為原點、X 軸沿視圖方向的座標
            auto rot = [&](const QPointF& q) {
                const double dx = q.x() - c0.x(), dy = q.y() - c0.y();
                return QPointF(dx * rx + dy * ry, -dx * ry + dy * rx);
            };
            struct Rc { QPointF p[4]; QPointF mid; double minSide; };
            QVector<Rc> rcs;
            double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
            double mx0 = 1e18, my0 = 1e18, mx1 = -1e18, my1 = -1e18;     // 模型座標範圍（過濾線形用）
            for (const DepotView& dv : all) {
                const QPointF cc = core::geometry::ProjectOrigin::instance().toLocal(dv.e, dv.n);
                const double a2 = planProfileDepotAngle(doc, dv);
                const double ux = std::cos(a2), uy = std::sin(a2);
                Rc r;
                const double sx[4] = { -1, 1, 1, -1 }, sy[4] = { -1, -1, 1, 1 };
                for (int i = 0; i < 4; ++i) {
                    const double u = sx[i] * dv.w / 2, v = sy[i] * dv.h / 2;
                    const QPointF m(cc.x() + u * ux - v * uy, cc.y() + u * uy + v * ux);
                    r.p[i] = rot(m);
                    x0 = std::min(x0, r.p[i].x()); x1 = std::max(x1, r.p[i].x());
                    y0 = std::min(y0, r.p[i].y()); y1 = std::max(y1, r.p[i].y());
                    mx0 = std::min(mx0, m.x()); mx1 = std::max(mx1, m.x());
                    my0 = std::min(my0, m.y()); my1 = std::max(my1, m.y());
                }
                r.mid = rot(cc);
                r.minSide = std::min(dv.w, dv.h);
                rcs.append(r);
            }
            const double bw = std::max(1.0, x1 - x0), bh = std::max(1.0, y1 - y0);
            const double k2 = std::min((box.width() - 6.0) / bw, (box.height() - 9.0) / bh);
            const QPointF mc((x0 + x1) / 2, (y0 + y1) / 2);
            const QPointF bc(box.center().x(), box.center().y() + 2.0);
            auto mp = [&](const QPointF& q) { return QPointF(bc.x() + (q.x() - mc.x()) * k2, bc.y() - (q.y() - mc.y()) * k2); };

            painter.save();
            painter.setPen(QPen(Qt::black, cfg.lineWidthThin));
            painter.setBrush(Qt::white);
            painter.drawRect(box);
            painter.setBrush(Qt::NoBrush);
            painter.setClipRect(box.adjusted(0.3, 0.3, -0.3, -0.3));

            // 簡單線形線條（粗取樣）
            const double diag = std::hypot(mx1 - mx0, my1 - my0);
            const double stp = std::max(5.0, diag / 120.0);
            const QRectF reach(mx0 - diag, my0 - diag, (mx1 - mx0) + 2 * diag, (my1 - my0) + 2 * diag);
            painter.setPen(QPen(QColor(110, 110, 110), cfg.lineWidthThin));
            for (const TrackCenterLine* tl : doc->trackCenterLines()) {
                double a3 = 0, b3 = 0;
                if (!tl || !planProfileChainageRange(tl, a3, b3) || !(b3 - a3 > 1e-6)) continue;
                QPolygonF pl;
                for (double x = a3;; x += stp) {
                    const double ch = std::min(x, b3);
                    const QPointF q = tl->getXYForCalc(ch);
                    if (reach.contains(q)) pl << mp(rot(q));
                    else { if (pl.size() >= 2) painter.drawPolyline(pl); pl.clear(); }
                    if (ch >= b3) break;
                }
                if (pl.size() >= 2) painter.drawPolyline(pl);
            }

            // 矩形與序號
            for (int i = 0; i < rcs.size(); ++i) {
                QPolygonF poly;
                for (int j = 0; j < 4; ++j) poly << mp(rcs[i].p[j]);
                const bool isCur = (i == cur);
                painter.setPen(QPen(isCur ? QColor(200, 60, 0) : Qt::black, cfg.lineWidthMedium));
                painter.setBrush(isCur ? QBrush(QColor(255, 170, 60, 170)) : Qt::NoBrush);
                painter.drawPolygon(poly);
                painter.setBrush(Qt::NoBrush);
                const double sz = std::max(1.3, std::min(3.4, rcs[i].minSide * k2 * 0.55));
                painter.setPen(Qt::black);
                painter.setFont(dtFont(sz, true));
                drawTextAt(painter, mp(rcs[i].mid), QString::number(i + 1),
                           Qt::AlignHCenter | Qt::AlignVCenter, sz);
            }
            painter.setClipping(false);
            painter.setPen(Qt::black);
            painter.setFont(dtFont(2.2, true));
            painter.drawText(QRectF(box.left() + 1.0, box.top() + 0.5, 40, 3.5), Qt::AlignLeft | Qt::AlignVCenter,
                             QStringLiteral("KEY PLAN"));
            painter.restore();
        }
    }

    // 指北針與比例尺
    {
        const QPointF nv = toView(c + QPointF(0, 1)) - toView(c);     // 北向在視圖中的方向
        const double nl = std::hypot(nv.x(), nv.y());
        const QPointF o(frame.right() - 12.0, frame.top() + 16.0);
        const QPointF u = nl > 1e-9 ? QPointF(nv.x() / nl, -nv.y() / nl) : QPointF(0, -1);   // 紙面方向
        painter.setPen(QPen(Qt::black, cfg.lineWidthMedium));
        painter.drawLine(o - u * 8.0, o + u * 8.0);
        const QPointF side(-u.y(), u.x());
        QPolygonF head;
        head << o + u * 8.0 << o + u * 4.0 + side * 1.8 << o + u * 4.0 - side * 1.8;
        painter.setBrush(Qt::black);
        painter.drawPolygon(head);
        painter.setBrush(Qt::NoBrush);
        painter.setFont(dtFont(3, true));
        drawTextAt(painter, o + u * 9.5, QStringLiteral("N"), Qt::AlignHCenter | Qt::AlignVCenter, 3);

        // 比例尺：取 1/2/5×10^n m，約 30 mm 長
        double bar = std::pow(10.0, std::floor(std::log10(30.0 / k)));
        double nice = bar;
        for (double mul : {2.0, 5.0, 10.0}) if (bar * mul * k <= 40.0) nice = bar * mul;
        const QPointF s0(frame.left() + 8.0, frame.bottom() - 6.0);
        painter.drawLine(s0, s0 + QPointF(nice * k, 0));
        painter.drawLine(s0 + QPointF(0, -1.2), s0 + QPointF(0, 1.2));
        painter.drawLine(s0 + QPointF(nice * k, -1.2), s0 + QPointF(nice * k, 1.2));
        painter.setFont(dtFont(2.6));
        drawTextAt(painter, s0 + QPointF(nice * k / 2, -1.5), QStringLiteral("%1 m").arg(nice, 0, 'f', 0),
                   Qt::AlignHCenter | Qt::AlignBottom, 2.6);
    }
    painter.restore();
}

/**
 * 平縱面圖的 Key Plan：標題欄右上角（圖名下方、資料格上方），正北朝上。
 * 畫出全部線形；本頁範圍以紅色粗線標示，並加紅色圓圈與引線指出位置（本頁相對全線很小）。
 */
static void paintPPKeyPlan(QPainter& painter, const DrawingSheet& sheet, const ChainPath& path)
{
    cad::Document* doc = sheet.sourceDocument();
    if (!doc || path.pieces.isEmpty() || !(path.total > 0)) return;
    const QSizeF paper = sheet.config().paperSizeMM();
    const SheetConfig& cfg = sheet.config();
    const double tbX = paper.width() - 10.0 - 60.0;
    const double topY = 10.0 + 22.0 + 34.0 + 2.0;
    const double botY = paper.height() - 10.0 - 98.0 - 2.0;
    const double kh = std::min(48.0, botY - topY);
    if (kh < 20.0) return;
    const QRectF box(tbX + 2.0, topY, 56.0, kh);

    // 全部線形取樣（粗），同時求範圍
    struct Line { QPolygonF pts; };
    QVector<Line> lines;
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (const TrackCenterLine* tl : doc->trackCenterLines()) {
        double a = 0, b = 0;
        if (!tl || !planProfileChainageRange(tl, a, b) || !(b - a > 1e-6)) continue;
        const double stp = std::max(5.0, (b - a) / 150.0);
        Line ln;
        for (double x = a;; x += stp) {
            const double ch = std::min(x, b);
            const QPointF q = tl->getXYForCalc(ch);
            ln.pts << q;
            x0 = std::min(x0, q.x()); x1 = std::max(x1, q.x());
            y0 = std::min(y0, q.y()); y1 = std::max(y1, q.y());
            if (ch >= b) break;
        }
        lines.append(ln);
    }
    // 本頁範圍
    QPolygonF mine;
    {
        const double stp = std::max(1.0, path.total / 60.0);
        for (double d = 0;; d += stp) {
            const double dd = std::min(d, path.total);
            const QPointF q = path.xy(dd);
            mine << q;
            x0 = std::min(x0, q.x()); x1 = std::max(x1, q.x());
            y0 = std::min(y0, q.y()); y1 = std::max(y1, q.y());
            if (dd >= path.total) break;
        }
    }
    if (lines.isEmpty() && mine.isEmpty()) return;
    const double bw = std::max(1.0, x1 - x0), bh = std::max(1.0, y1 - y0);
    const double k2 = std::min((box.width() - 8.0) / bw, (box.height() - 9.0) / bh);
    const QPointF mc((x0 + x1) / 2, (y0 + y1) / 2);
    const QPointF bc(box.center().x(), box.center().y() + 2.0);
    auto mp = [&](const QPointF& q) { return QPointF(bc.x() + (q.x() - mc.x()) * k2, bc.y() - (q.y() - mc.y()) * k2); };

    painter.save();
    painter.setPen(QPen(Qt::black, cfg.lineWidthThin));
    painter.setBrush(Qt::white);
    painter.drawRect(box);
    painter.setBrush(Qt::NoBrush);
    painter.setClipRect(box.adjusted(0.3, 0.3, -0.3, -0.3));

    painter.setPen(QPen(QColor(90, 90, 90), cfg.lineWidthThin));
    for (const Line& ln : lines) {
        QPolygonF pl;
        for (const QPointF& q : ln.pts) pl << mp(q);
        if (pl.size() >= 2) painter.drawPolyline(pl);
    }
    // 本頁：紅色粗線
    const QColor red(220, 20, 20);
    QPolygonF pm;
    for (const QPointF& q : mine) pm << mp(q);
    painter.setPen(QPen(red, cfg.lineWidthMedium * 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (pm.size() >= 2) painter.drawPolyline(pm);
    // 粗黑箭頭指向本頁中點：從框內空間最大的方向指過來，箭尖離線 1.2 mm
    {
        const QRectF pr = pm.boundingRect();
        const QPointF ctr = pr.center();
        const double len = 11.0, gap = std::max(1.2, std::hypot(pr.width(), pr.height()) / 2 + 0.8);
        const QRectF inner = box.adjusted(1.5, 4.5, -1.5, -1.5);   // 避開左上「KEY PLAN」字
        double bestM = -1e18; QPointF bestDir(-1, -1);
        for (int k = 0; k < 16; ++k) {
            const double a = k * 3.14159265358979323846 / 8.0;
            const QPointF dir(std::cos(a), std::sin(a));            // 箭尾所在方向（自中點算起）
            const QPointF tail = ctr + dir * (gap + len);
            const double m = std::min({ tail.x() - inner.left(), inner.right() - tail.x(),
                                        tail.y() - inner.top(), inner.bottom() - tail.y() });
            if (m > bestM) { bestM = m; bestDir = dir; }
        }
        const QPointF tip = ctr + bestDir * gap;
        const QPointF tail = ctr + bestDir * (gap + len);
        const QPointF u = (tip - tail) / len;                        // 朝向箭尖
        const QPointF sd(-u.y(), u.x());
        const double hl = 4.5, hw = 2.2;
        painter.setPen(QPen(Qt::black, 1.0, Qt::SolidLine, Qt::FlatCap));
        painter.drawLine(tail, tip - u * hl);
        QPolygonF head;
        head << tip << tip - u * hl + sd * hw << tip - u * hl - sd * hw;
        painter.setBrush(Qt::black);
        painter.setPen(QPen(Qt::black, 0.2));
        painter.drawPolygon(head);
        painter.setBrush(Qt::NoBrush);
    }
    // 北方箭頭
    painter.setPen(QPen(Qt::black, cfg.lineWidthThin));
    const QPointF na(box.right() - 5.0, box.top() + 11.0);
    painter.drawLine(na, na + QPointF(0, -6.0));
    painter.setBrush(Qt::black);
    QPolygonF ah; ah << na + QPointF(0, -7.5) << na + QPointF(-1.2, -5.0) << na + QPointF(1.2, -5.0);
    painter.drawPolygon(ah);
    painter.setBrush(Qt::NoBrush);
    painter.setFont(dtFont(2.2, true));
    drawTextAt(painter, na + QPointF(0, 1.8), QStringLiteral("N"), Qt::AlignHCenter | Qt::AlignVCenter, 2.2);

    painter.setClipping(false);
    painter.setPen(Qt::black);
    painter.setFont(dtFont(2.2, true));
    painter.drawText(QRectF(box.left() + 1.0, box.top() + 0.5, 40, 3.5), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("KEY PLAN"));
    painter.restore();
}

void paintPlanProfileLayer(QPainter& painter, const DrawingSheet& sheet)
{
    if (sheet.planProfile().tableKind == 4) { paintDepotView(painter, sheet); return; }
    if (sheet.planProfile().tableKind == 3) { paintSchematic(painter, sheet); return; }
    if (sheet.planProfile().tableKind > 0) { paintAlignmentTable(painter, sheet); return; }
    const PlanProfileLayer& layer = sheet.planProfile();
    if (!layer.enabled) return;

    const PlanProfileSettings& s = layer.settings;
    const QSizeF paper = sheet.config().paperSizeMM();
    const PlanProfileLayout L0 = computePlanProfileLayout(paper, s);

    painter.save();

    QFont warn(QStringLiteral("Arial"));
    warn.setPointSizeF(3.5);

    cad::Document* doc = sheet.sourceDocument();

    Ctx c;
    QStringList names;
    if (doc) {
        for (const PlanProfileSegment& g : layer.pieces) {
            const TrackCenterLine* t = doc->findTrackCenterLine(g.tclId);
            if (!t || !t->horizontal() || t->horizontal()->isEmpty() || !(g.end > g.start)) { c.path.pieces.clear(); c.path.total = 0; break; }
            c.path.add(t, g.start, g.end);
            if (names.isEmpty() || names.last() != t->name()) names << t->name();
        }
    }
    if (c.path.pieces.isEmpty() || !(c.path.total > 0)) {
        painter.setFont(warn);
        painter.setPen(QColor(200, 0, 0));
        painter.drawText(L0.area, Qt::AlignCenter,
                         QStringLiteral("平縱面圖：找不到線形或里程範圍無效（%1）").arg(s.tclId));
        painter.restore();
        return;
    }

    c.mm = 1000.0 / s.hScale;
    // 平面框高度：彎曲很大的分段自動加高，容納整段平面圖（上下各留文字與標示空間）
    const double needPlanH = planVExtentMeters(c.path, c.mm) * c.mm + 2 * 16.0;
    const PlanProfileLayout L = computePlanProfileLayout(paper, s, needPlanH);

    c.p = &painter; c.s = &s; c.L = &L; c.doc = doc;
    c.lwThin = sheet.config().lineWidthThin;
    c.lwMed  = sheet.config().lineWidthMedium;
    c.th = s.textSize;
    c.prevNo = layer.prevNo;
    c.nextNo = layer.nextNo;

    paintPlan(c);
    const ProfileInfo pi = paintProfile(c);
    paintTable(c);
    paintPPKeyPlan(painter, sheet, c.path);

    // 框下方的標示
    c.font(c.th * 1.15);
    painter.setPen(Qt::black);
    painter.drawText(QRectF(L.planRect.left(), L.planRect.bottom() + 1.0, L.planRect.width(), 6.0),
                     Qt::AlignHCenter | Qt::AlignVCenter,
                     QStringLiteral("平面圖  PLAN    1:%1").arg(s.hScale, 0, 'f', 0));
    painter.drawText(QRectF(L.profileRect.left(), L.profileRect.bottom() + 1.0, L.profileRect.width(), 6.0),
                     Qt::AlignHCenter | Qt::AlignVCenter,
                     QStringLiteral("縱斷面  PROFILE    H 1:%1   V 1:%2")
                         .arg(s.hScale, 0, 'f', 0).arg(pi.vScale, 0, 'f', 0));

    // 頁首資訊
    c.font(c.th);
    painter.setPen(Qt::black);
    painter.drawText(QRectF(L.area.left(), 10.3, L.area.width(), 4.5),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("%1   第 %2 / %3 張   %4 ~ %5")
                         .arg(names.join(QStringLiteral(" → "))).arg(layer.index).arg(layer.count)
                         .arg(c.path.fmt(0.0, s), c.path.fmt(c.path.total, s)));

    painter.restore();
}

} // namespace drawing
} // namespace aicad
