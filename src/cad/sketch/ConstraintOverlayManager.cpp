#include "ConstraintOverlayManager.h"
#include "../Sketch.h"
#include "../SketchInstance.h"
#include <QDebug>
#include <AIS_InteractiveContext.hxx>

namespace aicad::cad {

ConstraintOverlayManager::ConstraintOverlayManager(
        const Handle(AIS_InteractiveContext)& ctx,
        QObject* parent)
    : QObject(parent)
    , m_ctx(ctx)
{}

ConstraintOverlayManager::~ConstraintOverlayManager() {
    clearAll();
}

// ─────────────────────────────────────────────────────────────────────────────
// Attach / Detach
// ─────────────────────────────────────────────────────────────────────────────

void ConstraintOverlayManager::attachMaster(Sketch* sketch, const gp_Trsf& toWorld) {
    detach();
    m_mode   = Mode::Master;
    m_sketch = sketch;
    m_toWorld = toWorld;

    if (!sketch) return;

    connect(sketch, &Sketch::shapeChanged,
            this, &ConstraintOverlayManager::onSourceRebuilt,
            Qt::UniqueConnection);

    rebuildAll();
}

void ConstraintOverlayManager::attachInstance(SketchInstance* instance,
                                               const gp_Trsf& toWorld) {
    detach();
    m_mode     = Mode::Instance;
    m_instance = instance;
    m_toWorld  = toWorld;

    if (!instance) return;

    connect(instance, &SketchInstance::instanceRebuilt,
            this, &ConstraintOverlayManager::onSourceRebuilt,
            Qt::UniqueConnection);

    rebuildAll();
}

void ConstraintOverlayManager::detach() {
    clearAll();
    if (m_sketch)   disconnect(m_sketch,   nullptr, this, nullptr);
    if (m_instance) disconnect(m_instance, nullptr, this, nullptr);
    m_sketch   = nullptr;
    m_instance = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Source 存取
// ─────────────────────────────────────────────────────────────────────────────

const QList<SketchGeometry*>* ConstraintOverlayManager::sourceGeoms() const {
    if (m_mode == Mode::Master && m_sketch)
        return &m_sketch->geometriesRef();   // const ref — not rvalue
    if (m_mode == Mode::Instance && m_instance) {
        if (m_instance->masterSketch())
            return &m_instance->masterSketch()->geometriesRef();
    }
    return nullptr;
}

const QList<SketchConstraint>* ConstraintOverlayManager::sourceConstraints() const {
    if (m_mode == Mode::Master && m_sketch)
        return &m_sketch->constraints();
    if (m_mode == Mode::Instance && m_instance) {
        if (m_instance->masterSketch())
            return &m_instance->masterSketch()->constraints();
    }
    return nullptr;
}

SolveStatus ConstraintOverlayManager::lastSolveStatus() const {
    // 預設回傳 UnderConstrained（實際可從 Sketch 取得）
    return SolveStatus::UnderConstrained;
}

QList<SketchGeometry*> ConstraintOverlayManager::geomsForConstraint(
        const SketchConstraint& c) const
{
    const QList<SketchGeometry*>* geoms = sourceGeoms();
    if (!geoms) return {};

    QList<SketchGeometry*> result;
    for (const GeomRef& ref : c.refs) {
        for (SketchGeometry* g : *geoms) {
            if (g && g->uuid == ref.geomUuid) {
                result.append(g);
                break;
            }
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// 建立 / 更新 / 移除
// ─────────────────────────────────────────────────────────────────────────────

void ConstraintOverlayManager::createSymbolFor(const SketchConstraint& c) {
    if (m_hiddenTypes.contains(c.type)) return;
    if (m_ctx.IsNull()) return;

    QList<SketchGeometry*> geoms = geomsForConstraint(c);
    SolveStatus ss = lastSolveStatus();

    if (c.isDimensional()) {
        // 尺寸線
        Handle(AIS_DimensionLine) dim =
            new AIS_DimensionLine(c, geoms, m_toWorld, ss);
        m_dimLines[c.uuid] = dim;
        m_ctx->Display(dim, Standard_False);
        if (!m_visible) m_ctx->Erase(dim, Standard_False);
    } else {
        // 幾何約束符號
        Handle(AIS_ConstraintSymbol) sym =
            new AIS_ConstraintSymbol(c, geoms, m_toWorld, ss);
        m_geomSymbols[c.uuid] = sym;
        m_ctx->Display(sym, Standard_False);
        if (!m_visible) m_ctx->Erase(sym, Standard_False);
    }
}

void ConstraintOverlayManager::updateSymbolFor(const SketchConstraint& c) {
    QList<SketchGeometry*> geoms = geomsForConstraint(c);
    SolveStatus ss = lastSolveStatus();

    if (m_geomSymbols.contains(c.uuid)) {
        m_geomSymbols[c.uuid]->Update(c, geoms, ss);
        m_ctx->Redisplay(m_geomSymbols[c.uuid], Standard_False);
    } else if (m_dimLines.contains(c.uuid)) {
        m_dimLines[c.uuid]->Update(c, geoms, ss);
        m_ctx->Redisplay(m_dimLines[c.uuid], Standard_False);
    } else {
        createSymbolFor(c);
    }
}

void ConstraintOverlayManager::removeSymbolFor(const QString& uuid) {
    if (m_geomSymbols.contains(uuid)) {
        m_ctx->Remove(m_geomSymbols[uuid], Standard_False);
        m_geomSymbols.remove(uuid);
    }
    if (m_dimLines.contains(uuid)) {
        m_ctx->Remove(m_dimLines[uuid], Standard_False);
        m_dimLines.remove(uuid);
    }
}

void ConstraintOverlayManager::clearAll() {
    if (!m_ctx.IsNull()) {
        for (auto& sym : m_geomSymbols) m_ctx->Remove(sym, Standard_False);
        for (auto& dim : m_dimLines)    m_ctx->Remove(dim, Standard_False);
        // ✅ Task D: 清除點 AIS
        for (auto& pt  : m_pointAISMap) m_ctx->Remove(pt,  Standard_False);
        m_ctx->UpdateCurrentViewer();
    }
    m_geomSymbols.clear();
    m_dimLines.clear();
    m_pointAISMap.clear();  // ✅ Task D
}

// ─────────────────────────────────────────────────────────────────────────────
// 公開更新介面
// ─────────────────────────────────────────────────────────────────────────────

void ConstraintOverlayManager::rebuildAll() {
    clearAll();
    const QList<SketchConstraint>* constraints = sourceConstraints();
    if (!constraints) return;

    for (const SketchConstraint& c : *constraints)
        createSymbolFor(c);

    // ✅ Task D: 重建 SketchPoint AIS 物件
    rebuildPoints();

    if (!m_ctx.IsNull())
        m_ctx->UpdateCurrentViewer();
}

void ConstraintOverlayManager::setVisible(bool v) {
    m_visible = v;
    if (m_ctx.IsNull()) return;

    for (auto& sym : m_geomSymbols) {
        if (v) m_ctx->Display(sym, Standard_False);
        else   m_ctx->Erase(sym, Standard_False);
    }
    for (auto& dim : m_dimLines) {
        if (v) m_ctx->Display(dim, Standard_False);
        else   m_ctx->Erase(dim, Standard_False);
    }
    m_ctx->UpdateCurrentViewer();
}

void ConstraintOverlayManager::setTypeVisible(ConstraintType type, bool v) {
    if (v) m_hiddenTypes.remove(type);
    else   m_hiddenTypes.insert(type);
    rebuildAll();
}

// ─────────────────────────────────────────────────────────────────────────────
// Private Slots
// ─────────────────────────────────────────────────────────────────────────────

void ConstraintOverlayManager::onSourceRebuilt() {
    rebuildAll();
}

void ConstraintOverlayManager::onConstraintAdded(const QString& /*uuid*/) {
    rebuildAll();
}

void ConstraintOverlayManager::onConstraintRemoved(const QString& uuid) {
    removeSymbolFor(uuid);
    if (!m_ctx.IsNull()) m_ctx->UpdateCurrentViewer();
}


// ─────────────────────────────────────────────────────────────────────────────
// Phase 3B 新增：輕量尺寸線更新 & dimLineAIS 查詢
// ─────────────────────────────────────────────────────────────────────────────

void ConstraintOverlayManager::updateDimLine(const QString& uuid,
                                              double newOffsetX,
                                              double newOffsetY)
{
    auto it = m_dimLines.find(uuid);
    if (it == m_dimLines.end()) return;

    Handle(AIS_DimensionLine) dimAIS = it.value();
    if (dimAIS.IsNull()) return;

    dimAIS->setDimLineOffset(newOffsetX, newOffsetY);

    if (!m_ctx.IsNull()) {
        m_ctx->RecomputePrsOnly(dimAIS, Standard_False);
        m_ctx->UpdateCurrentViewer();
    }
}

Handle(AIS_DimensionLine) ConstraintOverlayManager::dimLineAISForConstraint(
    const QString& constraintUuid) const
{
    return m_dimLines.value(constraintUuid);
}

// ── Task D: SketchPoint AIS 重建 ─────────────────────────────────────────────
void ConstraintOverlayManager::rebuildPoints()
{
    // 只在 master 模式且有 sketch 時執行
    if (m_mode != Mode::Master || !m_sketch || m_ctx.IsNull()) return;

    SolveStatus status = lastSolveStatus();
    const auto pts = m_sketch->points();

    // 取得草圖平面的 OCCT Ax3
    gp_Ax3 ax3;
    if (m_sketch->plane()) {
        QVector3D orig = m_sketch->plane()->origin();
        QVector3D xDir = m_sketch->plane()->xAxis();
        QVector3D yDir = m_sketch->plane()->yAxis();
        ax3 = gp_Ax3(gp_Pnt(orig.x(), orig.y(), orig.z()),
                     gp_Dir(m_sketch->plane()->normal().x(),
                            m_sketch->plane()->normal().y(),
                            m_sketch->plane()->normal().z()),
                     gp_Dir(xDir.x(), xDir.y(), xDir.z()));
    }

    for (auto* pt : pts) {
        // Intersection 點（臨時交點）不顯示
        if (pt->origin == SketchPoint::Origin::Intersection) continue;

        Handle(SketchPointAIS) ais = new SketchPointAIS(pt, ax3);
        ais->updateSolveStatus(status);
        m_ctx->Display(ais, Standard_False);
        m_ctx->Deactivate(ais);   // 預設關閉選取，由 GetGeom mode 主動激活
        m_pointAISMap.insert(pt->uuid, ais);
    }
}
} // namespace aicad::cad
