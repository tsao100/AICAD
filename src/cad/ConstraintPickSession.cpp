#include "ConstraintPickSession.h"
#include "Sketch.h"
#include <QDebug>

namespace aicad::cad {

ConstraintPickSession::ConstraintPickSession(QObject* parent)
    : QObject(parent)
{}

// ─────────────────────────────────────────────────────────────────────────────

int ConstraintPickSession::requiredPointCount(ConstraintType type) const {
    switch (type) {
    case ConstraintType::FixedDistance:
        return 2;   // 點 A → 點 B
    case ConstraintType::FixedRadius:
        return 1;   // 選圓/弧
    case ConstraintType::FixedX:
    case ConstraintType::FixedY:
        return 1;   // 選一個點
    case ConstraintType::FixedAngleDim:
        return 2;   // 選兩條線上的點（取線方向）
    default:
        return 1;
    }
}

void ConstraintPickSession::begin(Sketch* sketch,
                                  ConstraintType type,
                                  double value,
                                  const QString& paramExpr,
                                  bool driving)
{
    m_sketch    = sketch;
    m_type      = type;
    m_value     = value;
    m_paramExpr = paramExpr;
    m_driving   = driving;
    m_active    = true;
    m_required  = requiredPointCount(type);
    m_refs.clear();

    Q_EMIT promptChanged(promptText());
    qDebug() << "[PickSession] begin" << static_cast<int>(type)
             << "need" << m_required << "points";
}

void ConstraintPickSession::cancel() {
    if (!m_active) return;
    m_active = false;
    m_refs.clear();
    Q_EMIT sessionEnded();
}

QString ConstraintPickSession::promptText() const {
    if (!m_active) return {};
    int picked = m_refs.size();
    int total  = m_required;

    switch (m_type) {
    case ConstraintType::FixedDistance:
        if (picked == 0) return tr("選取第一個點（端點/中心點）…");
        return tr("選取第二個點…");
    case ConstraintType::FixedRadius:
        return tr("選取圓或弧…");
    case ConstraintType::FixedX:
        return tr("選取要固定 X 座標的點…");
    case ConstraintType::FixedY:
        return tr("選取要固定 Y 座標的點…");
    case ConstraintType::FixedAngleDim:
        if (picked == 0) return tr("選取第一條線上的點…");
        return tr("選取第二條線上的點…");
    default:
        return tr("選取點 %1/%2…").arg(picked + 1).arg(total);
    }
}

// ─────────────────────────────────────────────────────────────────────────────

GeomRef ConstraintPickSession::makeRef(const QString& geomUuid,
                                       int geomHandle,
                                       const QVector2D& planePt)
{
    if (!geomUuid.isEmpty() && geomHandle >= 0) {
        // snap 到已知草圖幾何端點 → 精確 GeomRef
        return GeomRef(geomUuid, static_cast<GeomHandle>(geomHandle));
    }

    // 自由點：找最近的幾何端點（容差 5 mm），否則建立新的 FixedPoint 幾何
    if (m_sketch) {
        constexpr float kTol = 5.0f;
        float bestDist = kTol;
        QString bestUuid;
        GeomHandle bestHandle = GeomHandle::WholeGeom;

        for (const SketchGeometry* g : m_sketch->geometriesRef()) {
            if (!g || g->points.isEmpty()) continue;

            auto check = [&](const QVector2D& p, GeomHandle h) {
                float d = (p - planePt).length();
                if (d < bestDist) {
                    bestDist   = d;
                    bestUuid   = g->uuid;
                    bestHandle = h;
                }
            };

            check(g->points.front(), GeomHandle::Start);
            if (g->points.size() > 1)
                check(g->points.back(), GeomHandle::End);
            // 圓/弧：中心點
            if (g->type == SketchGeometryType::Circle ||
                g->type == SketchGeometryType::Arc)
                check(g->points.front(), GeomHandle::Center);
        }

        if (!bestUuid.isEmpty())
            return GeomRef(bestUuid, bestHandle);

        // 找不到最近點：建立一個 FixedPoint（代表草圖中的自由點）
        // 此處只回傳空 ref，讓 ConstraintSolver 以 value 中的座標處理
        qWarning() << "[PickSession] Free point at" << planePt
                   << "— no nearby geom found, using WholeGeom fallback";
    }

    return GeomRef(QString(), GeomHandle::WholeGeom);
}

void ConstraintPickSession::feedPoint(const QVector2D& planePt,
                                      const QString& geomUuid,
                                      int geomHandle)
{
    if (!m_active) return;

    GeomRef ref = makeRef(geomUuid, geomHandle, planePt);
    m_refs.append(ref);

    qDebug() << "[PickSession] point" << m_refs.size() << "/"  << m_required
             << "uuid:" << ref.geomUuid
             << "handle:" << static_cast<int>(ref.handle);

    if (m_refs.size() < m_required) {
        // 還需要更多點
        Q_EMIT promptChanged(promptText());
        return;
    }

    // 所有點收齊
    m_active = false;
    Q_EMIT constraintReady(m_refs, m_value, m_paramExpr, m_driving, m_type);
    Q_EMIT sessionEnded();
}

} // namespace aicad::cad
