#include <QPointF>
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
    // ── 尺寸約束（原有） ──────────────────────────────────────────
    case ConstraintType::FixedDistance:
        return 2;   // 點 A → 點 B
    case ConstraintType::FixedRadius:
        return 1;   // 選圓/弧
    case ConstraintType::FixedX:
    case ConstraintType::FixedY:
        return 1;   // 選一個點
    case ConstraintType::FixedAngleDim:
        return 2;   // 選兩條線上的點（取線方向）
    case ConstraintType::Slope:
        return 1;   // 選一條線（斜度）

    // ── 幾何約束：單一幾何 ────────────────────────────────────────
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
    case ConstraintType::Fixed:
    case ConstraintType::FixedLength:
    case ConstraintType::FixedDiameter:
    case ConstraintType::FixedArcLength:
        return 1;

    // ── 幾何約束：兩個幾何 ────────────────────────────────────────
    case ConstraintType::Coincident:
    case ConstraintType::Parallel:
    case ConstraintType::Perpendicular:
    case ConstraintType::Tangent:
    case ConstraintType::Concentric:
    case ConstraintType::EqualLength:
    case ConstraintType::EqualRadius:
    case ConstraintType::Collinear:
    case ConstraintType::Midpoint:
    case ConstraintType::PointOnCurve:
    case ConstraintType::PointOnMidpoint:
        return 2;

    // ── 幾何約束：三個幾何 ───────────────────────────────────────
    case ConstraintType::Symmetric:
        return 3;   // 點A、點B、軸線

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
        if (picked == 0)
            return tr("選取第一個元素（端點 / 線段）…");
        if (picked == 1) {
            // 根據第一個 ref 是點還是線，給出更明確的第二步提示
            bool firstIsPoint = false;
            if (m_sketch && !m_refs[0].geomUuid.isEmpty()) {
                const SketchGeometry* g = m_sketch->findGeometry(m_refs[0].geomUuid);
                firstIsPoint = g && (
                    g->type == SketchGeometryType::Point ||
                    m_refs[0].handle == GeomHandle::Start ||
                    m_refs[0].handle == GeomHandle::End   ||
                    m_refs[0].handle == GeomHandle::Center);
            }
            if (firstIsPoint)
                return tr("選取第二個元素（端點 → 點點距；線段 → 點線距）…");
            else
                return tr("選取第二個元素（線段 → 線線距；端點 → 點線距）…");
        }
        return tr("選取第 %1/%2 個元素…").arg(picked + 1).arg(total);
    case ConstraintType::FixedRadius:
        return tr("選取圓或弧…");
    case ConstraintType::FixedX:
        return tr("選取要固定 X 座標的點…");
    case ConstraintType::FixedY:
        return tr("選取要固定 Y 座標的點…");
    case ConstraintType::FixedAngleDim:
        if (picked == 0) return tr("選取第一條線上的點…");
        return tr("選取第二條線上的點…");
    case ConstraintType::Slope:
        return tr("選取線段（斜度）…");

    // ── 幾何約束：依類型給出明確提示 ────────────────────────────
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
        return tr("選取線段…");
    case ConstraintType::Fixed:
        return tr("選取要固定的幾何元素…");
    case ConstraintType::Parallel:
        if (picked == 0) return tr("選取第一條線段（平行）…");
        return tr("選取第二條線段（平行）…");
    case ConstraintType::Perpendicular:
        if (picked == 0) return tr("選取第一條線段（垂直）…");
        return tr("選取第二條線段（垂直）…");
    case ConstraintType::Tangent:
        if (picked == 0) return tr("選取第一個幾何（相切）…");
        return tr("選取第二個幾何（相切）…");
    case ConstraintType::Concentric:
        if (picked == 0) return tr("選取第一個圓/弧（同心）…");
        return tr("選取第二個圓/弧（同心）…");
    case ConstraintType::EqualLength:
        if (picked == 0) return tr("選取第一條線段（等長）…");
        return tr("選取第二條線段（等長）…");
    case ConstraintType::EqualRadius:
        if (picked == 0) return tr("選取第一個圓/弧（等半徑）…");
        return tr("選取第二個圓/弧（等半徑）…");
    case ConstraintType::Coincident:
        if (picked == 0) return tr("選取第一個幾何元素（重合）…");
        return tr("選取第二個幾何元素（重合）…");
    case ConstraintType::Collinear:
        if (picked == 0) return tr("選取第一條線段（共線）…");
        return tr("選取第二條線段（共線）…");
    case ConstraintType::Midpoint:
        if (picked == 0) return tr("選取點（中點約束）…");
        return tr("選取線段（中點約束）…");
    case ConstraintType::PointOnCurve:
        if (picked == 0) return tr("選取點（點在曲線上）…");
        return tr("選取曲線（點在曲線上）…");
    case ConstraintType::Symmetric:
        if (picked == 0) return tr("選取第一個點（對稱）…");
        if (picked == 1) return tr("選取第二個點（對稱）…");
        return tr("選取對稱軸線（對稱）…");

    default:
        return tr("選取幾何 %1/%2…").arg(picked + 1).arg(total);
    }
}

