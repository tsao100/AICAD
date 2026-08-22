/**
 * @file SketchSelectionPicker.cpp
 * @brief 見 SketchSelectionPicker.h 檔頭說明。互動邏輯比照
 *        EraseCommand.cpp 的模式 B（GEOM_PICKED / STRING_INPUT /
 *        COMMAND_CANCELLED 訂閱），泛化為可重用元件。
 */
#include "SketchSelectionPicker.h"

#include "../core/Application.h"
#include "../core/CommandLineManager.h"
#include "../core/EventBus.h"
#include "../cad/Sketch.h"
#include "../ui/UIManager.h"
#include "../view/CadView.h"

#include <QDebug>

namespace aicad {
namespace command {

using namespace cad;

namespace {
/// 比照 EraseCommand.cpp 的 isFixedReferenceUuid()：草圖平面參考幾何
/// （X 軸／Y 軸／原點）為固定參考，不可被選取。
bool isFixedReferenceUuid(const QString& uuid)
{
    return uuid.startsWith("sketch_xaxis:") ||
           uuid.startsWith("sketch_yaxis:") ||
           uuid.startsWith("sketch_origin:");
}

/// 供窗選/穿越窗選/籬選整合使用：取得目前的 CadView（找不到回傳 nullptr）。
view::CadView* activeCadView()
{
    auto* app   = core::Application::instance();
    auto* uiMgr = app ? app->uiManager() : nullptr;
    return uiMgr ? uiMgr->cadView() : nullptr;
}
} // namespace

SketchSelectionPicker::SketchSelectionPicker(QObject* parent)
    : QObject(parent)
{
}

SketchSelectionPicker::~SketchSelectionPicker()
{
    if (m_active) cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// begin
// ─────────────────────────────────────────────────────────────────────────

void SketchSelectionPicker::begin(cad::Sketch* sketch, Mode mode, const QString& prompt)
{
    if (m_active) unsubscribeAll();

    m_sketch     = sketch;
    m_mode       = mode;
    m_basePrompt = prompt;
    m_pending.clear();
    m_active     = true;

    subscribeAll();

    if (mode == Mode::PickMultiple) {
        if (auto* cadView = activeCadView())
            cadView->setCommandBoxSelectEligible(true);
    }

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->showPrompt(prompt);
        cmdMgr->waitForInput(core::InputType::String);
    }
}

void SketchSelectionPicker::abortSilently()
{
    cleanup();
}

// ─────────────────────────────────────────────────────────────────────────
// subscribeAll / unsubscribeAll
// ─────────────────────────────────────────────────────────────────────────

void SketchSelectionPicker::subscribeAll()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;

    bus->subscribe(core::Events::GEOM_PICKED, this,
                   [this](const QVariant& data) { onGeomPicked(data); });

    bus->subscribe(core::Events::STRING_INPUT, this,
                   [this](const QVariant& data) { onConfirm(data); });

    bus->subscribe(core::Events::COMMAND_CANCELLED, this,
                   [this](const QVariant& data) { onCancelled(data); });

