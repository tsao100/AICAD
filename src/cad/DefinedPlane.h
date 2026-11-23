/**
 * @file DefinedPlane.h
 * @brief 使用者定義的平面：可用多種方式定義（見 docs/DefinedPlane_Methods.md）。
 *
 *  1 從既有平面偏移  2 取線端點、切線當法向量  3 一點+法向量方向  4 三點
 *  5 過一點+一線     6 兩線（共面）            7 繞軸旋轉角度     8 兩平面的中間／等分平面
 *  9 過一點平行於平面 10 線形里程橫斷面
 *
 * 平面 = 原點 O + 法向量 N + 面內 X 軸。Feature Browser 的獨立項目，3D 視圖中為半透明方形，
 * 可當草圖平面（右鍵「在此平面建立草圖」，或 Create Sketch 時在樹狀圖點選）。
 * 參照的草圖／線形／基準平面改變時自動重算。
 */
#pragma once

#include "Feature.h"

#include <QJsonObject>
#include <QMetaObject>
#include <QString>
#include <QVector3D>

namespace aicad {
namespace cad {

class Plane;
class Sketch;

// ─────────────────────────────────────────────────────────────────────────────
// 參照
// ─────────────────────────────────────────────────────────────────────────────

/// 取草圖幾何的哪一個點
enum PlaneGeomWhich { WhichStart = 0, WhichEnd = 1, WhichMid = 2, WhichCenter = 3 };

/// 點的參照
struct PlanePointRef {
    enum Kind { Coord = 0, GeomPoint = 1, Alignment = 2 };
    Kind    kind = Coord;
    double  x = 0, y = 0, z = 0;          ///< Coord：世界座標
    QString sketchId, geomUuid;           ///< GeomPoint：草圖與其中的幾何
    int     which = WhichStart;           ///< GeomPoint：起點／終點／中點／圓心（見 PlaneGeomWhich）
    QString tclId;                        ///< Alignment：線路與里程
    double  chainage = 0;

    QJsonObject toJson() const;
    static PlanePointRef fromJson(const QJsonObject& o);
};

/// 方向的參照
struct PlaneDirRef {
    enum Kind { Vector = 0, GeomTangent = 1, Axis = 2, AlignmentTangent = 3 };
    Kind    kind = Vector;
    double  x = 0, y = 0, z = 1;          ///< Vector
    QString sketchId, geomUuid;           ///< GeomTangent：草圖幾何端點的切線
    int     which = WhichEnd;             ///< GeomTangent：起點／終點（WhichStart／WhichEnd）
    int     axis = 2;                     ///< Axis：0 = X、1 = Y、2 = Z
    QString tclId;                        ///< AlignmentTangent
    double  chainage = 0;
    bool    reverse = false;              ///< 反向

    QJsonObject toJson() const;
    static PlaneDirRef fromJson(const QJsonObject& o);
};

/// 線（有位置的方向）的參照
struct PlaneLineRef {
    enum Kind { SketchLine = 0, TwoPoints = 1 };
    Kind    kind = SketchLine;
    QString sketchId, geomUuid;           ///< SketchLine：草圖中的直線（起點→終點）
    PlanePointRef a, b;                   ///< TwoPoints：a → b

    QJsonObject toJson() const;
    static PlaneLineRef fromJson(const QJsonObject& o);
};

// ─────────────────────────────────────────────────────────────────────────────
// 定義
// ─────────────────────────────────────────────────────────────────────────────
enum class PlaneMethod {
    Offset = 0,            ///< 從既有平面偏移（baseRef, offsetExpr）
    EndpointTangent,       ///< 取線端點，切線當法向量（p1 = GeomPoint 端點，dir1 = 同一幾何同一端的切線）
    PointNormal,           ///< 一點 + 法向量方向（p1, dir1）
    ThreePoints,           ///< 三點（p1, p2, p3）
    PointLine,             ///< 過一點 + 一線（p1, line1）
    TwoLines,              ///< 兩線共面（line1, line2）
    AngleFromPlane,        ///< 繞軸線旋轉角度（line1 = 軸、baseRef = 參考平面、angleExpr = 度）
    MidPlane,              ///< 兩平面的中間／等分平面（baseRef, baseRef2）
    ParallelThroughPoint,  ///< 過一點且平行於平面（baseRef, p1）
    AlignmentSection       ///< 線形里程橫斷面（p1 = Alignment，dir1 = AlignmentTangent）
};

struct PlaneDef {
    PlaneMethod method = PlaneMethod::Offset;
    QString baseRef  = QStringLiteral("XY");   ///< "XY"/"XZ"/"YZ" 或 DefinedPlane 的 id
    QString baseRef2 = QStringLiteral("XZ");   ///< MidPlane 的第二個平面
    QString offsetExpr = QStringLiteral("0");  ///< Offset：沿基準法向量的有號偏移
    QString angleExpr  = QStringLiteral("90"); ///< AngleFromPlane：角度（度）
    bool    flip = false;                      ///< 反向（翻轉法向量）
    PlanePointRef p1, p2, p3;
    PlaneDirRef   dir1;
    PlaneLineRef  line1, line2;

