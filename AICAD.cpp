/*
 * AICAD.cpp
 * Single-file example: Qt5 Widgets + QOpenGLWidget to create a combined 2D/3D CAD view
 * Features:
 *  - 2D view: custom QWidget with high-quality grid, pan/zoom, rubber-band selection
 *  - 3D view: QOpenGLWidget with simple trackball camera, perspective/orthographic toggle,
 *            axis triad, simple geometry rendering
 *  - Main window with toolbar to switch modes and simple status bar showing coords
 *
 * Build (Qt5):
 *  qmake -project "QT += widgets opengl" && qmake && make
 *  or use CMake with find_package(Qt5 COMPONENTS Widgets OpenGL REQUIRED)
 *
 * Note: This is a starting foundation for CAD app development. Extend it to add
 * snapping, CAD entities, file IO, selection semantics, GPU shaders, and precise
 * numeric UI as needed.
 */

#include <QtWidgets>
#include <QtOpenGL>
#include <QOpenGLFunctions>
#include <cmath>

// ---------------------------------------------------------
// Utility: 2D Point / Transform helpers
// ---------------------------------------------------------
struct Vec2 { double x=0,y=0; Vec2(){} Vec2(double a,double b):x(a),y(b){} };

// ---------------------------------------------------------
// 2D View: simple immediate-mode painter using QWidget and QPainter
// Supports: pan (middle mouse drag), zoom (wheel), left-drag rubber band
// Draws: grid, sample geometry (lines, rectangle), coordinate readout
// ---------------------------------------------------------
class CadView2D : public QWidget {
    Q_OBJECT
public:
    CadView2D(QWidget *parent=nullptr) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setBackgroundRole(QPalette::Base);
        setAutoFillBackground(true);
        m_transform = QTransform();
        m_scale = 1.0;
    }

    // convert screen->world and world->screen
    QPointF toScreen(const QPointF &world) const {
        return m_transform.map(world);
    }
    QPointF toWorld(const QPointF &screen) const {
        return m_transform.inverted().map(screen);
    }

