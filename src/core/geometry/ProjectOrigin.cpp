/**
 * @file ProjectOrigin.cpp
 * @brief 專案 Global（TM2 / TWD97）↔ Local（OCCT / CAD）座標轉換核心實作。
 *
 * 轉換公式：
 *   local  = global − origin          （toLocal）
 *   global = local  + origin          （toGlobal）
 *
 * 當 isSet()==false 時，origin=(0,0,0)，兩個方向均為恆等映射，
 * 確保未設定 origin 的舊專案行為不變。
 */

#include "core/geometry/ProjectOrigin.h"
#include <gp_Pnt.hxx>  // OCCT — only included in .cpp, not in .h
#include "core/Application.h"
#include "core/EventBus.h"

#include <QJsonObject>
#include <QDebug>

namespace aicad {
namespace core {
namespace geometry {

// ─────────────────────────────────────────────────────────────────────────────
//  單例
// ─────────────────────────────────────────────────────────────────────────────
ProjectOrigin& ProjectOrigin::instance()
{
    static ProjectOrigin s_instance;
    return s_instance;
}

// ─────────────────────────────────────────────────────────────────────────────
//  狀態查詢
// ─────────────────────────────────────────────────────────────────────────────
bool    ProjectOrigin::isSet()     const { return m_isSet; }
double  ProjectOrigin::originE()   const { return m_originE; }
double  ProjectOrigin::originN()   const { return m_originN; }
double  ProjectOrigin::originZ()   const { return m_originZ; }
QString ProjectOrigin::epsgCode()  const { return m_epsgCode; }

// ─────────────────────────────────────────────────────────────────────────────
//  設定 / 清除
// ─────────────────────────────────────────────────────────────────────────────
void ProjectOrigin::setOrigin(double globalE, double globalN, double globalZ)
{
    m_originE = globalE;
    m_originN = globalN;
    m_originZ = globalZ;
    m_isSet   = true;

    qDebug() << "[ProjectOrigin] origin set:"
             << "E=" << m_originE
             << "N=" << m_originN
             << "Z=" << m_originZ
             << "EPSG=" << m_epsgCode;

    publishChanged();
}

void ProjectOrigin::setEpsgCode(const QString& code)
{
    m_epsgCode = code;
}

void ProjectOrigin::clear()
{
    m_originE = 0.0;
    m_originN = 0.0;
    m_originZ = 0.0;
    m_isSet   = false;

    qDebug() << "[ProjectOrigin] origin cleared";
    publishChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Global(TM2) → Local(CAD)
// ─────────────────────────────────────────────────────────────────────────────
QPointF ProjectOrigin::toLocal(const QPointF& globalEN) const
{
    // local = global − origin
    return QPointF(globalEN.x() - m_originE,
                   globalEN.y() - m_originN);
}

QPointF ProjectOrigin::toLocal(double E, double N) const
{
    return QPointF(E - m_originE, N - m_originN);
}

void ProjectOrigin::toLocal(double E, double N, double Z,
                             double& outX, double& outY, double& outZ) const
{
    outX = E - m_originE;
    outY = N - m_originN;
    outZ = Z - m_originZ;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Local(CAD) → Global(TM2)
// ─────────────────────────────────────────────────────────────────────────────
QPointF ProjectOrigin::toGlobal(const QPointF& localXY) const
{
    // global = local + origin
    return QPointF(localXY.x() + m_originE,
                   localXY.y() + m_originN);
}

QPointF ProjectOrigin::toGlobal(double x, double y) const
{
    return QPointF(x + m_originE, y + m_originN);
}

void ProjectOrigin::toGlobal(double x, double y, double z,
                              double& outE, double& outN, double& outZ) const
{
    outE = x + m_originE;
    outN = y + m_originN;
    outZ = z + m_originZ;
}

// ─────────────────────────────────────────────────────────────────────────────
//  持久化
// ─────────────────────────────────────────────────────────────────────────────
QJsonObject ProjectOrigin::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("isSet")]    = m_isSet;
    obj[QStringLiteral("originE")]  = m_originE;
    obj[QStringLiteral("originN")]  = m_originN;
    obj[QStringLiteral("originZ")]  = m_originZ;
    obj[QStringLiteral("epsgCode")] = m_epsgCode;
    return obj;
}

void ProjectOrigin::fromJson(const QJsonObject& obj)
{
    // 向下相容：缺欄位時視為 isSet==false，恆等轉換
    if (!obj.contains(QStringLiteral("isSet"))) {
        m_isSet   = false;
        m_originE = 0.0;
        m_originN = 0.0;
        m_originZ = 0.0;
        return;
    }

    m_isSet   = obj[QStringLiteral("isSet")].toBool(false);
    m_originE = obj[QStringLiteral("originE")].toDouble(0.0);
    m_originN = obj[QStringLiteral("originN")].toDouble(0.0);
    m_originZ = obj[QStringLiteral("originZ")].toDouble(0.0);

    if (obj.contains(QStringLiteral("epsgCode")))
        m_epsgCode = obj[QStringLiteral("epsgCode")].toString(m_epsgCode);

    qDebug() << "[ProjectOrigin] fromJson: isSet=" << m_isSet
             << "E=" << m_originE << "N=" << m_originN;

    publishChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
//  gp_Pnt 多載（OCCT 直接使用，定義在 .cpp 避免 gp_Pnt.hxx 污染其他 TU）
// ─────────────────────────────────────────────────────────────────────────────
gp_Pnt ProjectOrigin::toLocalPnt(double E, double N, double Z) const
{
    return gp_Pnt(E - m_originE, N - m_originN, Z - m_originZ);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tuple 便利多載
// ─────────────────────────────────────────────────────────────────────────────
ProjectOrigin::LocalXYZ ProjectOrigin::toLocalTuple(double E, double N, double Z) const
{
    return { E - m_originE, N - m_originN, Z - m_originZ };
}

ProjectOrigin::GlobalENZ ProjectOrigin::toGlobalTuple(double x, double y, double z) const
{
    return { x + m_originE, y + m_originN, z + m_originZ };
}

// ─────────────────────────────────────────────────────────────────────────────
//  private helper — 發布 EventBus 通知
// ─────────────────────────────────────────────────────────────────────────────
void ProjectOrigin::publishChanged()
{
    auto* app = Application::instance();
    if (!app) return;

    QVariantMap payload;
    payload[QStringLiteral("isSet")]    = m_isSet;
    payload[QStringLiteral("originE")]  = m_originE;
    payload[QStringLiteral("originN")]  = m_originN;
    payload[QStringLiteral("originZ")]  = m_originZ;
    payload[QStringLiteral("epsgCode")] = m_epsgCode;

    app->eventBus()->publish(Events::PROJECT_ORIGIN_CHANGED, payload);
}

} // namespace geometry
} // namespace core
} // namespace aicad
