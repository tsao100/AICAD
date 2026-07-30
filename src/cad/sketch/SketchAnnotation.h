#pragma once
#include "SketchConstraint.h"   // GeomRef / GeomHandle / DistanceMode
#include <QList>
#include <QString>
#include <QVector2D>
#include <QJsonObject>
#include <QUuid>
#include <optional>

namespace aicad::core { class ParameterStore; }

// ─────────────────────────────────────────────────────────────────────────────
// GDIM 昇級規劃 v2 — Phase 1
//
// SketchAnnotation 是「標註（Annotation）」的純資料模型，從 SketchConstraint /
// ConstraintType 中拆出來。設計原則（見 GDIM_昇級規劃_v2.md 第一節）：
//
//   - SketchConstraint / ConstraintType ─ 只保留「純幾何約束」
//     （Coincident/Parallel/Tangent/... 等，無 Prefix/Suffix/Tolerance 等
//     標註專屬屬性），求解器（ConstraintSolver）只吃這個。
//   - SketchAnnotation / AnnotationKind ─ 標註專用，擁有 Prefix/Suffix/
//     Tolerance/Precision/Leader 等欄位，渲染層（AnnotationAIS 家族，
//     Phase 3）只吃這個。
//
//   兩者之間的橋接：當 driving == true 時，SketchAnnotation 透過
//   Sketch::addImplicitConstraint() 在內部生成/同步一個「隱含約束」
//   （仍是 SketchConstraint，type 沿用舊有的 FixedLength/FixedRadius/...
//   等值），餵給 ConstraintSolver。這是刻意保留的實作細節：
//   ConstraintEquation 目前以「const SketchConstraint&」參照方式綁定
//   幾何求解方程式（見 ConstraintSolver.h），若要讓 Solver 直接吃
//   SketchAnnotation，需要同時重寫所有 *Equation 子類別（涉及數值解算，
//   依專案的求解器驗證原則，須以真實 .ALD 資料數值驗證後才可變更）。
//   因此 Phase 1 選擇：對外（UI / 分類器 / 渲染）完全改用
//   SketchAnnotation；對內（Solver）沿用既有、已驗證過的方程式實作，
//   僅改變它們的「來源」——不再由外部直接建立，而是由
//   Sketch::addImplicitConstraint() 依 SketchAnnotation 內容產生並維護。
// ─────────────────────────────────────────────────────────────────────────────

namespace aicad::cad {

// ─────────────────────────────────────────────────────────────────────────────
// AnnotationKind — 取代 ConstraintType 中原本的「尺寸子集」
// 對應關係（舊 ConstraintType → 新 AnnotationKind）：
//   FixedDistance  → Distance
//   FixedRadius    → Radius
//   FixedX         → X
//   FixedY         → Y
//   FixedAngleDim  → AngleDim
//   FixedLength    → Length
//   FixedDiameter  → Diameter
//   FixedHorizDist → HorizDist
//   FixedVertDist  → VertDist
//   FixedArcLength → ArcLength
//   CoordinateDim  → Coordinate
// ─────────────────────────────────────────────────────────────────────────────
enum class AnnotationKind {
    Distance,     ///< 兩點距離（含 PointToPoint/PointToLine/LineToLine，見 DistanceMode）
    Radius,       ///< 半徑
    Diameter,     ///< 直徑（顯示 Ø）
    X,            ///< 點的 X 座標
    Y,            ///< 點的 Y 座標
    Coordinate,   ///< 點的 (X, Y) 座標尺寸（同時消耗 2 DOF）
    AngleDim,     ///< 兩線夾角
    Length,       ///< 線段長度
    HorizDist,    ///< 兩點水平距離
    VertDist,     ///< 兩點垂直距離
    ArcLength,    ///< 弧長

    // ── Phase 8：Leader / Hole Note（資料欄位於 Phase 1 先預留） ──────────
    LeaderNote,   ///< 引出線 + 文字註記（如 "M20"），無幾何驅動意義
};

inline size_t qHash(const aicad::cad::AnnotationKind &key, size_t seed = 0) noexcept {
    return ::qHash(static_cast<int>(key), seed);
}

// ─────────────────────────────────────────────────────────────────────────────
// ToleranceSpec — 公差設定（GDIM.md 第五節：Tolerance / Basic / Inspection）
// ─────────────────────────────────────────────────────────────────────────────
enum class ToleranceMode {
    None,        ///< 無公差
    Symmetric,   ///< ±value（單一數值）
    Deviation,   ///< +upper / -lower（不對稱偏差）
    Limit,       ///< 上限/下限（Limit Dimension，顯示為兩行數值）
    Basic,       ///< Basic Dimension（外框方框，理論值，通常無公差文字）
};

/// AnnotationKind → 舊 ConstraintType 對照（純值轉換，不含 refs/value 等）。
/// 供 GeneralDimCommand（Phase 2）等仍以 ConstraintType 驅動內部邏輯
/// （測量/預覽/commit）的呼叫端使用；LeaderNote 沒有對應值，回傳 std::nullopt。
std::optional<ConstraintType> annotationKindToConstraintType(AnnotationKind kind);

/// 反向對照（供仍以 ConstraintType 驅動內部邏輯的呼叫端，例如
/// GeneralDimCommand::commitDimension()，在真正寫入 Sketch 前轉換回
/// AnnotationKind 以便走 Sketch::addAnnotation() 這條統一路徑）。
/// 純幾何約束型別（Coincident/Parallel/.../FixedAngle 等）沒有對應的
/// AnnotationKind，回傳 std::nullopt。
std::optional<AnnotationKind> constraintTypeToAnnotationKind(ConstraintType type);

struct ToleranceSpec {
    ToleranceMode mode  = ToleranceMode::None;
    double        upper = 0.0;   ///< Symmetric 模式下亦作為 ± 值
    double        lower = 0.0;

