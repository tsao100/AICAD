/**
 * @file Feature.cpp
 * @brief Feature 實作
 * @author Ben
 * @date 2024-12-04
 */

#include "Feature.h"
#include <QDebug>

namespace aicad {
namespace cad {

class Feature::Private {
public:
    int id = -1;
    QString name;
    bool visible = true;
    bool invalidated = true;
    TopoDS_Shape shape;
    QVector<Feature*> dependents;
};

Feature::Feature(QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    qDebug() << "[Feature] Created";
}

Feature::~Feature() {
    qDebug() << "[Feature] Destroyed:" << d->name;
    delete d;
}

void Feature::update() {
    if (!d->invalidated && !d->shape.IsNull()) {
        qDebug() << "[Feature] Already up to date:" << d->name;
        return;
    }
    
    qDebug() << "[Feature] Updating:" << d->name;
    
    try {
        TopoDS_Shape newShape = compute();
        
        if (newShape.IsNull()) {
            QString error = QString("Compute returned null shape for feature '%1'")
                .arg(d->name);
            qWarning() << "[Feature]" << error;
            Q_EMIT computeFailed(error);
            Q_EMIT validityChanged(false);
            return;
        }
        
        setShape(newShape);
        d->invalidated = false;
        
        qDebug() << "[Feature] Update successful:" << d->name;
        Q_EMIT updated();
        Q_EMIT validityChanged(true);
        
    } catch (const std::exception& e) {
        QString error = QString("Exception during compute for feature '%1': %2")
            .arg(d->name).arg(e.what());
        qCritical() << "[Feature]" << error;
        Q_EMIT computeFailed(error);
        Q_EMIT validityChanged(false);
    } catch (...) {
        QString error = QString("Unknown exception during compute for feature '%1'")
            .arg(d->name);
        qCritical() << "[Feature]" << error;
        Q_EMIT computeFailed(error);
        Q_EMIT validityChanged(false);
    }
}

QVector<Feature*> Feature::dependencies() const {
    // 基礎類別沒有依賴，子類別覆寫此方法
    return QVector<Feature*>();
}

QVector<Feature*> Feature::dependents() const {
    return d->dependents;
}

void Feature::addDependent(Feature* dependent) {
    if (dependent && !d->dependents.contains(dependent)) {
        d->dependents.append(dependent);
        qDebug() << "[Feature]" << d->name << "added dependent:" << dependent->name();
    }
}

void Feature::removeDependent(Feature* dependent) {
    if (d->dependents.removeOne(dependent)) {
        qDebug() << "[Feature]" << d->name << "removed dependent:" << dependent->name();
    }
}

int Feature::id() const {
    return d->id;
}

void Feature::setId(int id) {
    d->id = id;
}

QString Feature::name() const {
    return d->name;
}

void Feature::setName(const QString& name) {
    if (d->name == name) {
        return;
    }
    
    d->name = name;
    qDebug() << "[Feature] Name changed to:" << name;
    Q_EMIT nameChanged(name);
}

bool Feature::isVisible() const {
    return d->visible;
}

void Feature::setVisible(bool visible) {
    if (d->visible == visible) {
        return;
    }
    
    d->visible = visible;
    qDebug() << "[Feature]" << d->name << "visibility changed to:" << visible;
    Q_EMIT visibilityChanged(visible);
}

bool Feature::isValid() const {
    return !d->shape.IsNull();
}

TopoDS_Shape Feature::shape() const {
    return d->shape;
}

bool Feature::needsUpdate() const {
    return d->invalidated;
}

void Feature::setShape(const TopoDS_Shape& shape) {
    d->shape = shape;
}

void Feature::invalidate() {
    if (d->invalidated) {
        return;
    }
    
    d->invalidated = true;
    qDebug() << "[Feature]" << d->name << "invalidated";
    
    // 遞迴標記所有依賴者為無效
    for (Feature* dependent : d->dependents) {
        dependent->invalidate();
    }
}

bool Feature::isInvalidated() const {
    return d->invalidated;
}

QString featureTypeToString(FeatureType type) {
    switch (type) {
        case FeatureType::Sketch:   return "Sketch";
        case FeatureType::Extrude:  return "Extrude";
        case FeatureType::Revolve:  return "Revolve";
        case FeatureType::Fillet:   return "Fillet";
        case FeatureType::Chamfer:  return "Chamfer";
        case FeatureType::Boolean:  return "Boolean";
        default:                    return "Unknown";
    }
}

} // namespace cad
} // namespace aicad