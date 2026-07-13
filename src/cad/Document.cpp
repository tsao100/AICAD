/**
 * @file Document.cpp
 * @brief CAD 文件實作
 * @author AICAD Team
 * @date 2025-01-08
 */

#include "core/Application.h"
#include "core/EventBus.h"
#include "Document.h"
#include "Feature.h"
#include "Sketch.h"
#include "Extrude.h"
#include "Plane.h"
#include "SketchInstance.h"
#include "AlignedProfileArray.h"
#include "ProfileLoftSolid.h"
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
    , m_parameterStore(new aicad::core::ParameterStore(this))
{
    initOCAF();

    // ✅ Subscribe visibility-changed here so Railway-only documents
    //    (which never call initializeOrigin) also handle eye icon toggles.
    //    Use a static flag per-instance to avoid duplicate subscriptions
    //    (e.g. if constructor is somehow called again for the same object).
    core::EventBus* bus = core::Application::instance()->eventBus();
    bus->unsubscribe("feature.visibility-changed", this);  // remove any existing
    bus->subscribe("feature.visibility-changed", this,
                   [this](const QVariant& data) {
                       onVisibilityChanged(data.toMap());
                   });

    // 當任一參數改變 → 標記所有參數化特徵 dirty 並重建
    connect(m_parameterStore, &aicad::core::ParameterStore::parameterChanged,
            this, [this](const QString&) { rebuildAll(); });

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

// ── Per-TCL alignment data ─────────────────────────────────────────────────

void Document::setTclAlignmentData(const QString& tclId, const QJsonObject& data)
{
    if (data.isEmpty())
        m_tclAlignmentData.remove(tclId);
    else
        m_tclAlignmentData[tclId] = data;
}

QJsonObject Document::tclAlignmentData(const QString& tclId) const
{
    return m_tclAlignmentData.value(tclId, QJsonObject{});
}

QStringList Document::tclAlignmentDataIds() const
{
    return QStringList(m_tclAlignmentData.keys());
}

// ─────────────────────────────────────────────────────────────────────────────

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

        // ✅ 修正：依拓撲順序（被依賴者先）序列化，確保 load 時順序正確
        QJsonArray featuresArray;
        bool cycle;
        QList<QString> order = m_depGraph.topologicalOrder(&cycle);

        // 先輸出有拓撲順序的
        QSet<QString> written;
        for (const QString& id : order) {
            Feature* f = findFeature(id);
            if (f) { featuresArray.append(f->toJson()); written.insert(id); }
        }
        // 再輸出不在依賴圖中的孤立 feature（如純 Sketch）
        for (Feature* feature : m_features) {
            if (feature && !written.contains(feature->id()))
                featuresArray.append(feature->toJson());
        }

        docJson["parameters"] = m_parameterStore->toJson();

        docJson["features"] = featuresArray;

        // ✅ 儲存視圖狀態
        if (!m_viewState.isEmpty())
            docJson["viewState"] = m_viewState;

        // ✅ 儲存 TrackCenterLine 資料（含 per-TCL alignmentEdit）
        if (!m_trackCenterLines.isEmpty()) {
            QJsonArray tclArray;
            for (railway::TrackCenterLine* tcl : m_trackCenterLines) {
                QJsonObject tclJson = tcl->toJson();
                // Embed the AlignmentDocument edit session for this TCL
                const QJsonObject editData = m_tclAlignmentData.value(tcl->id());
                if (!editData.isEmpty())
                    tclJson["alignmentEdit"] = editData;
                tclArray.append(tclJson);
            }
            docJson["trackCenterLines"] = tclArray;
        }

        // ✅ Legacy: also save global alignmentData for backward compat
        if (!m_alignmentData.isEmpty())
            docJson["alignment"] = m_alignmentData;

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

        if (docJson.contains("parameters")){
            // ✅ 阻斷 parameterChanged 在載入期間觸發 rebuildAll
            m_parameterStore->blockSignals(true);
            m_parameterStore->fromJson(docJson["parameters"].toObject());
            m_parameterStore->blockSignals(false);
        }

        // ✅ 載入特徵
        if (docJson.contains("features")) {
            QJsonArray featuresArray = docJson["features"].toArray();

            // ── 第一階段：建立所有 feature 物件並解析 JSON（不 rebuild）
            for (const QJsonValue& val : featuresArray) {
                QJsonObject featureJson = val.toObject();
                QString typeStr = featureJson["type"].toString();

                Feature* feature = nullptr;
                if (typeStr == "Sketch")  feature = new Sketch(this);
                else if (typeStr == "Extrude") feature = new Extrude(this);
                else if (typeStr == "Pattern") feature = new AlignedProfileArray(this);
                else if (typeStr == "Loft") feature = new ProfileLoftSolid(this);

                if (feature) {
                    if (feature->fromJson(featureJson)) {
                        addFeatureInternal(feature);
                    } else {
                        qWarning() << "[Document] Failed to load feature from JSON";
                        delete feature;
                    }
                }
            }

            // ── 第二階段：解析跨 feature 的參照（Extrude → Sketch）
            for (Feature* feature : m_features) {
                if (auto* ext = qobject_cast<Extrude*>(feature))
                    ext->resolveReferences(this);
            }

            // ── 第三階段：依拓撲順序 rebuild（Sketch 必須先於 Extrude）
            bool cycle;
            QList<QString> topoOrder = m_depGraph.topologicalOrder(&cycle);
            QSet<QString> rebuilt;

            // 先 rebuild 有拓撲順序的（被依賴者先）
            for (const QString& id : topoOrder) {
                Feature* f = findFeature(id);
                if (f && !f->isSuppressed()) {
                    f->rebuild();
                    f->clearDirty();
                    rebuilt.insert(id);
                }
            }
            // 再 rebuild 不在依賴圖中的（孤立 Sketch 等）
            // ⚠️ 對快照（副本）迭代，不可直接對 m_features 迭代：某些 feature
            //    的 rebuild()（例如 AlignedProfileArray）內部會呼叫
            //    Document::addFeature() 動態新增子 feature（SketchInstance），
            //    這會使 m_features 這個 QList 重新配置記憶體，讓 range-based
            //    for 迴圈中殘留的內部迭代器/指標失效 —— 下一輪迭代解參考到
            //    已釋放或未初始化的記憶體，導致 qobject_cast 內部讀到壞掉的
            //    vtable 指標而 SIGSEGV（曾在載入含 Pattern feature 的檔案時
            //    重現）。此處固定用 const 副本迭代，新增的 feature 不會被
            //    這一輪處理到也沒關係，因為它們稍後會在各自建立流程中被
            //    正確 rebuild 一次。
            const QList<Feature*> featuresSnapshot = m_features;
            for (Feature* f : featuresSnapshot) {
                if (f && !f->isSuppressed() && !rebuilt.contains(f->id())) {
                    f->rebuild();
                    f->clearDirty();
                }
            }
        }

        // ✅ 讀取視圖狀態
        if (docJson.contains("viewState"))
            m_viewState = docJson["viewState"].toObject();

        // ✅ 讀取 TrackCenterLine 資料（含 per-TCL alignmentEdit）
        if (docJson.contains("trackCenterLines")) {
            qDeleteAll(m_trackCenterLines);
            m_trackCenterLines.clear();
            m_tclAlignmentData.clear();
            for (const QJsonValue& val : docJson["trackCenterLines"].toArray()) {
                QJsonObject tclJson = val.toObject();
                // Extract and remove embedded alignmentEdit before TCL fromJson
                const QJsonObject editData = tclJson.take("alignmentEdit").toObject();
                auto* tcl = new railway::TrackCenterLine(this);
                if (tcl->fromJson(tclJson)) {
                    m_trackCenterLines.append(tcl);
                    if (!editData.isEmpty())
                        m_tclAlignmentData[tcl->id()] = editData;
                } else {
                    delete tcl;
                }
            }
            if (!m_trackCenterLines.isEmpty()) {
                Q_EMIT trackCenterLinesChanged();
                Q_EMIT treeStructureChanged();
            }
        }

        // ✅ AlignedProfileArray 同時依賴 Sketch 與 TrackCenterLine，後者要等上面
        //    的 trackCenterLines 區塊載入完才存在，所以在此單獨補一輪
        //    resolveReferences + rebuild（前面第三階段對它的 rebuild() 嘗試
        //    會因 TCL 尚未解析而失敗，屬預期行為，不影響這裡的正確重建）。
        // ⚠️ 先取快照再迭代：AlignedProfileArray::rebuild() 會呼叫
        //    Document::createSketchInstance() → addFeature()，對 m_features
        //    做 append()，若直接對 m_features 本身做 range-based for，
        //    append 觸發的重新配置會讓迴圈迭代到一半就失效，導致下一輪
        //    對垃圾記憶體呼叫 qobject_cast、讀到壞掉的 vtable 指標而
        //    SIGSEGV（即本次回報的 crash：Document::load 在
        //    qobject_cast<AlignedProfileArray*> 內部墜毀）。
        //    新增出來的 SketchInstance 不需要被這裡的迴圈再處理一次，
        //    它們已在 arr->rebuild() 內部自行建立並 rebuild 完成。
        const QList<Feature*> apaSnapshot = m_features;
        for (Feature* feature : apaSnapshot) {
            if (auto* arr = qobject_cast<AlignedProfileArray*>(feature))
                arr->resolveReferences(this);
        }
        for (Feature* feature : apaSnapshot) {
            if (auto* arr = qobject_cast<AlignedProfileArray*>(feature)) {
                // ⚠️ 用 rebuildFeature() 而非直接呼叫 rebuild()：後者只更新資料
                //    （setShape()/shapeChanged()），不會 Q_EMIT featureShapeUpdated，
                //    CadView 是靠這個訊號（Qt::QueuedConnection）才知道要
                //    displayAllFeatures() 重繪。這正是 setMasterSketch()／
                //    setTrackCenterLine() 註解裡描述的「Loft 有時看得到、有時
                //    看不到」根因——load() 這裡先前正是唯一還在直接呼叫
                //    rebuild() 而繞過這個訊號的地方，導致重新載入檔案後只能
                //    依賴 CadView::setDocument() 裡那唯一一次 QTimer::singleShot
                //    來補顯示，一旦有任何時序落差，多份 Pattern/Loft 中除了
                //    最後被處理到的之外都可能顯示不出來。
                rebuildFeature(arr);
            }
        }

        // ✅ ProfileLoftSolid 依賴 AlignedProfileArray，必須在後者 rebuild 完成後
        //    才能取得 stationWireLoops()，故再補一輪。
        // ⚠️ 同上，改用快照迭代以策安全，避免未來若 ProfileLoftSolid::rebuild()
        //    也開始動態新增 feature 時重蹈覆轍。
        const QList<Feature*> loftSnapshot = m_features;
        for (Feature* feature : loftSnapshot) {
            if (auto* loft = qobject_cast<ProfileLoftSolid*>(feature))
                loft->resolveReferences(this);
        }
        for (Feature* feature : loftSnapshot) {
            if (auto* loft = qobject_cast<ProfileLoftSolid*>(feature)) {
                // ⚠️ 同上：改用 rebuildFeature() 確保 Q_EMIT featureShapeUpdated，
                //    讓 CadView 的 Qt::QueuedConnection 監聽者能在 load() 結束、
                //    事件迴圈恢復後確實收到通知並重繪，而不是只能賭
                //    CadView::setDocument() 裡那唯一一次 QTimer::singleShot
                //    是否還沒被其他時序蓋掉。
                rebuildFeature(loft);
            }
        }

        // ✅ Legacy: read global alignmentData for old files
        if (docJson.contains("alignment"))
            m_alignmentData = docJson["alignment"].toObject();

        setFileName(fileName);
        setModified(false);

        // ✅ 修正：掃描所有 feature name 中的最大數字，避免命名衝突
        int maxNum = 0;
        QRegularExpression re(R"(\d+)");
        for (Feature* f : m_features) {
            auto it = re.globalMatch(f->name());
            while (it.hasNext())
                maxNum = qMax(maxNum, it.next().captured().toInt());
        }
        m_nextFeatureNumber = maxNum + 1;

        // ✅ 載入完成後：重建 tree items 並通知所有監聽者
        rebuildFeatureTreeItems();
        Q_EMIT treeStructureChanged();

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
// addFeature：同時更新依賴圖
void Document::addFeatureInternal(Feature* feature) {
    m_features.append(feature);

    // 向依賴圖登記
    m_depGraph.ensureNode(feature->id());   // ← 需在 DependencyGraph 加 public ensureNode
    for (const QString& depId : feature->featureDependencies())
        m_depGraph.addDependency(feature->id(), depId);

    // 當特徵 dirty → 通知依賴者
    connect(feature, &Feature::dirtyStateChanged, this, [this, feature]() {
        // 連鎖標記受影響特徵
        for (const QString& affectedId : m_depGraph.affectedBy(feature->id())) {
            if (Feature* f = findFeature(affectedId))
                f->markDirty();
        }
    });

    connect(feature, &Feature::nameChanged,
            this, &Document::onFeatureChanged);
    connect(feature, &Feature::shapeChanged,
            this, &Document::onFeatureChanged);
    connect(feature, &Feature::rebuildRequested,
            this, &Document::onFeatureRebuildRequested);
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
        // ✅ 若 Feature 層有父特徵，將 parentId 設為父特徵的 ID，讓 FeatureBrowser
        //    能正確地把此 tree item 掛在父節點下（例如草圖掛在擠出下）。
        treeItem.parentId  = feature->parent() ? feature->parent()->id() : QString();
        treeItem.visible   = feature->isVisible();
        treeItem.selectable = true;
        treeItem.data      = QVariant::fromValue(feature);

        if (qobject_cast<Sketch*>(feature)) {
            treeItem.type = ui::ItemType::Sketch;
        } else if (qobject_cast<Extrude*>(feature)) {
            treeItem.type = ui::ItemType::Extrude;
        } else if (auto* arr = qobject_cast<AlignedProfileArray*>(feature)) {
            treeItem.type = ui::ItemType::Pattern;
            treeItem.name = QString("%1 (%2 stations)").arg(arr->name()).arg(arr->stationCount());
        } else if (qobject_cast<ProfileLoftSolid*>(feature)) {
            treeItem.type = ui::ItemType::Loft;
        } else if (qobject_cast<SketchInstance*>(feature)) {
            treeItem.type = ui::ItemType::Sketch;  // 副本沿用草圖圖示
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

    // ✅ 新增：與 addFeatureInternal 一致，向依賴圖登記
    m_depGraph.ensureNode(feature->id());
    for (const QString& depId : feature->featureDependencies())
        m_depGraph.addDependency(feature->id(), depId);

    connect(feature, &Feature::dirtyStateChanged, this, [this, feature]() {
        for (const QString& affectedId : m_depGraph.affectedBy(feature->id()))
            if (Feature* f = findFeature(affectedId))
                f->markDirty();
    });

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
    if (!feature) return;

    // ✅ 修正：先檢查存在，再移除；把多餘的第一個 removeOne 刪掉
    if (!m_features.contains(feature)) {
        qWarning() << "[Document] Feature not in document:" << feature->name();
        return;
    }

    qDebug() << "[Document] Removing feature:" << feature->name();

    m_depGraph.removeNode(feature->id());

    Q_EMIT featureAboutToBeRemoved(feature);

    m_features.removeOne(feature);

    m_treeItems.erase(
        std::remove_if(m_treeItems.begin(), m_treeItems.end(),
                       [&](const ui::FeatureTreeItem& item) {
                           return item.id == feature->id();
                       }),
        m_treeItems.end());

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

    // ✅ 清除 TrackCenterLine
    qDeleteAll(m_trackCenterLines);
    m_trackCenterLines.clear();

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

    // Phase 2: sketch store → global store（scope chain）
    sketch->parameterStore()->setParentStore(m_parameterStore);

    // 全域參數重算 → 草圖重建
    connect(sketch->parameterStore(), &aicad::core::ParameterStore::parametersRecomputed,
            sketch, &Sketch::scheduleRebuild, Qt::UniqueConnection);

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

SketchInstance* Document::createSketchInstance(
        Sketch* master,
        Plane* plane,
        const QHash<QString, double>& overrides,
        const QString& name)
{
    if (!master) {
        qWarning() << "[Document] createSketchInstance: master is null";
        return nullptr;
    }
    if (!plane) {
        qWarning() << "[Document] createSketchInstance: plane is null";
        return nullptr;
    }

    auto* inst = new SketchInstance(this);
    inst->setName(name.isEmpty()
        ? QString("%1_copy_%2").arg(master->name()).arg(m_nextFeatureNumber++)
        : name);

    inst->setMasterSketch(master);
    inst->setPlane(plane);

    // instance store parent 由 setMasterSketch() 已設定

    // 套用初始覆寫
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
        inst->parameterStore()->setLocal(it.key(), it.value());

    addFeature(inst);

    // 加入 FeatureTree
    ui::FeatureTreeItem item;
    item.type       = ui::ItemType::Sketch;   // 複用 Sketch icon
    item.id         = inst->id();
    item.name       = inst->name();
    item.parentId   = master->id();
    item.visible    = inst->isVisible();
    item.selectable = true;
    item.data       = QVariant::fromValue(static_cast<QObject*>(inst));
    m_treeItems.append(item);
    Q_EMIT treeStructureChanged();

    inst->rebuild();

    qDebug() << "[Document] SketchInstance created:" << inst->name()
             << "from master:" << master->name();
    return inst;
}

AlignedProfileArray* Document::createAlignedProfileArray(
    Sketch* master,
    railway::TrackCenterLine* tcl,
    double startChainage,
    double endChainage,
    double interval,
    const QString& name)
{
    if (!master) {
        qWarning() << "[Document] createAlignedProfileArray: master is null";
        return nullptr;
    }
    if (!tcl) {
        qWarning() << "[Document] createAlignedProfileArray: tcl is null";
        return nullptr;
    }

    auto* arr = new AlignedProfileArray(this);
    arr->setName(name.isEmpty()
        ? QString("ProfileArray %1").arg(m_nextFeatureNumber++)
        : name);

    arr->setMasterSketch(master);
    arr->setTrackCenterLine(tcl);
    arr->setRange(startChainage, endChainage, interval);

    addFeature(arr);

    // ✅ 陣列自己的 tree item 要在呼叫 rebuildFeature() 之前先加進去：rebuild()
    // 內部會逐一建立測站 SketchInstance 並把它們的 parentId 指到這個陣列的
    // id，如果陣列自己的 item 這時候還不存在，FeatureBrowser 每次
    // treeStructureChanged() 重建畫面時都會找不到父節點（"Parent not found"），
    // 站位數量的名稱會在 rebuild() 結束時透過 setTreeItemName() 自動補上。
    ui::FeatureTreeItem item;
    item.type       = ui::ItemType::Pattern;
    item.id         = arr->id();
    item.name       = arr->name();
    item.parentId   = "";
    item.visible    = arr->isVisible();
    item.selectable = true;
    item.data       = QVariant::fromValue(static_cast<QObject*>(arr));
    m_treeItems.append(item);

    rebuildFeature(arr);
    Q_EMIT treeStructureChanged();

    qDebug() << "[Document] AlignedProfileArray created:" << arr->name()
             << "stations:" << arr->stationCount();
    return arr;
}

ProfileLoftSolid* Document::createProfileLoftSolid(
    AlignedProfileArray* sourceArray,
    const QString& name)
{
    if (!sourceArray) {
        qWarning() << "[Document] createProfileLoftSolid: sourceArray is null";
        return nullptr;
    }

    auto* loft = new ProfileLoftSolid(this);
    loft->setName(name.isEmpty()
        ? QString("ProfileLoft %1").arg(m_nextFeatureNumber++)
        : name);
    loft->setSourceArray(sourceArray);

    addFeature(loft);

    ui::FeatureTreeItem item;
    item.type       = ui::ItemType::Loft;
    item.id         = loft->id();
    item.name       = loft->name();
    item.parentId   = sourceArray->id();
    item.visible    = loft->isVisible();
    item.selectable = true;
    item.data       = QVariant::fromValue(static_cast<QObject*>(loft));
    m_treeItems.append(item);

    rebuildFeature(loft);
    Q_EMIT treeStructureChanged();

    qDebug() << "[Document] ProfileLoftSolid created:" << loft->name();
    return loft;
}

Extrude* Document::createExtrude(Sketch* sketch, double height, const QString& name) {
    if (!sketch) {
        qWarning() << "[Document] Cannot create extrude: sketch is null";
        return nullptr;
    }

    Extrude* extrude = new Extrude(this);
    extrude->setSketch(sketch);
    extrude->setHeight(height);
    // ✅ 修正：先 addFeature 再建立 Feature 層父子關係，避免 Extrude 被刪時帶走 Sketch
    QString extrudeName = name.isEmpty()
                              ? QString("Extrude %1").arg(m_nextFeatureNumber++)
                              : name;
    extrude->setName(extrudeName);

    sketch->setVisible(false);  // hide sketch when it becomes child of extrude

    addFeature(extrude);
    // ✅ 改用 setFeatureParent：只影響 Feature 樹狀結構，不動 QObject 所有權
    sketch->setFeatureParent(extrude);

    // ✅ addFeature 已連接 rebuildRequested，現在明確執行一次 rebuild
    extrude->rebuild();

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

    // ✅ 將草圖的 tree item 重新設為 extrude 的子節點
    //    （草圖在 createSketch 時以 parentId="" 加入，現在要修正它的父項目）
    for (ui::FeatureTreeItem& item : m_treeItems) {
        if (item.id == sketch->id()) {
            item.parentId = extrude->id();
            break;
        }
    }

    Q_EMIT treeStructureChanged();

    qDebug() << "[Document] Extrude created:" << extrudeName;
    return extrude;
}

// rebuildAll：改用拓撲順序
void Document::rebuildAll() {
    Q_EMIT rebuildStarted();
    bool cycle;
    QList<QString> order = m_depGraph.topologicalOrder(&cycle);
    if (cycle) {
        qWarning() << "[Document] Circular dependency, rebuild aborted";
        Q_EMIT rebuildFinished(false);
        return;
    }

    bool allOk = true;
    // 依拓撲順序重建（被依賴者先）
    for (const QString& id : order) {
        Feature* f = findFeature(id);
        if (!f || f->isSuppressed()) continue;
        if (!f->isDirty()) continue;             // ← 跳過未標記者

        // 若是帶表達式的特徵，先更新 cachedValue
        if (auto* ext = qobject_cast<Extrude*>(f)) {
            auto [ok, v] = m_parameterStore->evaluate(ext->heightExpression());
            if (ok) ext->setHeight(v);           // 更新快取，不會再 emit rebuildRequested
        }

        bool ok = f->rebuild();
        f->clearDirty();
        if (!ok) { allOk = false; f->setError("Rebuild failed"); }
        else        f->clearError();
    }
    // 未在圖中的特徵（孤立）線性補跑
    // ⚠️ 同 Document::load 的教訓：先取快照再迭代。f->rebuild() 對某些
    //    feature（例如 AlignedProfileArray）會呼叫 addFeature() 動態新增
    //    子 feature（SketchInstance），若直接對 m_features 做 range-based
    //    for，append 觸發的記憶體重新配置會讓迴圈迭代到一半就失效，導致
    //    對垃圾記憶體呼叫虛擬函式而 SIGSEGV。
    const QList<Feature*> orphanSnapshot = m_features;
    for (Feature* f : orphanSnapshot) {
        if (f && !order.contains(f->id()) && f->isDirty() && !f->isSuppressed()) {
            f->rebuild();
            f->clearDirty();
        }
    }
    Q_EMIT rebuildFinished(allOk);
}

void Document::rebuildFeature(Feature* feature) {
    if (!feature || feature->isSuppressed()) return;

    qDebug() << "[Document] Rebuilding feature:" << feature->name();

    Sketch* sketch = qobject_cast<Sketch*>(feature);

    // ① 先 erase 舊的 AIS shapes（rebuild 前 m_aisShapes 仍是舊 Handle）
    if (sketch && !m_aisContext.IsNull()) {
        sketch->eraseFromContext(m_aisContext);
    }

    // ② rebuild：清空並重建 m_aisShapes，emit rebuilt()
    //    → CadView::onSketchRebuilt 會在這裡更新 aisToFeatureId mapping
    feature->rebuild();
    feature->clearDirty();  // ← 允許下次 markDirty 再次觸發 rebuildRequested

    // ③ display 新的 AIS shapes
    if (sketch && !m_aisContext.IsNull()) {
        if (sketch->isVisible())
            sketch->displayInContext(m_aisContext);
        m_aisContext->UpdateCurrentViewer();
    } else {
        // ✅ 修正：不再自己管 AIS cache，改發事件讓 CadView 統一重繪
        Q_EMIT featureShapeUpdated(feature);   // ← 新增信號，見 Document.h
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

    // ✅ Respect visibility state
    if (!feature->isVisible()) return;

    if (Sketch* sketch = qobject_cast<Sketch*>(feature)) {
        sketch->displayInContext(context);
        return;
    }

    if (!feature->shape().IsNull()) {
        Handle(AIS_Shape) aisShape = new AIS_Shape(feature->shape());
        // ✅ 新增：設定 Shaded 模式並使用材質，避免面消失
        aisShape->SetDisplayMode(AIS_Shaded);
        aisShape->SetMaterial(Graphic3d_NameOfMaterial_Silver);
        context->Display(aisShape, AIS_Shaded, -1, Standard_False);
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

// rebuildFrom：只重建受影響的子樹
void Document::rebuildFrom(Feature* changedFeature) {
    if (!changedFeature) return;
    changedFeature->markDirty();
    QList<QString> affected = m_depGraph.affectedBy(changedFeature->id());
    // 在 topologicalOrder 中只取受影響子集
    bool cycle;
    QList<QString> fullOrder = m_depGraph.topologicalOrder(&cycle);
    if (cycle) return;

    QSet<QString> affectedSet(affected.begin(), affected.end());
    affectedSet.insert(changedFeature->id());

    for (const QString& id : fullOrder) {
        if (!affectedSet.contains(id)) continue;
        Feature* f = findFeature(id);
        if (!f || f->isSuppressed()) continue;
        f->rebuild();
        f->clearDirty();
    }
}


cad::Extrude* Document::lastExtrude() const
{
    for (auto it = m_features.rbegin(); it != m_features.rend(); ++it) {
        if (auto* e = qobject_cast<cad::Extrude*>(*it))
            return e;
    }
    return nullptr;
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

void Document::onVisibilityChanged(const QVariantMap& data) {
    const QString itemId = data["itemId"].toString();
    const bool    visible = data["visible"].toBool();

    qDebug() << "[Document] Visibility changed:" << itemId << visible;

    // ── 1. Update m_treeItems so checkbox stays in sync ───────────────────
    for (ui::FeatureTreeItem& treeItem : m_treeItems) {
        if (treeItem.id == itemId) {
            treeItem.visible = visible;
            break;
        }
    }

    // ── 2. Reference geometry ─────────────────────────────────────────────
    ReferenceGeometryType refType;
    bool isRefGeom = true;

    if      (itemId == "plane_xy")     refType = ReferenceGeometryType::XYPlane;
    else if (itemId == "plane_xz")     refType = ReferenceGeometryType::XZPlane;
    else if (itemId == "plane_yz")     refType = ReferenceGeometryType::YZPlane;
    else if (itemId == "axis_x")       refType = ReferenceGeometryType::XAxis;
    else if (itemId == "axis_y")       refType = ReferenceGeometryType::YAxis;
    else if (itemId == "axis_z")       refType = ReferenceGeometryType::ZAxis;
    else if (itemId == "origin_point") refType = ReferenceGeometryType::Origin;
    else                               isRefGeom = false;

    if (isRefGeom) {
        setReferenceGeometryVisible(refType, visible);

        if      (refType == ReferenceGeometryType::XYPlane)
            setReferenceGeometryVisible(ReferenceGeometryType::LabelXY, visible);
        else if (refType == ReferenceGeometryType::XZPlane)
            setReferenceGeometryVisible(ReferenceGeometryType::LabelXZ, visible);
        else if (refType == ReferenceGeometryType::YZPlane)
            setReferenceGeometryVisible(ReferenceGeometryType::LabelYZ, visible);

        Q_EMIT treeStructureChanged();  // ✅ sync checkbox
        return;
    }

    // ── 3. Feature (Sketch, Extrude …) ───────────────────────────────────
    Feature* feature = findFeature(itemId);
    if (!feature || m_aisContext.IsNull()) {
        // ── 3a. Railway 資料夾節點：彙總 3D Alignment 顯示 ─────────────────
        // eyeOpen  → 顯示所有 TrackCenterLine 的 3D Alignment（E,N,Z 折線），
        //            並暫時把每個 child（TrackCenterLine 及其 VAlignment）
        //            eyeClose（沿用既有 halign/valign-visibility-changed
        //            事件，讓 UIManager 一併隱藏個別的疊加/縱斷面 dock）。
        // eyeClose → 移除彙總 3D Alignment，並還原各 child 先前的可見狀態。
        if (itemId == QLatin1String("__railway_folder__")) {
            m_railway3DVisible = visible;
            setModified(true);

            core::EventBus* bus = core::Application::instance()->eventBus();

            if (visible) {
                for (railway::TrackCenterLine* tcl : m_trackCenterLines) {
                    RailwayChildVisSnapshot snap;
                    snap.hAlign = tcl->hAlignVisible();
                    snap.vAlign = tcl->vAlignVisible();
                    m_railwaySavedChildVisibility.insert(tcl->id(), snap);

                    if (snap.hAlign) {
                        tcl->setHAlignVisible(false);
                        QVariantMap vdata;
                        vdata["tclId"]   = tcl->id();
                        vdata["visible"] = false;
                        bus->publish("railway.halign-visibility-changed", vdata);
                    }
                    if (snap.vAlign) {
                        tcl->setVAlignVisible(false);
                        QVariantMap vdata;
                        vdata["tclId"]   = tcl->id();
                        vdata["visible"] = false;
                        bus->publish("railway.valign-visibility-changed", vdata);
                    }
                }
            } else {
                for (railway::TrackCenterLine* tcl : m_trackCenterLines) {
                    const auto it = m_railwaySavedChildVisibility.constFind(tcl->id());
                    if (it == m_railwaySavedChildVisibility.constEnd())
                        continue;

                    if (it->hAlign != tcl->hAlignVisible()) {
                        tcl->setHAlignVisible(it->hAlign);
                        QVariantMap vdata;
                        vdata["tclId"]   = tcl->id();
                        vdata["visible"] = it->hAlign;
                        bus->publish("railway.halign-visibility-changed", vdata);
                    }
                    if (it->vAlign != tcl->vAlignVisible()) {
                        tcl->setVAlignVisible(it->vAlign);
                        QVariantMap vdata;
                        vdata["tclId"]   = tcl->id();
                        vdata["visible"] = it->vAlign;
                        bus->publish("railway.valign-visibility-changed", vdata);
                    }
                }
                m_railwaySavedChildVisibility.clear();
            }

            QVariantMap rdata;
            rdata["visible"] = visible;
            bus->publish("railway.railway3d-visibility-changed", rdata);

            Q_EMIT treeStructureChanged();
            return;
        }

        // ── 4. TrackCenterLine or its VAlignment child ────────────────────
        // Check valign_ prefix first
        if (itemId.startsWith("valign_")) {
            const QString tclId = itemId.mid(7);  // strip "valign_"
            railway::TrackCenterLine* tcl = findTrackCenterLine(tclId);
            if (tcl) {
                tcl->setVAlignVisible(visible);
                setModified(true);
                // Publish a separate event so UIManager can show/hide dock
                core::EventBus* bus = core::Application::instance()->eventBus();
                QVariantMap vdata;
                vdata["tclId"]   = tclId;
                vdata["visible"] = visible;
                bus->publish("railway.valign-visibility-changed", vdata);
                Q_EMIT treeStructureChanged();
            }
            return;
        }
        // Check TrackCenterLine id
        railway::TrackCenterLine* tcl = findTrackCenterLine(itemId);
        if (tcl) {
            tcl->setHAlignVisible(visible);
            setModified(true);
            // Publish event so UIManager can show/hide AlignmentRenderer
            core::EventBus* bus = core::Application::instance()->eventBus();
            QVariantMap vdata;
            vdata["tclId"]   = tcl->id();
            vdata["visible"] = visible;
            bus->publish("railway.halign-visibility-changed", vdata);
            Q_EMIT treeStructureChanged();
            return;
        }
        qWarning() << "[Document] onVisibilityChanged: item not found:" << itemId;
        return;
    }

    feature->setVisible(visible);

    if (Sketch* sketch = qobject_cast<Sketch*>(feature)) {
        if (visible)
            sketch->displayInContext(m_aisContext);
        else
            sketch->eraseFromContext(m_aisContext);

        Q_EMIT treeStructureChanged();  // ✅ sync checkbox
        return;
    }

    // ✅ 對非 Sketch feature（Extrude 等），通知 CadView 重繪
    //    CadView::displayAllFeatures 會讀取 feature->isVisible() 決定是否顯示
    Q_EMIT featureShapeUpdated(feature);
    Q_EMIT treeStructureChanged();

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

void Document::setTreeItemParent(const QString& itemId, const QString& newParentId) {
    for (ui::FeatureTreeItem& item : m_treeItems) {
        if (item.id == itemId) {
            item.parentId = newParentId;
            Q_EMIT treeStructureChanged();
            return;
        }
    }
}

void Document::setTreeItemName(const QString& itemId, const QString& newName) {
    for (ui::FeatureTreeItem& item : m_treeItems) {
        if (item.id == itemId) {
            item.name = newName;
            Q_EMIT treeStructureChanged();
            return;
        }
    }
}

QVector<ui::FeatureTreeItem> Document::getFeatureTreeItems() const {
    QVector<ui::FeatureTreeItem> items = m_treeItems;

    // ── 永遠附加 Railway folder（即使空的也要顯示，讓使用者可右鍵新增）
    ui::FeatureTreeItem railwayFolder;
    railwayFolder.type       = ui::ItemType::Railway;
    railwayFolder.id         = "__railway_folder__";
    railwayFolder.name       = "Railway";
    railwayFolder.parentId   = "";
    railwayFolder.visible    = m_railway3DVisible;  ///< eyeOpen = 顯示彙總 3D Alignment
    railwayFolder.selectable = false;
    items.append(railwayFolder);

    for (railway::TrackCenterLine* tcl : m_trackCenterLines) {
        ui::FeatureTreeItem tclItem;
        tclItem.type       = ui::ItemType::TrackCenterLine;
        tclItem.id         = tcl->id();
        tclItem.name       = tcl->name();
        tclItem.parentId   = "__railway_folder__";
        tclItem.visible    = tcl->hAlignVisible();
        tclItem.selectable = true;
        items.append(tclItem);

        // ── Vertical Alignment child ────────────────────────────────────────
        ui::FeatureTreeItem vAlignItem;
        vAlignItem.type       = ui::ItemType::VAlignment;
        vAlignItem.id         = QStringLiteral("valign_%1").arg(tcl->id());
        vAlignItem.name       = tr("Vertical Alignment");
        vAlignItem.parentId   = tcl->id();
        vAlignItem.visible    = tcl->vAlignVisible();
        vAlignItem.selectable = true;
        items.append(vAlignItem);
    }

    return items;
}

// ── TrackCenterLine CRUD ───────────────────────────────────────────────────

railway::TrackCenterLine* Document::addTrackCenterLine(const QString& name)
{
    auto* tcl = new railway::TrackCenterLine(this);
    QString tclName = name.isEmpty()
        ? QStringLiteral("Track %1").arg(m_trackCenterLines.size() + 1)
        : name;
    tcl->setName(tclName);
    m_trackCenterLines.append(tcl);
    setModified(true);
    Q_EMIT trackCenterLinesChanged();
    Q_EMIT treeStructureChanged();
    return tcl;
}

void Document::removeTrackCenterLine(const QString& id)
{
    for (int i = 0; i < m_trackCenterLines.size(); ++i) {
        if (m_trackCenterLines[i]->id() == id) {
            railway::TrackCenterLine* tcl = m_trackCenterLines.takeAt(i);
            tcl->deleteLater();
            setModified(true);
            Q_EMIT trackCenterLinesChanged();
            Q_EMIT treeStructureChanged();
            return;
        }
    }
    qWarning() << "[Document] removeTrackCenterLine: id not found:" << id;
}

railway::TrackCenterLine* Document::findTrackCenterLine(const QString& id) const
{
    for (railway::TrackCenterLine* tcl : m_trackCenterLines) {
        if (tcl->id() == id) return tcl;
    }
    return nullptr;
}

} // namespace cad
} // namespace aicad