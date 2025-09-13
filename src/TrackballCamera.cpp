#include "TrackballCamera.h"
#include <cmath>

TrackballCamera::TrackballCamera() {
    reset();
}

void TrackballCamera::reset() {
    distance_ = 5.0f;
    pitch = 0.0f;
    yaw = 0.0f;
    center_ = QVector3D(0,0,0);
    up = QVector3D(0,1,0);
}

void TrackballCamera::rotateBy(float dx, float dy) {
    yaw += dx;
    pitch += dy;
}

void TrackballCamera::panBy(float dx, float dy) {
    QVector3D right = QVector3D::crossProduct(direction(), up).normalized();
    QVector3D u = up.normalized();
    center_ += -right * dx + u * dy;
}

void TrackballCamera::zoomBy(float dz) {
    distance_ *= std::pow(1.0015f, dz);
    if (distance_ < 0.01f) distance_ = 0.01f;
}

QMatrix4x4 TrackballCamera::viewMatrix() const {
    QMatrix4x4 m;
    QVector3D pos = eye();
    m.lookAt(pos, center_, up);
    return m;
}

QVector3D TrackballCamera::eye() const {
    QVector3D dir = direction();
    return center_ - dir * distance_;
}

QVector3D TrackballCamera::direction() const {
    float cp = std::cos(pitch), sp = std::sin(pitch);
    float cy = std::cos(yaw), sy = std::sin(yaw);
    return QVector3D(cy*cp, sp, sy*cp).normalized();
}
