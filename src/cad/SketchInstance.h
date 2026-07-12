#pragma once
#include "Feature.h"
#include "Sketch.h"
#include "../core/ParameterStore.h"
#include <QList>
#include <QHash>
#include <QSet>
#include <TopoDS_Wire.hxx>
#include <AIS_Shape.hxx>

namespace aicad::cad {

/**
 * @brief 草圖副本（SketchInstance）
 *
 * 參考一個 master Sketch，使用自身的 instance ParameterStore
 * 覆寫部分參數後重建幾何，產生獨立的 TopoDS_Shape。
 *
 * 特性：
 * - 幾何結構與 master 相同（相同的約束拓撲）
 * - 尺寸由 instance paramStore 決定（未覆寫的回退 master store）
 * - 可放置在不同的 Plane 上
 * - 不修改 master Sketch 的任何資料
 *
 * 使用範例：
 * @code
 * SketchInstance* inst = doc->createSketchInstance(master, xzPlane, {{"width", 200}});
 * inst->parameterStore()->setLocal("depth", 80.0);
 * @endcode
 */
class SketchInstance : public Feature {
    Q_OBJECT
public:
    explicit SketchInstance(Document* parent = nullptr);
    ~SketchInstance() override;

    FeatureType type()       const override { return FeatureType::SketchInstance; }
    QString     typeString() const override { return QStringLiteral("SketchInstance"); }

    // ── Master 草圖 ──────────────────────────────────────────────────────
    void    setMasterSketch(Sketch* master);
    Sketch* masterSketch() const { return m_master; }
    QString masterSketchId() const;

    // ── 放置平面 ─────────────────────────────────────────────────────────
    void   setPlane(Plane* plane);
    Plane* plane() const { return m_plane; }

    /**
     * @brief master 草圖座標 → 本副本世界座標的等比縮放係數（例如 master 以
     *        mm 繪製、要對齊以 m 為單位的 TrackCenterLine 時設為 0.001）。
     *        僅影響本副本輸出的幾何（buildWires()/orderedWireFrom()），
     *        不動 master 本身；預設 1.0（不縮放，與既有行為相容）。
     */
    void   setScale(double scale) { m_scale = scale; }
    double scale() const { return m_scale; }

    // ── Instance ParameterStore（本地覆寫）──────────────────────────────
    /**
     * 取得 instance 層的 ParameterStore。
     * parent 已設為 master->parameterStore()。
     * 直接呼叫 store->setLocal("width", 200) 即可覆寫。
     */
    aicad::core::ParameterStore* parameterStore() { return m_instanceStore; }

    /** 列出 instance 中已有覆寫值的參數名 */
    QStringList overriddenParams() const;

    /** 清除某個覆寫（恢復使用 master 預設） */
    void clearOverride(const QString& paramName);

    /** 清除所有覆寫 */
    void clearAllOverrides();

    /** 判斷某個參數是否被本 instance 覆寫 */
    bool isOverridden(const QString& paramName) const;

    // ── 重建 ─────────────────────────────────────────────────────────────
    /**
     * 以 m_instanceStore 為參數上下文，
     * 從 master 複製幾何並重新求解約束，
     * 產生此副本的 TopoDS_Shape。
     */
    bool rebuild() override;

    // ── 副本幾何（唯讀，供 Extrude 等使用）────────────────────────────
    QList<TopoDS_Wire> wires() const { return m_wires; }
    bool hasClosedProfile() const;

    /**
     * @brief 以 master 中指定 SketchPoint 的 UUID 為起點，固定繞線方向輸出
     *        封閉 wire（供 Loft 等對繞線起點/方向敏感的操作使用）。
     *
     * 直接用 master UUID 呼叫（不需呼叫端自行換算成 clone UUID）：內部透過
     * rebuild() 時記錄的 master→clone UUID 映射換算成此副本對應的幾何，
     * 再依已求解後的座標比對做圖走訪，找出封閉迴圈並以該起點、原始繪製方向
     * 輸出單一 TopoDS_Wire。若起點找不到對應幾何或迴圈不封閉，回傳空 wire。
     *
     * 目前僅支援由 Line／封閉 Polyline 組成的輪廓（與 buildWires() 目前的
     * 支援範圍一致）。
     */
    TopoDS_Wire orderedWireFrom(const QString& masterPointUuid) const;

    /**
     * @brief 自動偵測並回傳草圖中「每一個」互不相連的封閉輪廓（例如同一個
     *        斷面草圖裡有左右兩個獨立墊塊的情況）。不需要指定起點 UUID：
     *        走訪順序固定依 m_geomClones 的原始幾何順序決定，master 每次
     *        clone 出來的順序都相同，因此各測站之間的迴圈順序、繞行方向都
     *        會保持一致，可直接按索引與其他測站的對應迴圈配對放樣。
     *        目前僅支援由 Line／封閉 Polyline 組成的輪廓（與 buildWires()
     *        現有支援範圍一致）。
     */
    QList<TopoDS_Wire> allClosedWires() const;

    // ── 序列化 ───────────────────────────────────────────────────────────
    QJsonObject toJson()  const override;
    bool fromJson(const QJsonObject& json) override;

    // ── Feature 依賴 ─────────────────────────────────────────────────────
    QSet<QString> featureDependencies() const override;

Q_SIGNALS:
    void instanceRebuilt();
    void overrideChanged(const QString& paramName);

private Q_SLOTS:
    void onMasterRebuilt();         ///< 當 master 重建後，副本也重建
    void onMasterAboutToDestroy();  ///< master 刪除時警告

private:
    /// 從 master 深拷貝幾何元素（不共用指標）
    QList<SketchGeometry*> cloneGeometries(
        const Sketch* master,
        QHash<QString, QString>& uuidRemap) const;

    /// 拷貝 master 約束（UUID 映射到 clone 的幾何）
    QList<SketchConstraint> cloneConstraints(
        const Sketch* master,
        const QHash<QString, QString>& uuidRemap) const;

    /// 將求解後的 2D 幾何在 m_plane 的座標系下建立 3D Wires
    QList<TopoDS_Wire> buildWires(
        const QList<SketchGeometry*>& geoms,
        Plane* plane) const;

    Sketch*                         m_master        = nullptr;
    Plane*                          m_plane         = nullptr;
    double                          m_scale         = 1.0;
    aicad::core::ParameterStore*    m_instanceStore = nullptr;
    ConstraintSolver                m_solver;

    // 副本幾何（rebuild() 填入）
    QList<SketchGeometry*>          m_geomClones;
    QList<SketchConstraint>         m_constraintClones;
    QList<TopoDS_Wire>              m_wires;

    /// master 幾何 UUID → 本副本 clone 幾何 UUID 的映射（rebuild() 時填入），
    /// 供 orderedWireFrom() 換算「master 起點 UUID」用。
    QHash<QString, QString>         m_uuidRemap;

    QMetaObject::Connection         m_masterRebuildConn;
    QMetaObject::Connection         m_masterDestroyConn;

    // 序列化時暫存（fromJson post-pass 用）
    QString m_pendingMasterSketchId;
    QString m_pendingPlaneId;
};

} // namespace aicad::cad
