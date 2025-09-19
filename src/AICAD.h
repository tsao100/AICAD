#ifndef AICAD_H
#define AICAD_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMatrix4x4>
#include <QVector3D>
#include <QMouseEvent>
#include <QWheelEvent>
#include <vector>

struct Rectangle2D {
    QVector3D p1;
    QVector3D p2;
};

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

class AICAD : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    AICAD(QWidget* parent = nullptr);
    ~AICAD();

    void setSketchView(SketchView view);
    QVector3D screenToWorld(const QPoint& screenPos);

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
    void drawRectangle(const Rectangle2D& rect);
    void drawExtrudedCube(const Rectangle2D& rect, float height);
    QVector3D mapToPlane(int x, int y);

    QPoint lastMousePos;
    Rectangle2D currentRect;
    bool drawingRect;
    std::vector<Rectangle2D> extrudedRects;
    SketchView currentView;

    Camera camera;
};

#endif // AICAD_H
