/**
 * @file Document.cpp
 * @brief Document 實作
 * @author Ben
 * @date 2024-12-04
 */

#include "Document.h"
#include "features/Feature.h"
#include "features/Sketch.h"
#include "features/Extrude.h"
#include "geometry/CustomPlane.h"

#include <QFile>
#include <QDataStream>
#include <QDebug>

namespace aicad {
namespace cad {

class Document::Private {
public:
    QString fileName;
    bool modified = false;
    int nextFeatureId = 1;
    QVector<Feature*> features;
};

Document::Document(QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    qDebug() << "[Document] Created";
}

Document::~Document() {
    qDebug() << "[Document] Destroying...";
    clear();
    delete d;
}

void Document::newDocument() {
    qDebug() << "[Document] Creating new document";
    
    clear();
    
    d->fileName.clear();
    d->modified = false;
    d->nextFeatureId = 1;
    
    Q_EMIT fileNameChanged(d->fileName);
    Q_EMIT modifiedChanged(false);
    Q_EMIT documentCleared();
    
    qDebug() << "[Document] New document created";
}

bool Document::save(const QString& fileName) {
    qDebug() << "[Document] Saving to:" << fileName;
    
    // TODO: 實作完整的檔案儲存
    // 目前只是樁實作
    
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[Document] Failed to open file for writing:" << fileName;
        return false;
    }
    
    QDataStream stream(&file);
    
    // 寫入檔頭
    stream << QString("AICAD");
    stream << qint32(1);  // 版本號
    
    // 寫入特徵數量
    stream << qint32(d->features.size());
    
    // TODO: 寫入每個特徵的資料
    
    file.close();
    
    d->fileName = fileName;
    d->modified = false;
    
    Q_EMIT fileNameChanged(fileName);
    Q_EMIT modifiedChanged(false);
    
    qDebug() << "[Document] Save successful";
    return true;
}

bool Document::load(const QString& fileName) {
    qDebug() << "[Document] Loading from:" << fileName;
    
    // TODO: 實作完整的檔案載入
    // 目前只是樁實作
    
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[Document] Failed to open file for reading:" << fileName;
        return false;
    }
    
    QDataStream stream(&file);
    
    // 讀取檔頭
    QString magic;
    stream >> magic;
    if (magic != "AICAD") {
        qWarning() << "[Document] Invalid file format";
        return false;
    }
    
    qint32 version;
    stream >> version;
    
    if (version != 1) {
        qWarning() << "[Document] Unsupported file version:" << version;
        return false;
    }
    
    // 清除現有特徵
    clear();
    
    // 讀取特徵數量
    qint32 featureCount;
    stream >> featureCount;
    
    // TODO: 讀取每個特徵的資料
    
    file.close();
    
    d->fileName = fileName;
    d->modified = false;
    
    Q_EMIT fileNameChanged(fileName);
    Q_EMIT modifiedChanged(false);
    
    qDebug() << "[Document] Load successful";
    return true;
}

Sketch* Document::createSketch(const CustomPlane& plane, const QString& name) {
    qDebug() << "[Document] Creating sketch on" << plane.getDisplayName() << "plane";
    
    Sketch* sketch = new Sketch(plane, this);
    sketch->setId(generateFeatureId());
    
    if (!name.isEmpty()) {
        sketch->setName(name);
    } else {
        sketch->setName(QString("Sketch %1 (%2)")
            .arg(sketch->id())
            .arg(plane.getDisplayName()));
    }
    
    addFeature(sketch);
    
    qDebug() << "[Document] Sketch created:" << sketch->name();
    return sketch;
}

Extrude* Document::createExtrude(Sketch* sketch, double height, const QString& name) {
    if (!sketch) {
        qWarning() << "[Document] Cannot create extrude with null sketch";
        return nullptr;
    }
    
    qDebug() << "[Document] Creating extrude with height:" << height;
    
    Extrude* extrude = new Extrude(sketch, height, this);
    extrude->setId(generateFeatureId());
    
    if (!name.isEmpty()) {
        extrude->setName(name);
    } else {
        extrude->setName(QString("Extrude %1 (%.2f)")
            .arg(extrude->id())
            .arg(height));
    }
    
    addFeature(extrude);
    
    qDebug() << "[Document] Extrude created:" << extrude->name();
    return extrude;
}

