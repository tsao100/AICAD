/**
 * @file AlignedProfileArray.h
 * @brief 沿 TrackCenterLine 依樁號等距佈置的斷面草圖陣列特徵
 *
 * 對應規劃文件《ProfileArrayAlongAlignment_ImplementationPlan》§2.2。
 *
 * 一個 master Sketch（斷面輪廓，內含 `cant`／`H` 具名參數）沿指定
 * TrackCenterLine 以固定樁號間距佈置多份 SketchInstance 拷貝：
 *   - 每個測站的平面原點 = tcl->getXYZ(p, h)，h = tcl->getAppliedH(p)
 *   - 平面法向量 = 該樁號的 3D 切線方向（含縱坡）
 *   - 平面 X 軸　 = 未旋轉的左側橫向量，依 cant 換算出的傾角繞切線旋轉
 *   - instance 的 `cant`／`H` 參數即時寫入（不快取），驅動 master 中以
 *     `cant`／`H` 直接撰寫的尺寸運算式
 */
#pragma once

#include "Feature.h"
#include <QList>
#include <QString>
#include <QVector>
#include <QSet>
#include <QPointer>
#include <QMap>
#include <QHash>
#include <QStringList>
#include <utility>
#include <TopoDS_Wire.hxx>

namespace aicad {

namespace railway { class TrackCenterLine; }

namespace cad {

class Sketch;
class Plane;
class SketchInstance;
class ProfileLoftSolid;

/**
 * @brief 沿線陣列的一「段」輸入資料（分段表格的一列）。
 *
 * 每次輸入（PROFILEARRAY 命令一次、或在表格新增一列）= 一段：
 * 在 [start, end] 以 interval 為間距佈置 `profileId` 指定的斷面草圖。
 */
struct ProfileArraySegment {
    QString id;                    ///< 穩定識別碼（uuid；段改名／列順序改變都不變）。每段對應一個 Loft。
    QString name;                  ///< 段名稱（陣列內唯一）
    double  start    = 0.0;        ///< 起點里程 [m]
    double  end      = 0.0;        ///< 終點里程 [m]
    double  interval = 20.0;       ///< 測站間距 [m]
    QString profileId;             ///< 使用的 profile = master Sketch 的 feature id
    bool    hasThick = false;      ///< false = 不覆寫（沿用 profile 內 Thick 參數的預設值）
    double  thick    = 0.0;        ///< Thick 參數（hasThick 時才有效）
    bool    hasW     = false;      ///< false = 不覆寫（沿用 profile 內 w 參數的預設值）
    double  w        = 0.0;        ///< w 參數（步道位置；hasW 時才有效）
    /// 自訂欄位：profile 內「參數名 → 值」。缺 = 空白 = 不覆寫。
    /// 有值時優先於 TCL 的 cant/H（例如自訂欄位名為 cant，即可針對本段指定 cant）。
    QMap<QString, double> extra;
};

/**
 * @brief 沿線斷面草圖陣列（FeatureType::Pattern）。
 *
 * 使用範例：
 * @code
 * auto* arr = doc->createAlignedProfileArray(masterSketch, tcl, 0.0, 500.0, 20.0);
 * arr->rebuild();
 * @endcode
 */
class AlignedProfileArray : public Feature {
    Q_OBJECT
public:
    explicit AlignedProfileArray(Document* parent = nullptr);
    ~AlignedProfileArray() override;

    FeatureType type()       const override { return FeatureType::Pattern; }
    QString     typeString() const override { return QStringLiteral("Pattern"); }

    // ── 參考設定 ─────────────────────────────────────────────────────────
    void    setMasterSketch(Sketch* master);
    // ⚠️ 不能寫成 inline { return m_master.data(); }：QPointer<T>::data() 內部
    //    對 T 做 static_cast，這裡的 T（Sketch）在標頭裡只是前向宣告，於此處
    //    是不完整型別，inline 展開時會編譯失敗。实作移到 .cpp（那裡已
    //    #include "Sketch.h"，型別是完整的）。
    Sketch* masterSketch() const;
    QString masterSketchId() const;

    void                       setTrackCenterLine(railway::TrackCenterLine* tcl);
    railway::TrackCenterLine*  trackCenterLine() const;
    QString                    trackCenterLineId() const;

    /** 起訖樁號與間距（皆為 [m]）。呼叫後需自行呼叫 rebuild() 生效。 */
    // ── 分段資料（表格的資料來源）────────────────────────────────────────
    QList<ProfileArraySegment> segments() const { return m_segments; }
    int                        segmentCount() const { return m_segments.size(); }
    QStringList                extraColumns() const { return m_extraColumns; }

