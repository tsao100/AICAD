/**
 * @file AssemblyPart.h
 * @brief 組合中的一個「零件」：外部 *.aicad 的 3D 實體（內嵌 BRep 副本）＋ 剛體位姿 ＋ 是否固定。
 *
 * 零件本地座標 = 該 *.aicad 檔自己的座標（單位依檔案，通常 mm）。放進組合時：
 *     世界座標 = pose ∘ scale · 本地座標        （scale = unitScale；同單位放 1，mm 檔放進 m 制文件用 0.001）
 * pose 是剛體變換（旋轉＋平移），由「配合求解器」依 AssemblyMate 決定；使用者也可直接給初始位置／轉角，
 * 之後的配合會以此為起點做最小移動。固定（isFixed）的零件視為接地、不被求解器移動。
 *
 * 取得零件實體的規則與「3D 組件安裝」相同（ComponentInstall::extractFromDocument）：檔內所有 3D 特徵的終端結果合併；
 * 來源檔中歸屬 Void 根（開孔空間）的特徵不納入零件。來源檔存檔後可自動同步（QFileSystemWatcher），同步後配合依面／邊序號重新取得幾何並重新求解。
 */
#pragma once

#include "Feature.h"

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QStringList>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

namespace aicad {
namespace cad {

class AssemblyPart : public Feature {
    Q_OBJECT
public:
    explicit AssemblyPart(Document* parent = nullptr);
    ~AssemblyPart() override = default;

    FeatureType type()       const override { return FeatureType::AssemblyPart; }
    QString     typeString() const override { return QStringLiteral("AssemblyPart"); }

    // ── 來源 ─────────────────────────────────────────────────────────────
    void setSource(const QString& sourcePath, const TopoDS_Shape& localShape, const QStringList& featureNames);
    QString             sourcePath()   const { return m_sourcePath; }
    QStringList         featureNames() const { return m_featureNames; }
    const TopoDS_Shape& localShape()   const { return m_local; }

    // ── 單位 ─────────────────────────────────────────────────────────────
    double unitScale() const { return m_unitScale; }
    void   setUnitScale(double s) { if (s > 0.0) m_unitScale = s; }

    // ── 固定 ─────────────────────────────────────────────────────────────
    bool isFixed() const { return m_fixed; }
    void setFixed(bool f) { m_fixed = f; }

    // ── 位姿（剛體；不含縮放）──────────────────────────────────────────────
    gp_Trsf pose() const { return m_pose; }
    void    setPose(const gp_Trsf& t) { m_pose = t; }
    /// 本地（含單位縮放）→ 世界 的完整變換 = pose ∘ scale
    gp_Trsf fullTrsf() const;

    /// 以平移（組合單位）與繞 X、Y、Z 的轉角（度；外旋 XYZ：先 X、再 Y、最後 Z，皆繞世界軸）建立剛體位姿
    static gp_Trsf makePose(double x, double y, double z, double rxDeg, double ryDeg, double rzDeg);
    /// 反向：由位姿取平移與轉角（度）
    static void    decomposePose(const gp_Trsf& t, double& x, double& y, double& z,
                                 double& rxDeg, double& ryDeg, double& rzDeg);
    /// 剛體位姿 ⇄ 旋轉矩陣（列主序 9 個數）與平移
    static void    poseToRT(const gp_Trsf& t, double R[9], double T[3]);
    static gp_Trsf poseFromRT(const double R[9], const double T[3]);

    /// 求解後的剩餘自由度（資訊；不存檔）。-1 = 未知
    int  dof() const { return m_dof; }
    void setDof(int d) { m_dof = d; }

    // ── 與來源 *.aicad 同步 ────────────────────────────────────────────────
    bool sourceIsNewer() const;
    bool syncFromSource(QString* error = nullptr);
    void setAutoSync(bool on);
    bool autoSync() const { return m_autoSync; }
    bool attachSourceWatcher();           ///< 讀檔後呼叫；來源檔較新時自動同步一次，回傳是否有同步

    bool rebuild() override;              ///< 依 pose／scale 重新放置形狀（顯示用）

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;

    /// 刻意為空：被依賴的 Feature 會被 CadView 視為「已被消耗」而隱藏；零件必須一直顯示。
    QSet<QString> featureDependencies() const override { return {}; }

Q_SIGNALS:
    void sourceSynced();                  ///< 同步來源檔之後（由 Document 接手重新取得配合幾何並求解）

private:
    QString      m_sourcePath;
    QStringList  m_featureNames;
    QDateTime    m_sourceMTime;
    bool         m_autoSync = true;
    QFileSystemWatcher* m_watcher = nullptr;

    TopoDS_Shape m_local;
    double       m_unitScale = 1.0;
    bool         m_fixed = false;
    gp_Trsf      m_pose;
    int          m_dof = -1;
};

} // namespace cad
} // namespace aicad
