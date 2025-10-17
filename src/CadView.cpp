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
        // Check for custom plane
        if (pendingSketch && pendingSketch->plane == SketchPlane::Custom) {
            planeNormal = pendingSketch->customPlane.normal;
            planeD = -QVector3D::dotProduct(planeNormal, pendingSketch->customPlane.origin);
        } else {
            planeNormal = QVector3D(0,0,1); planeD = 0;
        }
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
            // Store 2D points directly
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

// ==================== Grip System ====================
void CadView::updateGrips() {
    activeGrips.clear();

    for (const auto& entityRef : selectedEntities) {
        if (entityRef.entity->type == EntityType::Polyline) {
            auto poly = std::dynamic_pointer_cast<PolylineEntity>(entityRef.entity);

            for (int i = 0; i < poly->points.size(); ++i) {
                Grip grip;
                // Convert 2D plane point to 3D world
                grip.position = planeToWorld(poly->points[i]);
                grip.entityRef = entityRef;
                grip.pointIndex = i;
                activeGrips.append(grip);
            }
        }
    }
}

void CadView::drawGrips() {
    if (activeGrips.isEmpty()) return;

    glDisable(GL_DEPTH_TEST);
    glPointSize(8.0f);

    for (int i = 0; i < activeGrips.size(); ++i) {
        const Grip& grip = activeGrips[i];

        if (i == hoveredGripIndex) {
            glColor3f(1.0f, 0.5f, 0.0f); // Orange when hovered
        } else {
            glColor3f(0.0f, 0.5f, 1.0f); // Blue
        }

        glBegin(GL_POINTS);
        glVertex3f(grip.position.x(), grip.position.y(), grip.position.z());
        glEnd();

        // Draw grip box
        glBegin(GL_LINE_LOOP);
        float size = 0.2f;
        glVertex3f(grip.position.x() - size, grip.position.y() - size, grip.position.z());
        glVertex3f(grip.position.x() + size, grip.position.y() - size, grip.position.z());
        glVertex3f(grip.position.x() + size, grip.position.y() + size, grip.position.z());
        glVertex3f(grip.position.x() - size, grip.position.y() + size, grip.position.z());
        glEnd();
    }

    glPointSize(1.0f);
    glEnable(GL_DEPTH_TEST);
}

// ==================== Object Snap ====================
QVector<QVector3D> CadView::getEntitySnapPoints(const EntityRef& entityRef) {
    QVector<QVector3D> points;
    if (!entityRef.entity) return points;

    if (entityRef.entity->type == EntityType::Polyline) {
        auto poly = std::dynamic_pointer_cast<PolylineEntity>(entityRef.entity);

        // Convert 2D points to 3D for snapping
        for (const auto& pt : poly->points) {
            points.append(planeToWorld(pt));
        }

        // Midpoints
        for (int i = 0; i < poly->points.size() - 1; ++i) {
            QVector2D mid = (poly->points[i] + poly->points[i + 1]) / 2.0f;
            points.append(planeToWorld(mid));
        }
    }
    return points;
}