    // ── 每段一個 Loft ───────────────────────────────────────────────────
    /// 第 idx 段的穩定識別碼；idx 超出範圍回傳空字串。
    QString segmentId(int idx) const;
    /// 以識別碼找段索引；找不到回傳 -1。
    int     segmentIndexById(const QString& id) const;

    /**
     * @brief 讓「每一個分段恰好有一個 ProfileLoftSolid」，並把各分段的測站掛在
     *        各自 Loft 之下（樹狀圖：Array → Loft → 測站）。
     *
     *  - 新增的分段 → 建立新的 Loft
     *  - 已刪除分段的 Loft（及其上的 Chamfer）→ 移除
     *  - 舊檔（單一 Loft 涵蓋全部分段）→ 該 Loft 轉為第一段的 Loft，其餘分段補建
     *  - Loft 名稱跟著分段名稱走
     *
     * 在 rebuild() 之外呼叫（會新增／移除 Feature）：commitSegments()、建立陣列的
     * 指令、以及載入檔案後。
     */
    void syncSegmentLofts();

    /// 只更新資料，不重建。
    void setSegments(const QList<ProfileArraySegment>& segs, const QStringList& extraColumns);

    /**
     * @brief 更新資料並立即重建（含更新依賴圖、master 訊號連線、畫面刷新）。
     * @return rebuild 是否成功；失敗原因見 errorMessage()。
     */
    bool commitSegments(const QList<ProfileArraySegment>& segs, const QStringList& extraColumns);

    /// 產生陣列內尚未使用的段名稱（Seg1、Seg2 …）。
    QString uniqueSegmentName(const QString& base = QStringLiteral("Seg")) const;

    // ── 舊版單段介面（相容：只作用於第一段；沒有任何段時會先建立一段）──────
    void   setRange(double startChainage, double endChainage, double interval);
    double startChainage() const;   ///< 所有段的最小起點
    double endChainage()   const;   ///< 所有段的最大終點
    double interval()      const;   ///< 第一段的間距

    /**
     * @brief master 草圖座標單位 → TrackCenterLine 座標單位（通常是 m）的
     *        等比縮放係數。master 若以 mm 繪製（常見的斷面草圖慣例），這裡
     *        設 0.001；若 master 本身已是 m，設 1.0（預設值）。
     *        影響每一站 SketchInstance 的輸出幾何（見 SketchInstance::setScale）。
     */
    void   setMasterUnitScale(double scale) { m_masterUnitScale = scale; }
    double masterUnitScale() const { return m_masterUnitScale; }

    // ── 重建 ─────────────────────────────────────────────────────────────
    bool rebuild() override;

    // ── 子測站存取（唯讀） ───────────────────────────────────────────────
    /**
     * @brief 依樁號順序排列的各測站 SketchInstance（已失效的指標會被過濾掉）。
     * @note 實作在 .cpp（原因同 masterSketch()：QPointer<SketchInstance>::data()
     *       需要 SketchInstance 是完整型別，這裡只有前向宣告）。
     */
    QList<SketchInstance*> instances() const;
    int stationCount() const { return m_instances.size(); }

    /**
     * @brief 依樁號順序，回傳每一測站「所有互不相連的封閉輪廓」
     *        （透過 SketchInstance::allClosedWires()）——一個斷面草圖裡可以
     *        有不只一個封閉輪廓（例如左右兩個獨立的墊塊斷面），此時每個
     *        測站會回傳相同數量、依相同順序排列的 wire，方便 ProfileLoftSolid
     *        依索引逐一配對放樣。任何一站求不出任何封閉輪廓時該項為空 list。
     */
    QVector<QList<TopoDS_Wire>> stationWireLoops() const;

    /**
     * @brief 依目前 startChainage/endChainage/interval 計算出的樁號清單
     *        （與 rebuild() 內部生成測站的邏輯完全一致），供 Step 8
     *        唯讀站位資料表使用。
     */
    QVector<double> stationChainages() const;
    // ── 里程超出線形範圍時，改接「端點相連」的線形 ──────────────────────────────
    //
    // 陣列的里程範圍可能超過 trackCenterLine() 本身的範圍。超出時不可外推（那不是真實
    // 線形），而是找端點與本線形終點／起點 (E, N, Z) 很接近、切線方向也大致連續的
    // 另一條線形，沿用其線形資料並接續里程（可連續接多條）。
    struct AlignmentSample {
        railway::TrackCenterLine* tcl = nullptr;  ///< 實際提供線形資料的線形
        double p            = 0.0;                ///< 該線形上的里程
        bool   connected    = false;              ///< 來自相連線形（非主線形）
        bool   outOfRange   = false;              ///< 找不到可接的線形，只能外推（結果不可信）
    };
    /// 把陣列里程 p 對應到實際的 (線形, 里程)。
    AlignmentSample resolveAlignment(double p) const;

