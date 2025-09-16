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

    QMatrix4x4 viewMatrix() const;
    QVector3D eye() const;
    QVector3D direction() const;

    // getters/setters
    float distance() const { return distance_; }
    void setDistance(float d) { distance_ = d; }

    QVector3D center() const { return center_; }
    void setCenter(const QVector3D &c) { center_ = c; }
    void setViewMatrix(const QMatrix4x4 &view) {
        m_view = view;
    }


private:
    float distance_;
    float pitch, yaw;
    QVector3D center_;
    QVector3D up;
    QMatrix4x4 m_view;
};
