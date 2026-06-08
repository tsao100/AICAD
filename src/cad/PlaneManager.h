/**
 * @file PlaneManager.h
 * @brief 平面管理器
 * @author AICAD Team
 * @date 2025-01-08
 */

#ifndef AICAD_CAD_PLANEMANAGER_H
#define AICAD_CAD_PLANEMANAGER_H

#include "Plane.h"
#include <QObject>
#include <QHash>
#include <QList>
#include <QString>
#include <QJsonObject>

namespace aicad {
namespace cad {

/**
 * @brief 平面管理器
 *
 * 單例模式，管理所有平面實體：
 * - 建立/刪除平面
 * - 活動平面管理
 * - 自動命名
 * - EventBus 事件通知
 *
 * 使用範例:
 * @code
 * PlaneManager* manager = PlaneManager::instance();
 *
 * // 建立標準平面
 * Plane* xy = manager->createPlane(Plane::Type::XY, "TopView");
 *
 * // 設定活動平面
 * manager->setActivePlane(xy);
 *
 * // 從法向量建立
 * Plane* custom = manager->createFromNormal(origin, normal, "Custom");
 *
 * // 序列化
 * QJsonObject json = manager->toJson();
 *
 * // 刪除平面
 * manager->deletePlane(xy);
 * @endcode
 */
class PlaneManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 取得單例實例
     */
    static PlaneManager* instance();

    /**
     * @brief 初始化 PlaneManager（建立標準平面）
     *
     * 此方法應在應用程式啟動時調用一次
     * 會建立並鎖定標準 XY, YZ, XZ 平面
     */
    void initialize();

    /**
     * @brief 取得標準 XY 平面
     * @return XY 平面指標，保證非 null
     */
    Plane* xyPlane() const { return m_xyPlane; }

    /**
     * @brief 取得標準 YZ 平面
     * @return YZ 平面指標，保證非 null
     */
    Plane* yzPlane() const { return m_yzPlane; }

    /**
     * @brief 取得標準 ZX 平面
     * @return ZX 平面指標，保證非 null
     */
    Plane* xzPlane() const { return m_xzPlane; }

    /**
     * @brief 建立標準平面
     * @param type 平面類型（XY, YZ, ZX, XZ）
     * @param name 平面名稱（空字串則自動命名）
     * @return 建立的平面指標
     */
    Plane* createPlane(Plane::Type type, const QString& name = QString());

    /**
     * @brief 建立自定義平面
     * @param origin 平面原點
     * @param normal 法向量
     * @param xAxis X 軸方向（U 軸）
     * @param name 平面名稱（空字串則自動命名）
     * @return 建立的平面指標
     */
    Plane* createPlane(const QVector3D& origin,
                       const QVector3D& normal,
                       const QVector3D& xAxis,
                       const QString& name = QString());

    /**
     * @brief 從法向量建立平面
     * @param origin 平面原點
     * @param normal 法向量
     * @param name 平面名稱（空字串則自動命名）
     * @return 建立的平面指標
     *
     * 此方法會自動選擇合適的 U/V 軸方向
     */
    Plane* createFromNormal(const QVector3D& origin,
                            const QVector3D& normal,
                            const QString& name = QString());

    /**
     * @brief 從三點建立平面
     * @param p1 平面原點
     * @param p2 定義 U 軸方向的點
     * @param p3 定義 V 軸方向的點（會被正交化）
     * @param name 平面名稱（空字串則自動命名）
     * @return 建立的平面指標
     */
    Plane* createFromThreePoints(const QVector3D& p1,
                                 const QVector3D& p2,
                                 const QVector3D& p3,
                                 const QString& name = QString());

    /**
     * @brief 建立偏移平面
     * @param sourcePlane 源平面
     * @param offset 偏移距離（沿法向量）
     * @param name 平面名稱（空字串則自動命名）
     * @return 建立的平面指標
     */
    Plane* createOffsetPlane(const Plane* sourcePlane,
                             double offset,
                             const QString& name = QString());

    /**
     * @brief 建立平行平面（通過指定點）
     * @param sourcePlane 源平面
     * @param throughPoint 平面通過的點
     * @param name 平面名稱（空字串則自動命名）
     * @return 建立的平面指標
     */
    Plane* createParallelPlane(const Plane* sourcePlane,
                               const QVector3D& throughPoint,
                               const QString& name = QString());

    /**
     * @brief 從 JSON 建立平面
     * @param json 平面的 JSON 資料
     * @return 建立的平面指標，失敗返回 nullptr
     */
    Plane* createFromJson(const QJsonObject& json);

