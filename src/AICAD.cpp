#include "AICAD.h"
#include <QKeyEvent>
#include <QtMath>
#include <QDebug>

// ---------------- Camera Implementation ----------------
Camera::Camera() {
    position = QVector3D(0,0,10);
    target = QVector3D(0,0,0);
    up = QVector3D(0,1,0);
    distance = 10.0f;
    pitch = -30.0f;
    yaw = 30.0f;
}

void Camera::setPerspective(float fov, float aspect, float nearPlane, float farPlane) {
    projection.setToIdentity();
    projection.perspective(fov, aspect, nearPlane, farPlane);
    perspectiveMode = true;
    fov_ = fov;  // ✅ store FOV so pan can use it later
}

void Camera::setOrthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane) {
    orthoLeft = left; orthoRight = right; orthoBottom = bottom; orthoTop = top;
    nearPlane_ = nearPlane; farPlane_ = farPlane;
    projection.setToIdentity();
    projection.ortho(left, right, bottom, top, nearPlane, farPlane);
    perspectiveMode = false;
}

QMatrix4x4 Camera::getViewMatrix() const {
    QMatrix4x4 view;
    view.lookAt(position, target, up);
    return view;
}

QMatrix4x4 Camera::getProjectionMatrix() const {
    return projection;
}

void Camera::lookAt(const QVector3D& pos, const QVector3D& tgt, const QVector3D& upVec) {
    position = pos;
    target = tgt;
    up = upVec;
    distance = (position - target).length();
}

void Camera::orbit(float deltaX, float deltaY) {
    yaw += deltaX;
    pitch += deltaY;
    pitch = qBound(-89.0f, pitch, 89.0f);

    float radPitch = qDegreesToRadians(pitch);
    float radYaw = qDegreesToRadians(yaw);

    QVector3D dir;
    dir.setX(distance * qCos(radPitch) * qSin(radYaw));
    dir.setY(distance * qSin(radPitch));
    dir.setZ(distance * qCos(radPitch) * qCos(radYaw));

    position = target + dir;
}

void Camera::zoom(float amount) {
    distance = qMax(0.1f, distance - amount);
    orbit(0,0); // recompute position based on pitch, yaw, distance
}

void Camera::setOrientation(float newPitch, float newYaw, float newDistance) {
    pitch = newPitch;
    yaw = newYaw;
    distance = newDistance;
    orbit(0,0); // update position
}

void Camera::scaleOrtho(float scale) {
    orthoLeft   *= scale;
    orthoRight  *= scale;
    orthoBottom *= scale;
    orthoTop    *= scale;
    setOrthographic(orthoLeft, orthoRight, orthoBottom, orthoTop, nearPlane_, farPlane_);
 }

void Camera::pan(const QVector3D& delta) {
    position += delta;
    target   += delta;
}


// ---------------- AICAD Implementation ----------------
AICAD::AICAD(QWidget* parent)
    : QOpenGLWidget(parent), drawingRect(false), currentView(SketchView::None)
{
    setMouseTracking(true);
}

AICAD::~AICAD() {}

void AICAD::setSketchView(SketchView view) {
    currentView = view;
    float aspect = float(width())/float(height());

    switch(view)
    {
    case SketchView::Top:
        // Looking down along +Z
        camera.lookAt(QVector3D(0,0,10), QVector3D(0,0,0), QVector3D(0,1,0));
        camera.setOrthographic(-5*aspect,5*aspect,-5,5,-20,20);
        break;

    case SketchView::Bottom:
        // Looking up along -Z
        camera.lookAt(QVector3D(0,0,-10), QVector3D(0,0,0), QVector3D(0,1,0));
        camera.setOrthographic(-5*aspect,5*aspect,-5,5,-20,20);
        break;

    case SketchView::Front:
        // Looking along -Y
        camera.lookAt(QVector3D(0,-10,0), QVector3D(0,0,0), QVector3D(0,0,1));
        camera.setOrthographic(-5*aspect,5*aspect,-5,5,-20,20);
        break;

    case SketchView::Back:
        // Looking along +Y
        camera.lookAt(QVector3D(0,10,0), QVector3D(0,0,0), QVector3D(0,0,1));
        camera.setOrthographic(-5*aspect,5*aspect,-5,5,-20,20);
        break;

    case SketchView::Right: // Right side
        camera.lookAt(QVector3D(10,0,0), QVector3D(0,0,0), QVector3D(0,0,1));
        camera.setOrthographic(-5*aspect,5*aspect,-5,5,-20,20);
        break;

    case SketchView::Left:
        camera.lookAt(QVector3D(-10,0,0), QVector3D(0,0,0), QVector3D(0,0,1));
        camera.setOrthographic(-5*aspect,5*aspect,-5,5,-20,20);
        break;

    default: // Isometric / perspective
        // Standard isometric: rotate 35.264° around X and 45° around Y
        camera.setOrientation(-35.264f, 45.0f, 15.0f);
        camera.lookAt(QVector3D(5.773f,5.773f,5.773f), QVector3D(0,0,0), QVector3D(0,0,1));
        camera.setPerspective(45.0f, aspect, 0.1f, 100.0f);
        break;
    }

    update();
}

