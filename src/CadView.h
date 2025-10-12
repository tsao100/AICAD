#ifndef CADVIEW_H
#define CADVIEW_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMatrix4x4>
#include <QVector3D>
#include <QPrinter>
#include <QPainter>
#include <QPdfWriter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFile>
#include <QPushButton>
#include <vector>

enum class FeatureType {
    Sketch,
    Extrude,
    // Later: Revolve, Fillet, Boolean, etc.
};

static QString featureTypeToString(FeatureType type) {
    switch (type) {
    case FeatureType::Sketch:  return "Sketch";
    case FeatureType::Extrude: return "Extrude";
    default:                   return "Unknown";
    }
}

enum class CadMode {
    Idle,
    Sketching,
    Extruding
};

// Rubber band drawing system
enum class RubberBandMode {
    None,
    Line,
    Rectangle,
    Polyline,
    Arc,
    Circle
};

struct CustomPlane {
    QVector3D origin;
    QVector3D normal;
    QVector3D uAxis;
    QVector3D vAxis;
};

struct FeatureNode {
    int id;
    FeatureType type;
    QString name;   // user-facing name
    QVector<int> parents; // IDs of parent nodes (dependencies)
    QVector<int> children;

    virtual ~FeatureNode() {}
    virtual void evaluate() = 0;  // recompute geometry
    virtual void draw() const = 0;
    virtual void save(QTextStream& out) const = 0;
    virtual void load(QTextStream& in) = 0;
};

struct Rectangle2D {
    QVector3D p1;
    QVector3D p2;
};

enum class EntityType {
    Line,
    Arc,
    Polyline,
    Spline,
    Extrude
};

enum class SketchPlane {
    XY,   // Top
    XZ,   // Front
    YZ,   // Right
    Custom
};

struct Entity {
    EntityType type;
    SketchPlane plane;
    int id;                // unique ID for entity
    QString layer;         // optional: layer or group

    virtual ~Entity() {}
    virtual void draw() const = 0;
    virtual void save(QTextStream& out) const = 0;
    virtual void load(QTextStream& in) = 0;
};

/*
struct LineEntity : public Entity {
    QVector3D p1, p2;

    LineEntity() { type = EntityType::Line; }

    void draw() const override;
    void save(QTextStream& out) const override {
        out << "Line " << static_cast<int>(plane) << " " << p1.x() << " " << p1.y() << " " << p1.z()
            << " " << p2.x() << " " << p2.y() << " " << p2.z() << "\n";
    }
    void load(QTextStream& in) override {
        int planeInt;
        in >> planeInt;
        plane = static_cast<SketchPlane>(planeInt);

        float x, y, z;
        in >> x >> y >> z;
        p1 = QVector3D(x, y, z);

        in >> x >> y >> z;
        p2 = QVector3D(x, y, z);
    }
};

struct ArcEntity : public Entity {
    QVector3D center;
    float radius;
    float startAngle, endAngle; // degrees

    ArcEntity() { type = EntityType::Arc; }

    void draw() const override;
    void save(QTextStream& out) const override {
        out << "Arc " << static_cast<int>(plane) << " "
            << center.x() << " " << center.y() << " " << center.z()
            << " " << radius << " " << startAngle << " " << endAngle << "\n";
    }
    void load(QTextStream& in) override {
        int planeInt;
        in >> planeInt;
        plane = static_cast<SketchPlane>(planeInt);

        float x, y, z;
        in >> x >> y >> z;
        center = QVector3D(x, y, z);

        in >> radius >> startAngle >> endAngle;
    }
};
*/
struct PolylineEntity : public Entity {
    QVector<QVector3D> points;

    PolylineEntity() { type = EntityType::Polyline; }

    void draw() const override {
        if (points.isEmpty()) return;

        glColor3f(1, 1, 1);
        glBegin(GL_LINE_STRIP);
        for (const auto& p : points) {
            glVertex3f(p.x(), p.y(), p.z());
        }
        glEnd();
    }

    void save(QTextStream& out) const override {
        out << "Polyline " << static_cast<int>(plane) << " " << points.size();
        for (const auto& p : points)
            out << " " << p.x() << " " << p.y() << " " << p.z();
        out << "\n";
    }

