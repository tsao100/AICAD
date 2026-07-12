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
#include <TopoDS_Wire.hxx>

namespace aicad {

namespace railway { class TrackCenterLine; }

namespace cad {

class Sketch;
class Plane;
class SketchInstance;

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
    void   setRange(double startChainage, double endChainage, double interval);
    double startChainage() const { return m_startChainage; }
    double endChainage()   const { return m_endChainage; }
    double interval()      const { return m_interval; }

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

    // ── 序列化：只存參照 id／範圍／間距，不存子 instance、不存 cant/H ────
    QJsonObject toJson()  const override;
    bool fromJson(const QJsonObject& json) override;

    QSet<QString> featureDependencies() const override;

    /** 解析 fromJson() 暫存的 masterSketchId / trackCenterLineId。 */
    void resolveReferences(Document* doc);

private:
    void clearInstances();
    QVector<double> computeStations() const;

    QPointer<Sketch>                   m_master;
    QPointer<railway::TrackCenterLine> m_tcl;

    double m_startChainage = 0.0;
    double m_endChainage   = 0.0;
    double m_interval      = 20.0;

    /// master 草圖(mm) → TrackCenterLine(m) 的縮放係數；預設 1/1000。
    double m_masterUnitScale = 0.001;

    QList<QPointer<SketchInstance>> m_instances;  ///< 依樁號順序排列
    QList<QPointer<Plane>>          m_planes;     ///< 與 m_instances 一一對應

    QMetaObject::Connection m_tclDataChangedConn;
    QMetaObject::Connection m_masterRebuildConn;

    QString m_pendingMasterSketchId;
    QString m_pendingTclId;
};

} // namespace cad
} // namespace aicad
