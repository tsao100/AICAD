/**
 * @file Document.cpp
 * @brief CAD 文件實作
 * @author AICAD Team
 * @date 2025-01-08
 */

#include "core/FpuGuard.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include "Document.h"
#include "Feature.h"
#include "Sketch.h"
#include "sketch/SketchPointAIS.h"
#include "Extrude.h"
#include "Plane.h"
#include "SketchInstance.h"
#include "AlignedProfileArray.h"
#include "ProfileLoftSolid.h"
#include "TrackSlabPlan.h"
#include "ComponentInstall.h"
#include "DefinedPlane.h"
#include "ChamferSolid.h"
#include "ui/FeatureTreeItem.h"
#include <QUuid>
#include <QSignalBlocker>
#include <QRegularExpression>
#include "drawing/DrawingSheetManager.h"
#include <QTimer>
#include <QSet>
#include <QJsonObject>
#include <QJsonArray>

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

    // 道版 E,N 平面圖：整份文件一個、自動建立／移除（Loft 增減時重新檢查）
    connect(this, &Document::featureAdded,   this, [this](Feature*) { scheduleSlabSync(); });
    connect(this, &Document::featureRemoved, this, [this]()         { scheduleSlabSync(); });

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

    // 讓圖紙管理器等外部模組先把最新資料寫回 passthrough 欄位
    Q_EMIT aboutToSave();

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

        // ✅ 儲存圖紙（Drawing Sheets，passthrough）
        if (!m_drawingSheetsData.isEmpty())
            docJson["drawingSheets"] = m_drawingSheetsData;

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

    // 載入期間（QJSEngine 參數運算、OCCT 建構、約束求解）可能合法地產生
    // Inf/NaN；ECL 若曾開啟浮點例外 trap，會把它變成 FLOATING-POINT-OVERFLOW
    // 並使行程崩潰。此處再保險遮蔽一次，函式結束時自動還原。
    aicad::core::FpuMaskGuard fpuGuard;

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
                else if (typeStr == "Chamfer") feature = new ChamferSolid(this);
                else if (typeStr == "SlabPlan") feature = new TrackSlabPlan(this);
                else if (typeStr == "ComponentInstall") feature = new ComponentInstall(this);
                else if (typeStr == "DefinedPlane") feature = new DefinedPlane(this);

                if (feature) {
                    if (feature->fromJson(featureJson)) {
                        addFeatureInternal(feature);

                        // ✅ 還原 Sketch → Document 的參數作用域鏈（sketch store → global store），
                        //    否則載入後的 Sketch 無法解析引用全域參數的表達式，
                        //    且全域參數變更不會觸發此 Sketch 重建。
                        if (auto* sk = qobject_cast<Sketch*>(feature)) {
                            sk->parameterStore()->setParentStore(m_parameterStore);
                            connect(sk->parameterStore(), &aicad::core::ParameterStore::parametersRecomputed,
                                    sk, &Sketch::scheduleRebuild, Qt::UniqueConnection);
                        }
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

        // ✅ 讀取圖紙（Drawing Sheets，passthrough；由 DrawingSheetManager 延後解析）
        m_drawingSheetsData = docJson.value("drawingSheets").toArray();

        // 圖冊：載入後建立管理器（與線形比對、監看線形更新）。延後到事件迴圈，確保線形已載入。
        if (!m_drawingSheetsData.isEmpty())
            QTimer::singleShot(0, this, [this]() { drawing::DrawingSheetManager::forDocument(this); });

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
                emitTreeStructureChanged();
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

        // 先接上各 Loft 的來源陣列，再依「每個分段一個 Loft」整理（舊檔的單一 Loft 轉成
        // 第一段的 Loft、補建其餘分段的 Loft、測站改掛到各自的 Loft 底下）。
        {
            const QList<Feature*> preLoft = m_features;
            for (Feature* feature : preLoft)
                if (auto* loft = qobject_cast<ProfileLoftSolid*>(feature))
                    loft->resolveReferences(this);
            for (Feature* feature : preLoft)
                if (auto* arr = qobject_cast<AlignedProfileArray*>(feature))
                    arr->syncSegmentLofts();
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

        // ✅ ChamferSolid 依賴另一個實體 Feature（例如 ProfileLoftSolid），必須
        //    在來源已完成 rebuild 後才能取得正確的 shape() 來重新配對邊，故
        //    在所有其他 Feature（含 Loft）rebuild 完成後再補一輪（理由同上方
        //    ProfileLoftSolid 依賴 AlignedProfileArray 的那兩輪）。
        const QList<Feature*> chamferSnapshot = m_features;
        for (Feature* feature : chamferSnapshot) {
            if (auto* cf = qobject_cast<ChamferSolid*>(feature))
                cf->resolveReferences(this);
        }
        for (Feature* feature : chamferSnapshot) {
            if (auto* cf = qobject_cast<ChamferSolid*>(feature)) {
                // ⚠️ 同上：改用 rebuildFeature() 而非直接 rebuild()，確保
                //    Q_EMIT featureShapeUpdated 讓 CadView 收到通知並重繪。
                rebuildFeature(cf);
            }
        }

        // ✅ DefinedPlane（定義平面）：依檔案順序 rebuild（基準平面一定在前面）
        {
            const QList<Feature*> planeSnapshot = m_features;
            for (Feature* feature : planeSnapshot)
                if (auto* dp = qobject_cast<DefinedPlane*>(feature))
                    rebuildFeature(dp);
        }

        // ✅ TrackSlabPlan（道版 E,N 平面圖）依賴所有 Loft 的來源陣列與測站，必須在 Loft 之後 rebuild。
        //    之後由 syncSlabPlan() 補建／清理（舊檔沒有道版圖、或舊版每個 Loft 一張的情況）。
        {
            const QList<Feature*> slabSnapshot = m_features;
            for (Feature* feature : slabSnapshot)
                if (auto* sp = qobject_cast<TrackSlabPlan*>(feature))
                    rebuildFeature(sp);       // 走 rebuildFeature() 才會通知 CadView 重繪
        }
        // ✅ ComponentInstall（3D 組件安裝）：放置依賴斷面陣列的測站與 cant，必須在陣列之後 rebuild。
        //    Loft 在它之前已建好（未開孔）；syncSlabPlan() 偵測到組件清單變了會重建所有 Loft 開孔。
        {
            const QList<Feature*> compSnapshot = m_features;
            for (Feature* feature : compSnapshot)
                if (auto* ci = qobject_cast<ComponentInstall*>(feature))
                    ci->resolveReferences(this);                 // 座標線所在的主草圖
            for (Feature* feature : compSnapshot)
                if (auto* ci = qobject_cast<ComponentInstall*>(feature)) {
                    // 與來源 *.aicad 連動：建立監看；來源檔比內嵌副本新 → 自動同步一次
                    if (ci->attachSourceWatcher()) m_modified = true;
                    rebuildFeature(ci);
                }
        }
        scheduleSlabSync();

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
        emitTreeStructureChanged();

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
            treeItem.name = QString("%1 (%2 segments, %3 stations)")
                                .arg(arr->name()).arg(arr->segmentCount()).arg(arr->stationCount());
        } else if (qobject_cast<ProfileLoftSolid*>(feature)) {
            treeItem.type = ui::ItemType::Loft;
        } else if (qobject_cast<TrackSlabPlan*>(feature)) {
            treeItem.type = ui::ItemType::SlabPlan;
        } else if (qobject_cast<ComponentInstall*>(feature)) {
            treeItem.type = ui::ItemType::Component;
        } else if (qobject_cast<DefinedPlane*>(feature)) {
            treeItem.type = ui::ItemType::DefinedPlane;
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
    emitTreeStructureChanged();
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

    emitTreeStructureChanged();

    qDebug() << "[Document] Sketch created:" << sketchName;
    return sketch;
}

// ─────────────────────────────────────────────────────────────────────────────
// 草圖複製／貼上
// ─────────────────────────────────────────────────────────────────────────────

QJsonObject Document::exportSketchForCopy(const Sketch* sketch) const
{
    QJsonObject payload;
    if (!sketch) return payload;

    payload[QStringLiteral("format")]  = QStringLiteral("aicad-sketch");
    payload[QStringLiteral("version")] = 1;
    const QJsonObject sketchJson = sketch->toJson();
    payload[QStringLiteral("sketch")]  = sketchJson;

    // 找出草圖運算式引用到的文件全域參數（以識別字邊界比對整份 JSON 文字，
    // 再對新找到的全域參數的運算式重複一次，取得相依閉包）。
    QString haystack = QString::fromUtf8(
        QJsonDocument(sketchJson).toJson(QJsonDocument::Compact));
    QJsonObject globals;
    if (m_parameterStore) {
        const QStringList names = m_parameterStore->localNames();
        bool grew = true;
        int guard = 0;
        while (grew && guard++ < 16) {
            grew = false;
            for (const QString& n : names) {
                if (globals.contains(n)) continue;
                const QRegularExpression re(
                    QStringLiteral("(?<![A-Za-z0-9_])%1(?![A-Za-z0-9_])")
                        .arg(QRegularExpression::escape(n)));
                if (!re.match(haystack).hasMatch()) continue;
                const QString expr = m_parameterStore->expression(n);
                globals[n] = expr;
                haystack += QLatin1Char(' ') + expr;
                grew = true;
            }
        }
    }
    payload[QStringLiteral("globalParameters")] = globals;
    return payload;
}

bool Document::isSketchClipboardPayload(const QJsonObject& payload)
{
    return payload.value(QStringLiteral("format")).toString() == QLatin1String("aicad-sketch")
        && payload.value(QStringLiteral("sketch")).isObject();
}

Sketch* Document::pasteSketch(const QJsonObject& payload, QString* error)
{
    if (!isSketchClipboardPayload(payload)) {
        if (error) *error = QStringLiteral("剪貼簿內容不是 AICAD 草圖資料");
        return nullptr;
    }

    QJsonObject sketchJson = payload.value(QStringLiteral("sketch")).toObject();

    // ── 1. 補進來源文件的全域參數（只補缺少的，不覆蓋既有同名參數）──────────────
    const QJsonObject globals = payload.value(QStringLiteral("globalParameters")).toObject();
    if (m_parameterStore && !globals.isEmpty()) {
        // 新增參數不會改變既有草圖的求值結果，阻斷訊號可避免整份文件被重建
        QSignalBlocker blocker(m_parameterStore);
        for (auto it = globals.begin(); it != globals.end(); ++it) {
            if (!m_parameterStore->hasLocal(it.key()))
                m_parameterStore->setLocal(it.key(), it.value().toString());
        }
    }

    // ── 2. 新的 Feature id、清除來源專屬欄位、可見 ────────────────────────────
    sketchJson[QStringLiteral("id")]      = QUuid::createUuid().toString();
    sketchJson[QStringLiteral("visible")] = true;
    sketchJson[QStringLiteral("suppressed")] = false;
    sketchJson.remove(QStringLiteral("parentId"));
    sketchJson.remove(QStringLiteral("hasError"));
    sketchJson.remove(QStringLiteral("errorMessage"));

    // ── 3. 名稱不得與既有 Feature 重複 ────────────────────────────────────────
    const QString baseName = sketchJson.value(QStringLiteral("name")).toString();
    auto nameTaken = [this](const QString& n) {
        for (Feature* f : m_features)
            if (f && f->name() == n) return true;
        return false;
    };
    QString newName = baseName;
    if (newName.isEmpty()) newName = QStringLiteral("Sketch");
    if (nameTaken(newName)) {
        const QString stem = newName + QStringLiteral(" - 複本");
        newName = stem;
        for (int i = 2; nameTaken(newName); ++i)
            newName = QStringLiteral("%1 %2").arg(stem).arg(i);
    }
    sketchJson[QStringLiteral("name")] = newName;

    // ── 4. 建立草圖 ───────────────────────────────────────────────────────────
    auto* sketch = new Sketch(this);
    if (!sketch->fromJson(sketchJson)) {
        delete sketch;
        if (error) *error = QStringLiteral("草圖資料格式錯誤，無法貼上");
        return nullptr;
    }

    // 與 createSketch()／載入檔案相同：草圖參數 store → 文件全域 store 的作用域鏈
    sketch->parameterStore()->setParentStore(m_parameterStore);
    connect(sketch->parameterStore(), &aicad::core::ParameterStore::parametersRecomputed,
            sketch, &Sketch::scheduleRebuild, Qt::UniqueConnection);

    addFeature(sketch);

    ui::FeatureTreeItem sketchItem;
    sketchItem.type       = ui::ItemType::Sketch;
    sketchItem.id         = sketch->id();
    sketchItem.name       = sketch->name();
    sketchItem.parentId   = QString();
    sketchItem.visible    = sketch->isVisible();
    sketchItem.selectable = true;
    sketchItem.data       = QVariant::fromValue(sketch);
    m_treeItems.append(sketchItem);

    rebuildFeature(sketch);         // 建立 AIS 並通知畫面顯示
    emitTreeStructureChanged();

    qDebug() << "[Document] Sketch pasted:" << newName;
    return sketch;
}

SketchInstance* Document::createSketchInstance(
        Sketch* master,
        Plane* plane,
        const QHash<QString, double>& overrides,
        const QString& name,
        bool rebuildNow)
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
    emitTreeStructureChanged();

    // rebuildNow=false：呼叫端（例如 AlignedProfileArray）緊接著會自己以正確的
    // 參數／放置座標系建構，這裡先 rebuild 一次只是白白多解算一次。
    if (rebuildNow)
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
    const QString& name,
    bool autoInterval)
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
    arr->setRange(startChainage, endChainage, interval, autoInterval);

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
    emitTreeStructureChanged();

    qDebug() << "[Document] AlignedProfileArray created:" << arr->name()
             << "stations:" << arr->stationCount();
    return arr;
}

