#pragma once
#include <QList>
#include <QString>
#include <QVector2D>
#include <QJsonObject>
#include <QUuid>

// Forward declare to avoid circular includes
namespace aicad::core { class ParameterStore; }

// Forward declare Sketch for GeomRef methods
namespace aicad::cad { class Sketch; }

namespace aicad::cad {

// ─────────────────────────────────────────────────────────────────────────────
// 幾何子元素枚舉
// 用於精確指定約束「抓住」的是哪個控制點
// ─────────────────────────────────────────────────────────────────────────────
enum class GeomHandle {
    // 通用
    Start,          ///< 線段/弧 起點
    End,            ///< 線段/弧 終點
    Center,         ///< 圓/弧/橢圓 圓心
    // 圓/弧
    RadiusValue,    ///< 半徑（純量 DOF）
    // 弧
    ArcStartAngle,  ///< 起始角（弧度）
    ArcEndAngle,    ///< 結束角（弧度）
    // 橢圓
    MajorRadius,
    MinorRadius,
    // 整條曲線（用於方向性約束）
    Curve,
    // 固定點（不指定特定子元素時）
    WholeGeom
};

// ─────────────────────────────────────────────────────────────────────────────
// 約束類型
// ─────────────────────────────────────────────────────────────────────────────
enum class ConstraintType {
    // ── 點─點 ──────────────────────────────────
    Coincident,         ///< 兩點重合（0 DOF消耗：2）
    Midpoint,           ///< 點在線段中點
    Symmetric,          ///< 兩點關於一條線對稱

    // ── 點─曲線 ────────────────────────────────
    PointOnCurve,       ///< 點在曲線上（消耗 1 DOF）
    PointOnMidpoint,    ///< 點固定在曲線中點

    // ── 線段方向 ───────────────────────────────
    Horizontal,         ///< 水平（消耗 1 DOF）
    Vertical,           ///< 垂直（消耗 1 DOF）
    Parallel,           ///< 兩線平行（消耗 1 DOF）
    Perpendicular,      ///< 兩線垂直（消耗 1 DOF）
    Collinear,          ///< 共線（消耗 2 DOF）
    EqualLength,        ///< 等長
    FixedAngle,         ///< 兩線夾角 = value（弧度）

    // ── 圓/弧 ──────────────────────────────────
    Concentric,         ///< 同心（消耗 2 DOF）
    EqualRadius,        ///< 等半徑（消耗 1 DOF）
    Tangent,            ///< 相切（消耗 1 DOF）

    // ── 尺寸（帶數值） ─────────────────────────
    FixedDistance,      ///< 兩點距離 = value
    FixedRadius,        ///< 半徑 = value
    FixedX,             ///< 點的 X 座標 = value
    FixedY,             ///< 點的 Y 座標 = value
    FixedAngleDim,      ///< 線段角度 = value（相對水平）
    Slope,              ///< 線段斜度 dy/dx = value（無單位比值）。
                         ///< refs[0] = 線（WholeGeom/Curve 皆可，只取 geomUuid）。
                         ///< 依「工程慣例」定符號：以該線 Start→End 方向為準，
                         ///< value = (y_End - y_Start) / (x_End - x_Start)；
                         ///< 沿 Start→End 方向「上升」為正、「下降」為負
                         ///< （與既有 VAlignProfileView 坡度 grade 正負號慣例一致）。
                         ///< 輸入格式支援 "1:40"（比例，1 單位垂直:40 單位水平）
                         ///< 與 "2.5%"（百分比坡度），兩者換算後存成同一個
                         ///< 無單位 value（例如 1:40 與 2.5% 皆存為 0.025）。

    // ── 鎖定 ───────────────────────────────────
    Fixed,              ///< 整個幾何元素固定（消耗所有 DOF）

