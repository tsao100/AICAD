#include "DimPreviewOverlay.h"
#include "cad/Sketch.h"
#include "cad/sketch/SketchConstraint.h"

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QPaintEvent>
#include <cmath>

namespace aicad::view {

using CT = cad::ConstraintType;
using DM = cad::DistanceMode;
using cad::SketchLine;
using cad::SketchCircle;
using cad::SketchArc;

// ─────────────────────────────────────────────────────────────────────────────
DimPreviewOverlay::DimPreviewOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
}

void DimPreviewOverlay::setPreview(const PreviewInfo& info)
{
    m_info = info;
    update();
}

void DimPreviewOverlay::clearPreview()
{
    m_info.valid = false;
    update();
}

void DimPreviewOverlay::setMousePlanePt(const QVector2D& pt)
{
    m_mouse = pt;
    if (m_info.valid) update();
}

void DimPreviewOverlay::setPlaneToPxFn(PlaneToPxFn fn)
{
    m_planeToPx = std::move(fn);
}

// ─────────────────────────────────────────────────────────────────────────────
void DimPreviewOverlay::paintEvent(QPaintEvent* /*event*/)
{
    if (!m_info.valid || !m_sketch || !m_planeToPx) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    switch (m_info.type) {
    case CT::FixedLength:    drawFixedLength(p);   break;
    case CT::FixedDiameter:  drawFixedDiameter(p); break;
    case CT::FixedRadius:    drawFixedRadius(p);   break;
    case CT::FixedDistance:
    case CT::FixedHorizDist:
    case CT::FixedVertDist:  drawDistance(p);      break;
    case CT::FixedAngleDim:  drawAngle(p);         break;
    case CT::CoordinateDim:  drawCoordinate(p);    break;
    default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// helpers
// ─────────────────────────────────────────────────────────────────────────────
QPointF DimPreviewOverlay::toScr(const QVector2D& pt) const
{
    return m_planeToPx ? QPointF(m_planeToPx(pt)) : QPointF();
}

void DimPreviewOverlay::drawArrow(QPainter& p, QPointF tip, QPointF dir)
{
    double len = std::sqrt(dir.x()*dir.x() + dir.y()*dir.y());
    if (len < 1e-3) return;
    QPointF u = dir / len;
    QPointF n(-u.y(), u.x());
    p.drawLine(tip, tip - u*9 + n*4);
    p.drawLine(tip, tip - u*9 - n*4);
}

QVector2D DimPreviewOverlay::getPos(int i) const
{
    if (i < m_info.refs.size())
        return m_info.refs[i].resolvePosition(m_sketch);
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// 各情境繪製
// ─────────────────────────────────────────────────────────────────────────────

void DimPreviewOverlay::drawFixedLength(QPainter& p)
{
    if (m_info.refs.isEmpty()) return;
    auto* geom = m_sketch->findGeometry(m_info.refs[0].geomUuid);
    auto* ln   = dynamic_cast<const SketchLine*>(geom);
    if (!ln) return;

    QVector2D A = ln->start, B = ln->end;
    QVector2D mid = (A + B) * 0.5f;
    QVector2D dir = (B - A).normalized();
    QVector2D perp(-dir.y(), dir.x());

    float off = QVector2D::dotProduct(m_mouse - mid, perp);
    if (std::abs(off) < 8.f) off = 20.f;

    QVector2D dA = A + perp * off;
    QVector2D dB = B + perp * off;

    const QColor dimC(0, 210, 230, 210);
    const QColor txtC(255, 255, 60, 230);
    p.setPen(QPen(dimC, 0.9, Qt::DashLine));
    p.drawLine(toScr(A), toScr(dA));
    p.drawLine(toScr(B), toScr(dB));
    p.setPen(QPen(dimC, 1.4));
    p.drawLine(toScr(dA), toScr(dB));
    drawArrow(p, toScr(dA), toScr(dA) - toScr(dB));
    drawArrow(p, toScr(dB), toScr(dB) - toScr(dA));

    QString label = QString::number(m_info.value, 'f', 2);
    p.setPen(txtC);
    QFont f = p.font(); f.setPointSize(9); p.setFont(f);
    p.drawText((toScr(dA) + toScr(dB)) * 0.5 + QPointF(4, -4), label);
}

void DimPreviewOverlay::drawFixedDiameter(QPainter& p)
{
    if (m_info.refs.isEmpty()) return;
    auto* geom = m_sketch->findGeometry(m_info.refs[0].geomUuid);
    QVector2D center; float r = 0;
    if (auto* c = dynamic_cast<const SketchCircle*>(geom)) {
        center = c->center; r = c->radius;
    } else if (auto* a = dynamic_cast<const SketchArc*>(geom)) {
        cad::GeomRef cr(m_info.refs[0].geomUuid, cad::GeomHandle::Center);
        cad::GeomRef sr(m_info.refs[0].geomUuid, cad::GeomHandle::Start);
        center = cr.resolvePosition(m_sketch);
        r = (sr.resolvePosition(m_sketch) - center).length();
    }

    QVector2D dir = m_mouse - center;
    if (dir.length() < 1e-3f) dir = QVector2D(1, 0);
    dir.normalize();
    QVector2D tip = center + dir * r;
    QVector2D opp = center - dir * r;  // 直徑另一端

    const QColor dimC(0, 210, 230, 210);
    const QColor txtC(255, 255, 60, 230);
    p.setPen(QPen(dimC, 1.4));
    p.drawLine(toScr(opp), toScr(tip));
    drawArrow(p, toScr(tip), toScr(tip) - toScr(opp));
    drawArrow(p, toScr(opp), toScr(opp) - toScr(tip));
    p.setPen(txtC);
    QFont f = p.font(); f.setPointSize(9); p.setFont(f);
    p.drawText((toScr(center) + toScr(tip)) * 0.5 + QPointF(4, -4),
               "⌀" + QString::number(m_info.value, 'f', 2));
}

void DimPreviewOverlay::drawFixedRadius(QPainter& p)
{
    if (m_info.refs.isEmpty()) return;
    auto* geom = m_sketch->findGeometry(m_info.refs[0].geomUuid);
    QVector2D center; float r = 0;
    if (auto* c = dynamic_cast<const SketchCircle*>(geom)) {
        center = c->center; r = c->radius;
    } else if (auto* a = dynamic_cast<const SketchArc*>(geom)) {
        cad::GeomRef cr(m_info.refs[0].geomUuid, cad::GeomHandle::Center);
        cad::GeomRef sr(m_info.refs[0].geomUuid, cad::GeomHandle::Start);
        center = cr.resolvePosition(m_sketch);
        r = (sr.resolvePosition(m_sketch) - center).length();
    }

    QVector2D dir = m_mouse - center;
    if (dir.length() < 1e-3f) dir = QVector2D(1, 0);
    dir.normalize();
    QVector2D tip = center + dir * r;

    const QColor dimC(0, 210, 230, 210);
    const QColor txtC(255, 255, 60, 230);
    p.setPen(QPen(dimC, 1.4));
    p.drawLine(toScr(center), toScr(tip));
    drawArrow(p, toScr(tip), toScr(tip) - toScr(center));
    p.setPen(txtC);
    QFont f = p.font(); f.setPointSize(9); p.setFont(f);
    p.drawText((toScr(center) + toScr(tip)) * 0.5 + QPointF(4, -4),
               "R" + QString::number(m_info.value, 'f', 2));
}

void DimPreviewOverlay::drawDistance(QPainter& p)
{
    // 需要 2 個 refs
    if (m_info.refs.size() < 2) return;

    QVector2D A = getPos(0), B = getPos(1);
    QVector2D mid = (A + B) * 0.5f;
    QVector2D dimDir;

    if (m_info.type == CT::FixedHorizDist) {
        dimDir = QVector2D(0, 1);
    } else if (m_info.type == CT::FixedVertDist) {
        dimDir = QVector2D(1, 0);
    } else if (m_info.distMode == DM::PointToLine) {
        // 找線的方向
        for (auto& ref : m_info.refs) {
            auto* g = m_sketch->findGeometry(ref.geomUuid);
            if (auto* ln = dynamic_cast<const SketchLine*>(g)) {
                QVector2D d2 = (ln->end - ln->start).normalized();
                dimDir = QVector2D(-d2.y(), d2.x());
                break;
            }
        }
        if (dimDir.isNull()) dimDir = QVector2D(0, 1);
    } else {
        // 點─點：方向垂直於連線，跟滑鼠同側
        QVector2D ab = (B - A);
        if (ab.length() < 1e-3f) ab = QVector2D(1, 0);
        ab.normalize();
        QVector2D perp(-ab.y(), ab.x());
        float sign = QVector2D::dotProduct(m_mouse - mid, perp) >= 0 ? 1.f : -1.f;
        dimDir = perp * sign;
    }
    if (dimDir.isNull()) dimDir = QVector2D(0, 1);

    float off = QVector2D::dotProduct(m_mouse - mid, dimDir);
    if (std::abs(off) < 8.f) off = 22.f;

    QVector2D dA = A + dimDir * off;
    QVector2D dB = B + dimDir * off;

    const QColor dimC(0, 210, 230, 210);
    const QColor txtC(255, 255, 60, 230);
    p.setPen(QPen(dimC, 0.9, Qt::DashLine));
    p.drawLine(toScr(A), toScr(dA));
    p.drawLine(toScr(B), toScr(dB));
    p.setPen(QPen(dimC, 1.4));
    p.drawLine(toScr(dA), toScr(dB));
    drawArrow(p, toScr(dA), toScr(dA) - toScr(dB));
    drawArrow(p, toScr(dB), toScr(dB) - toScr(dA));

    p.setPen(txtC);
    QFont f = p.font(); f.setPointSize(9); p.setFont(f);
    p.drawText((toScr(dA) + toScr(dB)) * 0.5 + QPointF(4, -4),
               QString::number(m_info.value, 'f', 2));
}

void DimPreviewOverlay::drawAngle(QPainter& p)
{
    if (m_info.refs.size() < 2) return;
    auto* gA = m_sketch->findGeometry(m_info.refs[0].geomUuid);
    auto* gB = m_sketch->findGeometry(m_info.refs[1].geomUuid);
    auto* lA = dynamic_cast<const SketchLine*>(gA);
    auto* lB = dynamic_cast<const SketchLine*>(gB);
    if (!lA || !lB) return;

    QVector2D dA = (lA->end - lA->start).normalized();
    QVector2D dB = (lB->end - lB->start).normalized();
    float cross = dA.x()*dB.y() - dA.y()*dB.x();
    if (std::abs(cross) < 1e-6f) return;
    QVector2D diff = lB->start - lA->start;
    float t = (diff.x()*dB.y() - diff.y()*dB.x()) / cross;
    QVector2D vertex = lA->start + dA * t;

    float rr = (m_mouse - vertex).length();
    if (rr < 8.f) rr = 22.f;

    float a0 = std::atan2(dA.y(), dA.x());
    float a1 = std::atan2(dB.y(), dB.x());
    float da = a1 - a0;
    while (da < 0)          da += float(M_PI) * 2;
    while (da > float(M_PI) * 2) da -= float(M_PI) * 2;
    if (da > float(M_PI))  { std::swap(a0, a1); da = float(M_PI)*2 - da; }

    const QColor dimC(0, 210, 230, 210);
    const QColor txtC(255, 255, 60, 230);
    p.setPen(QPen(dimC, 1.4));
    QVector<QPointF> pts;
    for (int i = 0; i <= 32; ++i) {
        float a = a0 + da * i / 32;
        pts << toScr(vertex + QVector2D(std::cos(a)*rr, std::sin(a)*rr));
    }
    for (int i = 0; i < pts.size()-1; ++i)
        p.drawLine(pts[i], pts[i+1]);
    p.setPen(QPen(dimC, 0.9, Qt::DashLine));
    p.drawLine(toScr(vertex), toScr(vertex + dA * rr));
    p.drawLine(toScr(vertex), toScr(vertex + dB * rr));

    float amid = a0 + da * 0.5f;
    QPointF labelPt = toScr(vertex + QVector2D(std::cos(amid)*rr, std::sin(amid)*rr));
    p.setPen(txtC);
    QFont f = p.font(); f.setPointSize(9); p.setFont(f);
    p.drawText(labelPt + QPointF(4, -4),
               QString::number(m_info.value, 'f', 1) + "°");
}

void DimPreviewOverlay::drawCoordinate(QPainter& p)
{
    if (m_info.refs.isEmpty()) return;
    QVector2D pt = getPos(0);
    const QColor dimC(0, 210, 230, 210);
    const QColor txtC(255, 255, 60, 230);
    p.setPen(QPen(dimC, 0.9, Qt::DashLine));
    p.drawLine(toScr(QVector2D(0,0)), toScr(QVector2D(pt.x(),0)));
    p.drawLine(toScr(QVector2D(pt.x(),0)), toScr(pt));
    p.setPen(txtC);
    QFont f = p.font(); f.setPointSize(9); p.setFont(f);
    p.drawText(toScr(pt) + QPointF(4, -14),
               QString("X=%1").arg(pt.x(), 0, 'f', 2));
    p.drawText(toScr(pt) + QPointF(4, -2),
               QString("Y=%1").arg(pt.y(), 0, 'f', 2));
}

} // namespace aicad::view
