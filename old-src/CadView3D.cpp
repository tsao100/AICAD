#include "CadView3D.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVector4D>
#include <cmath>

// ctor
CadView3D::CadView3D(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    m_camera.reset();
}

// --- OpenGL lifecycle ---
void CadView3D::initializeGL()
{
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glClearColor(0.95f, 0.95f, 0.95f, 1.0f);
}

void CadView3D::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
    m_proj.setToIdentity();
    float aspect = float(w) / qMax(h, 1);
    if (m_ortho) {
        float s = 5.0f;
        m_proj.ortho(-s * aspect, s * aspect, -s, s, 0.01f, 1000.0f);
    } else {
        m_proj.perspective(60.0f, aspect, 0.01f, 1000.0f);
    }
}

void CadView3D::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    QMatrix4x4 view = m_camera.viewMatrix();
    drawAxis(view);
    drawCube(view);
}

// --- Input ---
void CadView3D::mousePressEvent(QMouseEvent *ev)
{
    m_lastPos = ev->pos();
    // use button, not buttons() for the press event semantics
    if (ev->button() == Qt::LeftButton) m_orbit = true;
    if (ev->button() == Qt::MiddleButton) m_pan = true;
}

void CadView3D::mouseMoveEvent(QMouseEvent *ev)
{
    QPointF p = ev->pos();
    QPointF d = p - m_lastPos;
    m_lastPos = p;
    if (m_orbit) {
        // rotate: scale down delta for reasonable sensitivity
        m_camera.rotateBy(-d.x() * 0.01f, -d.y() * 0.01f);
        update();
    } else if (m_pan) {
        m_camera.panBy(d.x() * 0.01f, -d.y() * 0.01f);
        update();
    }
}

void CadView3D::mouseReleaseEvent(QMouseEvent *ev)
{
    Q_UNUSED(ev);
    m_orbit = m_pan = false;
}

void CadView3D::wheelEvent(QWheelEvent *ev)
{
    // Use the current projection matrix (m_proj) and view to unproject the mouse into world space
    QPointF pos = ev->position(); // Qt 5.15+; fallback to ev->posF() older versions

    // compute normalized device coords (NDC) x,y in [-1,1]
    float x =  2.0f * pos.x() / float(width())  - 1.0f;
    float y = -2.0f * pos.y() / float(height()) + 1.0f;

    QMatrix4x4 view = m_camera.viewMatrix();

    bool ok = false;
    QMatrix4x4 inv = (m_proj * view).inverted(&ok);
    if (!ok) {
        // fallback: just zoom centered on camera center
        m_camera.zoomBy(ev->angleDelta().y());
        update();
        return;
    }

    // unproject near/far in clip space
    QVector4D nearCS(x, y, -1.0f, 1.0f);
    QVector4D farCS (x, y,  1.0f, 1.0f);

    QVector4D nearWorld = inv * nearCS;
    QVector4D farWorld  = inv * farCS;
    nearWorld /= nearWorld.w();
    farWorld  /= farWorld.w();

    QVector3D rayOrigin = nearWorld.toVector3D();
    QVector3D rayDir = (farWorld - nearWorld).toVector3D().normalized();

    // intersect ray with plane through camera.center having normal = camera.direction()
    QVector3D planeNormal = m_camera.direction();
    QVector3D planePoint  = m_camera.center(); // uses accessor
    float denom = QVector3D::dotProduct(planeNormal, rayDir);

    QVector3D hitPoint = planePoint;
    if (std::fabs(denom) > 1e-6f) {
        float t = QVector3D::dotProduct(planePoint - rayOrigin, planeNormal) / denom;
        if (t > 0.0f) hitPoint = rayOrigin + t * rayDir;
    }

    float oldDist = m_camera.distance();        // uses accessor
    m_camera.zoomBy(ev->angleDelta().y());
    float factor = m_camera.distance() / oldDist;
    // scale camera center about the hit point so zoom focuses under the cursor
    m_camera.setCenter(hitPoint + (m_camera.center() - hitPoint) * factor);

    update();
}

// --- Helpers ---
QMatrix4x4 CadView3D::projectionMatrix() const
{
    // return the current projection
    return m_proj;
}

void CadView3D::drawAxis(const QMatrix4x4 &view)
{
    QMatrix4x4 mvp = m_proj * view;
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(mvp.constData());

    glLineWidth(2.0f);
    glBegin(GL_LINES);
    // X - red
    glColor3f(1.0f, 0.0f, 0.0f); glVertex3f(0,0,0); glVertex3f(1,0,0);
    // Y - green
    glColor3f(0.0f, 1.0f, 0.0f); glVertex3f(0,0,0); glVertex3f(0,1,0);
    // Z - blue
    glColor3f(0.0f, 0.0f, 1.0f); glVertex3f(0,0,0); glVertex3f(0,0,1);
    glEnd();
}

void CadView3D::drawCube(const QMatrix4x4 &view)
{
    QMatrix4x4 model; model.setToIdentity(); model.scale(1.0f);
    QMatrix4x4 mvp = m_proj * view * model;
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(mvp.constData());

    glBegin(GL_QUADS);
    // top
    glColor3f(0.8f,0.2f,0.2f);
    glVertex3f( 1, 1,-1); glVertex3f(-1, 1,-1); glVertex3f(-1, 1, 1); glVertex3f( 1, 1, 1);
    // bottom
    glColor3f(0.2f,0.8f,0.2f);
    glVertex3f( 1,-1, 1); glVertex3f(-1,-1, 1); glVertex3f(-1,-1,-1); glVertex3f( 1,-1,-1);
    // front
    glColor3f(0.2f,0.2f,0.8f);
    glVertex3f( 1, 1, 1); glVertex3f(-1, 1, 1); glVertex3f(-1,-1, 1); glVertex3f( 1,-1, 1);
    // back
    glColor3f(0.8f,0.8f,0.2f);
    glVertex3f( 1,-1,-1); glVertex3f(-1,-1,-1); glVertex3f(-1, 1,-1); glVertex3f( 1, 1,-1);
    // left
    glColor3f(0.8f,0.2f,0.8f);
    glVertex3f(-1, 1, 1); glVertex3f(-1, 1,-1); glVertex3f(-1,-1,-1); glVertex3f(-1,-1, 1);
    // right
    glColor3f(0.2f,0.8f,0.8f);
    glVertex3f( 1, 1,-1); glVertex3f( 1, 1, 1); glVertex3f( 1,-1, 1); glVertex3f( 1,-1,-1);
    glEnd();
}
