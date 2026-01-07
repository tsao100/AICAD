/**
 * @file Document.cpp
 * @brief CAD 文件類別實作
 * @author Ben
 * @date 2025-01-06
 */

#include "Document.h"
#include "Feature.h"
#include "Sketch.h"
#include "Extrude.h"
#include "core/Application.h"
#include "core/EventBus.h"

#include <QDebug>
#include <QFileInfo>

// OCCT includes
#include <XCAFApp_Application.hxx>
#include <TDocStd_Document.hxx>
#include <TDataStd_Name.hxx>
#include <TDataStd_Integer.hxx>
#include <TDF_ChildIterator.hxx>
#include <BinDrivers.hxx>

namespace aicad {
namespace cad {

// Private implementation
class Document::Private {
public:
    Private()
        : nextFeatureId(1)
        , modified(false)
    {
    }
    
    ~Private() {
        qDeleteAll(features);
        features.clear();
    }
    
    Handle(TDocStd_Application) app;
    Handle(TDocStd_Document) doc;
    
    QVector<Feature*> features;
    QHash<int, Feature*> featureMap;
    
    QString fileName;
    bool modified;
    int nextFeatureId;
    
    // 特徵計數器 (用於生成預設名稱)
    int sketchCounter = 0;
    int extrudeCounter = 0;
};

Document::Document(QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    qDebug() << "[Document] Creating new document";
    
    // 初始化 OCCT Application
    d->app = XCAFApp_Application::GetApplication();
    BinDrivers::DefineFormat(d->app);
    
    // 建立新文件
    d->app->NewDocument("BinOcaf", d->doc);
    
    qDebug() << "[Document] Document created successfully";
}

Document::~Document() {
    qDebug() << "[Document] Destroying document:" << d->fileName;
    
    if (!d->doc.IsNull()) {
        d->app->Close(d->doc);
    }
    
    delete d;
}

Sketch* Document::createSketch(const QString& planeName, const QString& name) {
    qDebug() << "[Document] Creating sketch on plane:" << planeName;
    
    // 建立 OCCT 標籤
    TDF_Label root = d->doc->Main();
    TDF_Label label = TDF_TagSource::NewChild(root);
    
    // 解析平面
    SketchPlane plane;
    if (planeName.toUpper() == "XY") {
        plane = SketchPlane::XY();
    } else if (planeName.toUpper() == "XZ") {
        plane = SketchPlane::XZ();
    } else if (planeName.toUpper() == "YZ") {
        plane = SketchPlane::YZ();
    } else {
        qWarning() << "[Document] Unknown plane name:" << planeName << "- using XY";
        plane = SketchPlane::XY();
    }
    
    // 建立 Sketch 物件
    Sketch* sketch = new Sketch(this, label, plane);
    
    // 設定名稱
    QString featureName = name.isEmpty() ? 
        generateFeatureName("Sketch") : name;
    sketch->setName(featureName);
    
    // 設定 ID
    int featureId = generateFeatureId();
    TDataStd_Integer::Set(label, featureId);
    
    // 加入管理
    d->features.append(sketch);
    d->featureMap[featureId] = sketch;
    
    setModified(true);
    
    qDebug() << "[Document] Sketch created:" << featureName 
             << "ID:" << featureId;
    
    Q_EMIT featureCreated(sketch);
    Q_EMIT featureCountChanged(d->features.size());
    
    // 透過 EventBus 發布事件
    if (auto app = core::Application::instance()) {
        if (auto bus = app->eventBus()) {
            bus->publish(core::Events::FEATURE_CREATED, 
                        QVariant::fromValue(sketch));
        }
    }
    
    return sketch;
}

Feature* Document::createExtrude(Sketch* sketch, double distance, 
                                 const QString& name) {
    if (!sketch) {
        qWarning() << "[Document] Cannot create extrude: null sketch";
        return nullptr;
    }
    
    qDebug() << "[Document] Creating extrude from sketch:" 
             << sketch->name() << "distance:" << distance;
    
    // 建立 OCCT 標籤
    TDF_Label root = d->doc->Main();
    TDF_Label label = TDF_TagSource::NewChild(root);
    
    // 建立 Extrude 物件
    Extrude* extrude = new Extrude(this, label, sketch, distance);
    
    // 設定名稱
    QString featureName = name.isEmpty() ? 
        generateFeatureName("Extrude") : name;
    extrude->setName(featureName);
    
    // 設定 ID
    int featureId = generateFeatureId();
    TDataStd_Integer::Set(label, featureId);
    
    // 加入管理
    d->features.append(extrude);
    d->featureMap[featureId] = extrude;
    
    // 重建特徵
    extrude->rebuild();
    
    setModified(true);
    
    qDebug() << "[Document] Extrude created:" << featureName 
             << "ID:" << featureId;
    
    Q_EMIT featureCreated(extrude);
    Q_EMIT featureCountChanged(d->features.size());
    
    // 透過 EventBus 發布事件
    if (auto app = core::Application::instance()) {
        if (auto bus = app->eventBus()) {
            bus->publish(core::Events::FEATURE_CREATED, 
                        QVariant::fromValue(static_cast<Feature*>(extrude)));
        }
    }
    
    return extrude;
}

QVector<Feature*> Document::features() const {
    return d->features;
}

int Document::featureCount() const {
    return d->features.size();
}

Feature* Document::findFeature(int id) const {
    return d->featureMap.value(id, nullptr);
}

Feature* Document::findFeatureByName(const QString& name) const {
    for (Feature* feature : d->features) {
        if (feature && feature->name() == name) {
            return feature;
        }
    }
    return nullptr;
}

bool Document::deleteFeature(Feature* feature) {
    if (!feature) {
        return false;
    }
    
    int featureId = feature->id();
    QString featureName = feature->name();
    
    qDebug() << "[Document] Deleting feature:" << featureName 
             << "ID:" << featureId;
    
    // 從列表中移除
    d->features.removeOne(feature);
    d->featureMap.remove(featureId);
    
    // 刪除物件
    feature->deleteLater();
    
    setModified(true);
    
    Q_EMIT featureDeleted(featureId);
    Q_EMIT featureCountChanged(d->features.size());
    
    // 透過 EventBus 發布事件
    if (auto app = core::Application::instance()) {
        if (auto bus = app->eventBus()) {
            bus->publish(core::Events::FEATURE_DELETED, featureId);
        }
    }
    
    qDebug() << "[Document] Feature deleted. Remaining:" << d->features.size();
    
    return true;
}

bool Document::save(const QString& filePath) {
    if (d->doc.IsNull()) {
        qWarning() << "[Document] Cannot save: document is null";
        return false;
    }
    
    qDebug() << "[Document] Saving document to:" << filePath;
    
    TCollection_ExtendedString path(filePath.toStdWString().c_str());
    PCDM_StoreStatus status = d->app->SaveAs(d->doc, path);
    
    if (status != PCDM_SS_OK) {
        qWarning() << "[Document] Save failed with status:" << status;
        return false;
    }
    
    d->fileName = filePath;
    setModified(false);
    
    qDebug() << "[Document] Document saved successfully";
    
    Q_EMIT fileNameChanged(d->fileName);
    
    return true;
}

bool Document::load(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        qWarning() << "[Document] File does not exist:" << filePath;
        return false;
    }
    
