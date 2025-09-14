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
    void keyPressEvent(QKeyEvent *ev) override;

public slots:
    void printView();
    void exportPdf(const QString &file);

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

    std::vector<std::unique_ptr<Entity>> m_entities;

    Mode m_mode=Normal;

    // working state for drawing modes
    // for line drawing
    bool m_lineActive=false;
    bool m_polylineMode = false;   // continue line sequence
    QPointF m_lineStart;
    QPointF m_mouseWorld;

    // for arc drawing
    QPointF m_arcStart;      // first click
    QPointF m_arcMid;        // second click (mid-point)
    QPointF m_arcEnd;        // third click (end-point)
    int m_arcStage = 0;      // 0 = not started, 1 = first click, 2 = second click

    // draw line state
    QVector<LineEntity> m_lines;

    // draw arc state
    QVector<ArcEntity> m_arcs;
};