ProfileLoftSolid* Document::createProfileLoftSolid(
    AlignedProfileArray* sourceArray,
    const QString& name,
    const QString& segmentId)
{
    if (!sourceArray) {
        qWarning() << "[Document] createProfileLoftSolid: sourceArray is null";
        return nullptr;
    }

    auto* loft = new ProfileLoftSolid(this);
    loft->setName(name.isEmpty()
        ? QString("ProfileLoft %1").arg(m_nextFeatureNumber++)
        : name);
    loft->setSegmentId(segmentId);
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
    emitTreeStructureChanged();

    qDebug() << "[Document] ProfileLoftSolid created:" << loft->name();
    return loft;
}

TrackSlabPlan* Document::createTrackSlabPlan()
{
    auto* plan = new TrackSlabPlan(this);
    plan->setName(QStringLiteral("道版 E,N 平面圖"));
    // 不設 Feature 層的 parent → 樹狀圖列在 root 底下

    addFeature(plan);

    ui::FeatureTreeItem item;
    item.type       = ui::ItemType::SlabPlan;
    item.id         = plan->id();
    item.name       = plan->name();
    item.parentId   = QString();                 // root
    item.visible    = plan->isVisible();
    item.selectable = true;
    item.data       = QVariant::fromValue(static_cast<QObject*>(plan));
    m_treeItems.append(item);

    emitTreeStructureChanged();
    qDebug() << "[Document] TrackSlabPlan created:" << plan->name();
    return plan;
}

