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

#include <QObject>
#include <QString>
#include <QList>
#include <QJsonObject>
#include <TDocStd_Document.hxx>
#include <AIS_Shape.hxx>
#include <AIS_InteractiveObject.hxx>

namespace aicad {
namespace cad {

class Feature;
class Sketch;
class Extrude;

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
        const QString& name = QString());

    /**
     * @brief 建立擠出
     */
    Extrude* createExtrude(Sketch* sketch, double height, const QString& name = QString());
    
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

    cad::Extrude* lastExtrude() const;

    // 復原/重做（未來實作）
    // bool canUndo() const;
    // bool canRedo() const;
    // void undo();
    // void redo();

Q_SIGNALS:
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
    QJsonObject m_viewState;       // ✅ 儲存視圖狀態（camera eye/at/up/scale）
    QJsonObject m_alignmentData;   // legacy passthrough — kept for compatibility
    QHash<QString, QJsonObject> m_tclAlignmentData;  // per-TCL edit session JSON

    QList<railway::TrackCenterLine*> m_trackCenterLines;  ///< 線路中心線列表

    Q_DISABLE_COPY(Document)
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_DOCUMENT_H