QVector<Face> CadView::extractFacesFromFeature(std::shared_ptr<FeatureNode> feature) {
    QVector<Face> faces;

    if (feature->type != FeatureType::Extrude) return faces;

    auto extrude = std::static_pointer_cast<ExtrudeNode>(feature);
    auto s = extrude->sketch.lock();
    if (!s || s->entities.empty()) return faces;

    auto poly = std::dynamic_pointer_cast<PolylineEntity>(s->entities.front());
    if (!poly || poly->points.size() < 3) return faces;

    // Convert 2D points to 3D
    QVector<QVector3D> base3D;
    for (const auto& pt2d : poly->points) {
        QVector3D pt3d;
        switch (s->plane) {
        case SketchPlane::XY:
            pt3d = QVector3D(pt2d.x(), pt2d.y(), 0);
            break;
        case SketchPlane::XZ:
            pt3d = QVector3D(pt2d.x(), 0, pt2d.y());
            break;
        case SketchPlane::YZ:
            pt3d = QVector3D(0, pt2d.x(), pt2d.y());
            break;
        case SketchPlane::Custom:
            pt3d = s->customPlane.origin +
                   s->customPlane.uAxis * pt2d.x() +
                   s->customPlane.vAxis * pt2d.y();
            break;
        }
        base3D.append(pt3d);
    }

    QVector3D offset = extrude->direction.normalized() * extrude->height;

    // Bottom face
    Face bottomFace;
    bottomFace.vertices = base3D;
    bottomFace.normal = -extrude->direction.normalized();
    bottomFace.center = QVector3D(0, 0, 0);
    for (const auto& v : base3D) bottomFace.center += v;
    bottomFace.center /= base3D.size();
    bottomFace.featureId = feature->id;
    bottomFace.faceIndex = 0;
    faces.append(bottomFace);

    // Top face
    Face topFace;
    for (const auto& v : base3D) {
        topFace.vertices.append(v + offset);
    }
    topFace.normal = extrude->direction.normalized();
    topFace.center = QVector3D(0, 0, 0);
    for (const auto& v : topFace.vertices) topFace.center += v;
    topFace.center /= topFace.vertices.size();
    topFace.featureId = feature->id;
    topFace.faceIndex = 1;
    faces.append(topFace);

    // Side faces
    for (int i = 0; i < base3D.size() - 1; ++i) {
        Face sideFace;
        sideFace.vertices.append(base3D[i]);
        sideFace.vertices.append(base3D[i + 1]);
        sideFace.vertices.append(base3D[i + 1] + offset);
        sideFace.vertices.append(base3D[i] + offset);

        // Calculate face normal
        QVector3D edge1 = sideFace.vertices[1] - sideFace.vertices[0];
        QVector3D edge2 = sideFace.vertices[3] - sideFace.vertices[0];
        sideFace.normal = QVector3D::crossProduct(edge1, edge2).normalized();

        sideFace.center = QVector3D(0, 0, 0);
        for (const auto& v : sideFace.vertices) sideFace.center += v;
        sideFace.center /= 4.0f;

        sideFace.featureId = feature->id;
        sideFace.faceIndex = 2 + i;
        faces.append(sideFace);
    }

    return faces;
}

QVector<Face> CadView::getAllFaces() const {
    QVector<Face> allFaces;

    for (auto& feature : doc.features) {
        allFaces.append(const_cast<CadView*>(this)->extractFacesFromFeature(feature));
    }

    return allFaces;
}

Face* CadView::pickFace(const QPoint& screenPos) {
    QVector3D rayOrigin, rayDir;
    QVector3D worldPos = screenToWorld(screenPos);

    // Get ray direction
    float x = (2.0f * screenPos.x()) / width() - 1.0f;
    float y = 1.0f - (2.0f * screenPos.y()) / height();
    QVector4D rayClip(x, y, -1.0f, 1.0f);
    QVector4D rayFarClip(x, y, 1.0f, 1.0f);

    QMatrix4x4 inv = (camera.getProjectionMatrix() * camera.getViewMatrix()).inverted();
    QVector4D rayStartWorld = inv * rayClip;
    QVector4D rayEndWorld = inv * rayFarClip;
    rayStartWorld /= rayStartWorld.w();
    rayEndWorld /= rayEndWorld.w();

    rayOrigin = QVector3D(rayStartWorld);
    rayDir = (QVector3D(rayEndWorld) - rayOrigin).normalized();

    QVector<Face> allFaces = getAllFaces();
    Face* closestFace = nullptr;
    float minDist = std::numeric_limits<float>::max();

    for (int i = 0; i < allFaces.size(); ++i) {
        Face& face = allFaces[i];

        // Ray-plane intersection
        float denom = QVector3D::dotProduct(face.normal, rayDir);
        if (fabs(denom) < 0.0001f) continue; // Parallel

        float t = QVector3D::dotProduct(face.center - rayOrigin, face.normal) / denom;
        if (t < 0) continue; // Behind camera

        QVector3D hitPoint = rayOrigin + rayDir * t;

        // Check if hit point is inside face polygon
        bool inside = true;
        for (int j = 0; j < face.vertices.size(); ++j) {
            int next = (j + 1) % face.vertices.size();
            QVector3D edge = face.vertices[next] - face.vertices[j];
            QVector3D toPoint = hitPoint - face.vertices[j];
            QVector3D cross = QVector3D::crossProduct(edge, toPoint);

            if (QVector3D::dotProduct(cross, face.normal) < 0) {
                inside = false;
                break;
            }
        }

        if (inside && t < minDist) {
            minDist = t;
            // Store in a persistent location
            static QVector<Face> faceStorage;
            if (faceStorage.size() <= i) {
                faceStorage = allFaces;
            }
            closestFace = &faceStorage[i];
        }
    }

    return closestFace;
}

