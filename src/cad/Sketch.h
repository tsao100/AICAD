/**
 * @file Sketch.h
 * @brief 與增強版 Plane 整合的 Sketch 類別
 *        Phase 0B：SketchPoint 一等公民架構
 */

#ifndef AICAD_CAD_SKETCH_ENHANCED_H
#define AICAD_CAD_SKETCH_ENHANCED_H

#include "Feature.h"
#include "Plane.h"
#include "sketch/ConstraintSolver.h"
#include "sketch/SketchConstraint.h"
#include "sketch/SketchAnnotation.h"
#include "sketch/SketchRegion.h"
#include "../core/ParameterStore.h"

#include <optional>
#include <QVector>
#include <QVector2D>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <TopoDS_Wire.hxx>
#include <AIS_Shape.hxx>
#include <AIS_InteractiveObject.hxx>
#include <Geom_TrimmedCurve.hxx>

namespace aicad {
namespace cad {

/**
 * @brief 草圖幾何類型
 */
enum class SketchGeometryType {
    Line,        ///< 直線
    Arc,         ///< 圓弧
    Circle,      ///< 圓
    Ellipse,     ///< 橢圓
    Polyline,    ///< 多段線
    Spline,      ///< 樣條曲線
    Point,       ///< 獨立點（Phase 0B 新增）
};

/**
* @brief 幾何元素的角色
*/
enum class GeomRole {
    Normal,
    Construction,
    Centerline,
};

/**
 * @brief 草圖幾何元素基礎類別
 */
struct SketchGeometry {
    QString    uuid;
    SketchGeometryType type;
    GeomRole   role = GeomRole::Normal;
    QVector<QVector2D> points;

    SketchGeometry(SketchGeometryType t, GeomRole r = GeomRole::Normal)
        : uuid(QUuid::createUuid().toString(QUuid::WithoutBraces))
        , type(t), role(r) {}

    bool isConstruction() const {
        return role == GeomRole::Construction || role == GeomRole::Centerline;
    }
    virtual ~SketchGeometry() = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// Phase 0B：SketchPoint — 一等公民點
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 草圖點（一等公民幾何元素）
 *
 * 可由以下來源建立：
 *  - 繪製曲線時自動建立並關聯（端點、圓心）
 *  - 使用者明確繪製「構建點」
 *  - 兩條曲線共享端點時共用同一個 SketchPoint
 */
struct SketchPoint : public SketchGeometry {
    QVector2D pos;

    enum class Origin {
        Explicit,       ///< 使用者明確繪製的獨立點
        Endpoint,       ///< 曲線端點（由繪圖命令建立）
        Center,         ///< 圓/弧/橢圓的圓心
        Intersection,   ///< 兩條曲線的交點（未來擴充）
    };
    Origin origin = Origin::Endpoint;

    explicit SketchPoint(const QVector2D& p,
                         Origin o = Origin::Endpoint,
                         GeomRole r = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Point, r)
        , pos(p), origin(o)
    {
        points.clear();
        points.append(p);
    }
};

/**
 * @brief 草圖線段（Phase 0B：引用點 UUID）
 */
struct SketchLine : public SketchGeometry {
    // Phase 0B：新增 UUID 引用（向後相容保留 start/end）
    QString startUuid;   ///< → SketchPoint UUID
    QString endUuid;     ///< → SketchPoint UUID

    // 向後相容：由 Sketch 物件透過 UUID 取得實際座標
    QVector2D start;
    QVector2D end;

    SketchLine(const QVector2D& p1, const QVector2D& p2,
               GeomRole role = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Line, role)
        , start(p1), end(p2)
    {
        points.clear();
        points.append(p1);
        points.append(p2);
    }
};

/**
 * @brief 草圖三點弧（Phase 0B：引用點 UUID）
 */
struct SketchArc : public SketchGeometry
{
    Handle(Geom_TrimmedCurve) curve;
    QString startUuid;    ///< 弧起點 UUID
    QString endUuid;      ///< 弧終點 UUID
    QString centerUuid;   ///< 弧圓心 UUID