    void load(QTextStream& in) override {
        int planeInt, n;
        in >> planeInt >> n;
        plane = static_cast<SketchPlane>(planeInt);

        points.resize(n);
        for (int i = 0; i < n; ++i){
            float x, y, z;
            in >> x >> y >> z;
            points[i] = QVector3D(x, y, z);
            }
    }
};
/*
struct SplineEntity : public Entity {
    QVector<QVector3D> controlPoints;

    SplineEntity() { type = EntityType::Spline; }

    void draw() const override;

    void save(QTextStream& out) const override {
        out << "Spline " << static_cast<int>(plane) << " " << controlPoints.size();
        for (const auto& p : controlPoints)
            out << " " << p.x() << " " << p.y() << " " << p.z();
        out << "\n";
    }

    void load(QTextStream& in) override {
        int planeInt, n;
        in >> planeInt >> n;
        plane = static_cast<SketchPlane>(planeInt);

        controlPoints.resize(n);
        for (int i = 0; i < n; ++i){
            float x, y, z;
            in >> x >> y >> z;
            controlPoints[i] = QVector3D(x, y, z);}
    }
};
*/
struct Sketch {
    int id;                       // unique sketch ID
    SketchPlane plane;            // sketch plane (XY, XZ, YZ, Custom)
    QVector<std::shared_ptr<Entity>> entities; // only 2D entities

    void addEntity(const std::shared_ptr<Entity>& e) {
        entities.push_back(e);
    }

    void draw() const {
        for (auto& e : entities)
            e->draw();
    }

    void save(QTextStream& out) const {
        out << "Sketch " << id << " " << int(plane) << " " << entities.size() << "\n";
        for (auto& e : entities)
            e->save(out);
    }

    void load(QTextStream& in) {
        int n;
        in >> id >> (int&)plane >> n;
        for (int i = 0; i < n; i++) {
            QString type; in >> type;
            std::shared_ptr<Entity> e;
            if (type == "Polyline") e = std::make_shared<PolylineEntity>();
        /*    else if (type == "Arc") e = std::make_shared<ArcEntity>();
            else if (type == "Line") e = std::make_shared<LineEntity>();
            else if (type == "Spline") e = std::make_shared<SplineEntity>();*/
            if (e) { e->load(in); entities.push_back(e); }
        }
    }
};

struct ExtrudeEntity : public Entity {
    std::weak_ptr<Sketch> sketch; // weak reference to avoid cycles
    float height;
    QVector3D direction;

    ExtrudeEntity() { type = EntityType::Extrude; }

    void draw() const override {
        if (auto s = sketch.lock()) {
            s->draw();

            // Collect closed polylines from the sketch
            for (const auto& e : s->entities) {
                if (e->type == EntityType::Polyline) {
                    auto poly = std::dynamic_pointer_cast<PolylineEntity>(e);
                    if (!poly || poly->points.size() < 3) continue;

                    // Extrude each polyline
                    drawExtrusion(poly->points, height, direction);
                }
            }
        }
        }


    static void drawExtrusion(const QVector<QVector3D>& base,
                              float height,
                              const QVector3D& dir)
    {
        QVector3D offset = dir.normalized() * height;

        // --- Draw side walls ---
        glColor3f(0.2f, 0.7f, 1.0f);
        glBegin(GL_QUADS);
        for (int i = 0; i < base.size(); i++) {
            int j = (i + 1) % base.size(); // next vertex, wrap around
            QVector3D v0 = base[i];
            QVector3D v1 = base[j];
            QVector3D v2 = v1 + offset;
            QVector3D v3 = base[i] + offset;

            glVertex3f(v0.x(), v0.y(), v0.z());
            glVertex3f(v1.x(), v1.y(), v1.z());
            glVertex3f(v2.x(), v2.y(), v2.z());
            glVertex3f(v3.x(), v3.y(), v3.z());
        }
        glEnd();

        // --- Draw bottom face ---
        glColor3f(0.1f, 0.5f, 0.8f);
        glBegin(GL_POLYGON);
        for (const auto& v : base)
            glVertex3f(v.x(), v.y(), v.z());
        glEnd();

        // --- Draw top face ---
        glColor3f(0.1f, 0.5f, 0.8f);
        glBegin(GL_POLYGON);
        for (const auto& v : base) {
            QVector3D vt = v + offset;
            glVertex3f(vt.x(), vt.y(), vt.z());
        }
        glEnd();
    }


    void save(QTextStream& out) const override {
        int sketchId = sketch.expired() ? -1 : sketch.lock()->id;
        out << "Extrude " << static_cast<int>(plane) << " " << sketchId << " "
            << height << " "
            << direction.x() << " " << direction.y() << " " << direction.z() << "\n";
    }