void CadView::drawFaceHighlight(const Face& face, const QColor& color) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draw semi-transparent face
    glColor4f(color.redF(), color.greenF(), color.blueF(), 0.3f);
    glBegin(GL_POLYGON);
    for (const auto& v : face.vertices) {
        glVertex3f(v.x(), v.y(), v.z());
    }
    glEnd();

    // Draw border
    glLineWidth(3.0f);
    glColor4f(color.redF(), color.greenF(), color.blueF(), 0.8f);
    glBegin(GL_LINE_LOOP);
    for (const auto& v : face.vertices) {
        glVertex3f(v.x(), v.y(), v.z());
    }
    glEnd();

    glLineWidth(1.0f);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void CadView::highlightFace(Face* face) {
    selectedFace = face;
    update();
}

void CadView::startFaceSelectionMode() {
    mode = CadMode::SelectingFace;
    setCursor(Qt::CrossCursor);
    update();
    qDebug() << "Face selection mode started - click on a face to select";
}

CadView::SnapPoint CadView::findNearestSnapPoint(const QVector3D& worldPos) {
    SnapPoint result;
    result.entityRef.entity = nullptr;

    float minDist = snapTolerance * 3; // Larger snap radius

    // Check all entities in all sketches
    for (auto& sketch : doc.sketches) {
        for (int i = 0; i < sketch->entities.size(); ++i) {
            EntityRef ref = {sketch->entities[i], sketch, i};
            QVector<QVector3D> snapPts = getEntitySnapPoints(ref);

            for (const auto& snapPt : snapPts) {
                float dist = (worldPos - snapPt).length();

                if (dist < minDist) {
                    minDist = dist;
                    result.position = snapPt;
                    result.entityRef = ref;
                    result.snapType = "endpoint"; // Simplified
                }
            }
        }
    }

    return result;
}

void CadView::drawSnapMarker(const QVector3D& pos, const QString& snapType) {
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0f);
    glColor3f(0.0f, 1.0f, 0.0f); // Green

    float size = 0.3f;

    // Draw square marker
    glBegin(GL_LINE_LOOP);
    glVertex3f(pos.x() - size, pos.y() - size, pos.z());
    glVertex3f(pos.x() + size, pos.y() - size, pos.z());
    glVertex3f(pos.x() + size, pos.y() + size, pos.z());
    glVertex3f(pos.x() - size, pos.y() + size, pos.z());
    glEnd();

    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
}

void CadView::clearSelection() {
    selectedEntities.clear();
    activeGrips.clear();
    hoveredEntity.entity = nullptr;
    update();
}

void CadView::setActiveSketch(std::shared_ptr<SketchNode> sketch) {
    activeSketch = sketch;
    if (sketch) {
        // Set view to sketch plane
        switch (sketch->plane) {
        case SketchPlane::XY: setSketchView(SketchView::Top); break;
        case SketchPlane::XZ: setSketchView(SketchView::Front); break;
        case SketchPlane::YZ: setSketchView(SketchView::Right); break;
        default: setSketchView(SketchView::None); break;
        }
        pendingSketch = sketch;
        mode = CadMode::Sketching;
    }
    update();
}

