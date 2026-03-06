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
#include "ui/FeatureTreeItem.h"

#include <AIS_Point.hxx>
#include <AIS_Axis.hxx>
#include <AIS_Shape.hxx>
#include <AIS_InteractiveContext.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Geom_Axis1Placement.hxx>
#include <AIS_TextLabel.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Geom_Plane.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
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
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    BinDrivers::DefineFormat(app);
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
        QJsonObject docJson;
        docJson["version"] = "1.0";
        docJson["fileName"] = QFileInfo(saveFileName).fileName();

        QJsonArray featuresArray;
        for (Feature* feature : m_features) {
            if (feature) {
                featuresArray.append(feature->toJson());
            }
        }
        docJson["features"] = featuresArray;

        QFile file(saveFileName);
        if (!file.open(QIODevice::WriteOnly)) {
            qWarning() << "[Document] Cannot open file for writing:" << saveFileName;
            return false;
        }

        QJsonDocument jsonDoc(docJson);
        file.write(jsonDoc.toJson(QJsonDocument::Indented));
        file.close();

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
        // ✅ 清除現有 Feature（保留 origin tree items）
        clearFeatures();

        // ✅ Re-display reference geometry that was erased by clearFeatures' side-effects.
        if (!m_aisContext.IsNull()) {
            // Wipe and rebuild so planes appear cleanly for the new file.
            m_referenceGeometries.clear();
            initializeOrigin(m_aisContext);   // origin folder + planes always first
        }

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

        // ✅ 載入特徵
        if (docJson.contains("features")) {
            QJsonArray featuresArray = docJson["features"].toArray();

            for (const QJsonValue& val : featuresArray) {
                QJsonObject featureJson = val.toObject();
                QString typeStr = featureJson["type"].toString();

                Feature* feature = nullptr;

                if (typeStr == "Sketch") {
                    feature = new Sketch(this);
                } else if (typeStr == "Extrude") {
                    feature = new Extrude(this);
                }

                if (feature) {
                    if (feature->fromJson(featureJson)) {
                        // ✅ 先加入 feature list（不 emit featureAdded，
                        //    等 rebuild 完再統一更新 tree 和顯示）
                        addFeatureInternal(feature);
                        feature->rebuild();
                    } else {
                        qWarning() << "[Document] Failed to load feature from JSON";
                        delete feature;
                    }
                }
            }
        }

        setFileName(fileName);
        setModified(false);
        m_nextFeatureNumber = m_features.size() + 1;

        // ✅ 載入完成後：重建 tree items 並通知所有監聽者
        rebuildFeatureTreeItems();
        Q_EMIT treeStructureChanged();

        // ✅ 通知 UIManager 重新顯示所有 feature
        Q_EMIT allFeaturesLoaded();

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

// ✅ 內部加入 feature，不更新 tree（供 load 使用）
void Document::addFeatureInternal(Feature* feature) {
    if (!feature || m_features.contains(feature)) {
        return;
    }

    m_features.append(feature);

    connect(feature, &Feature::nameChanged,
            this, &Document::onFeatureChanged);
    connect(feature, &Feature::shapeChanged,
            this, &Document::onFeatureChanged);
    connect(feature, &Feature::rebuildRequested,
            this, &Document::onFeatureRebuildRequested);

    qDebug() << "[Document] Feature added internally:" << feature->name();
}

// ✅ 根據目前 m_features 重建 tree items（保留 origin 資料夾）
void Document::rebuildFeatureTreeItems() {
    // 保留 origin 相關項目（Folder / Plane / Axis / Point），移除舊的 feature 項目
    QVector<ui::FeatureTreeItem> originItems;
    for (const ui::FeatureTreeItem& item : m_treeItems) {
        if (item.type == ui::ItemType::Folder ||
            item.type == ui::ItemType::Plane  ||
            item.type == ui::ItemType::Axis   ||
            item.type == ui::ItemType::Point) {
            originItems.append(item);
        }
    }

    m_treeItems = originItems;

    // 依 m_features 順序重新加入
    for (Feature* feature : m_features) {
        if (!feature) continue;

        ui::FeatureTreeItem treeItem;
        treeItem.id        = feature->id();
        treeItem.name      = feature->name();
        treeItem.parentId  = "";          // 頂層
        treeItem.visible   = feature->isVisible();
        treeItem.selectable = true;
        treeItem.data      = QVariant::fromValue(feature);

        if (qobject_cast<Sketch*>(feature)) {
            treeItem.type = ui::ItemType::Sketch;
        } else if (qobject_cast<Extrude*>(feature)) {
            treeItem.type = ui::ItemType::Extrude;
        } else {
            treeItem.type = ui::ItemType::Base;
        }

        m_treeItems.append(treeItem);
    }

    qDebug() << "[Document] Tree items rebuilt:"
             << m_treeItems.size() << "total items,"
             << m_features.size() << "features";
}