    void load(QTextStream& in) override {
        int planeInt, sketchId;
        in >> planeInt >> sketchId >> height;
        float x, y, z;
        in >> x >> y >> z;
        direction = QVector3D(x, y, z);
        plane = static_cast<SketchPlane>(planeInt);

        pendingSketchId = sketchId;
    }

    void resolveSketchLink(const QVector<std::shared_ptr<Sketch>>& sketches) {
        if (pendingSketchId < 0) return;
        for (auto& s : sketches)
            if (s->id == pendingSketchId)
                sketch = s;
        pendingSketchId = -1;
    }

private:
    int pendingSketchId = -1;
};

struct SketchNode : public FeatureNode {
    SketchPlane plane;
    CustomPlane customPlane; // For arbitrary planes
    QVector<std::shared_ptr<Entity>> entities;
    bool visible = true;  // visibility toggle
    bool isAttached = false; // attached to feature flag

    SketchNode() { type = FeatureType::Sketch; }

    void evaluate() override {
        // Sketch is already geometric, nothing special
    }

    void draw() const override {
        for (auto& e : entities) e->draw();
    }

    void save(QTextStream& out) const override {
        out << "Sketch " << id << " " << int(plane) << " " << entities.size() << "\n";
        for (auto& e : entities) e->save(out);
    }

    void load(QTextStream& in) override {
        int n, planeInt;
        in >> id >> planeInt >> n;
        plane = static_cast<SketchPlane>(planeInt);

        for (int i = 0; i < n; i++) {
            QString type; in >> type;
            std::shared_ptr<Entity> e;
            if (type == "Polyline") e = std::make_shared<PolylineEntity>();
            if (e) {
                e->load(in);
                entities.push_back(e);
            }
        }
    }
};

struct ExtrudeNode : public FeatureNode {
    std::weak_ptr<SketchNode> sketch;  // reference to SketchNode
    float height;
    QVector3D direction;

    ExtrudeNode() { type = FeatureType::Extrude; }

    void draw() const override {
        auto s = sketch.lock();
        if (!s || s->entities.empty()) return;

        // Assume first entity is PolylineEntity with 4 corners (rectangle)
        auto poly = std::dynamic_pointer_cast<PolylineEntity>(s->entities.front());
        if (!poly || poly->points.size() < 4) return;

        QVector3D n = direction.normalized();
        QVector3D offset = n * height;

        // --- Bottom face (from sketch) ---
        glColor3f(0.1f, 0.5f, 0.8f);
        glBegin(GL_QUADS);
        for (int i = 0; i < 4; ++i) {
            const QVector3D& p = poly->points[i];
            glVertex3f(p.x(), p.y(), p.z());
        }
        glEnd();

        // --- Top face (offset by extrusion height) ---
        glColor3f(0.1f, 0.5f, 0.8f);
        glBegin(GL_QUADS);
        for (int i = 0; i < 4; ++i) {
            QVector3D p = poly->points[i] + offset;
            glVertex3f(p.x(), p.y(), p.z());
        }
        glEnd();

        // --- Side faces ---
        glColor3f(0.2f, 0.7f, 1.0f);
        for (int i = 0; i < 4; ++i) {
            int j = (i + 1) % 4;
            QVector3D p1 = poly->points[i];
            QVector3D p2 = poly->points[j];
            QVector3D p3 = p2 + offset;
            QVector3D p4 = p1 + offset;

            glBegin(GL_QUADS);
            glVertex3f(p1.x(), p1.y(), p1.z());
            glVertex3f(p2.x(), p2.y(), p2.z());
            glVertex3f(p3.x(), p3.y(), p3.z());
            glVertex3f(p4.x(), p4.y(), p4.z());
            glEnd();
        }
    }

    void save(QTextStream& out) const override {
        int sketchId = sketch.expired() ? -1 : sketch.lock()->id;
        out << "Extrude " << id << " " << sketchId << " "
            << height << " "
            << direction.x() << " " << direction.y() << " " << direction.z() << "\n";
    }

    void load(QTextStream& in) override {
        int sketchId;
        in >> id >> sketchId >> height;
        float x, y, z;
        in >> x >> y >> z;
        direction = QVector3D(x, y, z);
        pendingSketchId = sketchId;
    }

    void resolveSketchLink(const QVector<std::shared_ptr<SketchNode>>& sketches) {
        if (pendingSketchId < 0) return;
        for (auto& s : sketches) {
            if (s->id == pendingSketchId) {
                sketch = s;
                break;
            }
        }
        pendingSketchId = -1;
    }

