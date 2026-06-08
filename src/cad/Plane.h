/**
 * @file Plane.h
 * @brief 增強版平面類別
 * @author AICAD Team
 * @date 2025-01-08
 */

#ifndef AICAD_CAD_PLANE_H
#define AICAD_CAD_PLANE_H

#include <QObject>
#include <QVector2D>
#include <QVector3D>
#include <QString>
#include <QUuid>
#include <QDateTime>
#include <QJsonObject>
#include <gp_Pln.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>

namespace aicad {
namespace cad {

// 前向宣告
class PlaneManager;

/**
 * @brief 平面類別（增強版）
 *
 * 完整的平面實體，支援：
 * - 唯一 ID 識別
 * - 命名管理（自動避免衝突）
 * - 事件通知（EventBus）
 * - 序列化（JSON / OCAF）
 * - 生命週期管理
 *
 * 注意：請使用 PlaneManager 建立 Plane，不要直接 new
 *
 * 使用範例:
 * @code
 * // 建立平面
 * Plane* plane = PlaneManager::instance()->createPlane(
 *     Plane::Type::XY,
 *     "MyTopView"
 * );
 *
 * // 重命名
 * plane->setName("TopViewRevised");
 *
 * // 序列化
 * QJsonObject json = plane->toJson();
 * @endcode
 */
class Plane : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(bool isActive READ isActive NOTIFY activeStateChanged)
    Q_PROPERTY(bool isLocked READ isLocked WRITE setLocked NOTIFY lockedStateChanged)

public:
    /**
     * @brief 平面類型枚舉
     */
    enum class Type {
        XY,          ///< XY 標準平面
        YZ,          ///< YZ 標準平面
        ZX,          ///< ZX 標準平面
        XZ,          ///< XZ 標準平面（舊版）
        Custom,      ///< 自定義平面
        Offset,      ///< 偏移平面
        ThreePoint   ///< 三點定義平面
    };
    Q_ENUM(Type)

    /**
     * @brief 解構子
     */
    ~Plane() override;

    // ==================== 識別與屬性 ====================

    /**
     * @brief 取得唯一 ID
     */
    QString id() const { return m_id.toString(QUuid::WithoutBraces); }

    /**
     * @brief 取得 UUID
     */
    QUuid uuid() const { return m_id; }

    /**
     * @brief 取得平面類型
     */
    Type type() const { return m_type; }

    /**
     * @brief 取得/設定名稱
     */
    QString name() const { return m_name; }
    void setName(const QString& name);

    /**
     * @brief 取得顯示名稱（包含類型）
     */
    QString displayName() const;

    /**
     * @brief 取得建立時間
     */
    QDateTime createdTime() const { return m_createdTime; }

    /**
     * @brief 取得修改時間
     */
    QDateTime modifiedTime() const { return m_modifiedTime; }

    /**
     * @brief 是否為活動平面
     */
    bool isActive() const { return m_isActive; }

    /**
     * @brief 是否被鎖定（鎖定後不可修改）
     */
    bool isLocked() const { return m_isLocked; }
    void setLocked(bool locked);

    // ==================== 幾何屬性 ====================

    QVector3D origin() const { return m_origin; }
    QVector3D normal() const { return m_normal; }
    QVector3D xAxis() const { return m_xAxis; }
    QVector3D yAxis() const { return m_yAxis; }

    void setOrigin(const QVector3D& origin);
    void setNormal(const QVector3D& normal);
    void setXAxis(const QVector3D& xAxis);

    /**
     * @brief 設定完整的座標系統
     */
    void setCoordinateSystem(const QVector3D& origin,
                             const QVector3D& normal,
                             const QVector3D& xAxis);

    // ==================== 驗證與檢查 ====================

    /**
     * @brief 驗證平面是否符合右手坐標系統
     */
    bool validateRightHandRule() const;

    /**
     * @brief 判斷是否為標準平面
     */
    bool isXY() const;
    bool isYZ() const;
    bool isZX() const;
    bool isXZ() const;

    /**
     * @brief 判斷兩平面是否平行
     */
    bool isParallelTo(const Plane* other, double tolerance = 1e-6) const;

    /**
     * @brief 判斷兩平面是否相等
     */
    bool equals(const Plane* other, double tolerance = 1e-6) const;

    // ==================== OCCT 轉換 ====================

    gp_Pln toGpPln() const;
    gp_Ax2 toGpAx2() const;

    static Plane* fromGpPln(const gp_Pln& gpPlane, QObject* parent = nullptr);
    static Plane* fromGpAx2(const gp_Ax2& ax2, QObject* parent = nullptr);

    // ==================== 座標轉換 ====================

    /**
     * @brief 將 2D 平面座標轉換為 3D 世界座標
     */
    QVector3D toWorld(double u, double v) const;
    QVector3D toWorld(const QVector2D& planeCoord) const;

    /**
     * @brief 將 3D 世界座標投影到平面並取得 2D 座標
     */
    QVector2D toPlane(const QVector3D& worldPoint) const;

    /**
     * @brief 計算點到平面的距離（有號距離）
     */
    double distanceTo(const QVector3D& point) const;

    // ==================== 序列化 ====================

    /**
     * @brief 序列化為 JSON
     */
    QJsonObject toJson() const;

    /**
     * @brief 從 JSON 反序列化
     * @return 成功返回 true
     */
    bool fromJson(const QJsonObject& json);

    /**
     * @brief 複製平面
     */
    Plane* clone(QObject* parent = nullptr) const;

Q_SIGNALS:
    /**
     * @brief 名稱改變時發出
     */
    void nameChanged(const QString& newName);

    /**
     * @brief 幾何改變時發出
     */
    void geometryChanged();

    /**
     * @brief 活動狀態改變時發出
     */
    void activeStateChanged(bool isActive);

    /**
     * @brief 鎖定狀態改變時發出
     */
    void lockedStateChanged(bool isLocked);

    /**
     * @brief 即將被刪除時發出
     */
    void aboutToBeDeleted();

public Q_SLOTS:
    /**
     * @brief 標記為已修改
     */
    void markModified();

private:
    // 只有 PlaneManager 可以建立 Plane
    friend class PlaneManager;

    /**
     * @brief 建構子（私有，只能由 PlaneManager 調用）
     */
    explicit Plane(const QVector3D& origin,
                   const QVector3D& normal,
                   const QVector3D& xAxis,
                   Type type = Type::Custom,
                   QObject* parent = nullptr);

    /**
     * @brief 設定活動狀態（只能由 PlaneManager 調用）
     */
    void setActive(bool active);

    /**
     * @brief 正規化並重新計算座標軸
     */
    void normalize();

    /**
     * @brief 更新修改時間
     */
    void updateModifiedTime();

private:
    // 識別與管理
    QUuid m_id;                    ///< 唯一識別碼
    QString m_name;                ///< 名稱
    Type m_type;                   ///< 平面類型
    QDateTime m_createdTime;       ///< 建立時間
    QDateTime m_modifiedTime;      ///< 修改時間
    bool m_isActive;               ///< 是否為活動平面
    bool m_isLocked;               ///< 是否鎖定

    // 幾何屬性
    QVector3D m_origin;            ///< 平面原點
    QVector3D m_normal;            ///< 平面法向量（Z軸方向）
    QVector3D m_xAxis;             ///< 平面 X 軸方向（U軸）
    QVector3D m_yAxis;             ///< 平面 Y 軸方向（V軸）
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_PLANE_H