    QJsonObject toJson() const;
    static PlaneDef fromJson(const QJsonObject& o);
};

// ─────────────────────────────────────────────────────────────────────────────
class DefinedPlane : public Feature {
    Q_OBJECT
public:
    /// 求解結果：平面座標系
    struct Frame {
        QVector3D origin{0, 0, 0}, normal{0, 0, 1}, xAxis{1, 0, 0};
    };

    explicit DefinedPlane(Document* parent = nullptr);
    ~DefinedPlane() override = default;

    FeatureType type()       const override { return FeatureType::DefinedPlane; }
    QString     typeString() const override { return QStringLiteral("DefinedPlane"); }

    // ── 定義 ───────────────────────────────────────────────────────────
    PlaneDef definition() const { return m_def; }
    /// 設定定義（先求解驗證；失敗不修改）。成功後呼叫 Document::rebuildFeature() 重建。
    bool setDefinition(const PlaneDef& def, QString* error = nullptr);
    /// 相容舊介面：偏移平面
    bool setDefinition(const QString& baseRef, const QString& offsetExpr, QString* error = nullptr);

    // 偏移平面的相容存取（其他方式回傳預設值）
    QString baseRef()          const { return m_def.baseRef; }
    QString offsetExpression() const { return m_def.offsetExpr; }
    double  offset()           const { return m_offsetValue; }

    /// 例如「偏移（XY Plane，-90）」「三點」「線形里程橫斷面」
    QString description() const;
    /// 基準平面的顯示名稱（偏移／平行／中間平面等方式用；其他方式回傳方式名稱）
    QString baseDisplayName() const;
    static QString methodLabel(PlaneMethod m);

    // ── 計算結果 ───────────────────────────────────────────────────────
    QVector3D origin() const { return m_frame.origin; }
    QVector3D normal() const { return m_frame.normal; }
    QVector3D xAxis()  const { return m_frame.xAxis; }

    /// 以此平面建立一個可供 Document::createSketch() 使用的 cad::Plane（由 PlaneManager 持有）
    Plane* createSketchPlane() const;

    /**
     * @brief 求解一個平面定義（對話框預覽與 rebuild() 共用）。
     * @param self 編輯既有平面時傳入自己，用來擋掉「以自己（或自己的下游）當基準」的循環
     */
    static bool evaluate(const Document* doc, const PlaneDef& def, Frame& out,
                         const DefinedPlane* self, QString* error);

    /// 舊介面：偏移平面求解（相容）
    static bool computeFrame(const Document* doc, const QString& baseRef, double offsetValue,
                             QVector3D& origin, QVector3D& normal, QVector3D& xAxis,
                             const DefinedPlane* self, QString* error);

    /// 把運算式（數字或參數運算式）求值
    static bool evalNumber(const Document* doc, const QString& expr, double& out);
    static bool evalOffset(const Document* doc, const QString& expr, double& out) { return evalNumber(doc, expr, out); }

    /// 草圖幾何的標籤（供選單顯示）與可取的點（供對話框）
    static QString geometryLabel(const Sketch* sk, int index);

    /// 重新接上參照（草圖／線形／基準平面）的變更訊號；參照變了就排程重建
    void refreshSources();
    void requestRebuild();

    bool rebuild() override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;

    /// 刻意回傳空集合：被其他 Feature 依賴的 Feature 會被 CadView 視為已被消耗而隱藏（串接的基準平面不該被藏起來）
    QSet<QString> featureDependencies() const override { return {}; }

private:
    PlaneDef m_def;
    Frame    m_frame;
    double   m_offsetValue = 0.0;
    QList<QMetaObject::Connection> m_conns;
    bool     m_rebuildPending = false;
};

} // namespace cad
} // namespace aicad
