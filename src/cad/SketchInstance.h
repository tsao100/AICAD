#pragma once
#include "Feature.h"
#include "Sketch.h"
#include "../core/ParameterStore.h"
#include <QList>
#include <QHash>
#include <QSet>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
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

    // ── 雙精度放置座標系 ────────────────────────────────────────────────
    /**
     * @brief 放置座標系（double）。
     *
     * Plane 以 float QVector3D 儲存原點，座標在 1e5 m 量級（例如 TM2 本地座標
     * 30882, 113652）時解析度只有約 4~8 mm，而斷面本身只有幾十 mm，會造成
     * 肉眼可見的頂點抖動與變形。設定 frame 後，wire 一律以這組雙精度座標系
     * 建立，Plane 只作為 UI 顯示／相容用途。
     *   世界座標 = origin + X * u + Y * v     （u,v 為 master 草圖座標 × scale）
     */
    struct PlacementFrame {
        double ox = 0, oy = 0, oz = 0;   ///< 原點
        double xx = 1, xy = 0, xz = 0;   ///< X 軸（單位向量）
        double yx = 0, yy = 1, yz = 0;   ///< Y 軸（單位向量）
        bool   valid = false;
    };
    void setFrame(const PlacementFrame& f) { m_frame = f; m_frame.valid = true; }
    const PlacementFrame& frame() const { return m_frame; }

    /**
     * @brief 由外部（AlignedProfileArray）統一驅動重建。
     *
     * true 時：master 重建、instance store 參數變動、Document 相依傳播所觸發的
     * 自動 rebuild() 一律略過（直接回傳 true），改由擁有者呼叫 rebuildWith()。
     * 否則同一次陣列重建中，每個測站會被 master 訊號、參數重算訊號、相依傳播、
     * 陣列自己各解算一次。
     */
    void setExternallyDriven(bool on) { m_externallyDriven = on; }
    bool externallyDriven() const { return m_externallyDriven; }

    /**
     * @brief 以「參數簽章」為鍵的快速重建。
     * @param signature     本站 override 參數值的簽章（相同簽章 ⇒ 相同 2D 解）
     * @param donor         已用相同簽章求解過的另一站（可為 nullptr）；有的話
     *                      直接複製其解，不再求解。
     * @param forceResolve  master 改變時為 true：忽略既有解，一律重新求解。
     *
     * 簽章與上次相同且非 force → 只重建 3D wire（只有放置位置變了）。
     * 呼叫端須先設定好 instance store 的 override 與 setFrame()。
     */
    bool rebuildWith(const QString& signature,
                     const SketchInstance* donor,
                     bool forceResolve);
    QString solveSignature() const { return m_solveSig; }

    /**
     * @brief 最近一次求解是否「退回未求解的 master 幾何」（求解器 Conflict／發散）。
     *
     * 這種情況下本站形狀不會反映新的參數值（等於「套用後圖形沒更新」），
     * 但 rebuildWith() 仍回傳 true（形狀可建），所以由這個旗標讓擁有者
     * （AlignedProfileArray）得以回報。
     */
    bool solveFellBack() const { return m_solveFellBack; }

    /**
     * @brief 建立「本站實際套用值」的唯讀檢視草圖（呼叫端擁有，用完須 delete）。
     *
     * 與編輯草圖看到的畫面一致（master 草圖平面、草圖單位），但內容是本站
     * 求解後的幾何，以及以本站 instance 覆寫值重新求值的尺寸／約束／標註，
     * 因此尺寸標註顯示的是本站實際套用的數值。此草圖不屬於 Document 的
     * features，不會被存檔，也不會參與任何相依傳播。
     * master 未設定或尚未求解時回傳 nullptr。
     */
    Sketch* createViewSketch() const;

    /**
     * @brief 斷面中目前「無法組成 Loft 輪廓」的幾何種類（樣條、橢圓等，非建構線）。
     * 支援的有 Line／Polyline／Arc／Circle；其他幾何不會被納入輪廓，因此含有它們的
     * 斷面可能湊不成封閉迴圈。供 Loft 失敗時給出明確提示。
     */
    QStringList unsupportedProfileGeometry() const;

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
        const QList<SketchGeometry*>& geoms) const;

    /// (u,v)=已乘 scale 的草圖座標 → 世界座標（優先使用雙精度 frame）
    gp_Pnt toWorld(double u, double v) const;

    bool solveFromMaster();                       ///< 深拷貝 master + 求值 + 求解（含失敗退回）
    bool adoptSolution(const SketchInstance& d);  ///< 複製 donor 已求解的幾何
    bool buildShape();                            ///< 由 m_geomClones 建 wire / compound

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
    PlacementFrame                  m_frame;
    bool                            m_externallyDriven = false;
    QString                         m_solveSig;   ///< 目前 m_geomClones 對應的參數簽章（空 = 未知）
    bool                            m_solveFellBack = false;  ///< 最近一次求解退回 master 原始幾何

    QMetaObject::Connection         m_masterRebuildConn;
    QMetaObject::Connection         m_masterDestroyConn;

    // 序列化時暫存（fromJson post-pass 用）
    QString m_pendingMasterSketchId;
    QString m_pendingPlaneId;
};

} // namespace aicad::cad
