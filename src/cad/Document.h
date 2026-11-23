/**
 * @file Document.h
 * @brief CAD 文件類別
 * @author AICAD Team
 * @date 2025-01-08
 */

#ifndef AICAD_CAD_DOCUMENT_H
#define AICAD_CAD_DOCUMENT_H

#include "ui/FeatureTreeItem.h"
#include "DependencyGraph.h"
#include "core/ParameterStore.h"
#include "railway/RailwayAlignment.h"
#include "ChamferSolid.h"   // EdgeSignature 用於 createChamferSolid() 參數，需完整定義

#include <QObject>
#include <QString>
#include <QList>
#include <QSet>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <TDocStd_Document.hxx>
#include <AIS_Shape.hxx>
#include <AIS_InteractiveObject.hxx>

namespace aicad {
namespace cad {

class Feature;
class Sketch;
class Extrude;
class AlignedProfileArray;
class ProfileLoftSolid;
class TrackSlabPlan;
class ComponentInstall;
class DefinedPlane;
struct PlaneDef;
class ChamferSolid;

/**
 * @brief 參考幾何類型
 */
enum class ReferenceGeometryType {
    Origin,         ///< 原點
    XAxis,          ///< X 軸
    YAxis,          ///< Y 軸
    ZAxis,          ///< Z 軸
    XYPlane,        ///< XY 平面
    XZPlane,        ///< XZ 平面
    YZPlane,         ///< YZ 平面
    LabelXY,
    LabelXZ,
    LabelYZ
};

/**
 * @brief 參考幾何資訊
 */
struct ReferenceGeometry {
    ReferenceGeometryType type;
    QString name;
    Handle(AIS_InteractiveObject) aisObject;
    bool visible;
    bool selectable;

