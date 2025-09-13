#pragma once
#include <QPointF>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QTextStream>
#include <memory>
//#include <vector>

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
    QPointF center, start, end;
    ArcEntity(QPointF c={}, QPointF s={}, QPointF e={})
        : center(c), start(s), end(e) {}
    void paint(QPainter &p) const override {
        QPainterPath path;
        path.moveTo(start);
        double r = QLineF(center, start).length();
        path.arcTo(QRectF(center.x()-r, center.y()-r, 2*r, 2*r),
                   QLineF(center, start).angle(),
                   QLineF(center, start).angleTo(QLineF(center, end)));
        p.drawPath(path);
    }
    void save(QTextStream &out) const override {
        out << "ARC " << center.x() << " " << center.y()
        << " " << start.x() << " " << start.y()
        << " " << end.x() << " " << end.y() << "\n";
    }
    QString type() const override { return "ARC"; }
    std::unique_ptr<Entity> clone() const override {
        return std::make_unique<ArcEntity>(*this);
    }
};

// ----- Factory for loading -----
inline std::unique_ptr<Entity> loadEntity(QTextStream &in, const QString &type) {
    if (type == "LINE") {
        double x1,y1,x2,y2; in >> x1 >> y1 >> x2 >> y2;
        return std::make_unique<LineEntity>(QPointF(x1,y1), QPointF(x2,y2));
    }
    else if (type == "ARC") {
        double cx,cy,sx,sy,ex,ey; in >> cx >> cy >> sx >> sy >> ex >> ey;
        return std::make_unique<ArcEntity>(QPointF(cx,cy), QPointF(sx,sy), QPointF(ex,ey));
    }
    return nullptr;
}
