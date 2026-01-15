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

#include <AIS_Point.hxx>
#include <AIS_Axis.hxx>
#include <AIS_Shape.hxx>  // ✅ 改用 AIS_Shape
#include <AIS_InteractiveContext.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Geom_Axis1Placement.hxx>
#include <AIS_TextLabel.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Geom_Plane.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>  // ✅ 用來建立平面 Face
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>

#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Face.hxx>

#include <gp_Pnt.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <Aspect_TypeOfMarker.hxx>
#include <Quantity_NameOfColor.hxx>

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
    
    // ✅ 清除舊的參考幾何
    m_referenceGeometries.clear();

    qDebug() << "[Document] New document created (reference geometry will be initialized by UIManager)";
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

// ✅ 初始化參考幾何
void Document::initializeReferenceGeometry(const Handle(AIS_InteractiveContext)& context) {
    if (context.IsNull()) {
        qWarning() << "[Document] Cannot initialize reference geometry: context is null";
        return;
    }

    m_aisContext = context;
    m_referenceGeometries.clear();

    qDebug() << "[Document] Initializing reference geometry...";

    // 建立原點
    createOriginPoint();

    // 建立三個軸
    createAxes();

    // 建立三個平面
    createReferencePlanes();

    qDebug() << "[Document] Reference geometry initialized:"
             << m_referenceGeometries.size() << "objects";

    Q_EMIT referenceGeometryInitialized();
}

// ✅ 建立原點
void Document::createOriginPoint() {
    qDebug() << "[Document] Creating origin point...";

    try {
        // 建立幾何點
        Handle(Geom_CartesianPoint) geomPoint = new Geom_CartesianPoint(0, 0, 0);

        // 建立 AIS 點
        Handle(AIS_Point) aisPoint = new AIS_Point(geomPoint);

        // 設定點的外觀
        Handle(Prs3d_Drawer) drawer = aisPoint->Attributes();
        Handle(Prs3d_PointAspect) pointAspect = new Prs3d_PointAspect(
            Aspect_TOM_BALL,                    // 球形標記
            Quantity_NOC_YELLOW,                 // 黃色
            5.0                                  // 大小
            );
        drawer->SetPointAspect(pointAspect);
        aisPoint->SetAttributes(drawer);

        // 設定選擇模式
        //aisPoint->SetSelectable(Standard_True);

        // 顯示點
        m_aisContext->Display(aisPoint, Standard_False);

        // 儲存到參考幾何列表
        ReferenceGeometry refGeom;
        refGeom.type = ReferenceGeometryType::Origin;
        refGeom.name = "Origin";
        refGeom.aisObject = aisPoint;
        refGeom.visible = true;
        refGeom.selectable = true;

        m_referenceGeometries.append(refGeom);

        qDebug() << "[Document] Origin point created";

    } catch (const Standard_Failure& e) {
        qCritical() << "[Document] Failed to create origin:" << e.GetMessageString();
    }
}