void CadView::exitSketchEdit() {
    activeSketch.reset();
    pendingSketch.reset();
    mode = CadMode::Idle;
    setSketchView(SketchView::None);
    update();
}

void CadView::toggleSketchVisibility(int sketchId) {
    if (hiddenSketches.contains(sketchId)) {
        hiddenSketches.remove(sketchId);
    } else {
        hiddenSketches.insert(sketchId);
    }
    update();
}

bool CadView::isSketchVisible(int sketchId) const {
    return !hiddenSketches.contains(sketchId);
}

void CadView::toggleFeatureVisibility(int featureId) {
    if (hiddenFeatures.contains(featureId)) {
        hiddenFeatures.remove(featureId);
    } else {
        hiddenFeatures.insert(featureId);
    }
    update();
}

bool CadView::isFeatureVisible(int featureId) const {
    return !hiddenFeatures.contains(featureId);
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

    // Draw sketches
    for (auto& sketch : doc.sketches) {
        // Skip if hidden (regardless of feature usage)
        if (!isSketchVisible(sketch->id)) {
            continue;
        }

        // Check if used by a visible extrude
        bool usedByVisibleExtrude = false;
        for (auto& f : doc.features) {
            if (f->type == FeatureType::Extrude && isFeatureVisible(f->id)) {
                auto extrude = std::static_pointer_cast<ExtrudeNode>(f);
                if (auto s = extrude->sketch.lock()) {
                    if (s->id == sketch->id) {
                        usedByVisibleExtrude = true;
                        break;
                    }
                }
            }
        }

        // Hide sketch if used by visible extrude AND not actively editing
        if (usedByVisibleExtrude && sketch != activeSketch) {
            continue;
        }

        // Set up sketch context
        pendingSketch = sketch;

        // Draw entities
        for (int i = 0; i < sketch->entities.size(); ++i) {
            auto& entity = sketch->entities[i];

            bool isSelected = false;
            for (const auto& sel : selectedEntities) {
                if (sel.entity.get() == entity.get()) {
                    isSelected = true;
                    break;
                }
            }

            bool isHovered = (hoveredEntity.entity.get() == entity.get());

            if (isSelected) {
                glLineWidth(3.0f);
                glColor3f(0.0f, 0.8f, 1.0f);
            } else if (isHovered) {
                glLineWidth(2.0f);
                glColor3f(1.0f, 1.0f, 0.0f);
            } else {
                glLineWidth(1.0f);
                glColor3f(1.0f, 1.0f, 1.0f);
            }

            if (entity->type == EntityType::Polyline) {
                auto poly = std::dynamic_pointer_cast<PolylineEntity>(entity);
                if (poly && !poly->points.empty()) {
                    glBegin(GL_LINE_STRIP);
                    for (const auto& p : poly->points) {
                        QVector3D p3d = planeToWorld(p);
                        glVertex3f(p3d.x(), p3d.y(), p3d.z());
                    }
                    glEnd();
                }
            }
        }
    }

    glLineWidth(1.0f);

    // Draw features
    for (auto& f : doc.features) {
        if (!isFeatureVisible(f->id)) {
            continue;
        }

        if (f->id == highlightedFeatureId) {
            glColor3f(1.0f, 0.0f, 0.0f);
        } else {
            glColor3f(0.7f, 0.7f, 0.7f);
        }
        f->draw();
    }

    // Draw face highlights
    if (hoveredFace) {
        drawFaceHighlight(*hoveredFace, QColor(255, 255, 0));
    }

    if (selectedFace && selectedFace != hoveredFace) {
        drawFaceHighlight(*selectedFace, QColor(0, 255, 0));
    }

    if (awaitingHeight && pendingSketch) {
        drawExtrudedCube(previewHeight, true);
    }

    drawGrips();

    if (snapActive && currentSnapPoint.entityRef.entity) {
        drawSnapMarker(currentSnapPoint.position, currentSnapPoint.snapType);
    }

    drawRubberBand();
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
    SketchPlane plane,
    const CustomPlane* customPlane = nullptr)
{
    QVector<QVector3D> pts;

    switch (plane) {
    case SketchPlane::XY: {
        pts << QVector3D(rect.p1.x(), rect.p1.y(), 0)
            << QVector3D(rect.p2.x(), rect.p1.y(), 0)
            << QVector3D(rect.p2.x(), rect.p2.y(), 0)
            << QVector3D(rect.p1.x(), rect.p2.y(), 0)
            << QVector3D(rect.p1.x(), rect.p1.y(), 0);
        break;
    }
    case SketchPlane::YZ: {
        pts << QVector3D(0, rect.p1.x(), rect.p1.y())
            << QVector3D(0, rect.p2.x(), rect.p1.y())
            << QVector3D(0, rect.p2.x(), rect.p2.y())
            << QVector3D(0, rect.p1.x(), rect.p2.y())
            << QVector3D(0, rect.p1.x(), rect.p1.y());
        break;
    }
    case SketchPlane::XZ: {
        pts << QVector3D(rect.p1.x(), 0, rect.p1.y())
            << QVector3D(rect.p2.x(), 0, rect.p1.y())
            << QVector3D(rect.p2.x(), 0, rect.p2.y())
            << QVector3D(rect.p1.x(), 0, rect.p2.y())
            << QVector3D(rect.p1.x(), 0, rect.p1.y());
        break;
    }
    case SketchPlane::Custom: {
        if (customPlane) {
            auto to3D = [&](const QVector2D& p2d) {
                return customPlane->origin +
                       customPlane->uAxis * p2d.x() +
                       customPlane->vAxis * p2d.y();
            };
            pts << to3D(rect.p1)
                << to3D(QVector2D(rect.p2.x(), rect.p1.y()))
                << to3D(rect.p2)
                << to3D(QVector2D(rect.p1.x(), rect.p2.y()))
                << to3D(rect.p1);
        }
        break;
    }
    }
    return pts;
}