protected:
    void paintEvent(QPaintEvent *ev) override {
        Q_UNUSED(ev);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // fill background
        p.fillRect(rect(), palette().color(QPalette::Base));

        // grid in world coordinates
        drawGrid(&p);

        // sample geometry: draw some objects in world coordinates
        p.save();
        p.setTransform(m_transform, true); // world -> screen

        // example: crosshair origin
        p.setPen(QPen(QColor(200,40,40), 0));
        p.drawLine(QPointF(-1000,0), QPointF(1000,0));
        p.drawLine(QPointF(0,-1000), QPointF(0,1000));

        // rectangle
        p.setPen(QPen(Qt::blue, 0));
        p.setBrush(QBrush(QColor(0,0,255,40)));
        p.drawRect(QRectF(50,50,200,120));

        p.restore();

        // rubber band (in screen coords)
        if (m_rubberActive) {
            p.setPen(QPen(QColor(0,120,215), 1, Qt::DashLine));
            p.setBrush(QColor(0,120,215,30));
            p.drawRect(QRect(m_rubberStart, m_rubberEnd));
        }

        // HUD: show world coords under mouse
        p.setPen(Qt::black);
        QString coordText = QString("W: %1, %2").arg(m_mouseWorld.x(),0,'f',2).arg(m_mouseWorld.y(),0,'f',2);
        p.drawText(8, height()-8, coordText);
    }

    void resizeEvent(QResizeEvent *ev) override {
        Q_UNUSED(ev);
        updateTransform();
    }

    void mousePressEvent(QMouseEvent *ev) override {
        if (ev->button() == Qt::MiddleButton) {
            m_panning = true;
            m_panStart = ev->pos();
            setCursor(Qt::ClosedHandCursor);
        } else if (ev->button() == Qt::LeftButton) {
            m_rubberActive = true;
            m_rubberStart = ev->pos();
            m_rubberEnd = ev->pos();
        }
    }
    void mouseMoveEvent(QMouseEvent *ev) override {
        QPoint pos = ev->pos();
        m_mouseWorld = toWorld(pos);
        if (m_panning) {
            QPoint delta = pos - m_panStart;
            m_panStart = pos;
            m_transform.translate(delta.x(), delta.y());
            update();
        } else if (m_rubberActive) {
            m_rubberEnd = pos;
            update();
        } else {
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent *ev) override {
        if (ev->button() == Qt::MiddleButton) {
            m_panning = false;
            setCursor(Qt::ArrowCursor);
        } else if (ev->button() == Qt::LeftButton) {
            m_rubberActive = false;
            QRect r(m_rubberStart, m_rubberEnd);
            // convert rect to world and ideally select objects
            QRectF worldRect = QRectF(toWorld(r.topLeft()), toWorld(r.bottomRight())).normalized();
            qDebug() << "Rubber selection in world:", worldRect;
            update();
        }
    }
    void wheelEvent(QWheelEvent *ev) override {
        // zoom around cursor
        QPointF p = ev->position();
        const double zoomFactor = std::pow(1.0015, ev->angleDelta().y());
        m_transform.translate(p.x(), p.y());
        m_transform.scale(zoomFactor, zoomFactor);
        m_transform.translate(-p.x(), -p.y());
        m_scale *= zoomFactor;
        update();
    }

private:
    void updateTransform() {
        // if you want center the origin in center:
        // keep current transform; ensure valid
        if (m_transform.isIdentity()) {
            // center origin in widget center
            m_transform.translate(width()/2.0, height()/2.0);
        }
    }

    void drawGrid(QPainter *p) {
        // draw grid in world coordinates by mapping a range of world points
        // choose grid spacing relative to current scale
        // We'll compute approximate spacing in world units so lines appear nicely spaced.
        p->save();
        // find world rect
        QRectF worldRect = QRectF(toWorld(QPointF(0,0)), toWorld(QPointF(width(), height()))).normalized();
        double pixelsPerUnit = m_scale; // approximation: because transform may include translation only
        if (pixelsPerUnit <= 0) pixelsPerUnit = 1.0;
        // choose spacing: powers of 10 * {1,2,5}
        double targetPixels = 80; // approx spacing in pixels
        double worldSpacing = targetPixels / pixelsPerUnit;
        double base = std::pow(10.0, std::floor(std::log10(worldSpacing)));
        double multiples[] = {1,2,5};
        double spacing = base * multiples[0];
        for (double m : multiples) {
            double s = base*m;
            if (s >= worldSpacing) { spacing = s; break; }
        }
        // draw light grid lines
        QPen gridPen(QColor(220,220,220)); gridPen.setCosmetic(true);
        p->setPen(gridPen);
        // vertical lines
        int startX = std::floor(worldRect.left() / spacing) - 1;
        int endX = std::ceil(worldRect.right() / spacing) + 1;
        for (int i=startX;i<=endX;i++) {
            double x = i*spacing;
            QPointF a = toScreen(QPointF(x, worldRect.top()));
            QPointF b = toScreen(QPointF(x, worldRect.bottom()));
            p->drawLine(QPointF(a.x(), a.y()), QPointF(b.x(), b.y()));
        }
        // horizontal
        int startY = std::floor(worldRect.top() / spacing) - 1;
        int endY = std::ceil(worldRect.bottom() / spacing) + 1;
        for (int i=startY;i<=endY;i++) {
            double y = i*spacing;
            QPointF a = toScreen(QPointF(worldRect.left(), y));
            QPointF b = toScreen(QPointF(worldRect.right(), y));
            p->drawLine(QPointF(a.x(), a.y()), QPointF(b.x(), b.y()));
        }
        p->restore();
    }

    // state
    QTransform m_transform;
    double m_scale;
    bool m_panning=false;
    QPoint m_panStart;
    bool m_rubberActive=false;
    QPoint m_rubberStart, m_rubberEnd;
    QPointF m_mouseWorld;
};

// ---------------------------------------------------------
// Simple Arcball / Trackball camera for 3D
// ---------------------------------------------------------
class TrackballCamera {
public:
    TrackballCamera() { reset(); }
    void reset() { distance = 5.0; pitch=0; yaw=0; center = QVector3D(0,0,0); }
    void rotateBy(float dx, float dy) { yaw += dx; pitch += dy; }
    void panBy(float dx, float dy) {
        // pan in camera space
        QVector3D right = QVector3D::crossProduct(direction(), up).normalized();
        QVector3D u = up.normalized();
        center += -right * dx + u * dy;
    }
    void zoomBy(float dz) { distance *= std::pow(1.0015f, dz); if (distance < 0.01f) distance = 0.01f; }
    QMatrix4x4 viewMatrix() const {
        QMatrix4x4 m;
        QVector3D pos = eye();
        m.lookAt(pos, center, up);
        return m;
    }
    QVector3D eye() const {
        QVector3D dir = direction();
        return center - dir * distance;
    }
    QVector3D direction() const {
        // spherical to cartesian
        float cp = std::cos(pitch), sp = std::sin(pitch);
        float cy = std::cos(yaw), sy = std::sin(yaw);
        return QVector3D(cy*cp, sp, sy*cp).normalized();
    }
    float distance;
    float pitch, yaw;
    QVector3D center;
    QVector3D up = QVector3D(0,1,0);
};

// ---------------------------------------------------------
// 3D View: QOpenGLWidget using QOpenGLFunctions (Qt5)
// Renders: simple axis triad, cube; supports orbit/pan/zoom
// ---------------------------------------------------------
class CadView3D : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    CadView3D(QWidget *parent=nullptr) : QOpenGLWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        m_camera.reset();
    }
protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glClearColor(0.95f, 0.95f, 0.95f, 1.0f);
    }
    void resizeGL(int w, int h) override {
        glViewport(0,0,w,h);
        m_proj.setToIdentity();
        float aspect = float(w)/qMax(h,1);
        if (m_ortho) {
            float s = 5.0f;
            m_proj.ortho(-s*aspect, s*aspect, -s, s, 0.01f, 1000.0f);
        } else {
            m_proj.perspective(60.0f, aspect, 0.01f, 1000.0f);
        }
    }
    void paintGL() override {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        QMatrix4x4 view = m_camera.viewMatrix();
        // draw axis triad
        drawAxis(view);
        // draw cube at origin
        drawCube(view);
    }

    void mousePressEvent(QMouseEvent *ev) override {
        m_lastPos = ev->pos();
        if (ev->buttons() & Qt::LeftButton) m_orbit = true;
        if (ev->buttons() & Qt::MiddleButton) m_pan = true;
    }
    void mouseMoveEvent(QMouseEvent *ev) override {
        QPointF p = ev->pos();
        QPointF d = p - m_lastPos;
        m_lastPos = p;
        if (m_orbit) {
            // sensitivity
            m_camera.rotateBy(-d.x()*0.01f, -d.y()*0.01f);
            update();
        } else if (m_pan) {
            m_camera.panBy(d.x()*0.01f, -d.y()*0.01f);
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent *ev) override {
        Q_UNUSED(ev);
        m_orbit = m_pan = false;
    }
    void wheelEvent(QWheelEvent *ev) override {
        m_camera.zoomBy(ev->angleDelta().y());
        update();
    }

private:
    void drawAxis(const QMatrix4x4 &view) {
        QMatrix4x4 mvp = m_proj * view;
        // draw using immediate-mode GL for simplicity (compatibility)
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(mvp.constData());
        glLineWidth(2.0f);
        glBegin(GL_LINES);
        // X red
        glColor3f(1,0,0);
        glVertex3f(0,0,0); glVertex3f(1,0,0);
        // Y green
        glColor3f(0,1,0);
        glVertex3f(0,0,0); glVertex3f(0,1,0);
        // Z blue
        glColor3f(0,0,1);
        glVertex3f(0,0,0); glVertex3f(0,0,1);
        glEnd();
    }
    void drawCube(const QMatrix4x4 &view) {
        QMatrix4x4 model; model.setToIdentity(); model.scale(1.0f);
        QMatrix4x4 mvp = m_proj * view * model;
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(mvp.constData());
        // cube
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

    TrackballCamera m_camera;
    bool m_orbit=false, m_pan=false;
    bool m_ortho=false; // toggle
    QPoint m_lastPos;
    QMatrix4x4 m_proj;
};

// ---------------------------------------------------------
// Main window - integrates 2D and 3D views and a toolbar
// ---------------------------------------------------------
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow() {
        createActions();
        createToolbar();
        createCentral();
        statusBar()->showMessage("Ready");
        setWindowTitle("Qt5 CAD View - 2D/3D Example");
        resize(1100, 700);
    }
private slots:
    void toggle2D() { m_stack->setCurrentWidget(m_view2d); }
    void toggle3D() { m_stack->setCurrentWidget(m_view3d); }
private:
    void createActions() {
        m_act2D = new QAction(QIcon::fromTheme("view-grid"), "2D View", this);
        m_act3D = new QAction(QIcon::fromTheme("view3d"), "3D View", this);
        connect(m_act2D, &QAction::triggered, this, &MainWindow::toggle2D);
        connect(m_act3D, &QAction::triggered, this, &MainWindow::toggle3D);
    }
    void createToolbar() {
        QToolBar *tb = addToolBar("View");
        tb->addAction(m_act2D);
        tb->addAction(m_act3D);
        tb->addSeparator();
        tb->addWidget(new QLabel("Zoom/Pan: mouse wheel / middle drag"));
    }
    void createCentral() {
        QWidget *central = new QWidget(this);
        QVBoxLayout *lay = new QVBoxLayout(central);
        m_stack = new QStackedWidget(central);
        m_view2d = new CadView2D(central);
        m_view3d = new CadView3D(central);
        m_stack->addWidget(m_view2d);
        m_stack->addWidget(m_view3d);
        lay->addWidget(m_stack);
        setCentralWidget(central);
        m_stack->setCurrentWidget(m_view2d);
    }
    // actions
    QAction *m_act2D;
    QAction *m_act3D;
    // views
    QStackedWidget *m_stack;
    CadView2D *m_view2d;
    CadView3D *m_view3d;
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}

#include "qt5_cad_view.moc"