    // ── General Dimension 新增 ─────────────────────────────────
    FixedLength,        ///< 一條線段的長度 = value
    FixedDiameter,      ///< 圓/弧直徑 = value（顯示 Ø 符號）
    FixedHorizDist,     ///< 兩點水平距離 = value
    FixedVertDist,      ///< 兩點垂直距離 = value
    FixedArcLength,     ///< 圓弧弧長 = value
    CoordinateDim,      ///< 點相對原點的 (x, y) 座標尺寸（消耗 2 DOF）

    // ── 衍生幾何（角落特徵） ───────────────────────────────────
    Chamfer,            ///< 兩線倒角（消耗 2 DOF）。refs[0]=line1 的裁切端點
                         ///< （GeomHandle::Start 或 End，即被倒角移動到的那個
                         ///< 端點）、refs[1]=line2 的裁切端點（同理）；
                         ///< value=D1（line1 側裁切距離）、value2=D2（line2
                         ///< 側）。方程式見 ConstraintSolver.h 的
                         ///< ChamferEquation 類別說明——用 line1/line2「另一
                         ///< 端點」與裁切端點共同推導虛擬交點，讓 D1/D2 相對
                         ///< 「兩線目前即時位置」成立，而不是相對建立當下算
                         ///< 好、之後寫死的座標。取代舊版
                         ///< FixedDistance(交點,裁切點)+PointOnCurve(交點,線)
                         ///< 的 4 方程式組合——少 2 條方程式、也讓求解器把它
                         ///< 辨識成一個具名的「Chamfer」語意單位，而不是幾條
                         ///< 各自獨立、事後看不出彼此關聯的通用約束。
};

inline size_t qHash(const aicad::cad::ConstraintType &key, size_t seed = 0) noexcept {
    return ::qHash(static_cast<int>(key), seed);
}

// ─────────────────────────────────────────────────────────────────────────────
// 幾何參考：指向某個 SketchGeometry 的某個子元素
// ─────────────────────────────────────────────────────────────────────────────
struct GeomRef {
    QString    geomUuid;                  ///< 目標幾何的 UUID
    GeomHandle handle = GeomHandle::WholeGeom;

    GeomRef() = default;
    GeomRef(const QString& uuid, GeomHandle h) : geomUuid(uuid), handle(h) {}

    QJsonObject toJson() const;
    static GeomRef fromJson(const QJsonObject&);

    // ── Phase 0B / Task C ────────────────────────────────────────────────────
    /// 解析此 ref 在 Sketch 中對應的 SketchPoint UUID。
    /// - 直接指向 SketchPoint → 回傳 geomUuid
    /// - 指向 SketchLine  + Start/End      → startUuid / endUuid
    /// - 指向 SketchArc   + Start/End/Center → 對應 UUID
    /// - 指向 SketchCircle Center/WholeGeom → centerUuid
    /// - 其他 → 回傳 {}
    QString resolvedPointUuid(const Sketch* sketch) const;

    /// 判斷此 ref 是否直接引用一個 SketchPoint（非曲線）
    bool isDirectPoint(const Sketch* sketch) const;

    /// 解析此 ref 對應的 2D 草圖座標
    QVector2D resolvePosition(const Sketch* sketch) const;
};

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3B：DistanceMode — 必須在 SketchConstraint 之前定義
// ─────────────────────────────────────────────────────────────────────────────

enum class DistanceMode {
    PointToPoint,   ///< 兩端點之間直線距離
    PointToLine,    ///< 點到直線的最短（垂直）距離
    LineToLine,     ///< 兩平行線之間的垂直距離
    Invalid,        ///< 判斷失敗（如不平行的兩線）
};

// ─────────────────────────────────────────────────────────────────────────────
// 約束結構體
// ─────────────────────────────────────────────────────────────────────────────
struct SketchConstraint {
    QString        uuid;
    ConstraintType type;
    QList<GeomRef> refs;   ///< 參與約束的幾何參考（1~3個）
    double         value = 0.0;  ///< 尺寸約束的目標值（求值後的快取）
    double         value2 = 0.0; ///< 第二數值（CoordinateDim 的 Y 值）