    ReferenceGeometry()
        : type(ReferenceGeometryType::Origin)
        , visible(true)
        , selectable(true)
    {}
};

/**
 * @brief CAD 文件
 * 
 * 管理 CAD 設計的所有特徵和資料
 * - 特徵樹管理
 * - OCAF 資料綁定
 * - 檔案儲存/載入
 * - 復原/重做
 * 
 * 使用範例:
 * @code
 * Document* doc = new Document();
 * doc->setFileName("MyPart.aicad");
 * 
 * // 建立草圖
 * Sketch* sketch = doc->createSketch(Plane::xy());
 * sketch->addRectangle(QVector2D(0, 0), QVector2D(100, 50));
 * 
 * // 建立擠出
 * Extrude* extrude = doc->createExtrude(sketch, 10.0);
 * 
 * // 儲存
 * doc->save();
 * @endcode
 */
class Document : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString fileName READ fileName WRITE setFileName NOTIFY fileNameChanged)
    Q_PROPERTY(bool modified READ isModified NOTIFY modifiedChanged)
    Q_PROPERTY(int featureCount READ featureCount NOTIFY featureCountChanged)
    
public:
    /**
     * @brief 建構子
     */
    explicit Document(QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~Document() override;
    
    // 檔案操作
    QString fileName() const { return m_fileName; }
    void setFileName(const QString& fileName);
    
    bool isModified() const { return m_modified; }
    void setModified(bool modified);
    
    /**
     * @brief 儲存文件
     * @param fileName 檔案路徑（空則使用當前檔名）
     * @return 成功回傳 true
     */
    bool save(const QString& fileName = QString());
    
    /**
     * @brief 載入文件
     * @param fileName 檔案路徑
     * @return 成功回傳 true
     */
    bool load(const QString& fileName);
    
    /**
     * @brief 建立新文件（清除所有內容）
     */
    void newDocument();
    
    // 特徵管理
    /**
     * @brief 加入特徵到文件
     */
    void addFeature(Feature* feature);
    
    /**
     * @brief 移除特徵
     */
    void removeFeature(Feature* feature);
    
    /**
     * @brief 取得所有特徵
     */
    QList<Feature*> features() const { return m_features; }
    
    /**
     * @brief 取得特徵數量
     */
    int featureCount() const { return m_features.size(); }
    
    /**
     * @brief 根據 ID 尋找特徵
     */
    Feature* findFeature(const QString& id) const;
    
    /**
     * @brief 根據名稱尋找特徵
     */
    Feature* findFeatureByName(const QString& name) const;
    
    /**
     * @brief 取得特徵索引
     */
    int indexOf(Feature* feature) const;
    
    // 特徵建立便捷方法
    /**
     * @brief 建立草圖
     */
    Sketch* createSketch(class Plane* plane, const QString& name = QString());

    // ── 草圖複製／貼上（可跨檔案）────────────────────────────────────────────
    /// 剪貼簿的 MIME 型別（跨檔案、跨程式實體皆可貼上）
    static QString sketchClipboardMimeType() { return QStringLiteral("application/x-aicad-sketch+json"); }

    /**
     * @brief 把草圖匯出成可放進剪貼簿的 JSON。
     *
     * 內容：草圖完整 JSON（含平面幾何、幾何、約束、標註、本地參數），以及草圖運算式
     * 所引用到的「文件全域參數」（含其相依的全域參數）——貼到另一個檔案時，
     * 沒有這些全域參數會導致尺寸運算式無法求值。
     */
    QJsonObject exportSketchForCopy(const Sketch* sketch) const;

    /// payload 是否為 exportSketchForCopy() 產生的草圖資料
    static bool isSketchClipboardPayload(const QJsonObject& payload);

    /**
     * @brief 把 exportSketchForCopy() 的資料貼成本文件的一個新草圖。
     *
     * 一律產生新的 Feature id（不與來源衝突），名稱遇到重複會加「 - 複本」，
     * 來源文件缺少的全域參數會一併補進（已存在同名者不覆蓋）。
     * @return 新草圖；payload 無效時回傳 nullptr 並填入 error
     */
    Sketch* pasteSketch(const QJsonObject& payload, QString* error = nullptr);

    /**
     * @brief 建立草圖副本（Phase 3）
     * @param master     主草圖
     * @param plane      副本放置的平面
     * @param overrides  初始覆寫參數 name→value（可空）
     * @param name       副本名稱（空則自動命名）
     */
    class SketchInstance* createSketchInstance(
        Sketch* master,
        class Plane* plane,
        const QHash<QString, double>& overrides = {},
        const QString& name = QString(),
        bool rebuildNow = true);

    /**
     * @brief 批次更新 FeatureTree：begin/end 之間所有 treeStructureChanged() 合併
     *        成 end 時的「一次」發射（可巢狀）。沿線斷面陣列一次建立上百個測站
     *        節點時，避免 UI 樹狀圖重建上百次。
     */
    void beginTreeBatch();
    void endTreeBatch();

    /**
     * @brief 依 feature->featureDependencies() 重新登記它在依賴圖中「依賴誰」。
     *        別的特徵對它的依賴（例如 Loft 依賴陣列）保持不變。
     *        用於特徵建立後才改變依賴對象的情形（沿線陣列換了 profile）。
     */
    void refreshFeatureDependencies(Feature* feature);

    /**
     * @brief 建立擠出
     */
    Extrude* createExtrude(Sketch* sketch, double height, const QString& name = QString());

    /**
     * @brief 建立沿線斷面草圖陣列（ProfileArrayAlongAlignment 規劃 §2.2）
     * @param master         斷面 master Sketch（含 cant/H 具名參數）
     * @param tcl            佈置所依循的 TrackCenterLine
     * @param startChainage  起始樁號 [m]
     * @param endChainage    終止樁號上限 [m]（實際最後一站可能小於此值，見 §2.2）
     * @param interval       樁號間距 [m]
     * @param name           特徵名稱（空則自動命名）
     */
    AlignedProfileArray* createAlignedProfileArray(
        Sketch* master,
        railway::TrackCenterLine* tcl,
        double startChainage,
        double endChainage,
        double interval,
        const QString& name = QString(),
        bool autoInterval = false);

    /// 道版 E,N 平面圖（把 Loft 上方曲面的邊緣投影到隨 cant 變化的軌道面後展平，沿線形繪製）。
    /// 整份文件只有一個、列在 root 底下，由 syncSlabPlan() 自動建立／移除，不需要手動呼叫。
    TrackSlabPlan* createTrackSlabPlan();
    void syncSlabPlan();

    /// 定義平面：基準平面（"XY"／"XZ"／"YZ" 或另一個 DefinedPlane 的 id）沿其法線偏移 offsetExpr（可用參數）。
    /// 例：createDefinedPlane("XY", "-90") = XY 平面往 -Z 方向 90 單位。失敗回傳 nullptr 並在 error 說明。
    /// 更新既有定義平面的基準／偏移／名稱（含樹狀圖名稱），重建並標記已修改。已建立的草圖不會跟著移動。
    bool updateDefinedPlane(DefinedPlane* dp, const QString& baseRef, const QString& offsetExpr,
                            const QString& name, QString* error = nullptr);
    /// 以完整的平面定義（任一種方式）新增／更新
    bool updateDefinedPlane(DefinedPlane* dp, const PlaneDef& def, const QString& name, QString* error = nullptr);
    DefinedPlane* createDefinedPlane(const PlaneDef& def, const QString& name, QString* error = nullptr);
    DefinedPlane* createDefinedPlane(const QString& baseRef, const QString& offsetExpr,
                                     const QString& name = QString(), QString* error = nullptr);

    /// 3D 組件安裝：createComponentInstall() 建立空物件 → 呼叫端 setSource()／setPlacements() → addComponentInstall()
    ComponentInstall* createComponentInstall(const QString& name = QString());
    void addComponentInstall(ComponentInstall* ci);
    /// 組件的安裝位置被編輯（或組件被刪除）後：重算組件、並重建所有 Loft 重新開孔
    void applyComponentChange(ComponentInstall* ci);
    void rebuildLoftsForComponents();

    /**
     * @brief 將 AlignedProfileArray 各測站輪廓放樣成實體（Step 9）
     */
    ProfileLoftSolid* createProfileLoftSolid(
        AlignedProfileArray* sourceArray,
        const QString& name = QString(),
        const QString& segmentId = QString());   ///< 負責的分段 id（每段一個 Loft）

    /**
     * @brief 對來源實體 Feature（Loft/Extrude 等）的選取邊建立 Chamfer（倒角）特徵。
     * @param source   來源實體 Feature
     * @param distance 倒角距離（單一值，套用於所有選取邊 — 見 ChamferSolid.h 設計決議）
     * @param edges    選取邊的幾何簽章（見 EdgeSignature）
     * @param name     特徵名稱（空則自動命名）
     */
    ChamferSolid* createChamferSolid(
        Feature* source,
        double distance,
        const QVector<EdgeSignature>& edges,
        const QString& name = QString());
    
    /**
     * @brief 重建所有特徵
     */
    void rebuildAll();
    
    /**
     * @brief 重建指定特徵及其依賴特徵
     */
    void rebuildFeature(Feature* feature);
    
    // OCAF 綁定
    Handle(TDocStd_Document) ocafDocument() const { return m_ocafDoc; }
    
    /**
     * @brief 從 OCAF 文件同步資料
     */
    void syncFromOCAF();
    
    /**
     * @brief 將資料同步到 OCAF 文件
     */
    void syncToOCAF();

    // ✅ 新增：參考幾何管理
    /**
     * @brief 初始化參考幾何（原點、軸、平面）
     * @param context AIS 上下文（用於顯示）
     */
    void initializeReferenceGeometry(const Handle(AIS_InteractiveContext)& context);

    /**
     * @brief 取得參考幾何列表
     */
    QList<ReferenceGeometry> referenceGeometries() const { return m_referenceGeometries; }

    /**
     * @brief 取得特定類型的參考幾何
     */
    ReferenceGeometry* getReferenceGeometry(ReferenceGeometryType type);

    /**
     * @brief 設定參考幾何的可見性
     */
    void setReferenceGeometryVisible(ReferenceGeometryType type, bool visible);

    /**
     * @brief 設定參考幾何的可選擇性
     */
    void setReferenceGeometrySelectable(ReferenceGeometryType type, bool selectable);

    void displayFeature(Feature* feature,
                                  const Handle(AIS_InteractiveContext)& context);

    /**
     * @brief 顯示所有參考幾何
     */
    void showAllReferenceGeometry();

    /**
     * @brief 隱藏所有參考幾何
     */
    void hideAllReferenceGeometry();

    void onVisibilityChanged(const QVariantMap& data);

    /**
     * @brief 取得 AIS 上下文
     */
    Handle(AIS_InteractiveContext) aisContext() const { return m_aisContext; }

    /**
     * @brief 取得 Feature Tree 結構
     */
    QVector<ui::FeatureTreeItem> getFeatureTreeItems() const;

    /**
     * @brief 更改指定 tree item 的父節點（例如 SketchInstance 依規劃書 §2.5
     *        改掛在其所屬的 AlignedProfileArray 底下，而非 master sketch）。
     *        找不到對應 id 時什麼都不做。呼叫後會發出 treeStructureChanged()。
     */
    void setTreeItemParent(const QString& itemId, const QString& newParentId);

    /**
     * @brief 更改指定 tree item 的顯示名稱（例如 AlignedProfileArray 每次
     *        rebuild() 後把測站數量寫進節點名稱）。呼叫後會發出 treeStructureChanged()。
     */
    void setTreeItemName(const QString& itemId, const QString& newName);

    /**
     * @brief 初始化原點幾何（包含 tree 結構）
     */
    void initializeOrigin(const Handle(AIS_InteractiveContext)& context);

    // 新增 public method
    void displayAllFeatures(const Handle(AIS_InteractiveContext)& context);

    aicad::core::ParameterStore* parameterStore() { return m_parameterStore; }

    /**
     * 重建從 changedFeature 往下受影響的所有特徵
     * （依拓撲順序，只重建 dirty 者）
     */
    void rebuildFrom(Feature* changedFeature);

    // ✅ View state save/load
    void setViewState(const QJsonObject& state) { m_viewState = state; }
    QJsonObject viewState() const { return m_viewState; }

    // ── 圖紙（Drawing Sheets）passthrough ───────────────────────────────────
    /** 圖紙以 JSON 陣列內嵌於 .aicad（key = "drawingSheets"）。
     *  cad 層不認識 drawing 模組，只負責原樣保存／讀回；
     *  DrawingSheetManager 於 aboutToSave() 時寫入最新內容。 */
    void       setDrawingSheetsData(const QJsonArray& data) { m_drawingSheetsData = data; }
    QJsonArray drawingSheetsData() const { return m_drawingSheetsData; }

    /** 目前在 View 中顯示的機廠矩形視圖（key＝"<bookId>#<序號>"；不隨檔案保存）。 */
    QSet<QString>& shownDepotViews() { return m_shownDepotViews; }
    const QSet<QString>& shownDepotViews() const { return m_shownDepotViews; }

    /** Passthrough blob for AlignmentDocument — written/read by UIManager */
    void setAlignmentData(const QJsonObject& data) { m_alignmentData = data; }
    QJsonObject alignmentData() const { return m_alignmentData; }

    // ── Per-TCL alignment edit data ───────────────────────────────────────────
    /** Store the AlignmentDocument edit-session JSON for a specific TCL. */
    void        setTclAlignmentData(const QString& tclId, const QJsonObject& data);
    QJsonObject tclAlignmentData(const QString& tclId) const;
    QStringList tclAlignmentDataIds() const;

    // ── TrackCenterLine 管理 ───────────────────────────────────────────────
    railway::TrackCenterLine* addTrackCenterLine(const QString& name = QString());
    void removeTrackCenterLine(const QString& id);
    railway::TrackCenterLine* findTrackCenterLine(const QString& id) const;
    const QList<railway::TrackCenterLine*>& trackCenterLines() const { return m_trackCenterLines; }
    /// Railway 資料夾 eyeOpen（彙總 3D Alignment 顯示中）
    bool railway3DVisible() const { return m_railway3DVisible; }

    cad::Extrude* lastExtrude() const;

    // 復原/重做（未來實作）
    // bool canUndo() const;
    // bool canRedo() const;
    // void undo();
    // void redo();

Q_SIGNALS:
    /**
     * @brief save() 開始序列化之前發出（同步）。外部模組（如圖紙管理器）
     *        可在此把最新資料寫回 setXxxData()。
     */
    void aboutToSave();

    /**
     * @brief 檔名改變時發出
     */
    void fileNameChanged(const QString& fileName);
    
    /**
     * @brief 修改狀態改變時發出
     */
    void modifiedChanged(bool modified);
    
    /**
     * @brief 特徵被加入時發出
     */
    void featureAdded(Feature* feature);
    
    /**
     * @brief 特徵即將被移除時發出
     */
    void featureAboutToBeRemoved(Feature* feature);
    
    /**
     * @brief 特徵被移除後發出
     */
    void featureRemoved();
    
    /**
     * @brief 特徵數量改變時發出
     */
    void featureCountChanged(int count);
    
    /**
     * @brief 文件即將被關閉時發出
     */
    void aboutToClose();
    
    /**
     * @brief 重建開始時發出
     */
    void rebuildStarted();
    
    /**
     * @brief 重建完成時發出
     */
    void rebuildFinished(bool success);

    // ✅ 新增：參考幾何信號
    void referenceGeometryInitialized();
    void referenceGeometryVisibilityChanged(ReferenceGeometryType type, bool visible);

    /**
     * @brief Feature Tree 結構改變
     */
    void treeStructureChanged();

    void allFeaturesLoaded();

    void originReinitializationNeeded();   // ✅ UIManager will call initializeOrigin()

    void featureShapeUpdated(Feature* feature);

    void trackCenterLinesChanged();

private Q_SLOTS:
    /**
     * @brief 處理特徵重建請求
     */
    void onFeatureRebuildRequested();
    
    /**
     * @brief 處理特徵改變
     */
    void onFeatureChanged();

private:
    /**
     * @brief 初始化 OCAF 文件
     */
    void initOCAF();
    
    /**
     * @brief 清除所有特徵
     */
    void clearFeatures();

    // ✅ 新增：建立參考幾何的內部方法
    void createOriginPoint();
    void createAxes();
    void createReferencePlanes();

    QString m_fileName;                      ///< 檔案名稱
    bool m_modified;                         ///< 修改標記
    
    QList<Feature*> m_features;              ///< 特徵列表
    
    Handle(TDocStd_Document) m_ocafDoc;      ///< OCAF 文件
    
    int m_nextFeatureNumber;                 ///< 下一個特徵編號（用於自動命名）
    void emitTreeStructureChanged();
    int  m_treeBatchDepth   = 0;
    bool m_treeBatchPending = false;
    QString m_componentSig;            ///< 上次同步時的 ComponentInstall id 清單（變了 → 重建 Loft）
    bool m_slabSyncPending  = false;   ///< 道版圖同步已排程（合併多次 featureAdded/Removed）
    void scheduleSlabSync();

    // ✅ 新增：參考幾何和 AIS 上下文
    QList<ReferenceGeometry> m_referenceGeometries;
    Handle(AIS_InteractiveContext) m_aisContext;

    /**
     * @brief 建立原點資料夾項目
     */
    void createOriginFolderItems();

    QVector<ui::FeatureTreeItem> m_treeItems;  ///< Tree 項目列表

    // 新增 private method
    void addFeatureInternal(Feature* feature);
    void rebuildFeatureTreeItems();

    void continueLoad(const QString& jsonData);     // ✅ actual JSON parsing, called after re-init

    QString m_pendingLoadData;
    QString m_pendingLoadFile;

    aicad::core::ParameterStore* m_parameterStore;
    DependencyGraph              m_depGraph;
    QSet<QString> m_shownDepotViews;
    QJsonArray  m_drawingSheetsData;  // 圖紙 passthrough（見 setDrawingSheetsData）
    QJsonObject m_viewState;       // ✅ 儲存視圖狀態（camera eye/at/up/scale）
    QJsonObject m_alignmentData;   // legacy passthrough — kept for compatibility
    QHash<QString, QJsonObject> m_tclAlignmentData;  // per-TCL edit session JSON

    QList<railway::TrackCenterLine*> m_trackCenterLines;  ///< 線路中心線列表

    // ── Railway 資料夾 3D Alignment 彙總顯示 ──────────────────────────────
    /** Railway 資料夾節點目前是否為 eyeOpen（彙總 3D Alignment 顯示中）。 */
    bool m_railway3DVisible = false;

    /** eyeOpen 期間暫時關閉之前，各 TCL 原本的 hAlign/vAlign 可見狀態快照，
     *  供 eyeClose 時還原（key = TrackCenterLine::id()）。 */
    struct RailwayChildVisSnapshot { bool hAlign = false; bool vAlign = false; };
    QHash<QString, RailwayChildVisSnapshot> m_railwaySavedChildVisibility;

    Q_DISABLE_COPY(Document)
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_DOCUMENT_H