    void evaluate() override {
        // For now: no geometry caching.
        // Later: build triangulated mesh from sketch + height.
    }

private:
    int pendingSketchId = -1;
};

inline QString sketchPlaneToString(SketchPlane plane) {
    switch (plane) {
    case SketchPlane::XY: return "XY";
    case SketchPlane::XZ: return "XZ";
    case SketchPlane::YZ: return "YZ";
    case SketchPlane::Custom: return "Custom";
    }
    return "Unknown"; // fallback
}

struct Document {
    QVector<std::shared_ptr<SketchNode>> sketches;
    QVector<std::shared_ptr<FeatureNode>> features;
    int nextId = 1;

    void addFeature(const std::shared_ptr<FeatureNode>& f) {
        f->id = nextId++;
        f->name = QString("Feature %1").arg(f->id);
        features.push_back(f);
    }

    std::shared_ptr<SketchNode> createSketch(SketchPlane plane){
        auto sketch = std::make_shared<SketchNode>();
        sketch->id = nextId++;
        sketch->name = QString("Sketch %1 (%2)").arg(sketch->id).arg(sketchPlaneToString(plane));
        sketch->plane = plane;
        sketches.push_back(sketch);
        return sketch;
    }

    void addDependency(int parentId, int childId) {
        auto parent = findFeature(parentId);
        auto child = findFeature(childId);
        if (parent && child) {
            parent->children.push_back(childId);
            child->parents.push_back(parentId);
        }
    }

    std::shared_ptr<FeatureNode> findFeature(int id) const {
        for (auto& f : sketches)
            if (f->id == id) return f;
        return nullptr;
    }

    void rebuildAll() {
        for (auto& f : features) f->evaluate();
    }

    void drawAll() const {
        for (auto& f : features) f->draw();
        for (auto& s : sketches) s->draw();
    }

    // In Document struct
    void saveToFile(const QString& filename) {
        QFile file(filename);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        QTextStream out(&file);

        // Save sketches first
        out << "Sketches " << sketches.size() << "\n";
        for (auto& s : sketches) s->save(out);

        // Save features
        out << "Features " << features.size() << "\n";
        for (auto& f : features) f->save(out);
    }

    void loadFromFile(const QString& filename) {
        QFile file(filename);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        QTextStream in(&file);

        sketches.clear();
        features.clear();

        QString type; int count;

        // Load sketches
        in >> type >> count;
        for (int i = 0; i < count; i++) {
            QString ft; in >> ft;
            if (ft == "Sketch") {
                auto s = std::make_shared<SketchNode>();
                s->load(in);
                sketches.push_back(s);
            }
        }

        // Load features
        in >> type >> count;
        for (int i = 0; i < count; i++) {
            QString ft; in >> ft;
            std::shared_ptr<FeatureNode> f;
            if (ft == "Extrude") {
                auto e = std::make_shared<ExtrudeNode>();
                e->load(in);
                e->resolveSketchLink(sketches);
                f = e;
            }
            if (f) features.push_back(f);
        }

        // Update nextId
        nextId = 1;
        for (auto& s : sketches) nextId = qMax(nextId, s->id + 1);
        for (auto& f : features) nextId = qMax(nextId, f->id + 1);
    }};

enum class SketchView {
    None,
    Top,    // XY
    Front,  // XZ
    Right,    // YZ
    Bottom,
    Back,
    Left
};

// Embedded Camera class
class Camera {
public:
    Camera();

    void setPerspective(float fov, float aspect, float nearPlane, float farPlane);
    void setOrthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane);

    QMatrix4x4 getViewMatrix() const;
    QMatrix4x4 getProjectionMatrix() const;

    void lookAt(const QVector3D& pos, const QVector3D& tgt, const QVector3D& upVec);
    void orbit(float deltaX, float deltaY);
    void zoom(float amount);

    void setOrientation(float newPitch, float newYaw, float newDistance); // setter for pitch/yaw/distance

    void scaleOrtho(float scale);
    void pan(const QVector3D& delta);

    QVector3D position;
    QVector3D target;
    QVector3D up;
    float fov_; // already stored for perspective
    bool isPerspective() const { return perspectiveMode; }

private:
    // Projection parameters
    float orthoLeft, orthoRight, orthoBottom, orthoTop;
    float nearPlane_, farPlane_;
    QMatrix4x4 projection;
    float distance;
    float pitch; // rotation around X
    float yaw;   // rotation around Y
    bool perspectiveMode;
};

