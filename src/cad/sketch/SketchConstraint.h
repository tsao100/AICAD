#pragma once
#include <QString>
#include <QVector2D>
#include <QJsonObject>
#include <QUuid>

// Forward declare to avoid circular includes
namespace aicad::core { class ParameterStore; }

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

    // ── 鎖定 ───────────────────────────────────
    Fixed,              ///< 整個幾何元素固定（消耗所有 DOF）
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
};

// ─────────────────────────────────────────────────────────────────────────────
// 約束結構體
// ─────────────────────────────────────────────────────────────────────────────
struct SketchConstraint {
    QString        uuid;
    ConstraintType type;
    QVector<GeomRef> refs;   ///< 參與約束的幾何參考（1~3個）
    double         value = 0.0;  ///< 尺寸約束的目標值（求值後的快取）
    QString        paramExpr;    ///< 原始參數表達式（如 "width"、"width*2"）
    bool           driving = true;  ///< driving=true：約束驅動幾何；false：量測模式

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