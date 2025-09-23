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


// ---------------- CadView Implementation ----------------
CadView::CadView(QWidget* parent)
    : QOpenGLWidget(parent), drawingRect(false), currentView(SketchView::None)
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
    drawingRect = false;
    awaitingHeight = false;
    mode = CadMode::Sketching;
    update();
    qDebug() << "Sketch mode started on sketch" << sketch->id;
}

void CadView::startExtrudeMode(std::shared_ptr<SketchNode> sketch) {
    editMode = EditMode::Extruding;
    pendingSketch = sketch;
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

    // Apply camera matrices
    QMatrix4x4 projection = camera.getProjectionMatrix();
    QMatrix4x4 view = camera.getViewMatrix();

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(projection.constData());

    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.constData());

    drawAxes();

    // --- Draw all sketches and features ---
    doc.drawAll();

    for (auto& f : doc.features) {
        if (f->id == highlightedFeatureId) {
            glColor3f(1.0f, 0.0f, 0.0f); // highlight red
        } else {
            glColor3f(0.7f, 0.7f, 0.7f); // normal gray
        }
        f->draw();
    }

    // --- Rectangle rubber band preview ---
    if (drawingRect && !awaitingHeight) {
        drawRectangle(currentRect, Qt::DashLine); // use dashed outline
    }

    // --- Extrude ghost preview ---
    if (awaitingHeight && pendingSketch) {
        drawExtrudedCube({currentRect.p1, currentRect.p2}, previewHeight, true);
        // last param 'true' = ghost mode (transparent / wireframe)
    }
}

static SketchPlane viewToPlane(SketchView view) {
    switch (view) {
    case SketchView::Top:    return SketchPlane::XY;  // looking down Z
    case SketchView::Front:  return SketchPlane::XZ;  // looking along -Y
    case SketchView::Right:  return SketchPlane::YZ;  // looking along +X
    case SketchView::Bottom: return SketchPlane::XY;  // reversed normal
    case SketchView::Back:   return SketchPlane::XZ;  // reversed normal
    case SketchView::Left:   return SketchPlane::YZ;  // reversed normal
    default:                 return SketchPlane::XY;  // fallback
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
        QVector3D worldPos = screenToWorld(event->pos());

        // ───────────── Extrude mode ─────────────
        if (mode == CadMode::Extruding && awaitingHeight && pendingSketch) {
            float height = (worldPos - baseP2).length();

            auto extrude = std::make_shared<ExtrudeNode>();
            extrude->sketch = pendingSketch;   // link sketch
            extrude->height = height;
            extrude->direction = planeNormal(pendingSketch->plane);
            extrude->evaluate();
            doc.addFeature(extrude);

            // notify main window
            emit featureAdded();

            // reset state
            awaitingHeight = false;
            pendingSketch.reset();
            mode = CadMode::Idle;
            update();
            return;
        }

        // ───────────── Sketch mode ─────────────
        if (mode == CadMode::Sketching) {
            if (!drawingRect) {
                // first corner
                currentRect.p1 = worldPos;
                currentRect.p2 = worldPos;
                drawingRect = true;
                awaitingHeight = false;
            } else {
                // second corner → finalize sketch
                currentRect.p2 = worldPos;

                //SketchPlane plane = viewToPlane(currentView);
                //auto sketch = doc.createSketch(plane);

                auto poly = std::make_shared<PolylineEntity>();
                poly->points = rectanglePointsForPlane(currentRect, pendingSketch->plane);
                pendingSketch->entities.push_back(poly);

                // ready for extrusion
                //pendingSketch = sketch;
                baseP2 = currentRect.p2;
                awaitingHeight = true;

                drawingRect = false;
                mode = CadMode::Idle;
            }
            update();
        }
    }

    if (event->button() == Qt::RightButton) {
        lastMousePos = event->pos(); // orbit
    }

    if (event->button() == Qt::MiddleButton) {
        lastMousePos = event->pos();   // pan
        setCursor(Qt::ClosedHandCursor);
    }
}

void CadView::mouseMoveEvent(QMouseEvent* event) {
    // --- Orbit with right mouse button ---
    if ((event->buttons() & Qt::RightButton) && currentView == SketchView::None) {
        int dx = event->pos().x() - lastMousePos.x();
        int dy = event->pos().y() - lastMousePos.y();
        camera.orbit(-dy * 0.5f, -dx * 0.5f);
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
    if (drawingRect && !awaitingHeight) {
        QVector3D worldPos = screenToWorld(event->pos());
        currentRect.p2 = worldPos;   // update opposite corner dynamically
        update(); // triggers paintGL -> draws preview rectangle
    }

    // --- Rubber band preview while picking extrusion height ---
    if (mode == CadMode::Extruding && awaitingHeight) {
        QVector3D worldPos = screenToWorld(event->pos());
        previewHeight = (worldPos - baseP2).length();
        update(); // paintGL will show ghost extrude
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

void CadView::drawExtrudedCube(const Rectangle2D& rect, float height, bool ghost) {
    QVector3D p1 = rect.p1;
    QVector3D p2 = rect.p2;

    // Base corners in XY plane
    QVector3D v0(p1.x(), p1.y(), p1.z()); // bottom-left
    QVector3D v1(p2.x(), p1.y(), p1.z()); // bottom-right
    QVector3D v2(p2.x(), p2.y(), p1.z()); // top-right
    QVector3D v3(p1.x(), p2.y(), p1.z()); // top-left

    // Extruded (top) corners
    QVector3D v4 = v0 + QVector3D(0, 0, height);
    QVector3D v5 = v1 + QVector3D(0, 0, height);
    QVector3D v6 = v2 + QVector3D(0, 0, height);
    QVector3D v7 = v3 + QVector3D(0, 0, height);

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
    // Bottom face
    glVertex3f(v0.x(), v0.y(), v0.z());
    glVertex3f(v1.x(), v1.y(), v1.z());
    glVertex3f(v2.x(), v2.y(), v2.z());
    glVertex3f(v3.x(), v3.y(), v3.z());

    // Top face
    glVertex3f(v4.x(), v4.y(), v4.z());
    glVertex3f(v5.x(), v5.y(), v5.z());
    glVertex3f(v6.x(), v6.y(), v6.z());
    glVertex3f(v7.x(), v7.y(), v7.z());

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
