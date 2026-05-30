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


// ─────────────────────────────────────────────────────────────────────────────
// Phase 2：幾何選取（幾何約束命令用）
// ─────────────────────────────────────────────────────────────────────────────
void ConstraintPickSession::feedGeom(const QString& geomUuid, GeomHandle handle)
{
    if (!m_active) return;
    GeomRef ref(geomUuid, handle);
    m_refs.append(ref);

    if (m_refs.size() >= m_required) {
        m_active = false;
        Q_EMIT constraintReady(m_refs, m_value, m_paramExpr, m_driving, m_type);
        Q_EMIT sessionEnded();
    } else {
        Q_EMIT promptChanged(promptText());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3B：距離子類型判斷
// ─────────────────────────────────────────────────────────────────────────────
DistanceMode ConstraintPickSession::resolveDistanceMode() const
{
    if (m_refs.size() < 2) return DistanceMode::Invalid;

    auto isPointRef = [this](const GeomRef& r) {
        if (r.handle == GeomHandle::Start || r.handle == GeomHandle::End ||
            r.handle == GeomHandle::Center) return true;
        // WholeGeom + SketchPoint 類型
        if (m_sketch) {
            auto* g = m_sketch->findGeometry(r.geomUuid);
            if (g && g->type == SketchGeometryType::Point) return true;
        }
        return false;
    };

    bool r1IsPoint = isPointRef(m_refs[0]);
    bool r2IsPoint = isPointRef(m_refs[1]);

    if (r1IsPoint && r2IsPoint) return DistanceMode::PointToPoint;
    if (r1IsPoint || r2IsPoint) return DistanceMode::PointToLine;

    // 兩者皆為線：檢查是否平行
    if (m_sketch) {
        auto* g1 = m_sketch->findGeometry(m_refs[0].geomUuid);
        auto* g2 = m_sketch->findGeometry(m_refs[1].geomUuid);
        auto* l1 = dynamic_cast<SketchLine*>(g1);
        auto* l2 = dynamic_cast<SketchLine*>(g2);
        if (l1 && l2) {
            QVector2D d1 = (l1->end - l1->start).normalized();
            QVector2D d2 = (l2->end - l2->start).normalized();
            double cross = std::abs(d1.x() * d2.y() - d1.y() * d2.x());
            if (cross > 0.01) {
                // 不平行
                return DistanceMode::Invalid;
            }
            return DistanceMode::LineToLine;
        }
    }
    return DistanceMode::PointToLine;
}

void ConstraintPickSession::confirmDimLineOffset(double offsetX, double offsetY)
{
    // 將 dimLineOffset 儲存起來，讓命令可以讀取並寫入約束
    m_dimLineOffsetX = offsetX;
    m_dimLineOffsetY = offsetY;
}

} // namespace aicad::cad