class CadView : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    CadView(QWidget* parent = nullptr);
    ~CadView();
    enum class EditMode { None, Sketching, Extruding };

    void setSketchView(SketchView view);
    QVector3D screenToWorld(const QPoint& screenPos);
    Document doc;
    std::shared_ptr<SketchNode> pendingSketch;
    void highlightFeature(int id);
    void printView();
    void exportPdf(const QString &file);
    void startSketchMode(std::shared_ptr<SketchNode> sketch);
    void startExtrudeMode(std::shared_ptr<SketchNode> sketch);

    // GetPoint functionality
    void startGetPoint(const QString& prompt, const QVector2D* previousPt = nullptr);
    void cancelGetPoint();

    static void planeBasis(const QVector3D& normal, QVector3D& u, QVector3D& v);
    QVector2D worldToPlane(const QVector3D& worldPt);
    QVector3D planeToWorld(const QVector2D& planePt);

    void setObjectSnapEnabled(bool enabled) { objectSnapEnabled = enabled; update(); }
    bool isObjectSnapEnabled() const { return objectSnapEnabled; }
    void clearSelection();

    void enterSketchEditMode(std::shared_ptr<SketchNode> sketch);
    void exitSketchEditMode();
    bool isInSketchEditMode() const { return sketchEditMode; }
    std::shared_ptr<SketchNode> getCurrentEditSketch() const { return currentEditSketch; }

    // GetPoint state
    struct GetPointState {
        bool active = false;
        QString prompt;
        bool hasPreviousPoint = false;
        QVector2D previousPoint;
        QVector2D currentPoint;
        bool keyboardMode = false;
    };
    GetPointState getPointState;

    struct RubberBandState {
        RubberBandMode mode = RubberBandMode::None;
        QVector2D startPoint;
        QVector2D currentPoint;
        QVector<QVector2D> intermediatePoints; // For polyline
        bool active = false;
    };

    RubberBandState rubberBandState;

Q_SIGNALS:
    void featureAdded();
    void pointAcquired(QVector2D point);
    void getPointCancelled();
    void getPointKeyPressed(QString key);
    void sketchEditModeChanged(bool active, int sketchId);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void drawAxes();
    void drawRectangle(const Rectangle2D& rect, Qt::PenStyle style);
    void drawExtrudedCube(float height, bool ghost);
    QVector3D mapToPlane(int x, int y);

    void drawRubberBandLine(const QVector2D& p1, const QVector2D& p2);
    void drawRubberBand();

    bool sketchEditMode = false;
    std::shared_ptr<SketchNode> currentEditSketch;
    QPushButton* closeSketchButton = nullptr;

    QPoint lastMousePos;
    Rectangle2D currentRect;
    bool awaitingHeight = false;
    QVector3D baseP2;
    float previewHeight = 0.0f;

    std::vector<Rectangle2D> extrudedRects;
    SketchView currentView;

    Camera camera;

    int highlightedFeatureId = -1;
    EditMode editMode = EditMode::None;
    CadMode mode = CadMode::Idle;

    // Selection and highlighting
    struct EntityRef {
        std::shared_ptr<Entity> entity;
        std::shared_ptr<SketchNode> parentSketch;
        int entityIndex;

        bool operator==(const EntityRef& other) const {
            return entity.get() == other.entity.get();
        }
    };

    EntityRef hoveredEntity;
    QVector<EntityRef> selectedEntities;
    bool objectSnapEnabled = true;
    float snapTolerance = 0.5f; // World units

    // Grip system
    struct Grip {
        QVector3D position;
        EntityRef entityRef;
        int pointIndex; // Which point in the entity
        bool hovered = false;
    };
    QVector<Grip> activeGrips;
    int hoveredGripIndex = -1;
    int draggedGripIndex = -1;

    // Object snap
    struct SnapPoint {
        QVector3D position;
        QString snapType; // "endpoint", "midpoint", "center", "nearest"
        EntityRef entityRef;
    };
    SnapPoint currentSnapPoint;
    bool snapActive = false;

    // Helper methods
    EntityRef pickEntity(const QPoint& screenPos);
    void updateGrips();
    void drawGrips();
    void drawSnapMarker(const QVector3D& pos, const QString& snapType);
    SnapPoint findNearestSnapPoint(const QVector3D& worldPos);
    float distanceToEntity(const QVector3D& point, const EntityRef& entityRef);
    QVector<QVector3D> getEntitySnapPoints(const EntityRef& entityRef);};

#endif // CADVIEW_H