void CadView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // Check if in face selection mode
        if (mode == CadMode::SelectingFace) {
            Face* face = pickFace(event->pos());
            if (face) {
                selectedFace = face;
                emit faceSelected(face->toCustomPlane());
                mode = CadMode::Idle;
                update();
                return;
            }
        }
        // Check grip click first
        if (hoveredGripIndex >= 0) {
            draggedGripIndex = hoveredGripIndex;
            return;
        }

        // Handle GetPoint mode
        if (getPointState.active && !getPointState.keyboardMode) {
            QVector3D worldPos = screenToWorld(event->pos());

            // Use snap point if active
            if (snapActive && currentSnapPoint.entityRef.entity) {
                worldPos = currentSnapPoint.position;
            }

            QVector2D planePt = worldToPlane(worldPos);
            getPointState.active = false;
            emit pointAcquired(planePt);
            update();
            return;
        }

        // Entity selection
        EntityRef picked = pickEntity(event->pos());
        if (picked.entity) {
            bool isSelected = false;
            for (const auto& sel : selectedEntities) {
                if (sel == picked) {
                    isSelected = true;
                    break;
                }
            }

            if (!isSelected) {
                if (!(event->modifiers() & Qt::ShiftModifier)) {
                    selectedEntities.clear();
                }
                selectedEntities.append(picked);
                updateGrips();
            }
            update();
            return;
        } else {
            // Clicked empty space - clear selection
            if (!(event->modifiers() & Qt::ShiftModifier)) {
                selectedEntities.clear();
                activeGrips.clear();
                update();
            }
        }

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
    if (mode == CadMode::SelectingFace) {
        Face* face = pickFace(event->pos());
        if (face) {
            QString msg = QString("Face %1 of Feature %2 - Normal: (%3, %4, %5)")
                              .arg(face->faceIndex)
                              .arg(face->featureId)
                              .arg(face->normal.x(), 0, 'f', 2)
                              .arg(face->normal.y(), 0, 'f', 2)
                              .arg(face->normal.z(), 0, 'f', 2);
            emit statusMessage(msg);
        }
    }
    // Update face hover
    if (mode == CadMode::SelectingFace || mode == CadMode::Idle) {
        Face* face = pickFace(event->pos());
        if (face != hoveredFace) {
            hoveredFace = face;
            update();
        }
    }
    // Update rubber band for GetPoint
    if (getPointState.active && getPointState.hasPreviousPoint && !getPointState.keyboardMode) {
        QVector3D worldPos = screenToWorld(event->pos());
        rubberBandState.currentPoint = worldToPlane(worldPos);
        rubberBandState.active = true;
        update();
        return;
    }

    // Update hover state
    if (!(draggedGripIndex >= 0)) {
        EntityRef hovered = pickEntity(event->pos());
        if (!(hovered == hoveredEntity)) {
            hoveredEntity = hovered;
            update();
        }

        // Check grip hover
        if (!selectedEntities.isEmpty()) {
            QVector3D worldPos = screenToWorld(event->pos());
            hoveredGripIndex = -1;

            for (int i = 0; i < activeGrips.size(); ++i) {
                float dist = (worldPos - activeGrips[i].position).length();
                if (dist < snapTolerance * 2) {
                    hoveredGripIndex = i;
                    break;
                }
            }
            update();
        }
    }

    // Handle grip dragging
    if (draggedGripIndex >= 0 && (event->buttons() & Qt::LeftButton)) {
        QVector3D worldPos = screenToWorld(event->pos());
        QVector2D planePt = worldToPlane(worldPos);

        Grip& grip = activeGrips[draggedGripIndex];
        auto poly = std::dynamic_pointer_cast<PolylineEntity>(grip.entityRef.entity);

        if (poly && grip.pointIndex < poly->points.size()) {
            poly->points[grip.pointIndex] = planePt; // Store as 2D
            updateGrips();
        }
        update();
        return;
    }

    // Object snap during getpoint
    if (getPointState.active && objectSnapEnabled) {
        QVector3D worldPos = screenToWorld(event->pos());
        SnapPoint snap = findNearestSnapPoint(worldPos);

        if (snap.entityRef.entity) {
            snapActive = true;
            currentSnapPoint = snap;
            rubberBandState.currentPoint = worldToPlane(snap.position);
        } else {
            snapActive = false;
            rubberBandState.currentPoint = worldToPlane(worldPos);
        }

        rubberBandState.active = getPointState.hasPreviousPoint;
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
        currentRect.p2 = worldToPlane(worldPos); // ✅ Convert 3D to 2D
        update();
    }

    if (mode == CadMode::Extruding && awaitingHeight) {
        QVector3D worldPos = screenToWorld(event->pos());
        baseP2 = planeToWorld(currentRect.p2); // Convert 2D back to 3D for distance calc
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
    if (event->button() == Qt::LeftButton) {
        draggedGripIndex = -1;
    }

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

        // NEW: Forward any printable key to MainWindow's CommandInput
        if (!event->text().isEmpty() && event->text()[0].isPrint()) {
            getPointState.keyboardMode = true;
            // Signal MainWindow to activate commandInput AND insert the key
            emit getPointKeyPressed(event->text());
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

// Utility: build orthonormal basis from plane normal
void CadView::planeBasis(const QVector3D& normal, QVector3D& u, QVector3D& v) {
    QVector3D n = normal.normalized();
    // pick an arbitrary vector not parallel to n
    QVector3D helper = (fabs(n.x()) > 0.9f) ? QVector3D(0,1,0) : QVector3D(1,0,0);
    u = QVector3D::crossProduct(n, helper).normalized();
    v = QVector3D::crossProduct(n, u).normalized();
}

QVector2D CadView::worldToPlane(const QVector3D& worldPt) {
    if (!pendingSketch) {
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
    case SketchPlane::Custom: {
        // For arbitrary planes: project onto basis
        QVector3D localPt = worldPt - pendingSketch->customPlane.origin;
        return QVector2D(QVector3D::dotProduct(localPt, pendingSketch->customPlane.uAxis),
                         QVector3D::dotProduct(localPt, pendingSketch->customPlane.vAxis));
    }
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

    // Use sketch plane with proper axis mapping
    switch (pendingSketch->plane) {
    case SketchPlane::XY:
        return QVector3D(planePt.x(), planePt.y(), 0);
    case SketchPlane::XZ:
        return QVector3D(planePt.x(), 0, planePt.y());
    case SketchPlane::YZ:
        return QVector3D(0, planePt.x(), planePt.y());
    case SketchPlane::Custom: {
        return pendingSketch->customPlane.origin +
               pendingSketch->customPlane.uAxis * planePt.x() +
               pendingSketch->customPlane.vAxis * planePt.y();
    }
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

void CadView::drawRubberBand() {
    if (!rubberBandState.active) return;

    glEnable(GL_LINE_STIPPLE);
    glLineStipple(1, 0xAAAA);
    glColor3f(1.0f, 1.0f, 0.0f); // Yellow
    glLineWidth(1.5f);

    switch (rubberBandState.mode) {
    case RubberBandMode::Line: {
        QVector3D w1 = planeToWorld(rubberBandState.startPoint);
        QVector3D w2 = planeToWorld(rubberBandState.currentPoint);
        glBegin(GL_LINES);
        glVertex3f(w1.x(), w1.y(), w1.z());
        glVertex3f(w2.x(), w2.y(), w2.z());
        glEnd();
        break;
    }

    case RubberBandMode::Rectangle: {
        // Reuse existing drawRectangle logic
        Rectangle2D rect;
        rect.p1 = rubberBandState.startPoint;
        rect.p2 = rubberBandState.currentPoint;
        drawRectangle(rect, Qt::DashLine);
        break;
    }

    case RubberBandMode::Polyline: {
        glBegin(GL_LINE_STRIP);
        for (const auto& pt : rubberBandState.intermediatePoints) {
            QVector3D w = planeToWorld(pt);
            glVertex3f(w.x(), w.y(), w.z());
        }
        QVector3D wCur = planeToWorld(rubberBandState.currentPoint);
        glVertex3f(wCur.x(), wCur.y(), wCur.z());
        glEnd();
        break;
    }

    case RubberBandMode::Arc: {
        // Calculate arc preview
        QVector2D center = rubberBandState.startPoint;
        float radius = (rubberBandState.currentPoint - center).length();

        glBegin(GL_LINE_STRIP);
        for (int i = 0; i <= 32; ++i) {
            float angle = i * M_PI / 16.0f;
            QVector2D pt = center + QVector2D(cos(angle), sin(angle)) * radius;
            QVector3D w = planeToWorld(pt);
            glVertex3f(w.x(), w.y(), w.z());
        }
        glEnd();
        break;
    }

    case RubberBandMode::Circle: {
        QVector2D center = rubberBandState.startPoint;
        float radius = (rubberBandState.currentPoint - center).length();

        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 64; ++i) {
            float angle = i * 2.0f * M_PI / 64.0f;
            QVector2D pt = center + QVector2D(cos(angle), sin(angle)) * radius;
            QVector3D w = planeToWorld(pt);
            glVertex3f(w.x(), w.y(), w.z());
        }
        glEnd();
        break;
    }

    default:
        break;
    }

    glDisable(GL_LINE_STIPPLE);
    glLineWidth(1.0f);
}

// ==================== Entity Picking ====================
CadView::EntityRef CadView::pickEntity(const QPoint& screenPos) {
    EntityRef result;
    result.entity = nullptr;

    QVector3D rayOrigin, rayDir;
    QVector3D worldPos = screenToWorld(screenPos);

    // Cast ray for picking
    float minDist = std::numeric_limits<float>::max();

    // Check all sketches
    for (auto& sketch : doc.sketches) {
        for (int i = 0; i < sketch->entities.size(); ++i) {
            auto& entity = sketch->entities[i];
            float dist = distanceToEntity(worldPos, {entity, sketch, i});

            if (dist < snapTolerance && dist < minDist) {
                minDist = dist;
                result.entity = entity;
                result.parentSketch = sketch;
                result.entityIndex = i;
            }
        }
    }

    return result;
}

float CadView::distanceToEntity(const QVector3D& point, const EntityRef& entityRef) {
    if (!entityRef.entity) return std::numeric_limits<float>::max();

    if (entityRef.entity->type == EntityType::Polyline) {
        auto poly = std::dynamic_pointer_cast<PolylineEntity>(entityRef.entity);
        float minDist = std::numeric_limits<float>::max();

        // Convert 2D points to 3D for distance calculation
        for (int i = 0; i < poly->points.size() - 1; ++i) {
            QVector3D p1 = planeToWorld(poly->points[i]);
            QVector3D p2 = planeToWorld(poly->points[i + 1]);

            // Point-to-line-segment distance
            QVector3D v = p2 - p1;
            QVector3D w = point - p1;
            float c1 = QVector3D::dotProduct(w, v);

            if (c1 <= 0) {
                minDist = qMin(minDist, (point - p1).length());
                continue;
            }

            float c2 = QVector3D::dotProduct(v, v);
            if (c1 >= c2) {
                minDist = qMin(minDist, (point - p2).length());
                continue;
            }

            float b = c1 / c2;
            QVector3D pb = p1 + b * v;
            minDist = qMin(minDist, (point - pb).length());
        }

        return minDist;
    }

    return std::numeric_limits<float>::max();
}

void CadView::drawRectangle(const Rectangle2D& rect, Qt::PenStyle style) {
    if (!pendingSketch) return;

    // Convert 2D corners to 3D
    QVector3D v0 = planeToWorld(QVector2D(rect.p1.x(), rect.p1.y()));
    QVector3D v1 = planeToWorld(QVector2D(rect.p2.x(), rect.p1.y()));
    QVector3D v2 = planeToWorld(rect.p2);
    QVector3D v3 = planeToWorld(QVector2D(rect.p1.x(), rect.p2.y()));

    if (style == Qt::DashLine) {
        glEnable(GL_LINE_STIPPLE);
        glLineStipple(1, 0xF0F0);
        glColor3f(1.0f, 1.0f, 0.0f);
    } else {
        glDisable(GL_LINE_STIPPLE);
        glColor3f(1.0f, 1.0f, 1.0f);
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
    auto poly = std::make_shared<PolylineEntity>();

    // Get 3D points for drawing
    QVector<QVector3D> pts3D = rectanglePointsForPlane(
        currentRect,
        pendingSketch->plane,
        pendingSketch->plane == SketchPlane::Custom ? &pendingSketch->customPlane : nullptr
        );

    QVector3D v0 = pts3D[0];
    QVector3D v1 = pts3D[1];
    QVector3D v2 = pts3D[2];
    QVector3D v3 = pts3D[3];

    QVector3D extrusionDir;
    switch (pendingSketch->plane) {
    case SketchPlane::XY:
        extrusionDir = QVector3D(0, 0, height);
        break;
    case SketchPlane::XZ:
        extrusionDir = QVector3D(0, height, 0);
        break;
    case SketchPlane::YZ:
        extrusionDir = QVector3D(height, 0, 0);
        break;
    case SketchPlane::Custom:
        extrusionDir = pendingSketch->customPlane.normal * height;
        break;
    }

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
