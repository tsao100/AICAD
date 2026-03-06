/**
 * @file Feature.cpp
 * @brief CAD 特徵基礎類別實作
 * @author AICAD Team
 * @date 2025-01-08
 */

#include "Feature.h"
#include "Document.h"
#include <QJsonObject>
#include <QDebug>

namespace aicad {
namespace cad {

Feature::Feature(Document* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
    , m_name("Feature")
    , m_document(parent)
    , m_visible(true)
    , m_suppressed(false)
    , m_hasError(false)
    , m_parent(nullptr)
{
    qDebug() << "[Feature]" << m_id.toString() << "created";
}

Feature::~Feature() {
    qDebug() << "[Feature]" << m_id.toString() << "destroyed";

    // 移除所有子特徵
    for (Feature* child : m_children) {
        if (child) {
            child->m_parent = nullptr;
        }
    }
    m_children.clear();

    // 從父特徵移除自己
    if (m_parent) {
        m_parent->removeChild(this);
    }
}

QString Feature::typeString() const {
    switch (type()) {
    case FeatureType::Base:     return "Base";
    case FeatureType::Sketch:   return "Sketch";
    case FeatureType::Extrude:  return "Extrude";
    case FeatureType::Revolve:  return "Revolve";
    case FeatureType::Fillet:   return "Fillet";
    case FeatureType::Chamfer:  return "Chamfer";
    case FeatureType::Boolean:  return "Boolean";
    case FeatureType::Pattern:  return "Pattern";
    default:                    return "Unknown";
    }
}

void Feature::setName(const QString& name) {
    if (m_name != name) {
        m_name = name;
        qDebug() << "[Feature]" << m_id.toString() << "renamed to" << name;
        Q_EMIT nameChanged(name);
    }
}

void Feature::setShape(const TopoDS_Shape& shape) {
    m_shape = shape;
    clearError();
    qDebug() << "[Feature]" << m_name << "shape updated";
    Q_EMIT shapeChanged();
}

void Feature::setVisible(bool visible) {
    if (m_visible != visible) {
        m_visible = visible;
        qDebug() << "[Feature]" << m_name << "visibility:" << visible;
        Q_EMIT visibilityChanged(visible);
    }
}

void Feature::setSuppressed(bool suppressed) {
    if (m_suppressed != suppressed) {
        m_suppressed = suppressed;
        qDebug() << "[Feature]" << m_name << "suppressed:" << suppressed;
        Q_EMIT suppressedChanged(suppressed);

        // 抑制狀態改變時需要重建
        if (m_document) {
            Q_EMIT rebuildRequested();
        }
    }
}

void Feature::setError(const QString& message) {
    setErrorState(true, message);
}

void Feature::clearError() {
    setErrorState(false, QString());
}

void Feature::setErrorState(bool hasError, const QString& message) {
    if (m_hasError != hasError || m_errorMessage != message) {
        m_hasError = hasError;
        m_errorMessage = message;

        if (hasError) {
            qWarning() << "[Feature]" << m_name << "ERROR:" << message;
        } else {
            qDebug() << "[Feature]" << m_name << "error cleared";
        }

        Q_EMIT errorChanged(hasError, message);
    }
}

void Feature::setParent(Feature* parent) {
    if (m_parent == parent) {
        return;
    }

    // 從舊父特徵移除
    if (m_parent) {
        m_parent->removeChild(this);
    }

    m_parent = parent;

    // 加入新父特徵
    if (m_parent) {
        m_parent->addChild(this);
    }

    qDebug() << "[Feature]" << m_name << "parent changed to"
             << (parent ? parent->name() : "null");
}

void Feature::addChild(Feature* child) {
    if (!child || m_children.contains(child)) {
        return;
    }

    m_children.append(child);

    if (child->parent() != this) {
        child->setParent(this);
    }

    qDebug() << "[Feature]" << m_name << "child added:" << child->name();
}

void Feature::removeChild(Feature* child) {
    if (!child) {
        return;
    }

    if (m_children.removeOne(child)) {
        if (child->parent() == this) {
            child->m_parent = nullptr;
        }
        qDebug() << "[Feature]" << m_name << "child removed:" << child->name();
    }
}

int Feature::index() const {
    if (!m_document) {
        return -1;
    }

    // 這裡需要 Document 類別實作 features() 方法
    // 暫時返回 -1
    return -1;
}

QJsonObject Feature::toJson() const {
    QJsonObject json;
    json["id"] = m_id.toString();
    json["name"] = m_name;
    json["type"] = typeString();
    json["visible"] = m_visible;
    json["suppressed"] = m_suppressed;

    if (m_hasError) {
        json["hasError"] = true;
        json["errorMessage"] = m_errorMessage;
    }

    if (m_parent) {
        json["parentId"] = m_parent->id();
    }

    return json;
}

bool Feature::fromJson(const QJsonObject& json) {
    if (!json.contains("id") || !json.contains("name")) {
        qWarning() << "[Feature] Invalid JSON: missing id or name";
        return false;
    }

    m_id = QUuid::fromString(json["id"].toString());
    setName(json["name"].toString());

    if (json.contains("visible")) {
        m_visible = json["visible"].toBool();  // set directly, don't emit yet
    }

    if (json.contains("suppressed")) {
        setSuppressed(json["suppressed"].toBool());
    }

    if (json.contains("hasError") && json["hasError"].toBool()) {
        setError(json["errorMessage"].toString());
    }

    // 注意：父子關係需要在所有特徵載入後再建立
    // emit after full load
    Q_EMIT visibilityChanged(m_visible);

    return true;
}

} // namespace cad
} // namespace aicad