    SketchArc(const Handle(Geom_TrimmedCurve)& c,
              GeomRole role = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Arc, role)
        , curve(c)
    {
        // ✅ 修正：與 SketchLine/SketchPolyline 等其他幾何一致，points[] 必須
        // 在建構時就非空。舊版從不初始化 points，導致 points.isEmpty() 永遠
        // 為 true、points.size() 永遠為 0：
        //   - Sketch::syncGeometryFromPoints() 裡 Arc 分支的
        //     `if (!arc->points.isEmpty())` / `if (arc->points.size() > 2)`
        //     兩個保護判斷因此永遠不成立，points[] 從未真的被同步更新過。
        //   - ConstraintPickSession::pick() 用 `g->points.isEmpty()` 篩選可
        //     被點選的幾何，Arc 因此永遠被跳過、無法用「點選端點/圓心」的
        //     方式加入約束。
        // 這裡先塞入 3 個佔位值（[0]=起點 [1]=中點 [2]=終點），實際座標由
        // Sketch::addArcGeom() 在建構後立即以平面座標填入，之後每次
        // syncGeometryFromPoints() 也都能正常更新。
        points = { QVector2D(), QVector2D(), QVector2D() };
    }
};

/**
 * @brief 草圖多段線
 */
struct SketchPolyline : public SketchGeometry {
    bool closed;
    QVector<QString> vertexUuids;  ///< 各頂點對應的 SketchPoint UUID（Phase 0B）

    SketchPolyline(const QVector<QVector2D>& pts, bool isClosed,
                   GeomRole role = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Polyline, role)
        , closed(isClosed) {
        points = pts;
    }
};

/**
 * @brief 草圖樣條
 */
struct SketchSpline : public SketchGeometry {
    QVector<QString> controlPointUuids;  ///< 各控制點對應的 SketchPoint UUID（Phase 0B）

    SketchSpline(const QVector<QVector2D>& pts,
                 GeomRole role = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Spline, role)
    {
        points = pts;
    }
};

/**
 * @brief 草圖正多邊形
 */
struct SketchPolygon : public SketchGeometry {
    bool closed;

    SketchPolygon(const QVector<QVector2D>& pts, bool isClosed = true,
                  GeomRole role = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Polyline, role)
        , closed(isClosed) {
        points = pts;
    }
};

/**
 * @brief 草圖圓（Phase 0B：引用圓心 UUID）
 */
struct SketchCircle : public SketchGeometry {
    QString   centerUuid;  ///< 圓心 UUID（Phase 0B）
    QVector2D center;      ///< 向後相容
    double radius;

    SketchCircle(const QVector2D& c, double r,
                 GeomRole role = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Circle, role)
        , center(c)
        , radius(r) {
    }
};

/**
 * @brief 草圖橢圓
 */
struct SketchEllipse : public SketchGeometry {
    QString   centerUuid;  ///< 橢圓圓心 UUID（Phase 0B）
    QVector2D center;
    double majorRadius;
    double minorRadius;
    double angle;