    // 窗選/穿越窗選/籬選/多邊形選取完成後的結果（見標頭檔說明）。
    // PickSingle 模式下不會啟用 commandBoxSelectEligible，實務上不會收到
    // 這個事件；仍然訂閱以保持程式碼路徑單純，onBoxSelected() 內部會依
    // m_mode 自行判斷是否忽略。
    bus->subscribe(core::Events::SKETCH_GEOM_SELECTED, this,
                   [this](const QVariant& data) { onBoxSelected(data); });
}

void SketchSelectionPicker::unsubscribeAll()
{
    auto* bus = core::Application::instance()->eventBus();
    if (!bus) return;

    bus->unsubscribe(core::Events::GEOM_PICKED,        this);
    bus->unsubscribe(core::Events::STRING_INPUT,       this);
    bus->unsubscribe(core::Events::COMMAND_CANCELLED,  this);
    bus->unsubscribe(core::Events::SKETCH_GEOM_SELECTED, this);
}

// ─────────────────────────────────────────────────────────────────────────
// onGeomPicked
// ─────────────────────────────────────────────────────────────────────────

void SketchSelectionPicker::onGeomPicked(const QVariant& data)
{
    if (!m_active) return;

    const QVariantMap map = data.toMap();
    const QString uuid = map.value("geomUuid").toString();

    if (uuid.isEmpty() || isFixedReferenceUuid(uuid)) {
        auto* cmdMgr = core::CommandLineManager::instance();
        if (cmdMgr) cmdMgr->printWarning("⚠️  No selectable geometry at that point — click closer.");
        return;
    }

    if (m_mode == Mode::PickSingle) {
        m_pending.clear();
        m_pending.append(uuid);
        const QStringList result = m_pending;
        cleanup();
        Q_EMIT confirmed(result);
        return;
    }

    // Mode::PickMultiple：toggle 選取狀態
    if (m_pending.contains(uuid)) {
        m_pending.removeAll(uuid);
        setHighlight(uuid, false);
    } else {
        m_pending.append(uuid);
        setHighlight(uuid, true);
    }

    updatePrompt();
}

// ─────────────────────────────────────────────────────────────────────────
// onConfirm — 使用者按 Enter（STRING_INPUT，僅 PickMultiple 模式有意義）
// ─────────────────────────────────────────────────────────────────────────

void SketchSelectionPicker::onConfirm(const QVariant& /*data*/)
{
    if (!m_active || m_mode != Mode::PickMultiple) return;

    const QStringList result = m_pending;
    cleanup();

    // 一律發出 confirmed()，即使 result 是空的——「Enter 空選」對不同呼叫端
    // 意義不同（MOVE/COPY/ROTATE/MIRROR 視為取消；TRIM/EXTEND 視為「全選
    // 作為剪切邊/邊界邊」），因此交由呼叫端自行判斷 result.isEmpty() 後決定
    // 語意，本選取器不預設立場。
    Q_EMIT confirmed(result);
}

// ─────────────────────────────────────────────────────────────────────────
// onCancelled — 使用者按 Esc（COMMAND_CANCELLED）
// ─────────────────────────────────────────────────────────────────────────

void SketchSelectionPicker::onCancelled(const QVariant& /*data*/)
{
    if (!m_active) return;
    cleanup();
    Q_EMIT cancelled();
}

// ─────────────────────────────────────────────────────────────────────────
// onBoxSelected — CadView 窗選/穿越窗選/籬選/多邊形選取完成
// （Events::SKETCH_GEOM_SELECTED，見標頭檔「窗選／穿越窗選／籬選」說明）
// ─────────────────────────────────────────────────────────────────────────

void SketchSelectionPicker::onBoxSelected(const QVariant& data)
{
    if (!m_active || m_mode != Mode::PickMultiple) return;

    const QVariantMap map   = data.toMap();
    const QStringList uuids = map.value("uuids").toStringList();

    // uuids 是框選完成當下 AIS context 內「完整」的選取結果（CadView 一律
    // 以 additive 模式套用窗選，見 setCommandBoxSelectEligible() 說明），
    // 直接以此覆蓋 m_pending，並過濾掉固定參考幾何（軸線/原點的虛擬 UUID，
    // 若剛好落在框選範圍內也可能出現在這個清單中）。
    m_pending.clear();
    for (const QString& uuid : uuids) {
        if (uuid.isEmpty() || isFixedReferenceUuid(uuid)) continue;
        m_pending.append(uuid);
    }

    // CadView::finishBoxSelect()/finishBoxSelectPolygon() 結束時會呼叫
    // CommandLineManager::resetInputWait()，本選取器仍在等待使用者按 Enter
    // 確認（或繼續點選/框選更多物件），需要重新設定等待輸入狀態，否則
    // 命令列會卡在「未等待任何輸入」，使用者打字/按 Enter 不會有反應。
    updatePrompt();
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->waitForInput(core::InputType::String);
}

// ─────────────────────────────────────────────────────────────────────────
// setHighlight — 比照 EraseCommand::setHighlight()，僅處理一般幾何
// （不處理尺寸線/約束符號，見標頭檔說明）
// ─────────────────────────────────────────────────────────────────────────

void SketchSelectionPicker::setHighlight(const QString& uuid, bool on)
{
    auto* app     = core::Application::instance();
    auto* uiMgr   = app ? app->uiManager() : nullptr;
    auto* cadView = uiMgr ? uiMgr->cadView() : nullptr;
    if (!cadView || !m_sketch) return;

    auto context = cadView->context();
    if (context.IsNull()) return;

    const QList<QString>& uuids = m_sketch->aisShapeUuids();
    QList<Handle(AIS_InteractiveObject)> shapes = m_sketch->aisShapes();
    for (int i = 0; i < uuids.size() && i < shapes.size(); ++i) {
        if (uuids[i] != uuid) continue;
        const Handle(AIS_InteractiveObject)& obj = shapes[i];
        if (obj.IsNull()) return;
        const bool isSelected = context->IsSelected(obj);
        if (on != isSelected)
            context->AddOrRemoveSelected(obj, Standard_True);
        return;
    }
}

// ─────────────────────────────────────────────────────────────────────────
// updatePrompt
// ─────────────────────────────────────────────────────────────────────────

void SketchSelectionPicker::updatePrompt()
{
    auto* cmdMgr = core::CommandLineManager::instance();
    if (!cmdMgr) return;

    cmdMgr->showPrompt(
        QString("%1 (%2 selected)").arg(m_basePrompt).arg(m_pending.size()));
}

// ─────────────────────────────────────────────────────────────────────────
// cleanup — 取消訂閱、清空狀態、還原 CadView 選取高亮
//
// 注意：與 EraseCommand::cleanup() 不同，本函式「不」把 CadView 切回
// Sketching 模式、也「不」呼叫任何 Command::complete()——選取器只是命令的
// 一個互動子階段，命令可能接著要進入「取基準點」等下一階段，仍需要
// GetGeom／取點相關的 CadView 模式，切換時機交由擁有本選取器的 Command
// 自行決定。
// ─────────────────────────────────────────────────────────────────────────

void SketchSelectionPicker::cleanup()
{
    if (!m_active) return;
    unsubscribeAll();
    m_active = false;

    // 對稱關閉 begin() 在 PickMultiple 模式下開啟的窗選/穿越窗選/籬選
    // 資格宣告，避免殘留影響後續其他不支援框選的 GetGeom 用途（見
    // CadView::setCommandBoxSelectEligible() 標頭檔說明）。PickSingle
    // 模式下 begin() 未曾開啟過，這裡呼叫 false 是安全的 no-op。
    if (auto* cadView = activeCadView())
        cadView->setCommandBoxSelectEligible(false);

    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) cmdMgr->clearPrompt();
}

} // namespace command
} // namespace aicad
