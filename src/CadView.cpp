#include "CadView.h"
#include <QKeyEvent>
#include <QtMath>
#include <QDebug>
#include <QPrintDialog>

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
    // Compute current direction from target to camera
    QVector3D dir = position - target;
    float distance = dir.length();
    dir.normalize();

    // Compute current yaw and pitch from direction
    float radPitch = qAsin(dir.y());
    float radYaw = qAtan2(dir.x(), dir.z());

    // Apply deltas
    radYaw += qDegreesToRadians(deltaX);
    radPitch += qDegreesToRadians(deltaY);

    // Clamp pitch to avoid gimbal lock
    radPitch = qBound(qDegreesToRadians(-89.0f), radPitch, qDegreesToRadians(89.0f));

    // Compute new direction
    dir.setX(qCos(radPitch) * qSin(radYaw));
    dir.setY(qSin(radPitch));
    dir.setZ(qCos(radPitch) * qCos(radYaw));

    // Update position
    position = target + dir * distance;
}

void Camera::zoom(float amount) {
    // Direction from camera to target
    QVector3D viewDir = (target - position).normalized();

    // Move camera along view direction
    position += viewDir * amount;

    // Prevent flipping through target
    float minDist = 0.1f;
    if ((position - target).length() < minDist) {
        position = target - viewDir * minDist;
    }

    // Update distance (if you still want to track it separately)
    distance = (position - target).length();
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


// ---------------- CadView Implementation ----------------
CadView::CadView(QWidget* parent)
    : QOpenGLWidget(parent), currentView(SketchView::None)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

CadView::~CadView() {}

void CadView::setSketchView(SketchView view) {
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

QVector3D CadView::screenToWorld(const QPoint& screenPos) {
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

void CadView::highlightFeature(int id) {
    highlightedFeatureId = id;
    update();
}

// -------------------- Printing --------------------
void CadView::printView() {
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageOrientation(QPageLayout::Landscape);
    QPrintDialog dlg(&printer,this);
    if (dlg.exec()==QDialog::Accepted) {
        QPainter painter(&printer);
        render(&painter);
    }
}

void CadView::exportPdf(const QString &file) {
    QPdfWriter pdf(file);
    pdf.setPageOrientation(QPageLayout::Landscape);
    pdf.setPageSize(QPageSize(QPageSize::A4));

    QPainter painter(&pdf);
    QRect pageRect = pdf.pageLayout().paintRectPixels(pdf.resolution());
    QRectF srcRect = rect();

    qreal sx = (qreal)pageRect.width()  / srcRect.width();
    qreal sy = (qreal)pageRect.height() / srcRect.height();
    qreal s = qMin(sx, sy);

    painter.translate(pageRect.center());
    painter.scale(s, s);
    painter.translate(-srcRect.center());
    render(&painter); // render widget to PDF
}

void CadView::startSketchMode(std::shared_ptr<SketchNode> sketch) {
    pendingSketch = sketch;
    mode = CadMode::Sketching;
    update();
    qDebug() << "Sketch mode started on sketch" << sketch->id;
}

void CadView::startExtrudeMode(std::shared_ptr<SketchNode> sketch) {
    editMode = EditMode::Extruding;
    pendingSketch = sketch;
    if (auto polyline = std::dynamic_pointer_cast<PolylineEntity>(sketch->entities.at(0))) {
        if (polyline->points.size() >= 2) {
            currentRect.p1 = polyline->points[0];
            currentRect.p2 = polyline->points[2]; // Assuming rectangular order
        }
    }
    awaitingHeight = true;
    mode = CadMode::Extruding;
    qDebug() << "Extrude mode started on sketch" << sketch->id;
}

void CadView::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.8f,0.8f,0.8f,1.0f);
}

void CadView::resizeGL(int w, int h) {
    glViewport(0,0,w,h);
    setSketchView(currentView);
}

void CadView::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    QMatrix4x4 projection = camera.getProjectionMatrix();
    QMatrix4x4 view = camera.getViewMatrix();

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(projection.constData());

    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.constData());

    drawAxes();

    doc.drawAll();

    for (auto& f : doc.features) {
        if (f->id == highlightedFeatureId) {
            glColor3f(1.0f, 0.0f, 0.0f);
        } else {
            glColor3f(0.7f, 0.7f, 0.7f);
        }
        f->draw();
    }

