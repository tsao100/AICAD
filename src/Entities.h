#pragma once
#include <QPointF>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QTextStream>
#include <memory>
#include <QtMath>
//#include <vector>

struct ArcDef {
    QPointF center;
    double radius;
    double startAngle; // radians
    double sweepAngle; // radians
};

inline bool circleFrom3Points(const QPointF &p1, const QPointF &p2, const QPointF &p3, ArcDef &arc)
{
    double x1 = p1.x(), y1 = p1.y();
    double x2 = p2.x(), y2 = p2.y();
    double x3 = p3.x(), y3 = p3.y();

    double a = x1*(y2-y3) - y1*(x2-x3) + x2*y3 - x3*y2;
    if (std::fabs(a) < 1e-12) return false; // collinear

    double bx = -( (x1*x1 + y1*y1)*(y3-y2) +
                   (x2*x2 + y2*y2)*(y1-y3) +
                   (x3*x3 + y3*y3)*(y2-y1) ) / (2*a);

    double by = -( (x1*x1 + y1*y1)*(x2-x3) +
                   (x2*x2 + y2*y2)*(x3-x1) +
                   (x3*x3 + y3*y3)*(x1-x2) ) / (2*a);

    arc.center = QPointF(bx, by);
    arc.radius = std::hypot(x1 - bx, y1 - by);

    double a1 = std::atan2(y1 - by, x1 - bx);
    double am = std::atan2(y2 - by, x2 - bx);
    double a2 = std::atan2(y3 - by, x3 - bx);

    // normalize [0, 2π)
    auto norm = [](double ang){ return ang < 0 ? ang + 2*M_PI : ang; };
    a1 = norm(a1); am = norm(am); a2 = norm(a2);

    double sweep = a2 - a1;
    if (sweep < 0) sweep += 2*M_PI;

    // cross product orientation test
    double vx1 = x2 - x1, vy1 = y2 - y1;
    double vx2 = x3 - x1, vy2 = y3 - y1;
    double cross = vx1*vy2 - vy1*vx2; // z-component

    if (cross < 0) {
        // CW: flip sweep
        sweep = sweep - 2*M_PI;
    }

    arc.startAngle = a1;
    arc.sweepAngle = sweep;

    return true;
}

// ----- Base class -----
class Entity {
public:
    virtual ~Entity() = default;
    virtual void paint(QPainter &p) const = 0;
    virtual void save(QTextStream &out) const = 0;
    virtual QString type() const = 0;
    virtual std::unique_ptr<Entity> clone() const = 0;
};

// ----- Line -----
class LineEntity : public Entity {
public:
    QPointF p1, p2;
    LineEntity(QPointF a={}, QPointF b={}) : p1(a), p2(b) {}
    void paint(QPainter &p) const override {
        p.drawLine(p1, p2);
    }
    void save(QTextStream &out) const override {
        out << "LINE " << p1.x() << " " << p1.y()
        << " " << p2.x() << " " << p2.y() << "\n";
    }
    QString type() const override { return "LINE"; }
    std::unique_ptr<Entity> clone() const override {
        return std::make_unique<LineEntity>(*this);
    }
};

// ----- Arc -----
class ArcEntity : public Entity {
public:
    ArcEntity(const QPointF &p1, const QPointF &p2, const QPointF &p3) {
    ArcDef def;
    if (circleFrom3Points(p1,p2,p3, def)) {
        m_center = def.center;
        m_radius = def.radius;
        m_startAngle = def.startAngle;
        m_sweepAngle = def.sweepAngle;
    }
}

    void setParameters(const QPointF &center, double radius, double startAngle, double sweepAngle) {
        m_center = center;
        m_radius = radius;
        m_startAngle = startAngle;
        m_sweepAngle = sweepAngle;
    }

    void paint(QPainter &p) const override{
    QRectF rect(m_center.x()-m_radius, m_center.y()-m_radius,
                2*m_radius, 2*m_radius);
    p.setPen(QPen(Qt::blue, 0));
    // QPainter expects degrees *16
    p.drawArc(rect,
              int(-m_startAngle * 180/M_PI * 16),
              int(-m_sweepAngle * 180/M_PI * 16));
}
    void save(QTextStream &out) const override{
    out << "ARC " << m_center.x() << " " << m_center.y() << " "
        << m_radius << " " << m_startAngle << " " << m_sweepAngle << "\n";
}

    static std::unique_ptr<ArcEntity> load(QTextStream &in){
    double cx, cy, r, sa, sw;
    in >> cx >> cy >> r >> sa >> sw;
    auto arc = std::make_unique<ArcEntity>(QPointF(0,0), QPointF(0,0), QPointF(0,0));
    arc->m_center = QPointF(cx, cy);
    arc->m_radius = r;
    arc->m_startAngle = sa;
    arc->m_sweepAngle = sw;
    return arc;
}

    QString type() const override { return "ARC"; }
    std::unique_ptr<Entity> clone() const override {
        return std::make_unique<ArcEntity>(*this);
    }


public:
    QPointF m_center;
    double m_radius;
    double m_startAngle; // radians
    double m_sweepAngle; // radians
};

// ----- Factory for loading -----
inline std::unique_ptr<Entity> loadEntity(QTextStream &in, const QString &type) {
    if (type == "LINE") {
        double x1,y1,x2,y2; in >> x1 >> y1 >> x2 >> y2;
        return std::make_unique<LineEntity>(QPointF(x1,y1), QPointF(x2,y2));
    }
    else if (type == "ARC") {
        double cx, cy, r, sa, sw;
        in >> cx >> cy >> r >> sa >> sw;
        auto arc = std::make_unique<ArcEntity>(QPointF(0,0), QPointF(0,0), QPointF(0,0));
        arc->setParameters(QPointF(cx,cy), r, sa, sw); // add setter
        return arc;
    }
    return nullptr;
}