QVector3D AICAD::screenToWorld(const QPoint& screenPos) {
    // Convert screen → NDC
    float x = (2.0f * screenPos.x()) / width() - 1.0f;
    float y = 1.0f - (2.0f * screenPos.y()) / height();
    QVector4D rayClip(x, y, -1.0f, 1.0f); // start at near plane in clip space
    QVector4D rayFarClip(x, y, 1.0f, 1.0f); // end at far plane

    QMatrix4x4 inv = (camera.getProjectionMatrix() * camera.getViewMatrix()).inverted();
    QVector4D rayStartWorld = inv * rayClip;
    QVector4D rayEndWorld   = inv * rayFarClip;
    rayStartWorld /= rayStartWorld.w();
    rayEndWorld   /= rayEndWorld.w();

    QVector3D rayOrigin = QVector3D(rayStartWorld);
    QVector3D rayDir = (QVector3D(rayEndWorld) - rayOrigin).normalized();

    // Choose plane depending on view
    QVector3D planeNormal(0,0,1); // default Z=0 plane
    float planeD = 0.0f;          // plane equation: n·p + d = 0

    switch (currentView) {
    case SketchView::Top:
    case SketchView::Bottom:
        planeNormal = QVector3D(0,0,1); planeD = 0; break; // XY plane
    case SketchView::Front:
    case SketchView::Back:
        planeNormal = QVector3D(0,1,0); planeD = 0; break; // XZ plane
    case SketchView::Right:
    case SketchView::Left:
        planeNormal = QVector3D(1,0,0); planeD = 0; break; // YZ plane
    default:
        // Free orbit: use Z=0 plane for picking
        planeNormal = QVector3D(0,0,1); planeD = 0;
        break;
    }

    // Ray-plane intersection: t = -(n·o + d) / (n·dir)
    float denom = QVector3D::dotProduct(planeNormal, rayDir);
    if (qFuzzyIsNull(denom)) {
        return rayOrigin; // Parallel → return origin
    }
    float t = -(QVector3D::dotProduct(planeNormal, rayOrigin) + planeD) / denom;
    return rayOrigin + t * rayDir;
}

void AICAD::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.8f,0.8f,0.8f,1.0f);
}

void AICAD::resizeGL(int w, int h) {
    glViewport(0,0,w,h);
    setSketchView(currentView);
}

void AICAD::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Apply camera matrices
    QMatrix4x4 projection = camera.getProjectionMatrix();
    QMatrix4x4 view = camera.getViewMatrix();

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(projection.constData());

    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.constData());

    drawAxes();

    for (const auto& rect : extrudedRects){
        drawExtrudedCube(rect,1.0f);
        glColor3f(0.8f,0.2f,0.2f);
        drawRectangle(rect);}

    if (drawingRect){
        glColor3f(0.0f, 1.0f, 0.0f); // bright green
        drawRectangle(currentRect);}
}