void Document::scheduleSlabSync()
{
    if (m_slabSyncPending) return;
    m_slabSyncPending = true;
    QTimer::singleShot(0, this, [this]() { syncSlabPlan(); });
}

void Document::syncSlabPlan()
{
    m_slabSyncPending = false;

    QList<TrackSlabPlan*> plans;
    for (Feature* f : m_features)
        if (auto* sp = qobject_cast<TrackSlabPlan*>(f)) plans.append(sp);

    const bool wanted = TrackSlabPlan::isApplicable(this);

    // 衍生資料的自動增減不算使用者修改：保留原本的 modified 旗標
    const bool wasModified = m_modified;

    // 只留一個（舊版「每個 Loft 一張」的存檔會有多個）
    while (plans.size() > 1) removeFeature(plans.takeLast());

    if (!wanted) {
        if (!plans.isEmpty()) removeFeature(plans.first());
    } else {
        TrackSlabPlan* plan = plans.isEmpty() ? createTrackSlabPlan() : plans.first();
        plan->refreshSources();           // 重新接上各 Loft；Loft 清單變了會排程重建
    }

    // ── 3D 組件安裝：重新接上陣列；組件清單（增／減）改變 → 重建所有 Loft 以便開孔／補回 ──
    QStringList compIds;
    for (Feature* f : m_features)
        if (auto* ci = qobject_cast<ComponentInstall*>(f)) {
            compIds << ci->id();
            ci->refreshSources();
        }
    compIds.sort();
    const QString compSig = compIds.join(QLatin1Char('|'));
    const bool compChanged = (compSig != m_componentSig);
    m_componentSig = compSig;

    setModified(wasModified);

    if (compChanged) rebuildLoftsForComponents();
}

