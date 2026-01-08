/**
 * @file Document.cpp
 * @brief CAD 文件實作
 * @author AICAD Team
 * @date 2025-01-08
 */

#include "Document.h"
#include "Feature.h"
#include "Sketch.h"
#include "Extrude.h"
#include "Plane.h"

#include <XCAFApp_Application.hxx>
#include <TDataStd_Name.hxx>
#include <BinDrivers.hxx>

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace aicad {
namespace cad {

Document::Document(QObject* parent)
    : QObject(parent)
    , m_fileName()
    , m_modified(false)
    , m_nextFeatureNumber(1)
{
    initOCAF();
    qDebug() << "[Document] Created";
}

Document::~Document() {
    qDebug() << "[Document] Destroying...";
    Q_EMIT aboutToClose();
    clearFeatures();
    qDebug() << "[Document] Destroyed";
}

void Document::initOCAF() {
    // 初始化 OCAF 應用程式
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    BinDrivers::DefineFormat(app);
    
    // 建立新的 OCAF 文件
    app->NewDocument("BinOcaf", m_ocafDoc);
    
    qDebug() << "[Document] OCAF initialized";
}

void Document::setFileName(const QString& fileName) {
    if (m_fileName != fileName) {
        m_fileName = fileName;
        qDebug() << "[Document] File name set to:" << fileName;
        Q_EMIT fileNameChanged(fileName);
    }
}

void Document::setModified(bool modified) {
    if (m_modified != modified) {
        m_modified = modified;
        qDebug() << "[Document] Modified state:" << modified;
        Q_EMIT modifiedChanged(modified);
    }
}

bool Document::save(const QString& fileName) {
    QString saveFileName = fileName.isEmpty() ? m_fileName : fileName;
    
    if (saveFileName.isEmpty()) {
        qWarning() << "[Document] Cannot save: no file name specified";
        return false;
    }
    
    qDebug() << "[Document] Saving to:" << saveFileName;
    
    try {
        // 方法 1: 使用 JSON 格式儲存（較簡單，適合初期開發）
        QJsonObject docJson;
        docJson["version"] = "1.0";
        docJson["fileName"] = QFileInfo(saveFileName).fileName();
        
        // 儲存所有特徵
        QJsonArray featuresArray;
        for (Feature* feature : m_features) {
            if (feature) {
                featuresArray.append(feature->toJson());
            }
        }
        docJson["features"] = featuresArray;
        
        // 寫入檔案
        QFile file(saveFileName);
        if (!file.open(QIODevice::WriteOnly)) {
            qWarning() << "[Document] Cannot open file for writing:" << saveFileName;
            return false;
        }
        
        QJsonDocument jsonDoc(docJson);
        file.write(jsonDoc.toJson(QJsonDocument::Indented));
        file.close();
        
        // 方法 2: 使用 OCAF 原生格式（未來實作）
        // syncToOCAF();
        // TCollection_ExtendedString path(saveFileName.toStdWString().c_str());
        // Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        // PCDM_StoreStatus status = app->SaveAs(m_ocafDoc, path);
        // if (status != PCDM_SS_OK) { return false; }
        
        setFileName(saveFileName);
        setModified(false);
        
        qDebug() << "[Document] Saved successfully";
        return true;
        
    } catch (const Standard_Failure& e) {
        qCritical() << "[Document] OCCT error:" << e.GetMessageString();
        return false;
    } catch (...) {
        qCritical() << "[Document] Unknown error during save";
        return false;
    }
}

bool Document::load(const QString& fileName) {
    if (fileName.isEmpty()) {
        qWarning() << "[Document] Cannot load: no file name specified";
        return false;
    }
    
    if (!QFile::exists(fileName)) {
        qWarning() << "[Document] File does not exist:" << fileName;
        return false;
    }
    
    qDebug() << "[Document] Loading from:" << fileName;
    
    try {
        // 清除現有內容
        clearFeatures();
        
        // 方法 1: 從 JSON 載入
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "[Document] Cannot open file for reading:" << fileName;
            return false;
        }
        
        QByteArray data = file.readAll();
        file.close();
        
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        if (jsonDoc.isNull()) {
            qWarning() << "[Document] Invalid JSON format";
            return false;
        }
        
        QJsonObject docJson = jsonDoc.object();
        
        // 載入特徵
        if (docJson.contains("features")) {
            QJsonArray featuresArray = docJson["features"].toArray();
            
            for (const QJsonValue& val : featuresArray) {
                QJsonObject featureJson = val.toObject();
                QString typeStr = featureJson["type"].toString();
                
                Feature* feature = nullptr;
                
                // 根據類型建立特徵
                if (typeStr == "Sketch") {
                    feature = new Sketch(this);
                } else if (typeStr == "Extrude") {
                    feature = new Extrude(this);
                }
                // 其他類型未來加入
                
                if (feature) {
                    if (feature->fromJson(featureJson)) {
                        addFeature(feature);
                        feature->rebuild();
                    } else {
                        qWarning() << "[Document] Failed to load feature from JSON";
                        delete feature;
                    }
                }
            }
        }
        
        // 方法 2: 從 OCAF 原生格式載入（未來實作）
        // TCollection_ExtendedString path(fileName.toStdWString().c_str());
        // Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        // PCDM_ReaderStatus status = app->Open(path, m_ocafDoc);
        // if (status != PCDM_RS_OK) { return false; }
        // syncFromOCAF();
        
        setFileName(fileName);
        setModified(false);
        
        qDebug() << "[Document] Loaded successfully," << m_features.size() << "features";
        return true;
        
    } catch (const Standard_Failure& e) {
        qCritical() << "[Document] OCCT error:" << e.GetMessageString();
        return false;
    } catch (...) {
        qCritical() << "[Document] Unknown error during load";
        return false;
    }
}

void Document::newDocument() {
    qDebug() << "[Document] Creating new document";
    
    Q_EMIT aboutToClose();
    clearFeatures();
    
    // 重新初始化 OCAF
    if (!m_ocafDoc.IsNull()) {
        Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        app->Close(m_ocafDoc);
    }
    initOCAF();
    
    setFileName(QString());
    setModified(false);
    m_nextFeatureNumber = 1;
    
    qDebug() << "[Document] New document created";
}

void Document::addFeature(Feature* feature) {
    if (!feature) {
        qWarning() << "[Document] Cannot add null feature";
        return;
    }
    
    if (m_features.contains(feature)) {
        qWarning() << "[Document] Feature already in document:" << feature->name();
        return;
    }
    
    m_features.append(feature);
    
    // 連接信號
    connect(feature, &Feature::nameChanged, 
            this, &Document::onFeatureChanged);
    connect(feature, &Feature::shapeChanged, 
            this, &Document::onFeatureChanged);
    connect(feature, &Feature::rebuildRequested, 
            this, &Document::onFeatureRebuildRequested);
    
    setModified(true);
    
    qDebug() << "[Document] Feature added:" << feature->name();
    Q_EMIT featureAdded(feature);
    Q_EMIT featureCountChanged(m_features.size());
}

void Document::removeFeature(Feature* feature) {
    if (!feature) {
        return;
    }
    
    if (!m_features.contains(feature)) {
        qWarning() << "[Document] Feature not in document:" << feature->name();
        return;
    }
    
    qDebug() << "[Document] Removing feature:" << feature->name();
    
    Q_EMIT featureAboutToBeRemoved(feature);
    
    m_features.removeOne(feature);
    
    // 斷開信號
    disconnect(feature, nullptr, this, nullptr);
    
    // 刪除特徵
    feature->deleteLater();
    
    setModified(true);
    
    Q_EMIT featureRemoved();
    Q_EMIT featureCountChanged(m_features.size());
}

void Document::clearFeatures() {
    qDebug() << "[Document] Clearing all features";
    
    // 逆序刪除（避免父子關係問題）
    while (!m_features.isEmpty()) {
        Feature* feature = m_features.takeLast();
        Q_EMIT featureAboutToBeRemoved(feature);
        disconnect(feature, nullptr, this, nullptr);
        delete feature;
        Q_EMIT featureRemoved();
    }
    
    Q_EMIT featureCountChanged(0);
}

Feature* Document::findFeature(const QString& id) const {
    for (Feature* feature : m_features) {
        if (feature && feature->id() == id) {
            return feature;
        }
    }
    return nullptr;
}

Feature* Document::findFeatureByName(const QString& name) const {
    for (Feature* feature : m_features) {
        if (feature && feature->name() == name) {
            return feature;
        }
    }
    return nullptr;
}

int Document::indexOf(Feature* feature) const {
    return m_features.indexOf(feature);
}

Sketch* Document::createSketch(const Plane& plane, const QString& name) {
    Sketch* sketch = new Sketch(this);
    sketch->setPlane(plane);
    
    // 自動命名
    QString sketchName = name;
    if (sketchName.isEmpty()) {
        sketchName = QString("Sketch %1 (%2)")
            .arg(m_nextFeatureNumber++)
            .arg(plane.displayName());
    }
    sketch->setName(sketchName);
    
    addFeature(sketch);
    
    qDebug() << "[Document] Sketch created:" << sketchName;
    return sketch;
}

Extrude* Document::createExtrude(Sketch* sketch, double height, const QString& name) {
    if (!sketch) {
        qWarning() << "[Document] Cannot create extrude: sketch is null";
        return nullptr;
    }
    
    Extrude* extrude = new Extrude(this);
    extrude->setSketch(sketch);
    extrude->setHeight(height);
    
    // 自動命名
    QString extrudeName = name;
    if (extrudeName.isEmpty()) {
        extrudeName = QString("Extrude %1").arg(m_nextFeatureNumber++);
    }
    extrude->setName(extrudeName);
    
    addFeature(extrude);
    
    qDebug() << "[Document] Extrude created:" << extrudeName;
    return extrude;
}

void Document::rebuildAll() {
    qDebug() << "[Document] Rebuilding all features...";
    
    Q_EMIT rebuildStarted();
    
    bool success = true;
    int rebuilt = 0;
    
    for (Feature* feature : m_features) {
        if (feature && !feature->isSuppressed()) {
            if (feature->rebuild()) {
                rebuilt++;
            } else {
                success = false;
                qWarning() << "[Document] Failed to rebuild:" << feature->name();
            }
        }
    }
    
    qDebug() << "[Document] Rebuild complete:" << rebuilt << "features";
    Q_EMIT rebuildFinished(success);
}

void Document::rebuildFeature(Feature* feature) {
    if (!feature) {
        return;
    }
    
    qDebug() << "[Document] Rebuilding feature:" << feature->name();
    
    if (!feature->isSuppressed()) {
        feature->rebuild();
    }
    
    // TODO: 重建依賴此特徵的其他特徵
}

void Document::syncFromOCAF() {
    // TODO: 從 OCAF 文件同步資料到 Feature 物件
    qDebug() << "[Document] Sync from OCAF (not implemented)";
}

void Document::syncToOCAF() {
    // TODO: 將 Feature 物件同步到 OCAF 文件
    qDebug() << "[Document] Sync to OCAF (not implemented)";
}

void Document::onFeatureRebuildRequested() {
    Feature* feature = qobject_cast<Feature*>(sender());
    if (feature) {
        rebuildFeature(feature);
    }
}

void Document::onFeatureChanged() {
    setModified(true);
}

} // namespace cad
} // namespace aicad