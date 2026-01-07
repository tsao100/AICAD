/**
 * @file Feature.cpp
 * @brief 特徵基礎類別實作
 * @author Ben
 * @date 2025-01-06
 */

#include "Feature.h"
#include "Document.h"
#include "core/Application.h"
#include "core/EventBus.h"

#include <QDebug>

// OCCT includes
#include <TDataStd_Name.hxx>
#include <TDataStd_Integer.hxx>
#include <TNaming_NamedShape.hxx>
#include <TNaming_Builder.hxx>

namespace aicad {
namespace cad {

// Private implementation
class Feature::Private {
public:
    Private(Document* doc, TDF_Label lbl)
        : document(doc)
        , label(lbl)
        , visible(true)
        , valid(true)
    {
    }
    
    Document* document;
    TDF_Label label;
    QString name;
    bool visible;
    bool valid;
    QHash<QString, QVariant> properties;
};

Feature::Feature(Document* doc, TDF_Label label)
    : QObject(doc)
    , d(new Private(doc, label))
{
    qDebug() << "[Feature] Constructor called";
}

Feature::~Feature() {
    qDebug() << "[Feature] Destructor called:" << d->name;
    delete d;
}

int Feature::id() const {
    Handle(TDataStd_Integer) idAttr;
    if (d->label.FindAttribute(TDataStd_Integer::GetID(), idAttr)) {
        return idAttr->Get();
    }
    return -1;
}

QString Feature::name() const {
    if (!d->name.isEmpty()) {
        return d->name;
    }
    
    // 從 OCCT 標籤讀取
    Handle(TDataStd_Name) nameAttr;
    if (d->label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        TCollection_ExtendedString extStr = nameAttr->Get();
        return QString::fromUtf16(
            reinterpret_cast<const char16_t*>(extStr.ToExtString())
        );
    }
    
    return QString("Feature_%1").arg(id());
}

void Feature::setName(const QString& name) {
    if (d->name != name) {
        d->name = name;
        
        // 儲存到 OCCT 標籤
        TDataStd_Name::Set(d->label, 
            TCollection_ExtendedString(name.toStdWString().c_str())
        );
        
        Q_EMIT nameChanged(d->name);
        notifyModified();
        
        qDebug() << "[Feature] Name changed to:" << name;
    }
}

QString Feature::typeName() const {
    return featureTypeToString(type());
}

bool Feature::isVisible() const {
    return d->visible;
}

void Feature::setVisible(bool visible) {
    if (d->visible != visible) {
        d->visible = visible;
        Q_EMIT visibleChanged(d->visible);
        
        qDebug() << "[Feature] Visibility changed:" << d->visible;
    }
}

bool Feature::isValid() const {
    return d->valid;
}

Document* Feature::document() const {
    return d->document;
}

TDF_Label Feature::label() const {
    return d->label;
}

TopoDS_Shape Feature::shape() const {
    Handle(TNaming_NamedShape) namedShape;
    if (d->label.FindAttribute(TNaming_NamedShape::GetID(), namedShape)) {
        return namedShape->Get();
    }
    return TopoDS_Shape();
}

QVariant Feature::property(const QString& name) const {
    return d->properties.value(name);
}

void Feature::setProperty(const QString& name, const QVariant& value) {
    d->properties[name] = value;
    notifyModified();
}

QStringList Feature::propertyNames() const {
    return d->properties.keys();
}

void Feature::setShape(const TopoDS_Shape& shape) {
    if (!d->label.IsNull() && !shape.IsNull()) {
        TNaming_Builder builder(d->label);
        builder.Generated(shape);
        
        qDebug() << "[Feature] Shape set for:" << name();
    }
}

void Feature::setValid(bool valid) {
    if (d->valid != valid) {
        d->valid = valid;
        Q_EMIT validChanged(d->valid);
        
        qDebug() << "[Feature] Valid state changed:" << d->valid;
    }
}

void Feature::notifyModified() {
    Q_EMIT modified();
    
    if (d->document) {
        d->document->setModified(true);
        Q_EMIT d->document->featureModified(this);
    }
    
    // 透過 EventBus 發布事件
    if (auto app = core::Application::instance()) {
        if (auto bus = app->eventBus()) {
            bus->publish(core::Events::FEATURE_UPDATED, 
                        QVariant::fromValue(this));
        }
    }
}

// Helper functions
QString featureTypeToString(FeatureType type) {
    switch (type) {
        case FeatureType::Sketch:
            return "Sketch";
        case FeatureType::Extrude:
            return "Extrude";
        case FeatureType::Revolve:
            return "Revolve";
        case FeatureType::Fillet:
            return "Fillet";
        case FeatureType::Chamfer:
            return "Chamfer";
        default:
            return "Unknown";
    }
}

FeatureType stringToFeatureType(const QString& str) {
    QString lower = str.toLower();
    if (lower == "sketch") return FeatureType::Sketch;
    if (lower == "extrude") return FeatureType::Extrude;
    if (lower == "revolve") return FeatureType::Revolve;
    if (lower == "fillet") return FeatureType::Fillet;
    if (lower == "chamfer") return FeatureType::Chamfer;
    return FeatureType::Unknown;
}

} // namespace cad
} // namespace aicad