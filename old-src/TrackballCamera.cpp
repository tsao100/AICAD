#include "TrackballCamera.h"
#include <cmath>
#include <QtMath>

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

QMatrix4x4 TrackballCamera::getProjectionMatrix() const {
    return projection;
}

void TrackballCamera::setOrthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane) {
    projection.setToIdentity();
    projection.ortho(left, right, bottom, top, nearPlane, farPlane);
}

void TrackballCamera::lookAt(const QVector3D& pos, const QVector3D& tgt, const QVector3D& upVec) {
    position = pos;
    center_ = tgt;
    up = upVec;
    distance_ = (position - center_).length();
}

void TrackballCamera::orbit(float deltaX, float deltaY) {
    yaw += deltaX;
    pitch += deltaY;
    pitch = qBound(-89.0f, pitch, 89.0f);

    float radPitch = qDegreesToRadians(pitch);
    float radYaw = qDegreesToRadians(yaw);

    QVector3D dir;
    dir.setX(distance_ * qCos(radPitch) * qSin(radYaw));
    dir.setY(distance_ * qSin(radPitch));
    dir.setZ(distance_ * qCos(radPitch) * qCos(radYaw));

    position = center_ + dir;
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

void TrackballCamera::setViewXY() {
    float aspect = 1.33f;
    lookAt(QVector3D(0,10,0), QVector3D(0,0,0), QVector3D(0,0,-1));
    setOrthographic(-5*aspect,5*aspect,-5,5,-20,20);
    is2D = false;                    // 可開啟 2D 模式
}

void TrackballCamera::setViewXZ() {
    float aspect = 1.33f;
    lookAt(QVector3D(0,0,10), QVector3D(0,0,0), QVector3D(0,1,0));
    setOrthographic(-5*aspect,5*aspect,-5,5,-20,20);
    is2D = false;                   // 正投影 2D 視角
}

void TrackballCamera::setViewYZ() {
    float aspect = 1.33f;
    lookAt(QVector3D(10,0,0), QVector3D(0,0,0), QVector3D(0,1,0));
    setOrthographic(-5*aspect,5*aspect,-5,5,-20,20);
    is2D = false;
}
