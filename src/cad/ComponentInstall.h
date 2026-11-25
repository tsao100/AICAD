/**
 * @file ComponentInstall.h
 * @brief 3D 組件安裝（Component Installation）：把外部 *.aicad 內的「3D 組件」與其「所需空間」
 *        依超高（cant）定位到線形的多個里程位置，並讓道版 Loft 在所需空間處開孔。
 *
 * 組件局部座標系（組件在來源 *.aicad 中的座標；斷面草圖在 XY 平面、其 Z 軸 = -T）：
 *   原點 = 該里程中心線上、軌道面（cant 直線）的點
 *   +X   = 與 cant 直線貼合、朝右手邊鋼軌（面向里程增加方向）
 *   +Y   = 草圖 -Z 方向（= 沿線、里程增加方向 T）
 *   +Z   = X × Y（垂直軌道面朝上；與一般 CAD「Z 向上」的建模習慣一致）
 *
 * 定位（每個安裝里程 p）：
 *   1. 在涵蓋 p 的兩個相鄰測站取各自的軌道面直線（findCantLineUV，斷面座標），
 *      依里程線性內插 → 該里程的軌道面中心點 A(p) 與傾角方向 D(p)（即超高的傾斜）
 *   2. 以 AlignedProfileArray::frameAt(p) 取得該里程精確的放置座標系（含曲線與縱坡）
 *   3. 組件座標系 = { 原點 A(p) 的世界座標 + 橫向偏移、X = D(p)、Y = -（座標系 Z）= T、Z = X × Y }
 *      （「flip180」= 繞軌道面法線轉 180°，即組件的 X、Z 同時反向）
 *
 * 道版開孔：ProfileLoftSolid／ChamferSolid 重建完成後呼叫 cutSpaces()，把本文件所有
 * ComponentInstall 在該 Loft 里程範圍內的「所需空間」放置後從實體中扣除。
 */
#pragma once

#include "Feature.h"
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QList>
#include <QMetaObject>
#include <QPointer>
#include <QStringList>
#include <QVector>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

namespace aicad {
namespace cad {

class AlignedProfileArray;
class Sketch;

class ComponentInstall : public Feature {
    Q_OBJECT
public:
    /// 一個安裝位置
    struct Placement {
        QString tclId;            ///< 沿哪條線路（空字串 = 任何涵蓋該里程的陣列）
        double  chainage = 0.0;   ///< 安裝里程 [m]（與斷面陣列使用的里程相同）
        double  lateral  = 0.0;   ///< 沿軌道面的橫向偏移 [m]（+ = 右手邊）
        bool    flip180  = false; ///< 繞軌道面法線旋轉 180°
        // 沿線形安裝時如何使用來源檔的內容（來源檔 = Part＋Assembly 組成「組件」，Void 組成「開孔」）：
        bool    installAssembly = true;   ///< 在此里程安裝組件（Part／Assembly 的實體）
        bool    cutVoid         = true;   ///< 在此里程用 Void 對道版 Loft 開孔
    };

    explicit ComponentInstall(Document* parent = nullptr);
    ~ComponentInstall() override = default;

    FeatureType type()       const override { return FeatureType::ComponentInstall; }
    QString     typeString() const override { return QStringLiteral("ComponentInstall"); }

    // ── 組件資料（來自外部 *.aicad；座標為組件局部座標）──────────────────
    void setSource(const QString& sourcePath,
                   const TopoDS_Shape& component, const QStringList& componentNames,
                   const TopoDS_Shape& space,     const QStringList& spaceNames,
                   const QStringList& componentIds = QStringList(),
                   const QStringList& spaceIds     = QStringList());
    QString             sourcePath()     const { return m_sourcePath; }
    /// 組件座標的單位換算：世界座標（m）= 組件座標 × unitScale。*.aicad 與 Sketch 預設為 mm → 0.001。
    double              unitScale()      const { return m_unitScale; }
    void                setUnitScale(double s) { if (s > 0.0) m_unitScale = s; }
    QStringList         componentNames() const { return m_componentNames; }
    QStringList         spaceNames()     const { return m_spaceNames; }
    const TopoDS_Shape& componentShape() const { return m_component; }
    const TopoDS_Shape& spaceShape()     const { return m_space; }