// ✅ 建立三個軸
void Document::createAxes() {
    qDebug() << "[Document] Creating axes...";

    try {
        // X 軸（紅色）
        {
            gp_Pnt origin(0, 0, 0);
            gp_Dir xDir(1, 0, 0);
            gp_Ax1 xAxis(origin, xDir);

            Handle(Geom_Axis1Placement) geomXAxis = new Geom_Axis1Placement(xAxis);
            Handle(AIS_Axis) aisXAxis = new AIS_Axis(geomXAxis);

            // 設定 X 軸外觀（紅色）
            Handle(Prs3d_Drawer) drawer = aisXAxis->Attributes();
            Handle(Prs3d_LineAspect) lineAspect = new Prs3d_LineAspect(
                Quantity_NOC_RED,
                Aspect_TOL_SOLID,
                2.0
                );
            drawer->SetLineAspect(lineAspect);
            aisXAxis->SetAttributes(drawer);

            m_aisContext->Display(aisXAxis, Standard_False);

            ReferenceGeometry refGeom;
            refGeom.type = ReferenceGeometryType::XAxis;
            refGeom.name = "X Axis";
            refGeom.aisObject = aisXAxis;
            refGeom.visible = true;
            refGeom.selectable = false;  // 軸通常不可選擇

            m_referenceGeometries.append(refGeom);
        }

        // Y 軸（綠色）
        {
            gp_Pnt origin(0, 0, 0);
            gp_Dir yDir(0, 1, 0);
            gp_Ax1 yAxis(origin, yDir);

            Handle(Geom_Axis1Placement) geomYAxis = new Geom_Axis1Placement(yAxis);
            Handle(AIS_Axis) aisYAxis = new AIS_Axis(geomYAxis);

            Handle(Prs3d_Drawer) drawer = aisYAxis->Attributes();
            Handle(Prs3d_LineAspect) lineAspect = new Prs3d_LineAspect(
                Quantity_NOC_GREEN,
                Aspect_TOL_SOLID,
                2.0
                );
            drawer->SetLineAspect(lineAspect);
            aisYAxis->SetAttributes(drawer);

            m_aisContext->Display(aisYAxis, Standard_False);

            ReferenceGeometry refGeom;
            refGeom.type = ReferenceGeometryType::YAxis;
            refGeom.name = "Y Axis";
            refGeom.aisObject = aisYAxis;
            refGeom.visible = true;
            refGeom.selectable = false;

            m_referenceGeometries.append(refGeom);
        }

        // Z 軸（藍色）
        {
            gp_Pnt origin(0, 0, 0);
            gp_Dir zDir(0, 0, 1);
            gp_Ax1 zAxis(origin, zDir);

            Handle(Geom_Axis1Placement) geomZAxis = new Geom_Axis1Placement(zAxis);
            Handle(AIS_Axis) aisZAxis = new AIS_Axis(geomZAxis);

            Handle(Prs3d_Drawer) drawer = aisZAxis->Attributes();
            Handle(Prs3d_LineAspect) lineAspect = new Prs3d_LineAspect(
                Quantity_NOC_BLUE,
                Aspect_TOL_SOLID,
                2.0
                );
            drawer->SetLineAspect(lineAspect);
            aisZAxis->SetAttributes(drawer);

            m_aisContext->Display(aisZAxis, Standard_False);

            ReferenceGeometry refGeom;
            refGeom.type = ReferenceGeometryType::ZAxis;
            refGeom.name = "Z Axis";
            refGeom.aisObject = aisZAxis;
            refGeom.visible = true;
            refGeom.selectable = false;

            m_referenceGeometries.append(refGeom);
        }

        qDebug() << "[Document] Axes created";

    } catch (const Standard_Failure& e) {
        qCritical() << "[Document] Failed to create axes:" << e.GetMessageString();
    }
}

