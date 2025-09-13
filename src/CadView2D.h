#pragma once
#include <QWidget>
#include <QTransform>
#include <QVector>
#include "Entities.h"

class CadView2D : public QWidget {
    Q_OBJECT
public:
    enum Mode { Normal, DrawLine, DrawArc };
    explicit CadView2D(QWidget *parent=nullptr);

    void setMode(Mode m);
    void saveEntities(const QString &file);
    void loadEntities(const QString &file);

protected:
    void paintEvent(QPaintEvent *ev) override;
    void resizeEvent(QResizeEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void wheelEvent(QWheelEvent *ev) override;

private:
    QPointF toScreen(const QPointF &world) const;
    QPointF toWorld(const QPointF &screen) const;
    void updateTransform();
    void drawGrid(QPainter *p);

    // state
    QTransform m_transform;
    double m_scale;
    bool m_panning=false;
    QPoint m_panStart;
    bool m_rubberActive=false;
    QPoint m_rubberStart, m_rubberEnd;
    QPointF m_mouseWorld;

    std::vector<std::unique_ptr<Entity>> m_entities;

    // working state for drawing modes
    Mode m_mode=Normal;
    bool m_lineActive=false;
    QPointF m_lineStart;

    int m_arcStage=0;
    QPointF m_arcCenter, m_arcStart;

    // draw line state
    QVector<LineEntity> m_lines;

    // draw arc state
    QVector<ArcEntity> m_arcs;
};