// ─────────────────────────────────────────────────────────────────────────────

GeomRef ConstraintPickSession::makeRef(const QString& geomUuid,
                                       int geomHandle,
                                       const QVector2D& planePt)
{
    if (!geomUuid.isEmpty() && geomHandle >= 0) {
        // OSnap 鎖定到已知草圖端點（Start/End/Center…）→ 精確 GeomRef
        return GeomRef(geomUuid, static_cast<GeomHandle>(geomHandle));
    }

    if (!geomUuid.isEmpty() && geomHandle < 0) {
        if (m_sketch) {
            const SketchGeometry* g = m_sketch->findGeometry(geomUuid);
            if (g) {
                // ✅ Step 9: 直接引用 SketchPoint
                if (g->type == SketchGeometryType::Point) {
                    GeomRef ref(geomUuid, GeomHandle::WholeGeom);
                    return ref;
                }
                // 線幾何：WholeGeom 表示「整條線」
                qDebug() << "[PickSession] Line/curve body picked:"
                         << geomUuid << "type:" << static_cast<int>(g->type);
                return GeomRef(geomUuid, GeomHandle::WholeGeom);
            }
        }
        return GeomRef(geomUuid, GeomHandle::WholeGeom);
    }

    // geomUuid 為空 → 自由點：找最近的幾何端點（容差 5 mm）
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

        qWarning() << "[PickSession] Free point at" << planePt
                   << "— no nearby geom found, using WholeGeom fallback";
    }

    return GeomRef(QString(), GeomHandle::WholeGeom);
}

void ConstraintPickSession::feedPoint(const QPointF& planePt,
                                      const QString& geomUuid,
                                      int geomHandle)
{
    if (!m_active) return;

    // QPointF → QVector2D（Sketch 內部座標仍使用 QVector2D）
    const QVector2D planePtV(static_cast<float>(planePt.x()),
                             static_cast<float>(planePt.y()));
    GeomRef ref = makeRef(geomUuid, geomHandle, planePtV);
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

    // 判斷一個 GeomRef 是否代表「點」（端點/圓心/獨立點）
    // 若為 WholeGeom 且幾何類型不是 Point，則視為「線/曲線」
    auto isPointRef = [this](const GeomRef& r) -> bool {
        // 明確的端點 handle：一定是點
        if (r.handle == GeomHandle::Start  ||
            r.handle == GeomHandle::End    ||
            r.handle == GeomHandle::Center)
            return true;

        // WholeGeom：取決於幾何類型
        if (r.handle == GeomHandle::WholeGeom && m_sketch && !r.geomUuid.isEmpty()) {
            const auto* g = m_sketch->findGeometry(r.geomUuid);
            if (!g) return false;
            // 只有 Point 幾何的 WholeGeom 才算「點」
            return g->type == SketchGeometryType::Point;
        }
        return false;
    };

    bool r1IsPoint = isPointRef(m_refs[0]);
    bool r2IsPoint = isPointRef(m_refs[1]);

    if (r1IsPoint && r2IsPoint) return DistanceMode::PointToPoint;
    if (r1IsPoint || r2IsPoint) return DistanceMode::PointToLine;

    // 兩者皆為線：檢查是否平行（LineToLine 才有意義）
    if (m_sketch) {
        const auto* g1 = m_sketch->findGeometry(m_refs[0].geomUuid);
        const auto* g2 = m_sketch->findGeometry(m_refs[1].geomUuid);
        const auto* l1 = dynamic_cast<const SketchLine*>(g1);
        const auto* l2 = dynamic_cast<const SketchLine*>(g2);
        if (l1 && l2) {
            QVector2D d1 = (l1->end - l1->start).normalized();
            QVector2D d2 = (l2->end - l2->start).normalized();
            double cross = std::abs(d1.x() * d2.y() - d1.y() * d2.x());
            if (cross > 0.01) {
                // 兩線不平行：線線距無意義，降級為 PointToLine（取各自端點）
                qWarning() << "[PickSession] Two lines are not parallel"
                           << "— LineToLine distance is undefined, treating as PointToLine";
                return DistanceMode::PointToLine;
            }
            return DistanceMode::LineToLine;
        }
        // 其中有非直線的曲線（弧/圓）：視為 PointToLine（點到曲線最短距）
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