    // ── 第 17 項回報：X/Y 軸、原點參考幾何的 Fixed 約束用（防止漂移）──────
    // 一般 Fixed 約束（使用者手動下的「固定」）語意上就是「鎖在使用者
    // 目前擺放的位置」，求解器用的是「每次求解開始時的當下座標」當
    // snapshot，這是正確、預期中的行為。但 X 軸／Y 軸／原點這幾個由
    // Sketch::xAxisGeomUuid()/yAxisGeomUuid()/originPointUuid() 自動產生
    // 的參考幾何，理論上应該永遠釘死在設計座標（例如原點永遠是
    // (0,0)），不該受這種「重新取樣」影響——否則多次求解下來，
    // SVD／Tikhonov 阻尼等數值誤差會逐次累積，讓這些理論上不動的參考
    // 幾何實際上慢慢飄移。fixedAbsolute=true 時，ConstraintSolver 改用
    // value/value2 當成永久不變的絕對座標，不管求解過程中座標飄了多少，
    // 下一次求解都會鎖回同一個絕對值。
    bool           fixedAbsolute = false;
    QString        paramExpr;    ///< 原始參數表達式（如 "width"、"width*2"）
    bool           driving = true;  ///< driving=true：約束驅動幾何；false：量測模式
    // Phase 3B 新增欄位
    DistanceMode    distMode = DistanceMode::PointToPoint;  ///< 距離子類型
    double          dimLineOffsetX = 0.0;  ///< 尺寸線偏移 X（草圖平面座標）
    double          dimLineOffsetY = 0.0;  ///< 尺寸線偏移 Y

    // ── 第 10 項回報的後續需求：「翻轉方向」設定後要能記住/存檔 ──────────
    // FixedDistance 用：記住上次求解收斂時的「哪一側」，取代每次求解都
    // 從當下幾何位置現算（那樣沒辦法跨越「翻轉方向」按鈕的操作、也没辦法
    // 撐過距離值經過/接近 0 的瞬間——見 ConstraintSolver.cpp 的
    // FixedDistanceEquation::lockReference() 說明）。
    // PointToLine/LineToLine 用 distSideSign（+1.0／-1.0）；PointToPoint
    // 用 distSideDirX/Y（單位方向向量）。全部為 0.0 表示「尚未鎖定過」，
    // 這種情況才會退回用當下幾何位置現算。
    // ⚠️ 對於 implicitOf 非空的隱含約束（GDIM 尺寸），這三個欄位只是
    // 「這次執行期間」的工作副本；真正的存檔來源是對應
    // SketchAnnotation 的同名欄位，見 SketchAnnotation::toImplicitConstraint()
    // /Sketch::solveConstraints() 尾端的同步說明。
    double          distSideSign = 0.0;
    double          distSideDirX = 0.0;
    double          distSideDirY = 0.0;

    // ── GDIM v2 Phase 1 ──────────────────────────────────────────────────
    /// 非空時，表示這是由某個 SketchAnnotation（uuid == implicitOf）
    /// 透過 Sketch::addImplicitConstraint() 自動生成/同步的「隱含約束」，
    /// 純供 ConstraintSolver 內部使用。這類約束不由使用者直接建立/編輯，
    /// 存檔時會被排除（不寫入 "constraints" JSON 陣列），改由對應的
    /// SketchAnnotation 於載入後重新生成，避免資料重複。
    QString         implicitOf;

    /**
     * 判斷此約束是否為尺寸約束（帶數值）
     */
    bool isDimensional() const;

    /**
     * 從指定 store（可為 instance store 或 master store）求值。
     * store 內部已實作父子 fallback，呼叫者無需關心層級。
     */
    bool evaluateValue(const aicad::core::ParameterStore* store);

    SketchConstraint()
        : uuid(QUuid::createUuid().toString(QUuid::WithoutBraces))
        , type(ConstraintType::Coincident) {}

