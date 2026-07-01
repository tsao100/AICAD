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

    connect(sketch, &Sketch::rebuilt,
            this, &ConstraintOverlayManager::onSourceRebuilt,
            Qt::UniqueConnection);
    // ⚠️ Qt::UniqueConnection 對 lambda/functor 連線無效（Qt 只能對
    //    pointer-to-member-function 比較是否重複），標上該旗標不會有任何
    //    保護效果，反而會誤導閱讀者以為已防止重複連線。
    //    這裡的「不重複」實際上是靠 attachMaster() 開頭的 detach()
    //    （內部 disconnect(m_sketch, nullptr, this, nullptr) 會先清除
    //    舊 sketch 對 this 的所有連線，包含 lambda）來保證，故移除旗標。
    connect(sketch, &Sketch::constraintSolved,
            this, [this](SolveResult){ rebuildAll(); });

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
        // 對有兩個 refs 的尺寸約束（如 FixedDistance），用 resolvePosition 取精確端點
        if (c.refs.size() >= 2 && m_sketch) {
            QVector2D rp1 = c.refs[0].resolvePosition(m_sketch);
            QVector2D rp2 = c.refs[1].resolvePosition(m_sketch);
            if (!rp1.isNull() || !rp2.isNull())
                dim->setRefPositions(rp1, rp2);
        } else if (c.refs.size() == 1 && m_sketch) {
            // 單 ref 類型（FixedX, FixedY, CoordinateDim）：設 rp1，rp2 依值合成
            QVector2D rp1 = c.refs[0].resolvePosition(m_sketch);
            QVector2D rp2;
            if (c.type == ConstraintType::FixedX)
                rp2 = QVector2D(static_cast<float>(c.value), rp1.y());
            else if (c.type == ConstraintType::FixedY)
                rp2 = QVector2D(rp1.x(), static_cast<float>(c.value));
            else if (c.type == ConstraintType::CoordinateDim)
                rp2 = QVector2D(static_cast<float>(c.value), rp1.y());
            else
                rp2 = rp1;
            if (!rp1.isNull())
                dim->setRefPositions(rp1, rp2);
        }
        // 套用已儲存的尺寸線偏移（使用者拖曳後存入 constraint）
        dim->setDimLineOffset(c.dimLineOffsetX, c.dimLineOffsetY);
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
        Handle(AIS_DimensionLine) dimAIS = m_dimLines[c.uuid];
        dimAIS->Update(c, geoms, ss);
        // 更新時也重新設定端點位置
        if (c.refs.size() >= 2 && m_sketch) {
            QVector2D rp1 = c.refs[0].resolvePosition(m_sketch);
            QVector2D rp2 = c.refs[1].resolvePosition(m_sketch);
            if (!rp1.isNull() || !rp2.isNull())
                dimAIS->setRefPositions(rp1, rp2);
        } else if (c.refs.size() == 1 && m_sketch) {
            QVector2D rp1 = c.refs[0].resolvePosition(m_sketch);
            QVector2D rp2;
            if (c.type == ConstraintType::FixedX)
                rp2 = QVector2D(static_cast<float>(c.value), rp1.y());
            else if (c.type == ConstraintType::FixedY)
                rp2 = QVector2D(rp1.x(), static_cast<float>(c.value));
            else if (c.type == ConstraintType::CoordinateDim)
                rp2 = QVector2D(static_cast<float>(c.value), rp1.y());
            else
                rp2 = rp1;
            if (!rp1.isNull())
                dimAIS->setRefPositions(rp1, rp2);
        }
        dimAIS->setDimLineOffset(c.dimLineOffsetX, c.dimLineOffsetY);
        // Redisplay 觸發 Compute()，填入最新 m_labelRegions；
        // 接著必須 RecomputeSelectionOnly 讓 ComputeSelection() 依新位置重建
        // Select3D_SensitivePoint，否則拖曳後 hover 偵測座標停在舊位置。
        m_ctx->Redisplay(dimAIS, Standard_False);
        m_ctx->RecomputeSelectionOnly(dimAIS);
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
        // RecomputePrsOnly 觸發 Compute()，重新填入 m_labelRegions（新位置）；
        // RecomputeSelectionOnly 再觸發 ComputeSelection()，依新的 m_labelRegions
        // 重建 Select3D_SensitivePoint，確保 hover 偵測點跟著數值文字一起移動。
        // 若只呼叫 RecomputePrsOnly 而省略 RecomputeSelectionOnly，拖曳後
        // SensitivePoint 仍停在舊座標，導致文字可見但 hover 偵測失效。
        m_ctx->RecomputePrsOnly(dimAIS, Standard_False);
        m_ctx->RecomputeSelectionOnly(dimAIS);
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
    // ✅ Phase 1 重構後：SketchPointAIS 已統一在 Sketch::m_aisShapes 中，
    //    由 Sketch::displayInContext() 統一顯示與管理。
    //    Overlay 僅需建立 pointAISMap 索引（從 sketch->aisShapes() 轉型），
    //    供 UIManager 的 GetGeom Activate/Deactivate 使用。
    if (m_mode != Mode::Master || !m_sketch) return;

    m_pointAISMap.clear();

    // 從 Sketch 的主 AIS 列表中找出 SketchPointAIS，填入 pointAISMap
    for (const auto& obj : m_sketch->aisShapes()) {
        if (auto ptAis = Handle(SketchPointAIS)::DownCast(obj)) {
            m_pointAISMap.insert(ptAis->pointUuid(), ptAis);
        }
    }
}
} // namespace aicad::cad