    bool isNone() const { return mode == ToleranceMode::None; }

    QJsonObject toJson() const;
    static ToleranceSpec fromJson(const QJsonObject&);
};

// ─────────────────────────────────────────────────────────────────────────────
// SketchAnnotation — 純標註物件
// ─────────────────────────────────────────────────────────────────────────────
struct SketchAnnotation {
    QString         uuid;
    AnnotationKind  kind = AnnotationKind::Distance;
    QList<GeomRef>  refs;               ///< 參與標註的幾何參考（1~3 個）

    double          value  = 0.0;       ///< 主數值（求值後快取）
    double          value2 = 0.0;       ///< 第二數值（Coordinate 的 Y 值）
    QString         paramExpr;          ///< 原始參數表達式

    bool            driving = true;     ///< true=驅動幾何（會產生隱含約束）；false=僅量測顯示

    DistanceMode    distMode = DistanceMode::PointToPoint;
    QVector2D       dimLineOffset;      ///< 尺寸線偏移（草圖平面座標）

    // ── 標註專屬屬性（SketchConstraint 從未支援，是拆分的主要理由） ──────
    QString         prefix;
    QString         suffix;
    ToleranceSpec   tolerance;
    int             precision   = 2;    ///< 小數位數，-1 表示沿用文件預設
    bool            isBasic      = false;
    bool            isInspection = false;

    // ── Phase 8 預留：Leader / Hole Note ─────────────────────────────────
    QList<QVector2D> leaderVertices;    ///< Leader 路徑上的中繼折點（可編輯）
    QString           noteText;         ///< LeaderNote 專用文字（如 "M20"）

    SketchAnnotation()
        : uuid(QUuid::createUuid().toString(QUuid::WithoutBraces)) {}

    /// 是否為尺寸類標註（消耗自由度，區別於單純的 Leader 文字註記）
    bool isDimensional() const { return kind != AnnotationKind::LeaderNote; }

    /// DOF 消耗量（僅在 driving == true 且 isDimensional() 時有意義；
    /// 供 UI／DOF 顯示使用，與 Sketch::addImplicitConstraint() 產生的
    /// 內部隱含約束的 dofConsumed() 應保持一致）
    int dofConsumed() const;

    /// 由指定 store 求值 paramExpr
    bool evaluateValue(const aicad::core::ParameterStore* store);

    // 工廠方法（對應舊 SketchConstraint::makeFixed*，供 GeneralDimCommand /
    // Phase 2 分類器使用）
    static SketchAnnotation makeDistance (const GeomRef& a, const GeomRef& b, double d, DistanceMode mode = DistanceMode::PointToPoint);
    static SketchAnnotation makeRadius   (const QString& geomUuid, double r);
    static SketchAnnotation makeDiameter (const QString& geomUuid, double d);
    static SketchAnnotation makeX        (const GeomRef& point, double x);
    static SketchAnnotation makeY        (const GeomRef& point, double y);
    static SketchAnnotation makeCoordinate(const GeomRef& point, double x, double y);
    static SketchAnnotation makeAngleDim (const GeomRef& a, const GeomRef& b, double angleRad);
    static SketchAnnotation makeLength   (const QString& lineUuid, double len);
    static SketchAnnotation makeHorizDist(const GeomRef& a, const GeomRef& b, double d);
    static SketchAnnotation makeVertDist (const GeomRef& a, const GeomRef& b, double d);
    static SketchAnnotation makeArcLength(const QString& arcUuid, double len);
    static SketchAnnotation makeLeaderNote(const GeomRef& target, const QString& text);

    /// 將此標註轉換為內部使用的「隱含約束」（供 Sketch::addImplicitConstraint()
    /// 呼叫）。回傳的 SketchConstraint::uuid 恆等於本標註的 uuid，
    /// SketchConstraint::implicitOf 也會設為本標註的 uuid，
    /// 兩者搭配用來讓 Sketch 在同步/移除時能找到對應的隱含約束。
    /// LeaderNote 沒有對應的隱含約束，回傳 std::nullopt。
    std::optional<SketchConstraint> toImplicitConstraint() const;

    QJsonObject toJson() const;
    static SketchAnnotation fromJson(const QJsonObject&);
};

} // namespace aicad::cad