    // 工廠方法（語意清晰）
    static SketchConstraint makeCoincident(const GeomRef& a, const GeomRef& b);
    static SketchConstraint makeHorizontal(const QString& lineUuid);
    static SketchConstraint makeVertical(const QString& lineUuid);
    static SketchConstraint makeParallel(const QString& lineA, const QString& lineB);
    static SketchConstraint makePerpendicular(const QString& lineA, const QString& lineB);
    static SketchConstraint makeTangent(const QString& geomA, const QString& geomB);
    static SketchConstraint makeEqualLength(const QString& lineA, const QString& lineB);
    static SketchConstraint makeEqualRadius(const QString& circA, const QString& circB);
    static SketchConstraint makeConcentric(const QString& geomA, const QString& geomB);
    static SketchConstraint makeFixed(const QString& geomUuid);
    static SketchConstraint makeFixedDistance(const GeomRef& a, const GeomRef& b, double dist);
    static SketchConstraint makeFixedRadius(const QString& geomUuid, double radius);
    static SketchConstraint makeFixedX(const GeomRef& point, double x);
    static SketchConstraint makeFixedY(const GeomRef& point, double y);
    static SketchConstraint makePointOnCurve(const GeomRef& point, const QString& curveUuid);
    static SketchConstraint makeMidpoint(const GeomRef& point, const QString& lineUuid);
    static SketchConstraint makeSymmetric(const GeomRef& a, const GeomRef& b, const QString& axisUuid);
    static SketchConstraint makeCollinear(const QString& lineA, const QString& lineB);

    // General Dimension 工廠方法
    static SketchConstraint makeFixedLength    (const QString& lineUuid, double len);
    static SketchConstraint makeFixedDiameter  (const QString& geomUuid, double dia);
    static SketchConstraint makeFixedHorizDist (const GeomRef& a, const GeomRef& b, double d);
    static SketchConstraint makeFixedVertDist  (const GeomRef& a, const GeomRef& b, double d);
    static SketchConstraint makeFixedArcLength (const QString& arcUuid, double len);
    static SketchConstraint makeCoordinateDim  (const GeomRef& point, double x, double y);
    static SketchConstraint makeSlope          (const QString& lineUuid, double slope);

    // 衍生幾何（角落特徵）工廠方法
    // ⚠️ line1Ref/line2Ref 的 geomUuid 必須是「線」的 UUID（handle=Start
    //    或 End 選裁切端點是哪一端），不能是裁切端點本身那個 SketchPoint
    //    的 UUID——GeomVarLayout::indexFor() 對點的 2 值 layout 呼叫
    //    indexFor(End) 會算出 offset+2，讀到不相關的變數，讓
    //    ChamferEquation 算出垃圾殘差、被 solveConstraints() 誤判成
    //    Conflict（真實案例：TrimExtendHelper.cpp::chamferAt() 曾經傳錯
    //    成點的 UUID，導致兩條單純相交、明明有解的線被判定無解）。
    static SketchConstraint makeChamfer(const GeomRef& line1Ref, const GeomRef& line2Ref,
                                        double d1, double d2);

    // DOF 消耗量（用於 under/over 約束檢查）
    int dofConsumed() const;

    QJsonObject toJson() const;
    static SketchConstraint fromJson(const QJsonObject&);
};

// ─────────────────────────────────────────────────────────────────────────────
// 求解狀態
// ─────────────────────────────────────────────────────────────────────────────
enum class SolveStatus {
    FullyConstrained,     ///< DOF = 0，解唯一
    UnderConstrained,     ///< DOF > 0，有自由度殘留
    OverConstrained,      ///< DOF < 0，約束矛盾
    Conflict,             ///< 幾何上無解（如要求兩平行線距離且互相垂直）
    SolverError
};

struct SolveResult {
    SolveStatus status   = SolveStatus::UnderConstrained;
    int         dof      = 0;       ///< 剩餘自由度
    double      residual = 0.0;     ///< 最終殘差範數
    int         iterations = 0;
    QStringList conflictingConstraints;  ///< 衝突的約束 UUID
};

} // namespace aicad::cad
// ─────────────────────────────────────────────────────────────────────────────
// Phase 0B / Phase 3B 擴充（DistanceMode 已移至 SketchConstraint 之前）
// ─────────────────────────────────────────────────────────────────────────────