void Document::addFeature(Feature* feature) {
    if (!feature) {
        qWarning() << "[Document] Cannot add null feature";
        return;
    }
    
    // 檢查是否已存在
    if (d->features.contains(feature)) {
        qWarning() << "[Document] Feature already in document:" << feature->name();
        return;
    }
    
    d->features.append(feature);
    d->modified = true;
    
    // 連接信號
    connect(feature, &Feature::updated, this, [this, feature]() {
        Q_EMIT featureUpdated(feature);
        setModified(true);
    });
    
    connect(feature, &Feature::nameChanged, this, [this]() {
        setModified(true);
    });
    
    Q_EMIT featureAdded(feature);
    Q_EMIT modifiedChanged(true);
    Q_EMIT featureCountChanged(d->features.size());
    
    qDebug() << "[Document] Feature added:" << feature->name() 
             << "Total features:" << d->features.size();
}

void Document::removeFeature(Feature* feature) {
    if (!feature) {
        return;
    }
    
    if (!d->features.contains(feature)) {
        qWarning() << "[Document] Feature not in document:" << feature->name();
        return;
    }
    
    // 檢查是否有其他特徵依賴此特徵
    if (!feature->dependents().isEmpty()) {
        qWarning() << "[Document] Cannot remove feature with dependents:" 
                   << feature->name();
        // TODO: 可以選擇級聯刪除或拒絕刪除
        return;
    }
    
    QString featureName = feature->name();
    
    d->features.removeOne(feature);
    d->modified = true;
    
    Q_EMIT featureRemoved(feature);
    Q_EMIT modifiedChanged(true);
    Q_EMIT featureCountChanged(d->features.size());
    
    feature->deleteLater();
    
    qDebug() << "[Document] Feature removed:" << featureName
             << "Remaining features:" << d->features.size();
}

QVector<Feature*> Document::features() const {
    return d->features;
}

Feature* Document::findFeature(int id) const {
    for (Feature* feature : d->features) {
        if (feature->id() == id) {
            return feature;
        }
    }
    return nullptr;
}

Feature* Document::findFeatureByName(const QString& name) const {
    for (Feature* feature : d->features) {
        if (feature->name() == name) {
            return feature;
        }
    }
    return nullptr;
}

void Document::updateAll() {
    qDebug() << "[Document] Updating all features...";
    
    int updatedCount = 0;
    
    // 簡單策略：依序更新所有特徵
    // TODO: 實作更智慧的依賴排序更新
    for (Feature* feature : d->features) {
        if (feature->needsUpdate()) {
            feature->update();
            updatedCount++;
        }
    }
    
    qDebug() << "[Document] Updated" << updatedCount << "features";
}

int Document::featureCount() const {
    return d->features.size();
}

QString Document::fileName() const {
    return d->fileName;
}

void Document::setFileName(const QString& fileName) {
    if (d->fileName == fileName) {
        return;
    }
    
    d->fileName = fileName;
    Q_EMIT fileNameChanged(fileName);
    
    qDebug() << "[Document] File name changed to:" << fileName;
}

bool Document::isModified() const {
    return d->modified;
}

void Document::setModified(bool modified) {
    if (d->modified == modified) {
        return;
    }
    
    d->modified = modified;
    Q_EMIT modifiedChanged(modified);
    
    qDebug() << "[Document] Modified state changed to:" << modified;
}

void Document::clear() {
    if (d->features.isEmpty()) {
        return;
    }
    
    qDebug() << "[Document] Clearing" << d->features.size() << "features";
    
    qDeleteAll(d->features);
    d->features.clear();
    
    Q_EMIT documentCleared();
    Q_EMIT featureCountChanged(0);
    
    qDebug() << "[Document] Cleared";
}

int Document::generateFeatureId() {
    return d->nextFeatureId++;
}

} // namespace cad
} // namespace aicad