void AICAD::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        QVector3D worldPos = screenToWorld(event->pos());

        if (!drawingRect) {
            // First corner picked
            currentRect.p1 = worldPos;
            currentRect.p2 = worldPos;
            drawingRect = true;
        } else {
            // Second corner picked → finalize rectangle
            currentRect.p2 = worldPos;
            extrudedRects.push_back(currentRect);
            drawingRect = false;
        }
        update();
    }

    if (event->button() == Qt::RightButton) {
        lastMousePos = event->pos(); // prepare for orbit
    }

    if (event->button() == Qt::MiddleButton) {
        lastMousePos = event->pos();   // prepare for pan
        setCursor(Qt::ClosedHandCursor); // ✅ hand cursor while panning
    }

}

void AICAD::mouseMoveEvent(QMouseEvent* event) {
    // --- Orbit with right mouse button ---
    if ((event->buttons() & Qt::RightButton) && currentView == SketchView::None) {
        int dx = event->x() - lastMousePos.x();
        int dy = event->y() - lastMousePos.y();
        camera.orbit(dx * 0.5f, dy * 0.5f);
        lastMousePos = event->pos();
        update();
        return;
    }

    // --- Pan with MMB ---
    if (event->buttons() & Qt::MiddleButton) {
        QPoint delta = event->pos() - lastMousePos;
        lastMousePos = event->pos();
        float aspect = float(width()) / float(height());

        if (camera.isPerspective()) {
            // --- Perspective pan ---
            float fovY = camera.fov_ * M_PI / 180.0f;
            float tanHalfFov = tan(fovY / 2.0f);

            float viewHeightAtTarget = 2.0f * (camera.position - camera.target).length() * tanHalfFov;
            float viewWidthAtTarget  = viewHeightAtTarget * aspect;

            // Convert pixel delta to world units at target distance
            float dx = (float(delta.y()) / height()) * viewHeightAtTarget; // remove negative here
            float dy = (float(delta.x()) / width())  * viewWidthAtTarget;

            // Extract camera axes from view matrix
            QMatrix4x4 view = camera.getViewMatrix();

            // OpenGL column-major: columns are right, up, -forward
            QVector3D right(view(0,0), view(1,0), view(2,0));
            QVector3D up   (view(0,1), view(1,1), view(2,1));

            // Apply pan (notice signs: +dx*right, +dy*up)
            camera.pan(dx * right + dy * up);
        }
        else {
            // Scale movement by viewport size
            float scale = 0.01f;
            QVector3D panDelta(-delta.x() * scale, delta.y() * scale, 0);

            // Pan in camera’s local axes
            QMatrix4x4 view = camera.getViewMatrix();
            QVector3D right(view(0,0), view(1,0), view(2,0));
            QVector3D up(view(0,1), view(1,1), view(2,1));

            camera.pan(right * panDelta.x() + up * panDelta.y());
        }
        update();
        return;
    }

    // --- Rubber band preview while drawing rectangle ---
    if (drawingRect) {
        QVector3D worldPos = screenToWorld(event->pos());
        currentRect.p2 = worldPos;   // update opposite corner
        update(); // triggers paintGL -> draws rubber band rectangle
    }
}

void AICAD::wheelEvent(QWheelEvent* event) {
    float numSteps = event->angleDelta().y() * 0.001f;  // scale factor
    QPoint cursorPos = event->position().toPoint();

    // 1. Get world coordinate under cursor BEFORE zoom
    QVector3D before = screenToWorld(cursorPos);

    if (currentView == SketchView::None) {
        // Free orbit/perspective mode zoom
        camera.zoom(numSteps * 10.0f);
    } else {
        // Orthographic zoom (scale projection window)
        float scale = (numSteps > 0) ? 0.9f : 1.1f;
        camera.scaleOrtho(scale);
    }

    // 2. Get world coordinate under cursor AFTER zoom
    QVector3D after = screenToWorld(cursorPos);

    // 3. Shift camera target so "before" stays under cursor
    QVector3D delta = before - after;
    camera.pan(delta);

    update();
}

void AICAD::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        setCursor(Qt::ArrowCursor); // ✅ restore cursor
    }
}


void AICAD::keyPressEvent(QKeyEvent* event) {
    switch(event->key())
    {
    case Qt::Key_S: setSketchView(SketchView::Top); break;
    case Qt::Key_X: setSketchView(SketchView::Bottom); break;
    case Qt::Key_T: setSketchView(SketchView::Left); break;
    case Qt::Key_Y: setSketchView(SketchView::Right); break;
    case Qt::Key_Q: setSketchView(SketchView::Front); break;
    case Qt::Key_H: setSketchView(SketchView::Back); break;
    case Qt::Key_I: setSketchView(SketchView::None); break;
    default: QWidget::keyPressEvent(event); break;
    }
}