// ✅ 建立三個參考平面
void Document::createReferencePlanes() {
    qDebug() << "[Document] Creating reference planes (alternative method)...";

    try {
        const double planeSize = 100.0;

        // XY 平面
        {
            // 建立四個角點
            std::vector<gp_Pnt> corners;
            corners.push_back(gp_Pnt(-planeSize/2, -planeSize/2, 0));
            corners.push_back(gp_Pnt( planeSize/2, -planeSize/2, 0));
            corners.push_back(gp_Pnt( planeSize/2,  planeSize/2, 0));
            corners.push_back(gp_Pnt(-planeSize/2,  planeSize/2, 0));

            // 建立邊
            TopoDS_Edge e1 = BRepBuilderAPI_MakeEdge(corners[0], corners[1]);
            TopoDS_Edge e2 = BRepBuilderAPI_MakeEdge(corners[1], corners[2]);
            TopoDS_Edge e3 = BRepBuilderAPI_MakeEdge(corners[2], corners[3]);
            TopoDS_Edge e4 = BRepBuilderAPI_MakeEdge(corners[3], corners[0]);

            // 建立 Wire
            TopoDS_Wire wire = BRepBuilderAPI_MakeWire(e1, e2, e3, e4);

            // 建立 Face
            TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);

            // 建立 AIS_Shape
            Handle(AIS_Shape) aisPlane = new AIS_Shape(face);
            aisPlane->SetColor(Quantity_NOC_LIGHTBLUE);
            aisPlane->SetTransparency(0.7);
            aisPlane->SetDisplayMode(AIS_Shaded);

            m_aisContext->Display(aisPlane, Standard_False);

            // Add text label for XY plane
            Handle(AIS_TextLabel) textLabel = new AIS_TextLabel();
            textLabel->SetText("XY");

            // Position label at top-right corner of the plane
            gp_Pnt labelPos(planeSize * 0.4, planeSize * 0.4, planeSize * 0.05);
            textLabel->SetPosition(labelPos);

            // Set label properties
            textLabel->SetColor(Quantity_NOC_RED);
            textLabel->SetHeight(planeSize * 0.2);  // 8% of plane size
            textLabel->SetTransparency(0.0);
            textLabel->SetZLayer(Graphic3d_ZLayerId_Top);  // Always on top

            // 設定文字方向，讓它平躺在 XY 平面上
            gp_Ax2 textAxis(
                gp_Pnt(planeSize * 0.35, planeSize * 0.35, 0.0),  // 位置
                gp_Dir(0, 0, -1),   // 法向量 (Y軸)
                gp_Dir(1, 0, 0)    // X軸方向
                );
            textLabel->SetOrientation3D(textAxis);

            ReferenceGeometry refGeom;
            refGeom.type = ReferenceGeometryType::XYPlane;
            refGeom.name = "XY Plane";
            refGeom.aisObject = aisPlane;
            refGeom.visible = true;
            refGeom.selectable = true;

            m_referenceGeometries.append(refGeom);

            // Optionally store label separately if you want to control it independently
            ReferenceGeometry labelGeom;
            labelGeom.type = ReferenceGeometryType::LabelXY;
            labelGeom.name = "XY Label";
            labelGeom.aisObject = textLabel;
            labelGeom.visible = true;
            labelGeom.selectable = false;  // Labels usually not selectable
            m_referenceGeometries.append(labelGeom);
        }

        // XZ 平面
        {
            std::vector<gp_Pnt> corners;
            corners.push_back(gp_Pnt(-planeSize/2, 0, -planeSize/2));
            corners.push_back(gp_Pnt( planeSize/2, 0, -planeSize/2));
            corners.push_back(gp_Pnt( planeSize/2, 0,  planeSize/2));
            corners.push_back(gp_Pnt(-planeSize/2, 0,  planeSize/2));

            TopoDS_Edge e1 = BRepBuilderAPI_MakeEdge(corners[0], corners[1]);
            TopoDS_Edge e2 = BRepBuilderAPI_MakeEdge(corners[1], corners[2]);
            TopoDS_Edge e3 = BRepBuilderAPI_MakeEdge(corners[2], corners[3]);
            TopoDS_Edge e4 = BRepBuilderAPI_MakeEdge(corners[3], corners[0]);

            TopoDS_Wire wire = BRepBuilderAPI_MakeWire(e1, e2, e3, e4);
            TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);

            Handle(AIS_Shape) aisPlane = new AIS_Shape(face);
            aisPlane->SetColor(Quantity_NOC_LIMEGREEN);
            aisPlane->SetTransparency(0.7);
            aisPlane->SetDisplayMode(AIS_Shaded);

            m_aisContext->Display(aisPlane, Standard_False);
            // Add text label for XZ plane
            Handle(AIS_TextLabel) textLabel = new AIS_TextLabel();
            textLabel->SetText("XZ");

            // Position label at top-right corner of the plane
            gp_Pnt labelPos(planeSize * 0.4, planeSize * 0.05, planeSize * 0.4);
            textLabel->SetPosition(labelPos);

            // Set label properties
            textLabel->SetColor(Quantity_NOC_RED);
            textLabel->SetHeight(planeSize * 0.2);  // 8% of plane size
            textLabel->SetTransparency(0.0);
            textLabel->SetZLayer(Graphic3d_ZLayerId_Top);  // Always on top

            // 設定文字方向，讓它平躺在 XZ 平面上
            gp_Ax2 textAxis(
                gp_Pnt(planeSize * 0.35, 0, planeSize * 0.35),  // 位置
                gp_Dir(0, -1, 0),   // 法向量 (Y軸)
                gp_Dir(1, 0, 0)    // X軸方向
                );
            textLabel->SetOrientation3D(textAxis);

            // Display the label
            m_aisContext->Display(textLabel, Standard_False);

            // Store plane
            ReferenceGeometry refGeom;
            refGeom.type = ReferenceGeometryType::XZPlane;
            refGeom.name = "XZ Plane";
            refGeom.aisObject = aisPlane;
            refGeom.visible = true;
            refGeom.selectable = true;

            m_referenceGeometries.append(refGeom);

            // Optionally store label separately if you want to control it independently
            ReferenceGeometry labelGeom;
            labelGeom.type = ReferenceGeometryType::LabelXZ;
            labelGeom.name = "XZ Label";
            labelGeom.aisObject = textLabel;
            labelGeom.visible = true;
            labelGeom.selectable = false;  // Labels usually not selectable
            m_referenceGeometries.append(labelGeom);
        }

        // YZ 平面
        {
            std::vector<gp_Pnt> corners;
            corners.push_back(gp_Pnt(0, -planeSize/2, -planeSize/2));
            corners.push_back(gp_Pnt(0,  planeSize/2, -planeSize/2));
            corners.push_back(gp_Pnt(0,  planeSize/2,  planeSize/2));
            corners.push_back(gp_Pnt(0, -planeSize/2,  planeSize/2));

            TopoDS_Edge e1 = BRepBuilderAPI_MakeEdge(corners[0], corners[1]);
            TopoDS_Edge e2 = BRepBuilderAPI_MakeEdge(corners[1], corners[2]);
            TopoDS_Edge e3 = BRepBuilderAPI_MakeEdge(corners[2], corners[3]);
            TopoDS_Edge e4 = BRepBuilderAPI_MakeEdge(corners[3], corners[0]);

            TopoDS_Wire wire = BRepBuilderAPI_MakeWire(e1, e2, e3, e4);
            TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);

            Handle(AIS_Shape) aisPlane = new AIS_Shape(face);
            aisPlane->SetColor(Quantity_NOC_LIGHTPINK);
            aisPlane->SetTransparency(0.7);
            aisPlane->SetDisplayMode(AIS_Shaded);

            m_aisContext->Display(aisPlane, Standard_False);

            // Add text label for YZ plane
            Handle(AIS_TextLabel) textLabel = new AIS_TextLabel();
            textLabel->SetText("YZ");

            // Position label at top-right corner of the plane
            gp_Pnt labelPos(planeSize * 0.05, planeSize * 0.4, planeSize * 0.4);
            textLabel->SetPosition(labelPos);

            // Set label properties
            textLabel->SetColor(Quantity_NOC_RED);
            textLabel->SetHeight(planeSize * 0.2);  // 8% of plane size
            textLabel->SetTransparency(0.0);
            textLabel->SetZLayer(Graphic3d_ZLayerId_Top);  // Always on top

            // 設定文字方向，讓它平躺在 YZ 平面上
            gp_Ax2 textAxis(
                gp_Pnt(0.0, planeSize * 0.35, planeSize * 0.35),  // 位置
                gp_Dir(1, 0, 0),   // 法向量 (Y軸)
                gp_Dir(0, 1, 0)    // X軸方向
                );
            textLabel->SetOrientation3D(textAxis);

            // Display the label
            m_aisContext->Display(textLabel, Standard_False);

            // Store plane

            ReferenceGeometry refGeom;
            refGeom.type = ReferenceGeometryType::YZPlane;
            refGeom.name = "YZ Plane";
            refGeom.aisObject = aisPlane;
            refGeom.visible = true;
            refGeom.selectable = true;

            m_referenceGeometries.append(refGeom);

            // Optionally store label separately if you want to control it independently
            ReferenceGeometry labelGeom;
            labelGeom.type = ReferenceGeometryType::LabelYZ;
            labelGeom.name = "YZ Label";
            labelGeom.aisObject = textLabel;
            labelGeom.visible = true;
            labelGeom.selectable = false;  // Labels usually not selectable
            m_referenceGeometries.append(labelGeom);
        }

        qDebug() << "[Document] Reference planes created (alternative method)";

    } catch (const Standard_Failure& e) {
        qCritical() << "[Document] Failed to create planes:" << e.GetMessageString();
    } catch (...) {
        qCritical() << "[Document] Failed to create planes (unknown exception)";
    }
}