void Document::newDocument() {
    qDebug() << "[Document] Creating new document";

    Q_EMIT aboutToClose();
    clearFeatures();

    if (!m_ocafDoc.IsNull()) {
        Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        app->Close(m_ocafDoc);
    }
    initOCAF();

    setFileName(QString());
    setModified(false);
    m_nextFeatureNumber = 1;

    m_referenceGeometries.clear();
    if (!m_aisContext.IsNull()) {
        initializeOrigin(m_aisContext);   // rebuilds planes + createOriginFolderItems()
    } else {
        // Context not ready yet (first launch); UIManager will call initializeOrigin().
        qDebug() << "[Document] newDocument: context not yet available, "
                    "UIManager must call initializeOrigin()";
    }
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

    // ✅ 同步移除 tree item
    m_treeItems.erase(
        std::remove_if(m_treeItems.begin(), m_treeItems.end(),
                       [&](const ui::FeatureTreeItem& item) {
                           return item.id == feature->id();
                       }),
        m_treeItems.end()
        );

    disconnect(feature, nullptr, this, nullptr);
    feature->deleteLater();

    setModified(true);

    Q_EMIT featureRemoved();
    Q_EMIT featureCountChanged(m_features.size());
    Q_EMIT treeStructureChanged();
}

void Document::clearFeatures() {
    qDebug() << "[Document] Clearing all features";

    while (!m_features.isEmpty()) {
        Feature* feature = m_features.takeLast();
        Q_EMIT featureAboutToBeRemoved(feature);
        disconnect(feature, nullptr, this, nullptr);
        delete feature;
        Q_EMIT featureRemoved();
    }

    // ✅ 只移除 feature 類型的 tree items，保留 origin 資料夾
    m_treeItems.erase(
        std::remove_if(m_treeItems.begin(), m_treeItems.end(),
                       [](const ui::FeatureTreeItem& item) {
                           return item.type == ui::ItemType::Sketch  ||
                                  item.type == ui::ItemType::Extrude ||
                                  item.type == ui::ItemType::Base;
                       }),
        m_treeItems.end()
        );

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

Sketch* Document::createSketch(Plane* plane, const QString& name) {
    Sketch* sketch = new Sketch(this);
    sketch->setPlane(plane);

    QString sketchName = name;
    if (sketchName.isEmpty()) {
        sketchName = QString("Sketch %1 (%2)")
                         .arg(m_nextFeatureNumber++)
                         .arg(plane->displayName());
    }
    sketch->setName(sketchName);

    addFeature(sketch);

    // ✅ 加入 tree item
    ui::FeatureTreeItem sketchItem;
    sketchItem.type      = ui::ItemType::Sketch;
    sketchItem.id        = sketch->id();
    sketchItem.name      = sketch->name();
    sketchItem.parentId  = "";
    sketchItem.visible   = sketch->isVisible();
    sketchItem.selectable = true;
    sketchItem.data      = QVariant::fromValue(sketch);

    m_treeItems.append(sketchItem);

    Q_EMIT treeStructureChanged();

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

    QString extrudeName = name;
    if (extrudeName.isEmpty()) {
        extrudeName = QString("Extrude %1").arg(m_nextFeatureNumber++);
    }
    extrude->setName(extrudeName);

    addFeature(extrude);

    // ✅ 加入 tree item
    ui::FeatureTreeItem extrudeItem;
    extrudeItem.type      = ui::ItemType::Extrude;
    extrudeItem.id        = extrude->id();
    extrudeItem.name      = extrude->name();
    extrudeItem.parentId  = "";
    extrudeItem.visible   = extrude->isVisible();
    extrudeItem.selectable = true;
    extrudeItem.data      = QVariant::fromValue(extrude);

    m_treeItems.append(extrudeItem);

    Q_EMIT treeStructureChanged();

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
}

void Document::syncFromOCAF() {
    qDebug() << "[Document] Sync from OCAF (not implemented)";
}

void Document::syncToOCAF() {
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

void Document::initializeReferenceGeometry(const Handle(AIS_InteractiveContext)& context) {
    if (context.IsNull()) {
        qWarning() << "[Document] Cannot initialize reference geometry: context is null";
        return;
    }

    m_aisContext = context;
    m_referenceGeometries.clear();

    qDebug() << "[Document] Initializing reference geometry...";

    createOriginPoint();
    createAxes();
    createReferencePlanes();

    qDebug() << "[Document] Reference geometry initialized:"
             << m_referenceGeometries.size() << "objects";

    hideAllReferenceGeometry();

    Q_EMIT referenceGeometryInitialized();
}

void Document::createOriginPoint() {
    qDebug() << "[Document] Creating origin point...";

    try {
        Handle(Geom_CartesianPoint) geomPoint = new Geom_CartesianPoint(0, 0, 0);
        Handle(AIS_Point) aisPoint = new AIS_Point(geomPoint);

        Handle(Prs3d_Drawer) drawer = aisPoint->Attributes();
        Handle(Prs3d_PointAspect) pointAspect = new Prs3d_PointAspect(
            Aspect_TOM_BALL, Quantity_NOC_YELLOW, 5.0);
        drawer->SetPointAspect(pointAspect);
        aisPoint->SetAttributes(drawer);

        m_aisContext->Display(aisPoint, Standard_False);

        ReferenceGeometry refGeom;
        refGeom.type       = ReferenceGeometryType::Origin;
        refGeom.name       = "Origin";
        refGeom.aisObject  = aisPoint;
        refGeom.visible    = true;
        refGeom.selectable = true;

        m_referenceGeometries.append(refGeom);

        qDebug() << "[Document] Origin point created";

    } catch (const Standard_Failure& e) {
        qCritical() << "[Document] Failed to create origin:" << e.GetMessageString();
    }
}

void Document::createAxes() {
    qDebug() << "[Document] Creating axes...";

    try {
        auto makeAxis = [&](const gp_Dir& dir,
                            ReferenceGeometryType type,
                            const QString& name,
                            Quantity_NameOfColor color) {
            gp_Ax1 ax1(gp_Pnt(0,0,0), dir);
            Handle(Geom_Axis1Placement) geomAxis = new Geom_Axis1Placement(ax1);
            Handle(AIS_Axis) aisAxis = new AIS_Axis(geomAxis);

            Handle(Prs3d_Drawer) drawer = aisAxis->Attributes();
            Handle(Prs3d_LineAspect) lineAspect =
                new Prs3d_LineAspect(color, Aspect_TOL_SOLID, 2.0);
            drawer->SetLineAspect(lineAspect);
            aisAxis->SetAttributes(drawer);

            m_aisContext->Display(aisAxis, Standard_False);

            ReferenceGeometry refGeom;
            refGeom.type       = type;
            refGeom.name       = name;
            refGeom.aisObject  = aisAxis;
            refGeom.visible    = true;
            refGeom.selectable = false;

            m_referenceGeometries.append(refGeom);
        };

        makeAxis(gp_Dir(1,0,0), ReferenceGeometryType::XAxis, "X Axis", Quantity_NOC_RED);
        makeAxis(gp_Dir(0,1,0), ReferenceGeometryType::YAxis, "Y Axis", Quantity_NOC_GREEN);
        makeAxis(gp_Dir(0,0,1), ReferenceGeometryType::ZAxis, "Z Axis", Quantity_NOC_BLUE);

        qDebug() << "[Document] Axes created";

    } catch (const Standard_Failure& e) {
        qCritical() << "[Document] Failed to create axes:" << e.GetMessageString();
    }
}

void Document::createReferencePlanes() {
    qDebug() << "[Document] Creating reference planes...";

    try {
        const double S = 100.0;  // half-size

        struct PlaneConfig {
            std::vector<gp_Pnt> corners;
            Quantity_NameOfColor color;
            ReferenceGeometryType type;
            QString name;
            ReferenceGeometryType labelType;
            QString labelText;
            gp_Pnt  labelPos;
            gp_Ax2  textAxis;
        };

        auto makeQuad = [&](double ax, double ay, double az,
                            double bx, double by, double bz,
                            double cx, double cy, double cz,
                            double dx, double dy, double dz) {
            return std::vector<gp_Pnt>{
                                       gp_Pnt(ax,ay,az), gp_Pnt(bx,by,bz),
                                       gp_Pnt(cx,cy,cz), gp_Pnt(dx,dy,dz)};
        };

        std::vector<PlaneConfig> configs = {
                                            // XY
                                            { makeQuad(-S/2,-S/2,0,  S/2,-S/2,0,  S/2,S/2,0,  -S/2,S/2,0),
                                             Quantity_NOC_LIGHTBLUE,
                                             ReferenceGeometryType::XYPlane, "XY Plane",
                                             ReferenceGeometryType::LabelXY, "XY",
                                             gp_Pnt(S*0.4, S*0.4, S*0.05),
                                             gp_Ax2(gp_Pnt(S*0.35,S*0.35,0), gp_Dir(0,0,1), gp_Dir(1,0,0)) },
                                            // XZ
                                            { makeQuad(-S/2,0,-S/2,  S/2,0,-S/2,  S/2,0,S/2,  -S/2,0,S/2),
                                             Quantity_NOC_LIMEGREEN,
                                             ReferenceGeometryType::XZPlane, "XZ Plane",
                                             ReferenceGeometryType::LabelXZ, "XZ",
                                             gp_Pnt(S*0.4, S*0.05, S*0.4),
                                             gp_Ax2(gp_Pnt(S*0.35,0,S*0.35), gp_Dir(0,-1,0), gp_Dir(1,0,0)) },
                                            // YZ
                                            { makeQuad(0,-S/2,-S/2,  0,S/2,-S/2,  0,S/2,S/2,  0,-S/2,S/2),
                                             Quantity_NOC_LIGHTPINK,
                                             ReferenceGeometryType::YZPlane, "YZ Plane",
                                             ReferenceGeometryType::LabelYZ, "YZ",
                                             gp_Pnt(S*0.05, S*0.4, S*0.4),
                                             gp_Ax2(gp_Pnt(0,S*0.35,S*0.35), gp_Dir(1,0,0), gp_Dir(0,1,0)) },
                                            };

        for (const PlaneConfig& cfg : configs) {
            // 建立 Face
            TopoDS_Edge e1 = BRepBuilderAPI_MakeEdge(cfg.corners[0], cfg.corners[1]);
            TopoDS_Edge e2 = BRepBuilderAPI_MakeEdge(cfg.corners[1], cfg.corners[2]);
            TopoDS_Edge e3 = BRepBuilderAPI_MakeEdge(cfg.corners[2], cfg.corners[3]);
            TopoDS_Edge e4 = BRepBuilderAPI_MakeEdge(cfg.corners[3], cfg.corners[0]);

            TopoDS_Wire wire = BRepBuilderAPI_MakeWire(e1, e2, e3, e4);
            TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);

            Handle(AIS_Shape) aisPlane = new AIS_Shape(face);
            aisPlane->SetColor(cfg.color);
            aisPlane->SetTransparency(0.7);
            aisPlane->SetDisplayMode(AIS_Shaded);
            m_aisContext->Display(aisPlane, Standard_False);

            ReferenceGeometry planeGeom;
            planeGeom.type       = cfg.type;
            planeGeom.name       = cfg.name;
            planeGeom.aisObject  = aisPlane;
            planeGeom.visible    = true;
            planeGeom.selectable = true;
            m_referenceGeometries.append(planeGeom);

            // 建立文字標籤
            Handle(AIS_TextLabel) textLabel = new AIS_TextLabel();
            textLabel->SetText(cfg.labelText.toStdString().c_str());
            textLabel->SetPosition(cfg.labelPos);
            textLabel->SetColor(Quantity_NOC_RED);
            textLabel->SetHeight(S * 0.2);
            textLabel->SetTransparency(0.0);
            textLabel->SetZLayer(Graphic3d_ZLayerId_Top);
            textLabel->SetOrientation3D(cfg.textAxis);
            m_aisContext->Display(textLabel, Standard_False);

            ReferenceGeometry labelGeom;
            labelGeom.type       = cfg.labelType;
            labelGeom.name       = cfg.name + " Label";
            labelGeom.aisObject  = textLabel;
            labelGeom.visible    = true;
            labelGeom.selectable = false;
            m_referenceGeometries.append(labelGeom);
        }

        qDebug() << "[Document] Reference planes created";

    } catch (const Standard_Failure& e) {
        qCritical() << "[Document] Failed to create planes:" << e.GetMessageString();
    } catch (...) {
        qCritical() << "[Document] Failed to create planes (unknown exception)";
    }
}

ReferenceGeometry* Document::getReferenceGeometry(ReferenceGeometryType type) {
    for (int i = 0; i < m_referenceGeometries.size(); ++i) {
        if (m_referenceGeometries[i].type == type) {
            return &m_referenceGeometries[i];
        }
    }
    return nullptr;
}

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

void Document::displayFeature(Feature* feature,
                              const Handle(AIS_InteractiveContext)& context) {
    if (!feature || context.IsNull()) {
        return;
    }

    if (Sketch* sketch = qobject_cast<Sketch*>(feature)) {
        sketch->displayInContext(context);
        return;
    }

    if (!feature->shape().IsNull()) {
        Handle(AIS_Shape) aisShape = new AIS_Shape(feature->shape());
        context->Display(aisShape, Standard_False);
    }
}

// ✅ 載入完成後，重新在 viewport 中顯示所有 feature
void Document::displayAllFeatures(const Handle(AIS_InteractiveContext)& context) {
    if (context.IsNull()) {
        qWarning() << "[Document] displayAllFeatures: context is null";
        return;
    }

    for (Feature* feature : m_features) {
        if (feature && feature->isVisible() && !feature->isSuppressed()) {
            displayFeature(feature, context);
        }
    }

    context->UpdateCurrentViewer();
    qDebug() << "[Document] All features displayed:" << m_features.size();
}

void Document::showAllReferenceGeometry() {
    for (const ReferenceGeometry& refGeom : m_referenceGeometries) {
        if (!refGeom.aisObject.IsNull()) {
            m_aisContext->Display(refGeom.aisObject, Standard_False);
        }
    }
    m_aisContext->UpdateCurrentViewer();
    qDebug() << "[Document] All reference geometry shown";
}

void Document::hideAllReferenceGeometry() {
    for (const ReferenceGeometry& refGeom : m_referenceGeometries) {
        if (!refGeom.aisObject.IsNull()) {
            m_aisContext->Erase(refGeom.aisObject, Standard_False);
        }
    }
    m_aisContext->UpdateCurrentViewer();
    qDebug() << "[Document] All reference geometry hidden";
}

void Document::initializeOrigin(const Handle(AIS_InteractiveContext)& context) {
    qDebug() << "[Document] Initializing origin";
    initializeReferenceGeometry(context);
    createOriginFolderItems();
    Q_EMIT treeStructureChanged();
}

void Document::createOriginFolderItems() {
    // ✅ 只移除 origin 相關的舊項目，保留 feature 項目
    m_treeItems.erase(
        std::remove_if(m_treeItems.begin(), m_treeItems.end(),
                       [](const ui::FeatureTreeItem& item) {
                           return item.type == ui::ItemType::Folder ||
                                  item.type == ui::ItemType::Plane  ||
                                  item.type == ui::ItemType::Axis   ||
                                  item.type == ui::ItemType::Point;
                       }),
        m_treeItems.end()
        );

    // ✅ Origin 資料夾
    ui::FeatureTreeItem originFolder;
    originFolder.type       = ui::ItemType::Folder;
    originFolder.id         = "origin_folder";
    originFolder.name       = "Origin";
    originFolder.parentId   = "";
    originFolder.visible    = true;
    originFolder.selectable = false;
    m_treeItems.prepend(originFolder);  // ✅ 置頂

    // ✅ 三個平面（插入在 origin_folder 後面，index 1~3）
    QStringList planeNames = {"XY Plane", "XZ Plane", "YZ Plane"};
    QStringList planeIds   = {"plane_xy", "plane_xz", "plane_yz"};

    for (int i = 0; i < 3; ++i) {
        ui::FeatureTreeItem item;
        item.type       = ui::ItemType::Plane;
        item.id         = planeIds[i];
        item.name       = planeNames[i];
        item.parentId   = "origin_folder";
        item.visible    = true;
        item.selectable = true;
        m_treeItems.insert(i + 1, item);
    }

    // ✅ 三個軸
    QStringList axisNames = {"X Axis", "Y Axis", "Z Axis"};
    QStringList axisIds   = {"axis_x", "axis_y", "axis_z"};

    for (int i = 0; i < 3; ++i) {
        ui::FeatureTreeItem item;
        item.type       = ui::ItemType::Axis;
        item.id         = axisIds[i];
        item.name       = axisNames[i];
        item.parentId   = "origin_folder";
        item.visible    = true;
        item.selectable = true;
        m_treeItems.insert(4 + i, item);
    }

    // ✅ 原點
    ui::FeatureTreeItem originPoint;
    originPoint.type       = ui::ItemType::Point;
    originPoint.id         = "origin_point";
    originPoint.name       = "Origin Point";
    originPoint.parentId   = "origin_folder";
    originPoint.visible    = true;
    originPoint.selectable = true;
    m_treeItems.insert(7, originPoint);

    qDebug() << "[Document] Origin folder items created:"
             << m_treeItems.size() << "total tree items";
}

QVector<ui::FeatureTreeItem> Document::getFeatureTreeItems() const {
    return m_treeItems;
}

} // namespace cad
} // namespace aicad
