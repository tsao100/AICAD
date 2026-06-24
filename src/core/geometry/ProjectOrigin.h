#ifndef AICAD_CORE_GEOMETRY_PROJECTORIGIN_H
#define AICAD_CORE_GEOMETRY_PROJECTORIGIN_H

/**
 * @file ProjectOrigin.h
 * @brief 專案 Global（TM2 / TWD97）↔ Local（OCCT / CAD）座標轉換核心。
 *
 * 架構原則：
 *   - OCCT / OpenGL / V3d_View 永遠只看到 Local 座標（量級 10³~10⁵）。
 *   - TM2 大數值只存在於「輸入框」「顯示文字」「檔案 I/O」三個邊界。
 *
 * 轉換公式（向量關係）：
 *   local  = global − origin         （global → local）
 *   global = local  + origin         （local  → global）
 *
 * 與 CoordinateTransform 的差異：
 *   - CoordinateTransform : 2D Sketch Plane ↔ 3D OCCT World，供 Sketch 使用，
 *                           與 TM2 完全無關。
 *   - ProjectOrigin       : TM2 Global ↔ CAD Local，供 Alignment / Railway 使用。
 *
 * 設計為 non-QObject 單例（與 PlaneManager 同風格），避免 MOC 處理 OCCT 標頭的
 * 既有限制。若需要跨模組通知 origin 變更，透過 EventBus 發布
 * Events::PROJECT_ORIGIN_CHANGED 事件。
 */

#include <QPointF>
// Forward-declare gp_Pnt to avoid pulling full OCCT headers into every TU
class gp_Pnt;
#include <QString>
#include <QJsonObject>

namespace aicad {
namespace core {
namespace geometry {

class ProjectOrigin
{
public:
    // ── 單例存取 ─────────────────────────────────────────────────────────────
    static ProjectOrigin& instance();

    // ── 狀態查詢 ─────────────────────────────────────────────────────────────
    /// 是否已設定 origin。
    /// 未設定時 toLocal / toGlobal 為恆等轉換，向下相容舊專案。
    bool isSet() const;

    double originE() const;   ///< Global East（TM2 二度分帶 X）
    double originN() const;   ///< Global North（TM2 二度分帶 Y）
    double originZ() const;   ///< Global Z（高程）

    QString epsgCode() const; ///< 座標系統代碼，預設 "EPSG:3826"（TWD97 / TM2 121）

    // ── 設定 / 清除 ─────────────────────────────────────────────────────────
    /// 設定 origin（Global 座標系中 Local 原點對應的位置）。
    /// 發布 EventBus "project.origin.changed" 通知所有訂閱者。
    void setOrigin(double globalE, double globalN, double globalZ = 0.0);

    void setEpsgCode(const QString& code);

    /// 清除 origin，回到恆等轉換狀態（isSet() == false）。
    void clear();

    // ── Global(TM2) → Local(CAD) ─────────────────────────────────────────────
    /// 將 TM2 東向 / 北向（E, N）轉成 CAD Local XY 平面座標。
    QPointF toLocal(const QPointF& globalEN) const;
    QPointF toLocal(double E, double N) const;

    /// 三維版本（含高程，Z 轉換同樣為平移）。
    void toLocal(double E, double N, double Z,
                 double& outX, double& outY, double& outZ) const;

    /// gp_Pnt 版本（供 AlignmentRenderer / OCCT 幾何直接使用，定義在 .cpp 避免標頭污染）。
    gp_Pnt toLocalPnt(double E, double N, double Z = 0.0) const;

    // ── Local(CAD) → Global(TM2) ─────────────────────────────────────────────
    /// 將 CAD Local XY 座標轉回 TM2 東向 / 北向。
    QPointF toGlobal(const QPointF& localXY) const;
    QPointF toGlobal(double x, double y) const;

    /// 三維版本。
    void toGlobal(double x, double y, double z,
                  double& outE, double& outN, double& outZ) const;

    // ── 便利多載（供 AlignmentRenderer 等直接傳 double E/N/Z 三個參數使用）────
    /// 三維轉換並直接回傳三個 double（不用 out-param 版），方便 gp_Pnt 構造。
    /// 範例：auto [lx,ly,lz] = origin.toLocalTuple(E, N, Z);
    struct LocalXYZ { double x, y, z; };
    LocalXYZ toLocalTuple(double E, double N, double Z = 0.0) const;

    struct GlobalENZ { double E, N, Z; };
    GlobalENZ toGlobalTuple(double x, double y, double z = 0.0) const;

    // ── 持久化 ───────────────────────────────────────────────────────────────
    /// 序列化成 JSON（供 AlignmentDocument::toJson() 嵌入）。
    QJsonObject toJson() const;

    /// 從 JSON 還原（缺欄位時視為 isSet()==false，向下相容舊檔案）。
    void fromJson(const QJsonObject& obj);

private:
    // 禁止外部構造／複製（單例模式）
    ProjectOrigin() = default;
    ~ProjectOrigin() = default;
    ProjectOrigin(const ProjectOrigin&) = delete;
    ProjectOrigin& operator=(const ProjectOrigin&) = delete;

    /// 發布 Events::PROJECT_ORIGIN_CHANGED 到 EventBus（setOrigin / clear / fromJson 後呼叫）
    void publishChanged();

    bool    m_isSet   = false;
    double  m_originE = 0.0;
    double  m_originN = 0.0;
    double  m_originZ = 0.0;
    QString m_epsgCode = QStringLiteral("EPSG:3826"); // TWD97 / TM2 121
};

} // namespace geometry
} // namespace core
} // namespace aicad

#endif // AICAD_CORE_GEOMETRY_PROJECTORIGIN_H