void Document::rebuildLoftsForComponents()
{
    // Loft 重建時會呼叫 ComponentInstall::cutSpaces()；Chamfer 連著 Loft 的 shapeChanged 會跟著重建。
    const QList<Feature*> snapshot = m_features;
    for (Feature* f : snapshot)
        if (auto* loft = qobject_cast<ProfileLoftSolid*>(f))
            rebuildFeature(loft);
}

bool Document::updateDefinedPlane(DefinedPlane* dp, const QString& baseRef, const QString& offsetExpr,
                                  const QString& name, QString* error)
{
    PlaneDef d;
    d.method = PlaneMethod::Offset;
    d.baseRef = baseRef;
    d.offsetExpr = offsetExpr.trimmed().isEmpty() ? QStringLiteral("0") : offsetExpr.trimmed();
    return updateDefinedPlane(dp, d, name, error);
}

bool Document::updateDefinedPlane(DefinedPlane* dp, const PlaneDef& def, const QString& name, QString* error)
{
    if (!dp) return false;
    if (!dp->setDefinition(def, error)) return false;
    if (!name.trimmed().isEmpty() && name.trimmed() != dp->name()) {
        dp->setName(name.trimmed());
        for (ui::FeatureTreeItem& it : m_treeItems)
            if (it.id == dp->id()) it.name = dp->name();
    }
    rebuildFeature(dp);
    emitTreeStructureChanged();
    setModified(true);
    return true;
}

