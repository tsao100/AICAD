#pragma once
#include <QMatrix4x4>
#include <QVector3D>

class TrackballCamera {
public:
    TrackballCamera();

    void reset();
    void rotateBy(float dx, float dy);
    void panBy(float dx, float dy);
    void zoomBy(float dz);
    void setOrthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane);
    void lookAt(const QVector3D& pos, const QVector3D& tgt, const QVector3D& upVec);
    void orbit(float deltaX, float deltaY);

    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 getProjectionMatrix() const;
    QVector3D eye() const;
    QVector3D direction() const;

    // 新增：快速切換到標準正視平面
    void setViewXY(); // Top (正視 XY 平面, Z 朝外)
    void setViewXZ(); // Front (正視 XZ 平面, Y 朝外)
    void setViewYZ(); // Right (正視 YZ 平面, X 朝外)

    // getters/setters
    float distance() const { return distance_; }
    void setDistance(float d) { distance_ = d; }

    QVector3D center() const { return center_; }
    void setCenter(const QVector3D &c) { center_ = c; }
    void setViewMatrix(const QMatrix4x4 &view) {
        m_view = view;
    }


private:
    QMatrix4x4 projection;
    float distance_;
    float pitch, yaw;
    QVector3D center_;
    QVector3D position;
    QVector3D up;
    QMatrix4x4 m_view;
    bool is2D = false;
};