    /// 某里程範圍 [start, end] 實際會用到哪些線形（依里程順序，主線形可能不在其中）。
    /// 供分段資料表的「線形」欄顯示「該段所依賴的線形」。
    struct SegmentAlignments {
        QStringList names;           ///< 依序用到的線形名稱（去重）
        bool        outOfRange = false;   ///< 範圍有一部分既不在主線形、也找不到相連線形
    };
    SegmentAlignments alignmentsForRange(double start, double end) const;

    /// 端點相連的判定容差 [m]（水平 E,N 距離／高程差）與切線方向容差 [rad]
    static constexpr double kConnectTolXY  = 0.30;
    static constexpr double kConnectTolZ   = 0.30;
    static constexpr double kConnectTolAz  = 0.785398163;   ///< 45°

    /// 與 instances()/stationWireLoops() 同順序：各測站所屬的段索引（供分段放樣）。
    QVector<int>    stationSegmentIndices() const { return m_stationSeg; }

    // ── 序列化：只存參照 id／範圍／間距，不存子 instance、不存 cant/H ────
    QJsonObject toJson()  const override;
    bool fromJson(const QJsonObject& json) override;

    QSet<QString> featureDependencies() const override;

    /** 解析 fromJson() 暫存的 masterSketchId / trackCenterLineId。 */
    void resolveReferences(Document* doc);

private:
    void clearInstances();
    struct StationRef { double p; int seg; };
    QVector<StationRef> computeStationRefs() const;
    QVector<double>     computeStations() const;
    void ensureSegmentIds();                                   ///< 補齊／去重各段 id
    QHash<QString, ProfileLoftSolid*> collectSegmentLofts() const;   ///< 段 id → 該段的 Loft
    void reparentInstances();                                  ///< 測站掛到各自段的 Loft（沒有則掛陣列）
    Sketch*             resolveProfile(const ProfileArraySegment& seg) const;
    void                syncMasterConnections();

    /// 本站要寫入 SketchInstance override 的參數（名稱, 值）。
    /// 目前：cant、H。Thick 的資料來源日後提供，屆時只需在此加一行。
    QVector<std::pair<QString, double>> stationParameters(double p, const ProfileArraySegment& seg) const;

    /// master 草圖內容（幾何點、約束值/運算式、整條參數作用域鏈的運算式）的指紋。
    /// 指紋不變 ⇒ 各站 2D 解可重用；改變 ⇒ 全部重新求解。
    static quint64 masterFingerprint(Sketch* master);

    QPointer<railway::TrackCenterLine> m_tcl;
    QList<ProfileArraySegment>         m_segments;
    QStringList                        m_extraColumns;
    double m_masterUnitScale = 0.001;

    QList<QPointer<SketchInstance>> m_instances;  ///< 依測站順序排列（先依段、再依里程）
    QList<QPointer<Plane>>          m_planes;     ///< 與 m_instances 一一對應
    QVector<int>                    m_stationSeg; ///< 與 m_instances 一一對應：所屬段索引

    // 線形路徑（主線形 + 接續的相連線形）。依需求建立，主線形或相連線形變動時失效。
    struct RouteLeg {
        QPointer<railway::TrackCenterLine> tcl;
        double a0 = 0.0, a1 = 0.0;   ///< 本段涵蓋的陣列里程範圍
        double off = 0.0;            ///< 線形里程 = 陣列里程 + off
    };
    mutable QVector<RouteLeg> m_route;          ///< [0] = 主線形；其後依序為接續的線形
    mutable bool              m_routeValid = false;
    QList<QMetaObject::Connection> m_routeConns; ///< 相連線形的 dataChanged 連線
    void ensureRoute() const;
    void syncRouteConnections();

    QMetaObject::Connection m_tclDataChangedConn;
    QHash<Sketch*, QMetaObject::Connection> m_masterConns;   ///< 各 profile 的 shapeChanged 連線
    QHash<QString, quint64> m_lastMasterFp;                  ///< profile id → 上次重建時的指紋
    QString m_pendingTclId;
};

} // namespace cad
} // namespace aicad