DefinedPlane* Document::createDefinedPlane(const QString& baseRef, const QString& offsetExpr,
                                           const QString& name, QString* error)
{
    PlaneDef d;
    d.method = PlaneMethod::Offset;
    d.baseRef = baseRef;
    d.offsetExpr = offsetExpr.trimmed().isEmpty() ? QStringLiteral("0") : offsetExpr.trimmed();
    return createDefinedPlane(d, name, error);
}

DefinedPlane* Document::createDefinedPlane(const PlaneDef& def, const QString& name, QString* error)
{
    auto* dp = new DefinedPlane(this);
    if (!dp->setDefinition(def, error)) {
        delete dp;
        return nullptr;
    }

    QString nm = name.trimmed();
    if (nm.isEmpty()) {                       // 未指定名稱：Plane 1、Plane 2…（避開重名）
        int n = 1;
        do { nm = QStringLiteral("Plane %1").arg(n++); } while (findFeatureByName(nm));
    }
    dp->setName(nm);

    addFeature(dp);

    ui::FeatureTreeItem item;
    item.type       = ui::ItemType::DefinedPlane;
    item.id         = dp->id();
    item.name       = dp->name();
    item.parentId   = QString();                 // root
    item.visible    = dp->isVisible();
    item.selectable = true;
    item.data       = QVariant::fromValue(static_cast<QObject*>(dp));
    m_treeItems.append(item);

    rebuildFeature(dp);
    emitTreeStructureChanged();
    setModified(true);
    return dp;
}

ComponentInstall* Document::createComponentInstall(const QString& name)
{
    auto* ci = new ComponentInstall(this);
    if (!name.isEmpty()) ci->setName(name);
    return ci;     // 呼叫端先 setSource()／setPlacements()，再呼叫 addComponentInstall()
}

void Document::addComponentInstall(ComponentInstall* ci)
{
    if (!ci) return;
    addFeature(ci);

    ui::FeatureTreeItem item;
    item.type       = ui::ItemType::Component;
    item.id         = ci->id();
    item.name       = ci->name();
    item.parentId   = QString();                 // root
    item.visible    = ci->isVisible();
    item.selectable = true;
    item.data       = QVariant::fromValue(static_cast<QObject*>(ci));
    m_treeItems.append(item);

    rebuildFeature(ci);
    emitTreeStructureChanged();
    scheduleSlabSync();          // → refreshSources() ＋ 組件清單變了 → 重建 Loft 開孔
}

void Document::applyComponentChange(ComponentInstall* ci)
{
    if (ci) rebuildFeature(ci);
    rebuildLoftsForComponents();
    setModified(true);
}