// ✅ 取得特定類型的參考幾何
ReferenceGeometry* Document::getReferenceGeometry(ReferenceGeometryType type) {
    for (int i = 0; i < m_referenceGeometries.size(); ++i) {
        if (m_referenceGeometries[i].type == type) {
            return &m_referenceGeometries[i];
        }
    }
    return nullptr;
}

// ✅ 設定參考幾何的可見性
void Document::setReferenceGeometryVisible(ReferenceGeometryType type, bool visible) {
    ReferenceGeometry* refGeom = getReferenceGeometry(type);
    if (!refGeom || refGeom->aisObject.IsNull()) {
        return;
    }

    refGeom->visible = visible;

    if (visible) {
        m_aisContext->Display(refGeom->aisObject, Standard_False);
    } else {
        m_aisContext->Erase(refGeom->aisObject, Standard_False);
    }

    m_aisContext->UpdateCurrentViewer();

    qDebug() << "[Document]" << refGeom->name << "visibility:" << visible;

    Q_EMIT referenceGeometryVisibilityChanged(type, visible);
}

// ✅ 設定參考幾何的可選擇性
void Document::setReferenceGeometrySelectable(ReferenceGeometryType type, bool selectable) {
    ReferenceGeometry* refGeom = getReferenceGeometry(type);
    if (!refGeom || refGeom->aisObject.IsNull()) {
        return;
    }

    refGeom->selectable = selectable;

    if (m_aisContext->IsDisplayed(refGeom->aisObject)) {
        if (selectable) {
            m_aisContext->Activate(refGeom->aisObject);
        } else {
            m_aisContext->Deactivate(refGeom->aisObject);
        }
    }

    qDebug() << "[Document]" << refGeom->name << "selectable:" << selectable;
}

// ✅ 顯示所有參考幾何
void Document::showAllReferenceGeometry() {
    for (const ReferenceGeometry& refGeom : m_referenceGeometries) {
        if (!refGeom.aisObject.IsNull()) {
            m_aisContext->Display(refGeom.aisObject, Standard_False);
        }
    }
    m_aisContext->UpdateCurrentViewer();

    qDebug() << "[Document] All reference geometry shown";
}

// ✅ 隱藏所有參考幾何
void Document::hideAllReferenceGeometry() {
    for (const ReferenceGeometry& refGeom : m_referenceGeometries) {
        if (!refGeom.aisObject.IsNull()) {
            m_aisContext->Erase(refGeom.aisObject, Standard_False);
        }
    }
    m_aisContext->UpdateCurrentViewer();

    qDebug() << "[Document] All reference geometry hidden";
}

} // namespace cad
} // namespace aicad