    // ── 由 *.aicad 取出「組件」與「void」──────────────────────────────────
    //
    // 組件檔內「所有 3D 特徵的組合結果」：
    //   • 歸屬 Feature Browser「Void」根的 3D 特徵（Feature::isVoid）→ 合成「void」：
    //     道版 Loft 在安裝位置要挖除（差集）的空間。Void 不一定要有。
    //     （舊檔沒有歸屬欄位時，名稱為 void／void1／void_2… 者視為 Void；見 Feature::isVoidName）
    //   • 其餘（「Assembly」根底下的擠出等造型特徵與插入的零件；只取終端結果，被其他特徵消耗的
    //     中間實體不重複計入）→ 合成「組件實體」：顯示在安裝位置。
    static bool isVoidName(const QString& featureName);
    static bool extractFromDocument(const Document* src,
                                    TopoDS_Shape& component, QStringList& componentNames,
                                    TopoDS_Shape& voidShape, QStringList& voidNames,
                                    QString* error = nullptr);

    // ── 草圖對位預覽 ───────────────────────────────────────────────────────
    /**
     * @brief 組件與所約束草圖「對上的情形」：把組件（與 void）依主草圖中組件座標線目前的解，
     *        放到「草圖所在位置、草圖單位（mm，不縮放）」。草圖 eyeOpen 時一併 3D 顯示，
     *        用來檢視組件原點／軸線與草圖幾何（cant 直線等）的約束結果。
     * @return 沒有座標線／草圖時為 false
     */
    bool sketchPreview(TopoDS_Shape& componentOut, TopoDS_Shape& voidOut, QString* error = nullptr) const;

    // ── 與來源 *.aicad 同步 ───────────────────────────────────────────────
    /// 來源檔最後修改時間晚於上次同步 → true
    bool sourceIsNewer() const;
    /**
     * @brief 重新讀取來源 *.aicad，更新組件與所需空間（依實體 id、其次依名稱對應原先指定的角色）。
     *        只更新資料；呼叫端接著 Document::applyComponentChange() 重算並讓 Loft 重新開孔。
     * @return 成功與否；failure 時原資料不變
     */
    bool syncFromSource(QString* error = nullptr);
    /// 開啟／關閉對來源檔的自動監看（檔案被存檔 → 自動同步並重新開孔）
    void setAutoSync(bool on);
    bool autoSync() const { return m_autoSync; }
    /// 讀檔後呼叫：重新建立監看；來源檔較新時自動同步一次。回傳是否有同步。
    bool attachSourceWatcher();

    // ── 組件座標線：與斷面草圖（Sketch）建立約束關係 ───────────────────────
    //
    // 組件的「原點／軸線／平面」以兩條 Centerline 角色的線畫在主草圖中：
    //   X 軸線（起點 = 組件原點，方向 = 組件 +X，貼合 cant 直線）、
    //   Z 軸線（共用起點，方向 = 組件 +Z，垂直軌道面朝上）。
    //   組件 +Y = 草圖 -Z（垂直於草圖平面、沿線），無法畫在 2D 草圖中，由定位自動給定。
    // 它們就是一般的草圖幾何：可用草圖的所有約束（重合、平行、共線、垂直、距離、角度…）
    // 與草圖軸線／原點／建構線（包含 cant 直線）互相約束，尺寸運算式可直接用 cant 參數。
    // 組件的安裝位置與轉角 = 各測站「求解後」這組座標線的位置與方向（依里程內插）。
    // XY 平面 ≙ X 軸線的跡線、YZ 平面 ≙ Z 軸線的跡線；XZ 平面恆平行於斷面平面（法線 Y = 沿線方向）。
    bool    hasFrame() const { return !m_frameXLine.isEmpty(); }
    QString frameXLineUuid() const { return m_frameXLine; }
    QString frameZLineUuid() const { return m_frameZLine; }
    Sketch* frameSketch() const;