ChamferSolid* Document::createChamferSolid(
    Feature* source,
    double distance,
    const QVector<EdgeSignature>& edges,
    const QString& name)
{
    if (!source) {
        qWarning() << "[Document] createChamferSolid: source is null";
        return nullptr;
    }
    if (edges.isEmpty()) {
        qWarning() << "[Document] createChamferSolid: no edges given";
        return nullptr;
    }

    auto* cf = new ChamferSolid(this);
    cf->setName(name.isEmpty()
        ? QString("Chamfer %1").arg(m_nextFeatureNumber++)
        : name);
    cf->setSourceFeature(source);
    cf->setDistance(distance);
    cf->setEdgeSignatures(edges);

    // ✅ 比照 createExtrude() 對來源 Sketch 的既定慣例（見上方
    //    "hide sketch when it becomes child of extrude"）：來源實體變成
    //    Chamfer 的父節點後就隱藏，否則兩個幾乎完全重疊的 shape 會疊在一起
    //    顯示——倒角量通常遠小於整體尺寸，原本不透明的來源實體會直接擋住
    //    新倒角實體的角落，畫面上完全看不出差異，像是「沒有顯示」。
    source->setVisible(false);

    addFeature(cf);

    ui::FeatureTreeItem item;
    item.type       = ui::ItemType::Chamfer;
    item.id         = cf->id();
    item.name       = cf->name();
    item.parentId   = source->id();
    item.visible    = cf->isVisible();
    item.selectable = true;
    item.data       = QVariant::fromValue(static_cast<QObject*>(cf));
    m_treeItems.append(item);

    rebuildFeature(cf);
    emitTreeStructureChanged();

    if (cf->hasError()) {
        qWarning() << "[Document] ChamferSolid created with error:" << cf->name()
                    << "-" << cf->errorMessage();
    } else {
        qDebug() << "[Document] ChamferSolid created:" << cf->name();
    }
    return cf;
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

    emitTreeStructureChanged();

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

    // ⚠️ 修正：SketchPoint 雙擊尺寸約束編輯數值後被隱藏。
    // 這個函式由 Feature::markDirty() → rebuildRequested 觸發，而
    // Sketch::solveConstraints()（雙擊編輯數值的提交路徑就是呼叫這個）
    // 內部本身也會在 markDirty() 之後、緊接著再呼叫一次自己的
    // rebuildShapesOnly()（見 Sketch.cpp）——兩者對同一個 sketch 的
    // m_aisShapes/AIS context 做重複且時序上互相競爭的 Erase/rebuild()/
    // Display，任何一個環節的邊界情況（例如上面 ③ 的 isVisible() 判斷、
    // 或兩次 rebuild() 之間 fingerprint 快照不同步）都可能讓某些
    // SketchPointAIS 在①被 Erase 之後，沒有被任何一邊真正重新 Display。
    // 與其繼續在複雜的雙重 rebuild 交互中追根究柢，這裡直接做最後一道
    // 保險：只要 sketch 存在且 context 有效，就無條件確保所有
    // SketchPointAIS 都是「顯示＋可選取」的狀態，滿足「SketchPoint 永遠
    // 顯示」的需求，且此操作對已經正確顯示的點是完全無副作用的
    // no-op（Display/Activate 對已顯示/已啟用的物件重複呼叫是安全的）。
    if (sketch && !m_aisContext.IsNull()) {
        for (const auto& obj : sketch->aisShapes()) {
            if (obj.IsNull()) continue;
            if (!Handle(SketchPointAIS)::DownCast(obj).IsNull()) {
                m_aisContext->Display(obj, Standard_False);
                m_aisContext->Activate(obj, 0, Standard_False);
            }
        }
        m_aisContext->UpdateCurrentViewer();
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
    // ⚠️ 修正：Document 的「已修改」旗標（isModified()／setModified()）只
    // 接在 Feature::nameChanged／shapeChanged 上，沒有接 rebuildRequested。
    // 但幾何新增/編輯（LINE、CIRCLE、POLYGON、POINT、CHAMFER、ERASE…）
    // 一律是透過 Sketch::addXxxGeom()／addConstraint()／removeGeometry()
    // 等方法 Q_EMIT rebuildRequested() 來觸發畫面重繪，從來不會單獨發
    // shapeChanged——結果是：光是畫一條線、加一個點，Document 完全不會
    // 認為「有未儲存的變更」。SaveCommand 本身是不管這個旗標、無條件寫檔
    // （見 BasicCommands.cpp），所以手動按 Save 不受影響；但任何看這個旗
    // 標的地方（例如「有未儲存變更，是否繼續？」的確認提示、標題列的
    // * 標記）都會誤判成「沒有變更」。這裡補上，讓幾何變更也正確標記為
    // 已修改。
    setModified(true);
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

            // ⚠️ 僅呼叫 Display() 不保證選取模式一定被(重新)啟用：物件先前被
            //    Erase() 後其選取會被 Deactivate，之後單純再 Display() 在部分
            //    OCCT 情境下並不會自動重新 Activate。過去因此造成第一次下
            //    Sketch 指令時 XY/XZ/YZ 平面顯示了卻點不到，需要再下一次
            //    Sketch 指令（Erase→Display 循環一次）才「意外」變成可選取。
            //    此處明確依 selectable 旗標呼叫 Activate()/Deactivate()，
            //    確保每次顯示都保證選取狀態正確、可重現。
            if (refGeom.selectable) {
                m_aisContext->Activate(refGeom.aisObject);
            } else {
                m_aisContext->Deactivate(refGeom.aisObject);
            }
        }
    }
    m_aisContext->UpdateCurrentViewer();
    qDebug() << "[Document] All reference geometry shown";
}