QVector3D AICAD::mapToPlane(int x, int y) {
    float nx = (2.0f * x / width()) - 1.0f;
    float ny = 1.0f - (2.0f * y / height());
    QMatrix4x4 inv = (camera.getProjectionMatrix() * camera.getViewMatrix()).inverted();
    QVector4D nearPoint = inv * QVector4D(nx, ny, -1.0f, 1.0f);
    QVector4D farPoint  = inv * QVector4D(nx, ny, 1.0f, 1.0f);
    nearPoint /= nearPoint.w();
    farPoint  /= farPoint.w();
    QVector3D p1 = nearPoint.toVector3D();
    QVector3D p2 = farPoint.toVector3D();
    QVector3D dir = p2 - p1;

    QVector3D intersection = p1;
    switch(currentView)
    {
    case SketchView::Top:
        if(dir.z()!=0) intersection = p1 + (-p1.z()/dir.z())*dir;
        break;
    case SketchView::Front:
        if(dir.y()!=0) intersection = p1 + (-p1.y()/dir.y())*dir;
        break;
    case SketchView::Right:
        if(dir.x()!=0) intersection = p1 + (-p1.x()/dir.x())*dir;
        break;
    default:
        if(dir.z()!=0) intersection = p1 + (-p1.z()/dir.z())*dir;
        break;
    }
    return intersection;
}

void AICAD::drawAxes() {
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glColor3f(1,0,0); glVertex3f(0,0,0); glVertex3f(5,0,0);
    glColor3f(0,1,0); glVertex3f(0,0,0); glVertex3f(0,5,0);
    glColor3f(0,0,1); glVertex3f(0,0,0); glVertex3f(0,0,5);
    glEnd();
}

void AICAD::drawRectangle(const Rectangle2D& rect) {
    glBegin(GL_LINE_LOOP);
    glVertex3f(rect.p1.x(), rect.p1.y(), rect.p1.z());
    glVertex3f(rect.p2.x(), rect.p1.y(), rect.p1.z());
    glVertex3f(rect.p2.x(), rect.p2.y(), rect.p2.z());
    glVertex3f(rect.p1.x(), rect.p2.y(), rect.p1.z());
    glEnd();
}

void AICAD::drawExtrudedCube(const Rectangle2D& rect, float height) {
    QVector3D p1 = rect.p1;
    QVector3D p2 = rect.p2;
    QVector3D v[8] = {
        {p1.x(),p1.y(),0}, {p2.x(),p1.y(),0}, {p2.x(),p2.y(),0}, {p1.x(),p2.y(),0},
        {p1.x(),p1.y(),height}, {p2.x(),p1.y(),height}, {p2.x(),p2.y(),height}, {p1.x(),p2.y(),height}
    };
    glColor3f(0.2f,0.2f,0.8f);
    glBegin(GL_QUADS);
    glVertex3fv((float*)&v[0]); glVertex3fv((float*)&v[1]);
    glVertex3fv((float*)&v[2]); glVertex3fv((float*)&v[3]);
    glVertex3fv((float*)&v[4]); glVertex3fv((float*)&v[5]);
    glVertex3fv((float*)&v[6]); glVertex3fv((float*)&v[7]);
    glVertex3fv((float*)&v[0]); glVertex3fv((float*)&v[1]);
    glVertex3fv((float*)&v[5]); glVertex3fv((float*)&v[4]);
    glVertex3fv((float*)&v[1]); glVertex3fv((float*)&v[2]);
    glVertex3fv((float*)&v[6]); glVertex3fv((float*)&v[5]);
    glVertex3fv((float*)&v[2]); glVertex3fv((float*)&v[3]);
    glVertex3fv((float*)&v[7]); glVertex3fv((float*)&v[6]);
    glVertex3fv((float*)&v[3]); glVertex3fv((float*)&v[0]);
    glVertex3fv((float*)&v[4]); glVertex3fv((float*)&v[7]);
    glEnd();
}