//    if (awaitingHeight) {
//        drawRectangle({getPointState.previousPoint, getPointState.currentPoint}, Qt::DashLine);
//    }

    if (awaitingHeight && pendingSketch) {
        drawExtrudedCube(previewHeight, true);
    }

    // Draw GetPoint rubber band
    if (getPointState.active && getPointState.hasPreviousPoint && !getPointState.keyboardMode) {
//        drawRubberBandLine(getPointState.previousPoint, getPointState.currentPoint);
        drawRectangle({getPointState.previousPoint, getPointState.currentPoint}, Qt::DashLine);
    }
}

QVector3D planeNormal(SketchPlane plane) {
    switch (plane) {
    case SketchPlane::XY: return QVector3D(0,0,1);
    case SketchPlane::XZ: return QVector3D(0,1,0);
    case SketchPlane::YZ: return QVector3D(1,0,0);
    case SketchPlane::Custom: return QVector3D(1,1,1);
    }
    return QVector3D(0,0,1); // fallback
}

static QVector<QVector3D> rectanglePointsForPlane(
    const Rectangle2D& rect,
    SketchPlane plane)
{
    QVector<QVector3D> pts;

    switch (plane) {
    case SketchPlane::XY: {
        float z = rect.p1.z();
        pts << QVector3D(rect.p1.x(), rect.p1.y(), z)
            << QVector3D(rect.p2.x(), rect.p1.y(), z)
            << QVector3D(rect.p2.x(), rect.p2.y(), z)
            << QVector3D(rect.p1.x(), rect.p2.y(), z)
            << QVector3D(rect.p1.x(), rect.p1.y(), z);
        break;
    }
    case SketchPlane::YZ: {
        float x = rect.p1.x();
        pts << QVector3D(x, rect.p1.y(), rect.p1.z())
            << QVector3D(x, rect.p2.y(), rect.p1.z())
            << QVector3D(x, rect.p2.y(), rect.p2.z())
            << QVector3D(x, rect.p1.y(), rect.p2.z())
            << QVector3D(x, rect.p1.y(), rect.p1.z());
        break;
    }
    case SketchPlane::XZ: {
        float y = rect.p1.y();
        pts << QVector3D(rect.p1.x(), y, rect.p1.z())
            << QVector3D(rect.p2.x(), y, rect.p1.z())
            << QVector3D(rect.p2.x(), y, rect.p2.z())
            << QVector3D(rect.p1.x(), y, rect.p2.z())
            << QVector3D(rect.p1.x(), y, rect.p1.z());
        break;
    }
    default:
        break;
    }

    return pts;
}



void CadView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // Handle GetPoint mode FIRST (highest priority)
        if (getPointState.active && !getPointState.keyboardMode) {
            QVector3D worldPos = screenToWorld(event->pos());
            QVector2D planePt = worldToPlane(worldPos);

            getPointState.active = false;
            emit pointAcquired(planePt);
            update();
            return;
        }

        QVector3D worldPos = screenToWorld(event->pos());

        // Handle Extrude mode
        if (mode == CadMode::Extruding && awaitingHeight && pendingSketch) {
            float height = (worldPos - baseP2).length();

            auto extrude = std::make_shared<ExtrudeNode>();
            extrude->sketch = pendingSketch;
            extrude->height = height;
            extrude->direction = planeNormal(pendingSketch->plane);
            extrude->evaluate();
            doc.addFeature(extrude);

            emit featureAdded();

            awaitingHeight = false;
            pendingSketch.reset();
            mode = CadMode::Idle;
            update();
            return;
        }

    }

    if (event->button() == Qt::RightButton) {
        // Cancel getpoint on right-click
        if (getPointState.active) {
            cancelGetPoint();
            return;
        }
        lastMousePos = event->pos();
    }

    if (event->button() == Qt::MiddleButton) {
        lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void CadView::mouseMoveEvent(QMouseEvent* event) {
    // Handle GetPoint rubber band
    bool bOk = false;
    if (getPointState.hasPreviousPoint) {
        QVector3D worldPos = screenToWorld(event->pos());
        getPointState.currentPoint = worldToPlane(worldPos);
        bOk=true;
//        return;
    }

    if (getPointState.active && bOk && !getPointState.keyboardMode) {
        QVector3D worldPos = screenToWorld(event->pos());
        getPointState.currentPoint = worldToPlane(worldPos);
        update();
        return;
    }

    // ... existing mouse move handling ...
    if ((event->buttons() & Qt::RightButton) && currentView == SketchView::None) {
        int dx = event->pos().x() - lastMousePos.x();
        int dy = event->pos().y() - lastMousePos.y();
        camera.orbit(-dy * 0.5f, -dx * 0.5f);
        lastMousePos = event->pos();
        update();
        return;
    }

    if (event->buttons() & Qt::MiddleButton) {
        QPoint delta = event->pos() - lastMousePos;
        lastMousePos = event->pos();
        float aspect = float(width()) / float(qMax(1, height()));

        if (camera.isPerspective()) {
            float fovY = camera.fov_ * M_PI / 180.0f;
            float tanHalfFov = tan(fovY / 2.0f);

            float viewHeightAtTarget = 2.0f * (camera.position - camera.target).length() * tanHalfFov;
            float viewWidthAtTarget  = viewHeightAtTarget * aspect;

            float dx = (float(delta.y()) / qMax(1, height())) * viewHeightAtTarget;
            float dy = (float(delta.x()) / qMax(1, width()))  * viewWidthAtTarget;

            QMatrix4x4 view = camera.getViewMatrix();

            QVector3D right(view(0,0), view(1,0), view(2,0));
            QVector3D up   (view(0,1), view(1,1), view(2,1));

            camera.pan(dx * right + dy * up);
        }
        else {
            float scale = 0.01f;
            QVector3D panDelta(-delta.x() * scale, delta.y() * scale, 0);

            QMatrix4x4 view = camera.getViewMatrix();
            QVector3D right(view(0,0), view(1,0), view(2,0));
            QVector3D up(view(0,1), view(1,1), view(2,1));

            camera.pan(right * panDelta.x() + up * panDelta.y());
        }
        update();
        return;
    }

    if (!awaitingHeight) {
        QVector3D worldPos = screenToWorld(event->pos());
        currentRect.p2 = worldPos;
        update();
    }

    if (mode == CadMode::Extruding && awaitingHeight) {
        QVector3D worldPos = screenToWorld(event->pos());
        baseP2 = currentRect.p2;
        previewHeight = (worldPos - baseP2).length();
        update();
    }
}

void CadView::wheelEvent(QWheelEvent* event) {
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

void CadView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        setCursor(Qt::ArrowCursor); // ✅ restore cursor
    }
}