    qDebug() << "[Document] Loading document from:" << filePath;
    
    // 關閉現有文件
    if (!d->doc.IsNull()) {
        d->app->Close(d->doc);
    }
    
    // 清空特徵列表
    qDeleteAll(d->features);
    d->features.clear();
    d->featureMap.clear();
    
    // 開啟文件
    TCollection_ExtendedString path(filePath.toStdWString().c_str());
    PCDM_ReaderStatus status = d->app->Open(path, d->doc);
    
    if (status != PCDM_RS_OK) {
        qWarning() << "[Document] Load failed with status:" << status;
        return false;
    }
    
    // TODO: 重建特徵樹 (需要從 OCCT 標籤讀取)
    // 這部分需要實作特徵的序列化/反序列化
    
    d->fileName = filePath;
    setModified(false);
    
    qDebug() << "[Document] Document loaded successfully";
    
    Q_EMIT fileNameChanged(d->fileName);
    
    return true;
}

QString Document::fileName() const {
    return d->fileName;
}

void Document::setFileName(const QString& name) {
    if (d->fileName != name) {
        d->fileName = name;
        Q_EMIT fileNameChanged(d->fileName);
    }
}

bool Document::isModified() const {
    return d->modified;
}

void Document::setModified(bool modified) {
    if (d->modified != modified) {
        d->modified = modified;
        Q_EMIT modifiedChanged(d->modified);
    }
}

Handle(TDocStd_Document) Document::occtDocument() const {
    return d->doc;
}

void Document::rebuild() {
    qDebug() << "[Document] Rebuilding all features...";
    
    Q_EMIT rebuildStarted();
    
    for (Feature* feature : d->features) {
        if (feature) {
            feature->rebuild();
        }
    }
    
    Q_EMIT rebuildCompleted();
    
    qDebug() << "[Document] Rebuild completed";
}

int Document::generateFeatureId() {
    return d->nextFeatureId++;
}

QString Document::generateFeatureName(const QString& type) {
    if (type == "Sketch") {
        return QString("Sketch_%1").arg(++d->sketchCounter);
    } else if (type == "Extrude") {
        return QString("Extrude_%1").arg(++d->extrudeCounter);
    } else {
        return QString("%1_%2").arg(type).arg(d->nextFeatureId);
    }
}

} // namespace cad
} // namespace aicad