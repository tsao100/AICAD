#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMatrix4x4>
#include <QTransform>
#include <QVector3D>
#include <QPrinter>
#include <QPdfWriter>
#include <QAction>
#include <memory>
#include <vector>

#include "Entities.h"
#include "TrackballCamera.h"

// Unified CadView: supports both 2D sketching and 3D modeling
class CadView : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    enum Mode { Normal, Sketch2D, Model3D, DrawLine, DrawArc };
    explicit CadView(QWidget *parent=nullptr);

    void setMode(Mode m);
    void saveEntities(const QString &file);
    void loadEntities(const QString &file);
    void printView();
    void exportPdf(const QString &file);

    enum ViewMode { Mode2D, Mode3D };
    void setViewMode(ViewMode m);

protected:
    // Qt overrides
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void wheelEvent(QWheelEvent *ev) override;
    void keyPressEvent(QKeyEvent *ev) override;

private:
    // 2D helpers
    void paint2D();
    void drawGrid();
    QPointF toScreen(const QPointF &world) const;
    QPointF toWorld(const QPointF &screen) const;
    void updateTransform();

    // 3D helpers
    void paint3D();
    void drawAxis(const QMatrix4x4 &view);
    void drawCube(const QMatrix4x4 &view);

    // State
    Mode m_mode = Normal;

    // ---- 2D state ----
    QTransform m_transform;
    double m_scale;
    QPointF m_mouseWorld;
    bool m_panning=false;
    QPoint m_panStart;
    bool m_rubberActive=false;
    QPoint m_rubberStart, m_rubberEnd;

    // line drawing
    bool m_lineActive=false;
    bool m_polylineMode=false;
    QPointF m_lineStart;

    // arc drawing
    int m_arcStage=0;
    QPointF m_arcStart, m_arcMid, m_arcEnd;

    std::vector<std::unique_ptr<Entity>> m_entities;

    // ---- 3D state ----
    TrackballCamera m_camera;
    QMatrix4x4 m_proj;
    bool m_orbit=false, m_pan=false;
    QPointF m_lastPos;
    bool m_ortho=false;
    ViewMode m_viewMode;
};