    /**
     * @brief 複製平面
     * @param source 源平面
     * @param name 新平面名稱（空字串則自動命名）
     * @return 複製的平面指標
     */
    Plane* clonePlane(const Plane* source, const QString& name = QString());

    /**
     * @brief 刪除平面（按 ID）
     * @param id 平面 ID
     * @return 成功返回 true
     */
    bool deletePlane(const QString& id);

    /**
     * @brief 刪除平面（按指標）
     * @param plane 平面指標
     * @return 成功返回 true
     */
    bool deletePlane(Plane* plane);

    /**
     * @brief 取得平面（按 ID）
     * @param id 平面 ID
     * @return 平面指標，未找到返回 nullptr
     */
    Plane* getPlane(const QString& id) const;

    /**
     * @brief 取得平面（按名稱）
     * @param name 平面名稱
     * @return 平面指標，未找到返回 nullptr
     */
    Plane* getPlaneByName(const QString& name) const;

    /**
     * @brief 取得所有平面
     * @return 所有平面的列表
     */
    QList<Plane*> planes() const;

    /**
     * @brief 取得平面數量
     */
    int planeCount() const { return m_planes.size(); }

    /**
     * @brief 取得活動平面
     * @return 活動平面指標，無活動平面返回 nullptr
     */
    Plane* activePlane() const { return m_activePlane; }

    /**
     * @brief 設定活動平面（按指標）
     * @param plane 平面指標，nullptr 表示取消活動平面
     */
    void setActivePlane(Plane* plane);

    /**
     * @brief 設定活動平面（按 ID）
     * @param id 平面 ID
     */
    void setActivePlane(const QString& id);

    /**
     * @brief 檢查名稱是否已存在
     * @param name 名稱
     * @return 存在返回 true
     */
    bool nameExists(const QString& name) const;

    /**
     * @brief 生成唯一名稱
     * @param baseName 基礎名稱
     * @return 唯一的名稱（如果衝突會加上數字後綴）
     */
    QString generateUniqueName(const QString& baseName) const;

    /**
     * @brief 清除所有平面
     *
     * 警告：此操作會刪除所有平面，包括活動平面
     */
    void clear();

    /**
     * @brief 序列化所有平面
     * @return JSON 物件
     */
    QJsonObject toJson() const;

    /**
     * @brief 從 JSON 載入所有平面
     * @param json JSON 物件
     * @return 成功返回 true
     *
     * 注意：會先清除現有的所有平面
     */
    bool fromJson(const QJsonObject& json);

Q_SIGNALS:
    /**
     * @brief 平面建立時發出
     * @param plane 新建立的平面
     */
    void planeCreated(Plane* plane);

    /**
     * @brief 平面刪除時發出
     * @param id 被刪除的平面 ID
     *
     * 注意：發出此信號時，Plane 物件已被刪除
     */
    void planeDeleted(const QString& id);

    /**
     * @brief 活動平面改變時發出
     * @param plane 新的活動平面（nullptr 表示無活動平面）
     */
    void activePlaneChanged(Plane* plane);

    /**
     * @brief 平面列表改變時發出
     *
     * 在平面建立、刪除或清除時發出
     */
    void planesChanged();

private:
    /**
     * @brief 建構子（私有，單例模式）
     */
    explicit PlaneManager(QObject* parent = nullptr);

    /**
     * @brief 解構子
     */
    ~PlaneManager() override;

    /**
     * @brief 建立平面的內部方法
     */
    Plane* createPlaneInternal(const QVector3D& origin,
                               const QVector3D& normal,
                               const QVector3D& xAxis,
                               Plane::Type type,
                               const QString& name);

    /**
     * @brief 註冊平面（加入管理）
     */
    void registerPlane(Plane* plane);

    /**
     * @brief 發送 EventBus 事件
     */
    void emitEvent(const QString& eventName, Plane* plane);

private:
    static PlaneManager* s_instance;

    QHash<QString, Plane*> m_planes;     ///< ID -> Plane 映射
    Plane* m_activePlane;                ///< 當前活動平面

    // 標準平面（預先建立）
    Plane* m_xyPlane;                    ///< XY 標準平面（俯視圖）
    Plane* m_yzPlane;                    ///< YZ 標準平面（右視圖）
    Plane* m_xzPlane;                    ///< XZ 標準平面（前視圖）
    bool m_initialized;                  ///< 是否已初始化標準平面

    // 命名計數器（用於自動命名）
    int m_xyCount;
    int m_yzCount;
    int m_zxCount;
    int m_customCount;
    int m_offsetCount;
};

} // namespace cad
} // namespace aicad

#endif // AICAD_CAD_PLANEMANAGER_H