void Document::hideAllReferenceGeometry() {
    for (const ReferenceGeometry& refGeom : m_referenceGeometries) {
        if (!refGeom.aisObject.IsNull()) {
            m_aisContext->Deactivate(refGeom.aisObject);
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

        emitTreeStructureChanged();  // ✅ sync checkbox
        return;
    }

    // ── 3. Feature (Sketch, Extrude …) ───────────────────────────────────
    Feature* feature = findFeature(itemId);
    if (!feature || m_aisContext.IsNull()) {
        // ── 3a0. 「組件安裝位置」資料夾：一次切換所有組件的顯示 ──────────────────────
        if (itemId == QLatin1String("__component_layout__")) {
            core::EventBus* bus = core::Application::instance()->eventBus();
            const QList<Feature*> snapshot = m_features;
            for (Feature* f : snapshot) {
                auto* ci = qobject_cast<ComponentInstall*>(f);
                if (!ci || ci->isVisible() == visible) continue;
                QVariantMap vm;
                vm["itemId"]  = ci->id();
                vm["visible"] = visible;
                bus->publish("feature.visibility-changed", vm);
            }
            emitTreeStructureChanged();
            return;
        }

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

            emitTreeStructureChanged();
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
                emitTreeStructureChanged();
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
            emitTreeStructureChanged();
            return;
        }
        qWarning() << "[Document] onVisibilityChanged: item not found:" << itemId;
        return;
    }

    feature->setVisible(visible);

    // ── ProfileArray／Loft 的眼睛圖示：底下的子項（Loft、測站…，沿樹狀圖 parentId）同步開關 ──
    //    Array → Loft(每段一個) → 測站。以 tree item 的 parentId 往下走，所以不論目前
    //    是舊檔（測站掛在陣列下）或新結構都適用；每個子孫的旗標與樹狀圖勾選一併更新。
    if (qobject_cast<AlignedProfileArray*>(feature) || qobject_cast<ProfileLoftSolid*>(feature)) {
        QSet<QString> seen;
        QStringList stack{ itemId };
        while (!stack.isEmpty()) {
            const QString pid = stack.takeLast();
            for (ui::FeatureTreeItem& ti : m_treeItems) {
                if (ti.parentId != pid || seen.contains(ti.id)) continue;
                seen.insert(ti.id);
                ti.visible = visible;
                if (Feature* child = findFeature(ti.id))
                    child->setVisible(visible);
                stack.append(ti.id);
            }
        }
    }

    if (Sketch* sketch = qobject_cast<Sketch*>(feature)) {
        if (visible)
            sketch->displayInContext(m_aisContext);
        else
            sketch->eraseFromContext(m_aisContext);

        emitTreeStructureChanged();  // ✅ sync checkbox
        return;
    }

    // ✅ 對非 Sketch feature（Extrude 等），通知 CadView 重繪
    //    CadView::displayAllFeatures 會讀取 feature->isVisible() 決定是否顯示
    Q_EMIT featureShapeUpdated(feature);
    emitTreeStructureChanged();

}

void Document::initializeOrigin(const Handle(AIS_InteractiveContext)& context) {
    qDebug() << "[Document] Initializing origin";
    initializeReferenceGeometry(context);
    createOriginFolderItems();

    emitTreeStructureChanged();
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
            emitTreeStructureChanged();
            return;
        }
    }
}

void Document::setTreeItemName(const QString& itemId, const QString& newName) {
    for (ui::FeatureTreeItem& item : m_treeItems) {
        if (item.id == itemId) {
            item.name = newName;
            emitTreeStructureChanged();
            return;
        }
    }
}

