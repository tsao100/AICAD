/**
 * @file TrackSlabPlan.h
 * @brief 道版 E,N 平面圖（Track Slab Plan）：把 Loft 上方曲面的邊緣，投影到「隨 cant 變化的軌道面」
 *        再展平，沿線形畫成 (E,N) 平面圖。整份文件一個、自動繪製。
 *
 * 做法（逐測站，與 Loft 使用同一組測站與同一組封閉輪廓）：
 *  1. 在每個測站的斷面草圖中，找出「受 cant 約束的直線」——尺寸（標註）的運算式直接或間接
 *     依賴參數 `cant` 的點所構成的那條線（例如 CurbPlinth 的建構線，兩端高度為
 *     abs(cant)/2 與 abs(cant)）。這條線的兩端延伸（等於無限長的直線）就是該測站的軌道面斷線；
 *     各測站的軌道面斷線沿樁號放樣，即為隨 cant 變化的軌道面。
 *  2. 取輪廓中「朝上」的邊（外法線與軌道面法線夾角小於門檻）。這些邊放樣後即為 Loft 的上方曲面。
 *  3. 每個測站把這些邊的點正交投影到該站的軌道面斷線 → 得到沿軌道面的橫向座標 t
 *     （以斷線起點為 0，方向為斷線方向，單位 m）。
 *  4. 展平並沿線形繪製：平面位置 (E, N) = 該測站在線形上的位置 + t × 橫向單位向量
 *     （橫向 = 斷面 +X 軸的水平分量，垂直於線形）。上方曲面的縱向邊 → 逐站連成折線；
 *     分段頭尾兩站的上緣 → 端部邊線。彎道處圖形自然隨線形轉彎。
 *
 * 結果是 z = 0 的 (E, N) 平面線框 compound，與模型同一座標系，可直接與線形、平面圖疊合。
 */
#pragma once

#include "Feature.h"
#include <QList>
#include <QMetaObject>
#include <QPointF>
#include <QVector>
#include <QSet>
#include <QString>

namespace aicad {
namespace cad {

class ProfileLoftSolid;

/**
 * 整份文件只有「一個」道版 E,N 平面圖，列在 Feature Browser 的根（root）底下。
 * 涵蓋文件中所有 ProfileLoftSolid（所有陣列、所有分段），由 Document::syncSlabPlan() 自動建立／移除：
 *   - 文件中有 Loft、且其斷面草圖有 `cant` 參數 → 自動建立（不需要手動操作）
 *   - Loft 重建／增減 → 自動重繪
 *   - 沒有適用的 Loft → 自動移除
 */
class TrackSlabPlan : public Feature {
    Q_OBJECT
public:
    explicit TrackSlabPlan(Document* parent = nullptr);
    ~TrackSlabPlan() override = default;

    FeatureType type()       const override { return FeatureType::SlabPlan; }
    QString     typeString() const override { return QStringLiteral("SlabPlan"); }

    /// 斷面草圖中代表「軌道面超高」的參數名稱
    static QString cantParameterName() { return QStringLiteral("cant"); }

    /// 文件中是否有適用的 Loft（來源陣列的斷面草圖有 cant 參數）→ 才需要道版圖
    static bool isApplicable(const Document* doc);

    /**
     * @brief 重新掃描文件中的 Loft：重新接上各 Loft 的 shapeChanged；Loft 清單與上次不同時
     *        排程重建。由 Document::syncSlabPlan() 呼叫。
     */
    void refreshSources();

    /// 排程（事件迴圈下一輪、合併多次請求）重建；走 Document::rebuildFeature() 以通知 CadView 重繪
    void requestRebuild();

    bool rebuild() override;

    /// 繪出的折線（Local CAD 座標 E,N）。kind:
    ///   0 = 縱向邊（沿線形）、1 = 分段端部邊、
    ///   2 = 已併入外圍輪廓的最外側縱向邊（匯出時略過，避免與輪廓重複）、
    ///   3 = 最外圍輪廓（封閉聚合線：最左／最右縱向邊 + 兩端連線，相鄰分段自動串接）
    struct Polyline {
        int              kind = 0;
        QVector<QPointF> pts;
    };
    const QVector<Polyline>& polylines() const { return m_polylines; }

    /**
     * @brief 匯出 DXF（AutoCAD R12 ASCII：POLYLINE/VERTEX，AutoCAD／各 CAD 皆可讀）。
     *        座標為 TM2 全域 (E,N)（Local → ProjectOrigin::toGlobal()），單位 m。
     *        圖層：SLAB_OUTLINE（最外圍封閉聚合線，顏色 1）、SLAB_OTHER（其餘未連接的線，顏色 7）。
     * @return 成功與否；失敗原因寫入 error
     */
    bool exportDxf(const QString& filePath, QString* error = nullptr) const;

    QJsonObject toJson()  const override;
    bool fromJson(const QJsonObject& json) override;

    /// 刻意回傳空集合：CadView::displayAllFeatures() 會把「被其他 Feature 依賴」的 Feature 視為
    /// 已被消耗而隱藏；道版圖只是 Loft 的衍生「圖」，不能把 Loft 實體藏起來。
    QSet<QString> featureDependencies() const override { return {}; }

private:
    QList<ProfileLoftSolid*> allLofts() const;

    QVector<Polyline>              m_polylines;
    QList<QMetaObject::Connection> m_conns;
    QString                        m_loftSig;          ///< 上次排程重建時的 Loft id 清單簽章
    bool                           m_rebuildPending = false;
};

} // namespace cad
} // namespace aicad