void CadView::keyPressEvent(QKeyEvent* event) {
    // Handle GetPoint keyboard input mode
    if (getPointState.active) {
        if (event->key() == Qt::Key_Escape) {
            cancelGetPoint();
            return;
        }

        // Any key press switches to keyboard mode
        if (!getPointState.keyboardMode && !event->text().isEmpty()) {
            getPointState.keyboardMode = true;
            // Signal MainWindow to activate commandInput with the key
            emit getPointCancelled(); // Reuse signal, MainWindow will check state
            return;
        }
    }

    // ... existing key handling ...
    switch(event->key())
    {
    case Qt::Key_U: setSketchView(SketchView::Top); break;
    case Qt::Key_D: setSketchView(SketchView::Bottom); break;
    case Qt::Key_L: setSketchView(SketchView::Left); break;
    case Qt::Key_R: setSketchView(SketchView::Right); break;
    case Qt::Key_F: setSketchView(SketchView::Front); break;
    case Qt::Key_B: setSketchView(SketchView::Back); break;
    case Qt::Key_I: setSketchView(SketchView::None); break;
    default: QWidget::keyPressEvent(event); break;
    }
}

QVector3D CadView::mapToPlane(int x, int y) {
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

void CadView::drawAxes() {
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glColor3f(1,0,0); glVertex3f(0,0,0); glVertex3f(5,0,0);
    glColor3f(0,1,0); glVertex3f(0,0,0); glVertex3f(0,5,0);
    glColor3f(0,0,1); glVertex3f(0,0,0); glVertex3f(0,0,5);
    glEnd();
}

QVector2D CadView::worldToPlane(const QVector3D& worldPt) {
    if (!pendingSketch) {
        // Default to current view plane
        switch (currentView) {
        case SketchView::Top:
        case SketchView::Bottom:
            return QVector2D(worldPt.x(), worldPt.y());
        case SketchView::Front:
        case SketchView::Back:
            return QVector2D(worldPt.x(), worldPt.z());
        case SketchView::Right:
        case SketchView::Left:
            return QVector2D(worldPt.y(), worldPt.z());
        default:
            return QVector2D(worldPt.x(), worldPt.y());
        }
    }

    // Project onto sketch plane
    switch (pendingSketch->plane) {
    case SketchPlane::XY:
        return QVector2D(worldPt.x(), worldPt.y());
    case SketchPlane::XZ:
        return QVector2D(worldPt.x(), worldPt.z());
    case SketchPlane::YZ:
        return QVector2D(worldPt.y(), worldPt.z());
    default:
        return QVector2D(worldPt.x(), worldPt.y());
    }
}

QVector3D CadView::planeToWorld(const QVector2D& planePt) {
    if (!pendingSketch) {
        switch (currentView) {
        case SketchView::Top:
        case SketchView::Bottom:
            return QVector3D(planePt.x(), planePt.y(), 0);
        case SketchView::Front:
        case SketchView::Back:
            return QVector3D(planePt.x(), 0, planePt.y());
        case SketchView::Right:
        case SketchView::Left:
            return QVector3D(0, planePt.x(), planePt.y());
        default:
            return QVector3D(planePt.x(), planePt.y(), 0);
        }
    }

    switch (pendingSketch->plane) {
    case SketchPlane::XY:
        return QVector3D(planePt.x(), planePt.y(), 0);
    case SketchPlane::XZ:
        return QVector3D(planePt.x(), 0, planePt.y());
    case SketchPlane::YZ:
        return QVector3D(0, planePt.x(), planePt.y());
    default:
        return QVector3D(planePt.x(), planePt.y(), 0);
    }
}

void CadView::startGetPoint(const QString& prompt, const QVector2D* previousPt) {
    getPointState.active = true;
    getPointState.prompt = prompt;
    getPointState.keyboardMode = false;

    if (previousPt) {
        getPointState.hasPreviousPoint = true;
        getPointState.previousPoint = *previousPt;
    } else {
        getPointState.hasPreviousPoint = false;
    }

    // Notify MainWindow to show prompt in commandInput
    emit pointAcquired(QVector2D()); // Special signal for prompt setup

    setFocus(); // Ensure CadView gets keyboard input
    update();
}

void CadView::cancelGetPoint() {
    getPointState.active = false;
    getPointState.hasPreviousPoint = false;
    getPointState.keyboardMode = false;
    emit getPointCancelled();
    update();
}

void CadView::drawRubberBandLine(const QVector2D& p1, const QVector2D& p2) {
    QVector3D w1 = planeToWorld(p1);
    QVector3D w2 = planeToWorld(p2);

    glEnable(GL_LINE_STIPPLE);
    glLineStipple(1, 0xAAAA);
    glColor3f(1.0f, 1.0f, 0.0f); // Yellow
    glLineWidth(1.5f);

    glBegin(GL_LINES);
    glVertex3f(w1.x(), w1.y(), w1.z());
    glVertex3f(w2.x(), w2.y(), w2.z());
    glEnd();

    glDisable(GL_LINE_STIPPLE);
    glLineWidth(1.0f);
}


// Utility: build orthonormal basis from plane normal
static void planeBasis(const QVector3D& normal, QVector3D& u, QVector3D& v) {
    QVector3D n = normal.normalized();
    // pick an arbitrary vector not parallel to n
    QVector3D helper = (fabs(n.x()) > 0.9f) ? QVector3D(0,1,0) : QVector3D(1,0,0);
    u = QVector3D::crossProduct(n, helper).normalized();
    v = QVector3D::crossProduct(n, u).normalized();
}

void CadView::drawRectangle(const Rectangle2D& rect, Qt::PenStyle style) {
    if (!pendingSketch) return;

    // 1. Get plane definition from sketch
    SketchPlane plane = pendingSketch->plane;
    QVector3D origin(0,0,0);
    QVector3D normal = planeNormal(plane);

    QVector3D u, v;
    planeBasis(normal, u, v);  // local 2D basis on this plane

    // 2. Convert corner points (rect.p1, rect.p2) from "picked" world points into plane coords
    // project onto plane basis
    auto toPlane = [&](const QVector3D& p) {
        return QVector2D(QVector3D::dotProduct(p - origin, u),
                         QVector3D::dotProduct(p - origin, v));
    };

    QVector2D p1_2d = toPlane(rect.p1);
    QVector2D p2_2d = toPlane(rect.p2);

    // 3. Build 4 corners in 2D plane coordinates
    QVector2D v0_2d(p1_2d.x(), p1_2d.y());
    QVector2D v1_2d(p2_2d.x(), p1_2d.y());
    QVector2D v2_2d(p2_2d.x(), p2_2d.y());
    QVector2D v3_2d(p1_2d.x(), p2_2d.y());

    // 4. Map back to world coordinates
    auto toWorld = [&](const QVector2D& p2d) {
        return origin + u * p2d.x() + v * p2d.y();
    };

    QVector3D v0 = toWorld(v0_2d);
    QVector3D v1 = toWorld(v1_2d);
    QVector3D v2 = toWorld(v2_2d);
    QVector3D v3 = toWorld(v3_2d);

    // 5. Draw with OpenGL
    if (style == Qt::DashLine) {
        glEnable(GL_LINE_STIPPLE);
        glLineStipple(1, 0xF0F0); // dashed
        glColor3f(1.0f, 1.0f, 0.0f); // yellow
    } else {
        glDisable(GL_LINE_STIPPLE);
        glColor3f(1.0f, 1.0f, 1.0f); // white
    }

    glBegin(GL_LINE_LOOP);
    glVertex3f(v0.x(), v0.y(), v0.z());
    glVertex3f(v1.x(), v1.y(), v1.z());
    glVertex3f(v2.x(), v2.y(), v2.z());
    glVertex3f(v3.x(), v3.y(), v3.z());
    glEnd();

    if (style == Qt::DashLine)
        glDisable(GL_LINE_STIPPLE);
}

void CadView::drawExtrudedCube(float height, bool ghost) {

    // Base corners - adjust based on sketch plane
    QVector3D v0, v1, v2, v3;
    QVector3D extrusionDir;

    auto poly = std::make_shared<PolylineEntity>();
    poly->points = rectanglePointsForPlane(currentRect, pendingSketch->plane);
    v0 = poly->points[0]; // bottom-left
    v1 = poly->points[1]; // bottom-right
    v2 = poly->points[2]; // top-right
    v3 = poly->points[3]; // top-left

    switch (pendingSketch->plane) {
    case SketchPlane::XY: // Top plane (original behavior)
        extrusionDir = QVector3D(0, 0, height);
        break;

    case SketchPlane::XZ: // Front plane
        extrusionDir = QVector3D(0, height, 0);
        break;

    case SketchPlane::YZ: // Right plane
        extrusionDir = QVector3D(height, 0, 0);
        break;

    case SketchPlane::Custom:
        // For custom planes, you might need additional logic
        // This assumes XY plane as default for now
        extrusionDir = QVector3D(0, 0, height);
        break;
    }

    // Extruded (opposite face) corners
    QVector3D v4 = v0 + extrusionDir;
    QVector3D v5 = v1 + extrusionDir;
    QVector3D v6 = v2 + extrusionDir;
    QVector3D v7 = v3 + extrusionDir;

    if (ghost) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glColor4f(0.2f, 0.8f, 1.0f, 0.6f); // cyan ghost
    } else {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glColor3f(0.2f, 0.8f, 1.0f);
    }

    glBegin(GL_QUADS);

    // Base face (sketch plane)
    glVertex3f(v0.x(), v0.y(), v0.z());
    glVertex3f(v1.x(), v1.y(), v1.z());
    glVertex3f(v2.x(), v2.y(), v2.z());
    glVertex3f(v3.x(), v3.y(), v3.z());

    // Top face (extruded face)
    glVertex3f(v4.x(), v4.y(), v4.z());
    glVertex3f(v7.x(), v7.y(), v7.z());
    glVertex3f(v6.x(), v6.y(), v6.z());
    glVertex3f(v5.x(), v5.y(), v5.z());

    // Side faces
    // v0-v1-v5-v4
    glVertex3f(v0.x(), v0.y(), v0.z());
    glVertex3f(v1.x(), v1.y(), v1.z());
    glVertex3f(v5.x(), v5.y(), v5.z());
    glVertex3f(v4.x(), v4.y(), v4.z());

    // v1-v2-v6-v5
    glVertex3f(v1.x(), v1.y(), v1.z());
    glVertex3f(v2.x(), v2.y(), v2.z());
    glVertex3f(v6.x(), v6.y(), v6.z());
    glVertex3f(v5.x(), v5.y(), v5.z());

    // v2-v3-v7-v6
    glVertex3f(v2.x(), v2.y(), v2.z());
    glVertex3f(v3.x(), v3.y(), v3.z());
    glVertex3f(v7.x(), v7.y(), v7.z());
    glVertex3f(v6.x(), v6.y(), v6.z());

    // v3-v0-v4-v7
    glVertex3f(v3.x(), v3.y(), v3.z());
    glVertex3f(v0.x(), v0.y(), v0.z());
    glVertex3f(v4.x(), v4.y(), v4.z());
    glVertex3f(v7.x(), v7.y(), v7.z());

    glEnd();

    if (ghost) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_BLEND);
    }
}