    SketchEllipse(const QVector2D& c, double r1, double r2, double a,
                  GeomRole role = GeomRole::Normal)
        : SketchGeometry(SketchGeometryType::Ellipse, role)
        , center(c)
        , majorRadius(r1)
        , minorRadius(r2)
        , angle(a)
    {
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Sketch 類別
// ─────────────────────────────────────────────────────────────────────────────

class Sketch : public Feature {
    Q_OBJECT

public:
    explicit Sketch(Document* parent = nullptr);
    ~Sketch() override;

    FeatureType type() const override { return FeatureType::Sketch; }
    bool rebuild() override;
    bool rebuildShapesOnly();

    // ==================== 平面管理 ====================

    Plane* plane() const { return m_plane; }
    QVector3D planeToWorld(const QVector2D& planePt) const;
    void setPlane(Plane* plane);
    bool hasValidPlane() const { return m_plane != nullptr; }

    // ==================== 幾何元素管理 ====================

    void addGeometry(SketchGeometry* geom);
    void removeGeometry(int index);
    /// 依 UUID 移除幾何元素（ERASE 命令用）。找不到回傳 false。
    bool removeGeometry(const QString& uuid);
    void clearGeometry();

    /// @brief 批次刪除一批幾何元素／約束（ERASE 命令一次刪除多個選取物件用）。
    ///
    /// 逐一呼叫 removeGeometry(const QString&)／removeConstraint(const QString&)
    /// 每次都會各自 Q_EMIT geometryChanged()／rebuildRequested()，而
    /// rebuildRequested() 掛在 Document 的完整 Feature 重建上（見
    /// Document.cpp connect(feature, &Feature::rebuildRequested, ...)），
    /// 一批 N 個物件就會重建 N 次。本函式對整批 uuids 只在最後統一
    /// Q_EMIT 一次，把「刪一個重繪一次」改成「刪一批只重繪一次」。
    ///
    /// @param uuids 要刪除的幾何或約束 UUID（可混合，逐一嘗試先當幾何、
    ///        找不到再當約束刪除，與既有 EraseCommand 內 eraseOne() 的邏輯
    ///        一致）。
    /// @return 實際成功刪除的物件數量。
    int removeMany(const QStringList& uuids);

    QList<SketchGeometry*> geometries() const { return m_geometries; }
    const QList<SketchGeometry*>& geometriesRef() const { return m_geometries; }
    int geometryCount() const { return m_geometries.size(); }

    aicad::core::ParameterStore* parameterStore() {
        if (!m_parameterStore)
            m_parameterStore = new aicad::core::ParameterStore(this);
        return m_parameterStore;
    }

    // ── 基本幾何添加（Phase 0B：傳回 UUID，自動建立點）─────────────────────
    /**
     * @param emitSignals 是否在建立後立即發出 geometryChanged()／
     *        rebuildRequested()（預設 true，維持原本行為）。呼叫端如果
     *        接下來還要繼續加其他幾何／約束、最後才要一次重繪（例如
     *        CHAMFER 一次建立倒角線＋交點＋多條約束），可以傳 false 跳過
     *        這裡的即時重繪，自己在最後統一 Q_EMIT rebuildRequested() 一
     *        次——原理同 addConstraint() 的 solve 參數、Sketch::removeMany()
     *        對批次刪除「只重繪一次」。
     */
    QString addLineGeom(const QVector2D& p1, const QVector2D& p2,
                        const QString& reuseStart = QString(),
                        const QString& reuseEnd   = QString(),
                        GeomRole role = GeomRole::Normal,
                        bool emitSignals = true);
    QString addCircleGeom(const QVector2D& center, double radius,
                          const QString& reuseCenterUuid = QString());
    QString addArcGeom(const QVector2D& startPoint,
                       const QVector2D& midPoint,
                       const QVector2D& endPoint,
                       const QString& reuseStartUuid  = QString(),
                       const QString& reuseEndUuid    = QString(),
                       const QString& reuseCenterUuid = QString());
    QString addPolylineGeom(const QVector<QVector2D>& pts, bool closed,
                            const QVector<QString>& reuseVertexUuids = {});

    /**
     * @brief 將一串點位「退化」為多條獨立的 SketchLine，並在相鄰線段的接點
     *        自動加上 Coincident 束制（closed=true 時，最後一段與第一段之間
     *        也會加上 Coincident，形成封閉迴路）。
     *
     * 與 addPolylineGeom() 建立單一 SketchPolyline 幾何不同：這裡每一段都是
     * 完全獨立、可個別選取/標註/設定束制的 SketchLine（與 LineCommand 連續畫線
     * 產生的結果一致）。用於 Polyline / Rectangle / Polygon 等指令的底層實作，
     * 使整個草圖系統的參數化行為一致。
     *
     * @return 依序建立的 SketchLine UUID 列表；呼叫端可視需要疊加其他束制
     *         （例如 Rectangle 額外加上 Horizontal/Vertical）。
     */
    QStringList addLineChainGeom(const QVector<QVector2D>& pts, bool closed);

    QString addSplineGeom(const QVector<QVector2D>& pts,
                          const QVector<QString>& reuseControlPointUuids = {});
    QString addEllipseGeom(const QVector2D& center,
                           double majorRadius, double minorRadius, double angle,
                           const QString& reuseCenterUuid = QString());

    // 向後相容的舊版方法（void，包裝新版）
    void addLine(const QVector2D& p1, const QVector2D& p2);
    void addPolyline(const QVector<QVector2D>& points, bool closed = false);
    void addSpline(const QVector<QVector2D>& points);
    void addCircle(const QVector2D& center, double radius);
    void addEllipse(const QVector2D& center, double majorRadius, double minorRadius, double angle);
    void addRectangle(const QVector2D& corner1, const QVector2D& corner2);
    void addArc(const QVector2D& startPoint, const QVector2D& midPoint, const QVector2D& endPoint);

    // ==================== Phase 0B：點管理 API ====================

    /**
     * 建立一個新的獨立 SketchPoint，回傳其 UUID
     */
    QString addPoint(const QVector2D& pos,
                     SketchPoint::Origin origin = SketchPoint::Origin::Explicit);

    /**
     * @brief 建立一個使用者明確繪製、需要立即顯示在畫面上的獨立點
     *        （POINT 命令用）。
     *
     * addPoint() 是給 TrimExtendHelper／SketchGeomTransformUtil 等內部
     * 幾何操作使用的低階 API：只把 SketchPoint 加進 m_geometries／
     * m_uuidToGeomIndex 快取，刻意不 Q_EMIT geometryChanged()／
     * rebuildRequested()，因為這些呼叫端一律緊接著呼叫其他會自動 emit
     * 的高階方法（如 addLine()），先 emit 只會造成多餘的一次重繪。
     * 但這代表如果只呼叫 addPoint() 就結束，畫面完全不會更新——新的點
     * 會「建立了但看不到」，直到下一次任何原因觸發的重繪才會意外冒出來。
     * POINT 命令要畫的是一個獨立、當下就要能看到的點，沒有後續動作，
     * 因此需要這個會自己 emit 一次的版本。
     */
    QString addExplicitPoint(const QVector2D& pos);

    /**
     * 取得指定 UUID 的點（若不存在回傳 nullptr）
     */
    SketchPoint* point(const QString& uuid) const;

    /**
     * 所有點（含曲線端點）
     */
    QList<SketchPoint*> points() const;

    /**
     * 移動一個點（會觸發 Solver 重算，所有引用此點的曲線自動跟隨）
     */
    void movePoint(const QString& uuid, const QVector2D& newPos);

    /**
     * 合併兩個點（讓所有引用 fromUuid 的曲線改引用 toUuid）
     * 等同於施加 Coincident
     */
    void mergePoints(const QString& fromUuid, const QString& toUuid);

    /**
     * 查詢哪些曲線引用了此點
     */
    QList<SketchGeometry*> curvesReferencingPoint(const QString& pointUuid) const;

    // ==================== OCCT ====================

    QList<TopoDS_Wire> wires() const;
    QList<Handle(AIS_InteractiveObject)> aisShapes() const;
    const QList<QString>& aisShapeUuids() const;

    /// ⚠️ 修正：建構幾何（Construction/Centerline）的可選取性。
    /// 供 CadView 建立 aisToGeomUuid／aisToFeatureId 反查表使用，讓建構
    /// 線/弧/圓能像一般幾何一樣被點選、參與 TRIM/EXTEND/FILLET/CHAMFER/
    /// MIRROR/ROTATE/MOVE/COPY/STRETCH/ERASE/GDIM 等所有命令（唯一的差異
    /// 只在於 buildWires()／SketchLoopFinder 不把它們納入輪廓／迴圈偵測）。
    /// 與 aisShapes()／aisShapeUuids() 是各自獨立的一組 parallel array，
    /// 不會混在一起，理由見 rebuildShapesOnly() 內的說明註解（建構幾何
    /// 目前沒有穩定的逐一 shape identity，每次都整批 erase+redisplay）。
    QList<Handle(AIS_Shape)> constructionShapes() const { return m_constructionShapes; }
    const QList<QString>& constructionShapeUuids() const { return m_constructionShapeUuids; }
    QList<Handle(AIS_InteractiveObject)> displayInContext(const Handle(AIS_InteractiveContext)& context);
    void eraseFromContext(const Handle(AIS_InteractiveContext)& context);
    TopoDS_Wire mainWire() const;
    bool hasClosedProfile() const;

    // ==================== 2D Region ====================

    QVector<SketchRegion> detectRegions() const;
    std::optional<SketchRegion> pickRegion(const QVector2D& sketchPt) const;
    bool hasExtrudableProfile() const;

    // ==================== 序列化 ====================

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;

    // ==================== 約束管理 ====================

    /**
     * @brief 新增一條約束
     * @param c 約束
     * @param solve 是否在加入後立即求解（預設 true，維持原本行為）。
     *
     * 呼叫端如果要一次加入一整批約束（例如 CHAMFER 一次建立交點＋距離＋
     * 共線共 4 條約束），可以把中間每一次都傳 false 跳過求解，全部加完
     * 後再自己呼叫一次 solveConstraints()——比照 removeMany() 對批次刪除
     * 幾何「只重繪一次」的同一個原則：中間每一次的 solve 除了浪費運算，
     * 用「只加了一部分約束」的不完整系統去解，中途還可能先收斂到一個錯誤
     * 的過渡狀態，反而讓最後那個用完整方程組做的正式求解要多繞一段路才
     * 收斂，不如全部加完、資訊完整後一次求解。
     */
    QString addConstraint(const SketchConstraint& c, bool solve = true);
    bool removeConstraint(const QString& uuid);
    const QList<SketchConstraint>& constraints() const { return m_constraints; }
    QList<SketchConstraint>& constraintsMutable() { return m_constraints; }
    SketchConstraint* findConstraint(const QString& uuid);
    QList<SketchConstraint*> constraintsOf(const QString& geomUuid);
    void removeConstraintsOf(const QString& geomUuid);
    /// 僅更新尺寸線偏移，不重新求解（拖曳尺寸線時輕量更新）
    bool updateConstraintDimOffset(const QString& uuid, double offsetX, double offsetY);

    /// 第 10 項回報的後續需求：讓使用者手動切換 FixedDistance 約束求解
    /// 的「哪一側」。做法是把約束的其中一個參考幾何反射到目前的另一側
    /// （PointToPoint：把 refs[1] 的點對 refs[0] 的點做點對稱；PointToLine：
    /// 把 refs[0] 的點對 refs[1] 的線做鏡射；LineToLine：把 refs[1] 整條
    /// 線的兩個端點對 refs[0] 的線做鏡射），距離值不變、只是換到另一側，
    /// 再重新求解一次讓其餘約束一起收斂。uuid 對應的約束型別不是
    /// FixedDistance 時回傳 false（no-op）。
    bool flipDistanceSide(const QString& uuid);

    // ==================== 標註管理（GDIM v2 Phase 1）====================
    // SketchAnnotation 是「標註」的唯一對外資料來源（GeneralDimClassifier /
    // GeneralDimCommand / AnnotationAIS 家族皆應改用這組 API，而非直接操作
    // m_constraints 中的尺寸型別）。driving == true 的標註會透過
    // addImplicitConstraint() 自動於 m_constraints 中維護一個對應的隱含約束
    // 供 ConstraintSolver 使用；使用者/UI 完全不需要知道這個隱含約束的存在。

    /// 新增或更新一筆標註（依 uuid 判斷；若不存在則新增）。
    /// 會同步呼叫 addImplicitConstraint() 建立/更新對應的隱含約束。
    QString addAnnotation(const SketchAnnotation& a);
    /// 移除標註，並一併移除其對應的隱含約束（若有）。
    bool removeAnnotation(const QString& uuid);
    const QList<SketchAnnotation>& annotations() const { return m_annotations; }
    QList<SketchAnnotation>& annotationsMutable() { return m_annotations; }
    SketchAnnotation* findAnnotation(const QString& uuid);
    QList<SketchAnnotation*> annotationsOf(const QString& geomUuid);
    void removeAnnotationsOf(const QString& geomUuid);

    /// 依 annotation 目前內容，在 m_constraints 中新增/同步對應的隱含約束。
    /// driving == false 或非尺寸型別（如 LeaderNote）時，會移除既有的隱含
    /// 約束（若有）且不新增。呼叫端（addAnnotation 已自動呼叫）通常不需要
    /// 手動呼叫，除非是在標註內容變更後（例如 Mini Toolbar 編輯數值）想
    /// 單獨同步、稍後再手動 solveConstraints()。
    void addImplicitConstraint(const SketchAnnotation& a);
    /// 移除 uuid 對應標註的隱含約束（若有），不影響標註本身。
    void removeImplicitConstraint(const QString& annotationUuid);
    /// 見 removeAnnotation() 的說明：刪除標註後，若它原本引用的參數名稱
    /// 符合 GDIM 自動命名格式且已無其他引用者，一併從 ParameterStore 移除。
    void maybeRemoveOrphanedAutoParam(const QString& name);
    /// 第 10 項回報後續需求：從目前（通常是剛求解完）的幾何位置，計算
    /// 一個 FixedDistance 約束「現在」對應到哪一側／方向。非退化（兩個
    /// 參考幾何沒有幾乎重合/重疊）時回傳 true，並把結果寫入 sign（給
    /// PointToLine/LineToLine）或 dirX/dirY（給 PointToPoint，單位向量）。
    /// 供 solveConstraints() 求解後同步「記住的哪一側」、以及
    /// flipDistanceSide() 共用。
    bool computeDistanceSide(const SketchConstraint& c,
                             double& sign, double& dirX, double& dirY) const;

    /**
     * @brief GDIM v2 Phase 6：重複尺寸偵測
     *
     * 若已存在一筆標註，其 kind 與 refs（不分順序比對——例如 A→B 距離
     * 與 B→A 距離視為相同）皆與給定條件相符，回傳該標註；否則回傳
     * nullptr。供 GeneralDimCommand 在 commit 前提醒使用者「已存在相同
     * 標註」，避免過度標註（見 GDIM.md 第九節「重複尺寸檢查」）。
     */
    const SketchAnnotation* findDuplicateAnnotation(
        const QList<GeomRef>& refs, AnnotationKind kind) const;

    SolveResult solveConstraints();
    SolveResult solveWithStore(const aicad::core::ParameterStore* store);

    /// ⚠️ 效能優化：取得最近一次 solveConstraints() 的結果，供呼叫端組
    /// 「約束已施加，DOF: X → Y」這類報告訊息，而不需要為了拿 SolveResult
    /// 再重新呼叫一次 solveConstraints()（那會讓 Newton-Raphson 疊代與
    /// AIS 重建/重繪整個再跑一遍，見 solveConstraints() 尾端註解）。
    SolveResult lastSolveResult() const { return m_lastSolveResult; }
    int degreesOfFreedom() const;

    // 便捷 API
    QString constrainCoincident(const GeomRef& a, const GeomRef& b);
    QString constrainHorizontal(const QString& lineUuid);
    QString constrainVertical(const QString& lineUuid);
    QString constrainParallel(const QString& lineA, const QString& lineB);
    QString constrainPerpendicular(const QString& lineA, const QString& lineB);
    QString constrainTangent(const QString& geomA, const QString& geomB);
    QString constrainEqualLength(const QString& lineA, const QString& lineB);
    QString constrainEqualRadius(const QString& lineA, const QString& lineB);
    QString constrainConcentric(const QString& geomA, const QString& geomB);
    QString constrainFixed(const QString& geomUuid);

    // ── 草圖平面參考幾何（X 軸 / Y 軸 / 原點）─────────────────────────────
    // 這些幾何在第一次呼叫時 lazy-init，屬於 Fixed 參考幾何（不可移動）。
    // AIS 層用 "sketch_xaxis:<id>" 等 UUID 顯示；約束解析時用這裡的真實 UUID。
    QString xAxisGeomUuid();    ///< Sketch 內 X 軸 SketchLine 的 UUID
    QString yAxisGeomUuid();    ///< Sketch 內 Y 軸 SketchLine 的 UUID
    QString originPointUuid();  ///< Sketch 內原點 SketchPoint 的 UUID
    QString constrainDistance(const GeomRef& a, const GeomRef& b, double dist);
    QString constrainRadius(const QString& geomUuid, double radius);
    QString constrainRadius(const GeomRef& ref, double radius);
    QString constrainFixedX(const GeomRef& point, double x);
    QString constrainFixedY(const GeomRef& point, double y);
    QString constrainAngle(const GeomRef& a, const GeomRef& b, double angleRad);
    QString constrainPointOnCurve(const GeomRef& point, const QString& curveUuid);
    QString constrainMidpoint(const GeomRef& point, const QString& lineUuid);
    QString constrainSymmetric(const GeomRef& a, const GeomRef& b, const QString& axisUuid);
    QString constrainCollinear(const QString& lineA, const QString& lineB);

    // ── 建構線便捷方法 ────────────────────────────────────────────
    void addConstructionLine(const QVector2D& p1, const QVector2D& p2);
    void addCenterline(const QVector2D& p1, const QVector2D& p2);
    void addConstructionCircle(const QVector2D& center, double radius);
    void addConstructionArc(const QVector2D& start,
                            const QVector2D& mid,
                            const QVector2D& end,
                            GeomRole role = GeomRole::Construction);

    // ── 查詢 ─────────────────────────────────────────────────────
    QList<SketchGeometry*> normalGeometries() const;
    QList<SketchGeometry*> constructionGeometries() const;
    SketchGeometry* findGeometry(const QString& uuid) const;

    /**
     * LISTPOINTS 命令用：取得所有點的摘要資訊
     */
    struct PointInfo {
        QString uuid;
        QVector2D pos;
        SketchPoint::Origin origin;
        QStringList referencedBy;  ///< 格式："lineUuid.Start"、"circUuid.Center"
    };
    QList<PointInfo> listPoints() const;

Q_SIGNALS:
    void planeChanged(Plane* plane);
    void geometryChanged();
    void rebuilt();
    void constraintAdded(const QString& uuid);
    void constraintRemoved(const QString& uuid);
    void annotationAdded(const QString& uuid);      ///< GDIM v2 Phase 1
    void annotationRemoved(const QString& uuid);
    void constraintSolved(SolveResult result);

public Q_SLOTS:
    void scheduleRebuild();

private Q_SLOTS:
    void onPlaneAboutToBeDeleted();
    void onPlaneGeometryChanged();

private:
    gp_Pnt toWorld(const QVector2D& point) const;
    void createDefaultPlane();
    void connectPlaneSignals();
    void disconnectPlaneSignals();
    Plane* resolveStandardPlane(const QString& planeName);
    Plane* reconstructPlaneFromJson(const QJsonObject& planeJson,
                                    const QString& hint = QString());

    // Phase 0B：從舊格式 JSON 遷移
    void migrateFromLegacyFormat(const QJsonObject& json);

    // Phase 0B：同步曲線的 start/end 座標（透過 UUID 查詢點座標）
    void syncGeometryFromPoints();

    /** Cheap content hash of a geometry's solved shape (type/role/points/
     *  type-specific fields). Used to detect whether a geometry's AIS
     *  representation actually needs rebuilding after solve(). */
    static quint64 geometryFingerprint(const SketchGeometry* geom);

    /// removeGeometry(int) 的內部實作：實際移除幾何/相關約束/標註，但不
    /// Q_EMIT geometryChanged()／rebuildRequested()。供 removeGeometry()
    /// （單一刪除，維持原本每次都 emit 的行為）與 removeMany()（批次刪除，
    /// 只在整批結束後 emit 一次）共用，避免邏輯重複。
    void removeGeometryAt(int index);

    /// removeConstraint(const QString&) 的內部實作：移除約束本身，但不
    /// Q_EMIT constraintRemoved()、不呼叫 solveConstraints()。供
    /// removeConstraint()（單一刪除）與 removeMany()（批次刪除）共用。
    bool removeConstraintInternal(const QString& uuid);

private:
    Plane* m_plane;
    QList<SketchGeometry*> m_geometries;
    mutable QHash<QString, int> m_uuidToGeomIndex;  ///< UUID → m_geometries 索引（O(1) 查找 cache）
    QList<TopoDS_Wire> m_wires;
    QList<Handle(AIS_InteractiveObject)> m_aisShapes;
    QList<QString>            m_aisShapeUuids;
    /**
     * @brief Content fingerprint of each geometry, index-aligned with
     *        m_aisShapes/m_aisShapeUuids, used by rebuildShapesOnly() to
     *        skip AIS Erase/Display churn for geometries whose solved
     *        shape didn't actually change since the last solve.
     */
    QHash<QString, quint64> m_geomFingerprints;
    QList<SketchConstraint> m_constraints;
    QList<SketchAnnotation> m_annotations;  ///< GDIM v2 Phase 1
    QList<Handle(AIS_Shape)>  m_constructionShapes;
    QList<QString>            m_constructionShapeUuids;    ///< parallel to m_constructionShapes（見 constructionShapes() 說明）
    QHash<QString, quint64>   m_constructionFingerprints;  ///< parallel cache for m_constructionShapes
    ConstraintSolver        m_solver;
    SolveResult             m_lastSolveResult;   ///< 見 lastSolveResult() 註解
    Handle(AIS_InteractiveContext) m_aisContext;
    aicad::core::ParameterStore*  m_parameterStore = nullptr;

    /**
     * @brief When true, rebuild() skips its Q_EMIT rebuilt() call.
     *
     * Set by rebuildShapesOnly() around its internal rebuild() call: at
     * that point m_aisShapes only holds *candidate* AIS handles (some of
     * which rebuildShapesOnly()'s diff will immediately discard in favour
     * of reused old handles for unchanged geometry). Letting rebuilt() fire
     * there would make CadView::onSketchRebuilt() register the
     * soon-to-be-discarded candidate pointers instead of the final ones,
     * breaking pick lookups (e.g. GDIM's second selection) for any
     * geometry whose AIS handle didn't actually change.
     * rebuildShapesOnly() re-emits rebuilt() itself once m_aisShapes
     * reflects the final, diffed state.
     */
    bool m_suppressRebuiltSignal = false;

    // 草圖平面參考幾何 UUID（lazy-init，初次存取時建立）
    QString m_xAxisGeomUuid;
    QString m_yAxisGeomUuid;
    QString m_originPointUuid;

    void applyConstructionStyle(Handle(AIS_Shape)& shape, GeomRole role);
};

} // namespace cad
} // namespace aicad

Q_DECLARE_METATYPE(aicad::cad::Sketch*)

#endif // AICAD_CAD_SKETCH_ENHANCED_H