    /**
     * @brief 在主草圖建立組件座標線，並加上預設約束（原點與 cant 直線起點重合、X 軸線與 cant 直線平行、
     *        X ⟂ Y），讓組件預設就貼在軌道面上、隨超高轉動。之後可在草圖中改約束。
     */
    bool createFrameInSketch(Sketch* master, QString* error = nullptr, QString* note = nullptr);

    // ── 安裝位置 ───────────────────────────────────────────────────────
    QVector<Placement> placements() const { return m_placements; }
    void setPlacements(const QVector<Placement>& p) { m_placements = p; }

    /// 某個安裝位置的「組件局部 → 世界」變換（在文件所有斷面陣列中找涵蓋該里程者）
    bool computeTrsf(const Placement& pl, gp_Trsf& out, QString* err = nullptr) const;

    /// 在指定陣列的里程範圍 [pMin, pMax] 內、放置後的「所需空間」實體清單（arr 為空 = 不限陣列）
    QVector<TopoDS_Shape> placedSpaces(const AlignedProfileArray* arr, double pMin, double pMax) const;

    /**
     * @brief 把本文件所有 ComponentInstall 的所需空間（在 arr 的 [pMin, pMax] 內）從 solid 扣除。
     *        沒有適用的空間、或布林運算失敗 → 回傳原 solid（失敗原因寫入 warn）。
     */
    static TopoDS_Shape cutSpaces(Document* doc, const TopoDS_Shape& solid,
                                  const AlignedProfileArray* arr, double pMin, double pMax,
                                  QString* warn = nullptr);

    /// 重新接上文件中各斷面陣列的 shapeChanged（陣列變了 → 重算放置後的組件）
    void refreshSources();
    void requestRebuild();      ///< 排程（合併多次請求）重建
    void resolveReferences(Document* doc);     ///< 讀檔後解析 masterSketchId

    bool rebuild() override;    ///< 計算放置後的組件（供顯示）；不會觸發 Loft 重建

    QJsonObject toJson()  const override;
    bool fromJson(const QJsonObject& json) override;

    /// 刻意回傳空集合：被其他 Feature「依賴」的 Feature 會被 CadView 視為已被消耗而隱藏。
    QSet<QString> featureDependencies() const override { return {}; }

private:
    bool trsfInArray(const AlignedProfileArray* arr, const Placement& pl,
                     gp_Trsf& out, QString* err) const;
    double spaceHalfLength() const;      ///< 所需空間在組件 Z 方向（沿線）的最大延伸 [m]


    double       m_unitScale = 0.001;                ///< 組件／草圖單位 → 世界單位 m（預設 mm → m = 1/1000）
    QString      m_sourcePath;
    QStringList  m_componentNames, m_spaceNames;
    QStringList  m_componentIds,   m_spaceIds;     ///< 來源檔內實體的 Feature id（同步時優先以 id 對應）
    QDateTime    m_sourceMTime;                    ///< 上次同步時來源檔的修改時間
    bool         m_autoSync = true;
    QFileSystemWatcher* m_watcher = nullptr;
    QString      m_masterSketchId;                 ///< 座標線所在的主草圖
    QPointer<Sketch> m_masterSketch;
    QString      m_frameXLine, m_frameZLine;       ///< 主草圖中 X／Z 軸線的 uuid
    TopoDS_Shape m_component, m_space;
    QVector<Placement> m_placements;

    QList<QMetaObject::Connection> m_conns;
    QString                        m_arraySig;
    bool                           m_rebuildPending = false;
};

} // namespace cad
} // namespace aicad