QVector<ui::FeatureTreeItem> Document::getFeatureTreeItems() const {
    QVector<ui::FeatureTreeItem> items = m_treeItems;

    // ── 「組件安裝位置」虛擬資料夾：有任何 ComponentInstall 時出現，各組件列在它底下 ──────
    {
        bool anyComponent = false, anyVisible = false;
        for (ui::FeatureTreeItem& it : items) {
            if (it.type != ui::ItemType::Component) continue;
            anyComponent = true;
            anyVisible = anyVisible || it.visible;
            it.parentId = QStringLiteral("__component_layout__");
        }
        if (anyComponent) {
            ui::FeatureTreeItem folder;
            folder.type       = ui::ItemType::ComponentLayout;
            folder.id         = QStringLiteral("__component_layout__");
            folder.name       = tr("組件安裝位置");
            folder.parentId   = QString();
            folder.visible    = anyVisible;           // eye：任一組件可見 → 開
            folder.selectable = false;
            items.append(folder);
        }
    }

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

    // ── Drawing Sheets：圖冊 → 封面／圖目錄／圖（由 drawingSheets JSON 即時建立）──
    {
        ui::FeatureTreeItem folder;
        folder.type       = ui::ItemType::Drawings;
        folder.id         = "__drawings_folder__";
        folder.name       = "Drawing Sheets";
        folder.visible    = true;
        folder.selectable = false;
        items.append(folder);

        QSet<QString> bookIds;
        for (const QJsonValue& v : m_drawingSheetsData) {
            const QJsonObject o = v.toObject();
            if (!o.value("__book").toBool()) continue;
            ui::FeatureTreeItem b;
            b.type       = ui::ItemType::DrawingBook;
            b.id         = QStringLiteral("book_") + o.value("id").toString();
            b.name       = o.value("name").toString();
            b.parentId   = folder.id;
            b.visible    = true;
            b.selectable = true;
            items.append(b);
            bookIds.insert(o.value("id").toString());

            // 機廠矩形視圖：資料夾＋每個視圖一項（資料來自圖冊設定的 depotViews）
            const QString bid = o.value("id").toString();
            const QJsonArray dv = o.value("settings").toObject().value("depotViews").toArray();
            ui::FeatureTreeItem df;
            df.type       = ui::ItemType::DepotViewFolder;
            df.id         = QStringLiteral("depotfolder_") + bid;
            df.name       = tr("機廠矩形視圖 (%1)").arg(dv.size());
            df.parentId   = b.id;
            df.visible    = false;
            df.selectable = true;
            for (int i = 0; i < dv.size(); ++i)
                if (m_shownDepotViews.contains(QStringLiteral("%1#%2").arg(bid).arg(i))) { df.visible = true; break; }
            items.append(df);
            for (int i = 0; i < dv.size(); ++i) {
                const QJsonObject vo = dv[i].toObject();
                ui::FeatureTreeItem vi;
                vi.type       = ui::ItemType::DepotViewItem;
                vi.id         = QStringLiteral("dview_%1_%2").arg(bid).arg(i);
                vi.name       = tr("%1. %2  (%3×%4 m, 排序 %5)").arg(i + 1).arg(vo.value("name").toString())
                                    .arg(vo.value("w").toDouble(), 0, 'f', 1).arg(vo.value("h").toDouble(), 0, 'f', 1)
                                    .arg(vo.value("order").toInt(1));
                vi.parentId   = df.id;
                vi.visible    = m_shownDepotViews.contains(QStringLiteral("%1#%2").arg(bid).arg(i));
                vi.selectable = true;
                items.append(vi);
            }
        }
        for (const QJsonValue& v : m_drawingSheetsData) {
            const QJsonObject o = v.toObject();
            if (o.value("__book").toBool()) continue;
            const QString bookId = o.value("bookId").toString();
            const QJsonObject tb = o.value("titleBlock").toObject();
            const bool isCover = o.value("isCover").toBool();
            const bool isIndex = o.value("planProfile").toObject().value("isIndex").toBool();
            QString title = tb.value("drawingTitle").toString();
            if (title.isEmpty()) title = tr("(untitled)");
            const QString no = tb.value("sheetNumber").toString();

            ui::FeatureTreeItem it;
            it.type       = ui::ItemType::DrawingSheetItem;
            it.id         = QStringLiteral("dsheet_") + o.value("id").toString();
            it.name       = isCover ? tr("封面 Cover — %1").arg(title)
                          : isIndex ? tr("圖目錄 Index — %1").arg(title)
                          : (no.isEmpty() ? title : QStringLiteral("%1  %2").arg(no, title));
            it.parentId   = bookIds.contains(bookId) ? QStringLiteral("book_") + bookId : folder.id;
            it.visible    = true;
            it.selectable = true;
            items.append(it);
        }
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
    emitTreeStructureChanged();
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
            emitTreeStructureChanged();
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

void Document::refreshFeatureDependencies(Feature* feature) {
    if (!feature) return;
    const QString id = feature->id();

    const QSet<QString> dependants = m_depGraph.dependantsOf(id);   // 誰依賴我：保留
    m_depGraph.removeNode(id);
    m_depGraph.ensureNode(id);
    for (const QString& depId : feature->featureDependencies())
        m_depGraph.addDependency(id, depId);
    for (const QString& d : dependants)
        m_depGraph.addDependency(d, id);
}

void Document::beginTreeBatch() { ++m_treeBatchDepth; }

void Document::endTreeBatch() {
    if (m_treeBatchDepth > 0 && --m_treeBatchDepth == 0 && m_treeBatchPending) {
        m_treeBatchPending = false;
        Q_EMIT treeStructureChanged();
    }
}

void Document::emitTreeStructureChanged() {
    if (m_treeBatchDepth > 0) { m_treeBatchPending = true; return; }
    Q_EMIT treeStructureChanged();
}


} // namespace cad
} // namespace aicad