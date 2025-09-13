#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMatrix4x4>
#include <QPointF>
#include "TrackballCamera.h"

class CadView3D : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit CadView3D(QWidget *parent=nullptr);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void wheelEvent(QWheelEvent *ev) override;

private:
    QMatrix4x4 projectionMatrix() const;
    void drawAxis(const QMatrix4x4 &view);
    void drawCube(const QMatrix4x4 &view);

    TrackballCamera m_camera;
    bool m_orbit=false, m_pan=false;
    bool m_ortho=false;   // toggle perspective/ortho
    QPointF m_lastPos;
    QMatrix4x4 m_proj;
};
