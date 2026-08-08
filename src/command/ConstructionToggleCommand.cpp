/**
 * @file ConstructionToggleCommand.cpp
 * @brief 見 ConstructionToggleCommand.h 檔頭說明。
 */
#include "ConstructionToggleCommand.h"

#include "../core/Application.h"
#include "../core/CommandLineManager.h"
#include "../cad/Sketch.h"
#include "../ui/UIManager.h"
#include "../view/CadView.h"

namespace aicad {
namespace command {

using namespace cad;

namespace {
bool isFixedReferenceUuid(const QString& uuid)
{
    // 草圖平面參考幾何（X 軸 / Y 軸 / 原點）為固定參考，不可切換 construction 狀態。
    return uuid.startsWith("sketch_xaxis:")  ||
           uuid.startsWith("sketch_yaxis:")  ||
           uuid.startsWith("sketch_origin:");
}
} // namespace

ConstructionToggleCommand::ConstructionToggleCommand()
    : Command("CONSTRUCTION",
              "Toggle selected sketch geometry between construction and normal (alias: CT)")
{
}

QString ConstructionToggleCommand::getUsage() const
{
    return "Usage: CONSTRUCTION — select geometry first then run CONSTRUCTION, "
           "or run CONSTRUCTION then click objects in the viewport and press Enter. "
           "Each selected line/arc/circle/... toggles between construction and "
           "normal geometry (construction geometry behaves exactly like normal "
           "geometry except it is excluded from profile/contour detection).";
}

cad::Sketch* ConstructionToggleCommand::activeSketch() const
{
    return core::Application::instance()->activeSketch();
}

// ─────────────────────────────────────────────────────────────────────────
// execute
// ─────────────────────────────────────────────────────────────────────────

CommandResult ConstructionToggleCommand::execute(const CommandContext& ctx)
{
    Sketch* sk = activeSketch();
    if (!sk) {
        auto* cmdMgr = core::CommandLineManager::instance();
        if (cmdMgr) cmdMgr->printError("No active sketch. Enter sketch edit mode first.");
        return CommandResult::Failure("No active sketch.");
    }

    // ── 模式 A：呼叫時已帶有選取的 UUID ───────────────────────────────
    if (!ctx.args.isEmpty()) {
        QStringList preSelected;
        for (const QString& uuid : ctx.args) {
            if (uuid.isEmpty() || isFixedReferenceUuid(uuid)) continue;
            if (sk->findGeometry(uuid)) preSelected.append(uuid);
        }
        if (preSelected.isEmpty()) {
            auto* cmdMgr = core::CommandLineManager::instance();
            if (cmdMgr) cmdMgr->printWarning("⚠️  No valid geometry in selection.");
            return CommandResult::Failure("No valid geometry in selection.");
        }
        applyToggle(sk, preSelected);
        return CommandResult::Success();
    }

    // ── 模式 B：尚未選取 — 進入互動選取模式 ───────────────────────────
    setWaitingForInput();
    beginSelection(sk);
    return CommandResult::Success("Waiting for selection...");
}

// ─────────────────────────────────────────────────────────────────────────
// 選取階段
// ─────────────────────────────────────────────────────────────────────────

void ConstructionToggleCommand::beginSelection(cad::Sketch* sketch)
{
    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::GetGeom);

    m_picker = new SketchSelectionPicker(this);
    connect(m_picker, &SketchSelectionPicker::confirmed,
            this, &ConstructionToggleCommand::onSelectionConfirmed);
    connect(m_picker, &SketchSelectionPicker::cancelled,
            this, &ConstructionToggleCommand::onSelectionCancelled);

    m_picker->begin(sketch, SketchSelectionPicker::Mode::PickMultiple,
                    "[CONSTRUCTION] Select objects to toggle, then press Enter:");
}

void ConstructionToggleCommand::onSelectionConfirmed(const QStringList& uuids)
{
    if (uuids.isEmpty()) {
        onSelectionCancelled();
        return;
    }
    applyToggle(activeSketch(), uuids);
    cleanup();
}

void ConstructionToggleCommand::onSelectionCancelled()
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->printMessage("CONSTRUCTION cancelled.");
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// applyToggle — 實際切換 GeomRole
// ─────────────────────────────────────────────────────────────────────────

void ConstructionToggleCommand::applyToggle(cad::Sketch* sketch, const QStringList& uuids)
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (!sketch) return;

    int toConstruction = 0;
    int toNormal       = 0;
    int skipped        = 0;

    for (const QString& uuid : uuids) {
        if (uuid.isEmpty() || isFixedReferenceUuid(uuid)) { ++skipped; continue; }

        SketchGeometry* geom = sketch->findGeometry(uuid);
        if (!geom) { ++skipped; continue; }

        // 二元切換：目前非 Normal（Construction 或 Centerline）一律視為
        // 「建構狀態」，切回 Normal；目前是 Normal 則切成 Construction。
        if (geom->isConstruction()) {
            geom->role = GeomRole::Normal;
            ++toNormal;
        } else {
            geom->role = GeomRole::Construction;
            ++toConstruction;
        }
    }

    if (toConstruction > 0 || toNormal > 0) {
        // 角色變更不影響幾何數值，理論上不需要重新求解；但沿用
        // TrimExtendHelper.cpp 修改幾何後的既有慣例（solveConstraints() +
        // emit rebuildRequested()），確保這個草圖若正驅動 Extrude 等下游
        // 3D 特徵，輪廓變更（因為某條邊變成/不再是建構線）能正確觸發
        // Document::rebuildFeature() 重新生成實體，而不只是刷新 2D 顯示。
        sketch->solveConstraints();
        Q_EMIT sketch->rebuildRequested();
    }

    if (cmdMgr) {
        QStringList parts;
        if (toConstruction > 0)
            parts << QString("%1 個 → 建構").arg(toConstruction);
        if (toNormal > 0)
            parts << QString("%1 個 → 一般").arg(toNormal);

        if (!parts.isEmpty())
            cmdMgr->printSuccess(QString("✅ 已切換: %1").arg(parts.join("，")));
        else
            cmdMgr->printWarning("⚠️  No valid geometry to toggle.");

        if (skipped > 0) {
            cmdMgr->printWarning(
                QString("⚠️  %1 個選取物件無效或為固定參考幾何，已略過。").arg(skipped));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
// cleanup
// ─────────────────────────────────────────────────────────────────────────

void ConstructionToggleCommand::cleanup()
{
    if (m_picker) {
        m_picker->abortSilently();
        m_picker->deleteLater();
        m_picker = nullptr;
    }

    auto* uiMgr   = core::Application::instance()->uiManager();
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (cadView) cadView->setMode(view::InteractionMode::Sketching);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->clearPrompt();

    if (state() == CommandState::Running)
        complete(CommandResult::Success());
}

} // namespace command
} // namespace aicad
