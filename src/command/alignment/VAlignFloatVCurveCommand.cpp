/**
 * @file VAlignFloatVCurveCommand.cpp
 * @brief Step 15 — VALIGNFLOATVCURVE 實作
 */

#include "command/alignment/VAlignFloatVCurveCommand.h"
#include "core/Application.h"
#include "core/EventBus.h"
#include <QDebug>

using namespace aicad::core;

namespace aicad {
namespace command {

VAlignFloatVCurveCommand::VAlignFloatVCurveCommand(QObject* parent)
    : Command("valignfloatvcurve", "Add VIP with Vertical Curve (K value)", parent)
{}

// ── execute ───────────────────────────────────────────────────────────────────

CommandResult VAlignFloatVCurveCommand::execute(const CommandContext& ctx)
{
    m_alignDoc    = ctx.alignmentDoc;
    m_profileView = ctx.profileView;

    // commandBar：透過 profileView 的父層 DockWidget 取得（動態 cast 尋找）
    if (m_profileView) {
        auto* parent = m_profileView->parent();
        while (parent) {
            if (auto* bar = parent->findChild<ui::VAlignCommandBar*>()) {
                m_commandBar = bar;
                break;
            }
            parent = parent->parent();
        }
    }

    if (!m_alignDoc) {
        return CommandResult::Failure(
            "No AlignmentDocument — 請先開啟或建立一條 Alignment。");
    }
    if (!m_profileView) {
        return CommandResult::Failure(
            "VAlignProfileView 未初始化 — 請先開啟縱斷面編輯器（PROFILEVIEW）。");
    }

    m_phase = Phase::WaitVip;
    m_newIdx = -1;

    // 切換至 AddVip 模式（觸發 Step 14 的插入預覽）
    m_profileView->setTool(ui::VAlignProfileView::Tool::AddVip);

    // ── 連接 Profile View 信號 ──────────────────────────────────────────────
    m_connVipAdded = connect(m_profileView, &ui::VAlignProfileView::vipAdded,
                             this, &VAlignFloatVCurveCommand::onVipAdded);

    // ── 連接 CommandBar 信號 ─────────────────────────────────────────────────
    if (m_commandBar) {
        m_commandBar->setPrompt("縱斷面 ▶ 點擊位置插入 VIP");
        m_commandBar->focusInput();

        m_connK   = connect(m_commandBar, &ui::VAlignCommandBar::kValueSet,
                          this, &VAlignFloatVCurveCommand::onKValueSet);
        m_connLvc = connect(m_commandBar, &ui::VAlignCommandBar::lvcSet,
                            this, &VAlignFloatVCurveCommand::onLvcSet);
        m_connCmd = connect(m_commandBar, &ui::VAlignCommandBar::commandEntered,
                            this, &VAlignFloatVCurveCommand::onCommandEntered);
        m_connCh  = connect(m_commandBar, &ui::VAlignCommandBar::chainageSet,
                           this, &VAlignFloatVCurveCommand::onChainageSet);
    }

    // ESC / right-click
    EventBus* bus = Application::instance()->eventBus();
    bus->subscribe(Events::POINT_CANCELLED, this,
                   [this](const QVariant&) {
                       QMetaObject::invokeMethod(this, [this]() { cancelCommand(); },
                                                 Qt::QueuedConnection);
                   });

    setState(CommandState::Running);
    outputMessage("縱斷面 AddVip：點擊 Profile 區域插入新 VIP");
    bus->publish(Events::COMMAND_PROMPT, tr("縱斷面 ▶ 點擊位置插入 VIP"));
    return CommandResult::Success("Waiting for VIP click");
}

// ── Slot：Profile 區域點擊 → 取得 ch / el ──────────────────────────────────

void VAlignFloatVCurveCommand::onVipAdded(double ch, double el)
{
    if (m_phase != Phase::WaitVip) return;

    m_pendingCh = ch;
    m_pendingEl = el;
    m_phase     = Phase::WaitK;

    // 切回 Select 避免重複點擊
    m_profileView->setTool(ui::VAlignProfileView::Tool::Select);

    promptForK();
}

// ── Slot：命令列輸入 CH=（允許數字精確輸入 chainage）─────────────────────────

void VAlignFloatVCurveCommand::onChainageSet(double ch)
{
    if (m_phase == Phase::WaitVip) {
        m_pendingCh = ch;
        // elevation 預設為現有 profile 高程（此處簡化為 0，使用者再以 EL= 覆蓋）
        m_pendingEl = 0.0;
        outputMessage(QString("CH=%1 — 請輸入 EL=<高程>").arg(ch, 0, 'f', 3));
    }
}

// ── Slot：K= 輸入 ─────────────────────────────────────────────────────────────

void VAlignFloatVCurveCommand::onKValueSet(double K)
{
    if (m_phase != Phase::WaitK) return;
    commitVip(K, 0.0 /* lvc 由 setKValue 計算 */);
}

// ── Slot：LVC= 直接指定曲線長 ────────────────────────────────────────────────

void VAlignFloatVCurveCommand::onLvcSet(double lvc)
{
    if (m_phase != Phase::WaitK) return;
    // 直接以 lvc 新增，不走 setKValue
    m_newIdx = m_alignDoc->vertical()->addVip(m_pendingCh, m_pendingEl, lvc);
    m_alignDoc->vertical()->solve();

    outputMessage(QString("VIP #%1  CH=%2  EL=%3  LVC=%4 m")
                      .arg(m_newIdx + 1)
                      .arg(m_pendingCh, 0, 'f', 3)
                      .arg(m_pendingEl, 0, 'f', 3)
                      .arg(lvc, 0, 'f', 1));

    Q_EMIT finished(CommandResult::Success("VAlignFloatVCurve: VIP added (LVC direct)"));
}

// ── Slot：一般命令文字（Enter 略過 → lvc=0；ESC 等）─────────────────────────

void VAlignFloatVCurveCommand::onCommandEntered(const QString& text)
{
    const QString upper = text.toUpper().trimmed();

    if (upper == "ESC" || upper == "CANCEL") {
        cancelCommand();
        return;
    }

    if (m_phase == Phase::WaitK) {
        // Enter（空白）→ lvc=0，純折點
        commitVip(0.0, 0.0);
    }
}

// ── 輸出 K 值提示 ─────────────────────────────────────────────────────────────

void VAlignFloatVCurveCommand::promptForK()
{
    const QString msg = QString(
                            "VIP @ CH=%1 EL=%2 — 輸入 K=<值>（豎曲線率）、LVC=<值>（曲線長），或按 Enter 建立純折點")
                            .arg(m_pendingCh, 0, 'f', 3)
                            .arg(m_pendingEl, 0, 'f', 3);

    outputMessage(msg);
    EventBus* bus = Application::instance()->eventBus();
    bus->publish(Events::COMMAND_PROMPT, msg);

    if (m_commandBar) {
        m_commandBar->setPrompt(
            QString("K=  LVC=  CH=%1  EL=%2")
                .arg(m_pendingCh, 0, 'f', 1)
                .arg(m_pendingEl, 0, 'f', 3));
    }
}

// ── 提交 VIP ──────────────────────────────────────────────────────────────────

void VAlignFloatVCurveCommand::commitVip(double K, double /*lvcDirect*/)
{
    railway::VerticalAlignmentEdit* va = m_alignDoc->vertical();

    // 先以 lvc=0 新增
    m_newIdx = va->addVip(m_pendingCh, m_pendingEl, 0.0);

    // 若指定了 K 值（>0），計算 lvc = K × |Δg%|
    if (K > 1e-9 && m_newIdx > 0 && m_newIdx < /* not last */-1) {
        va->setKValue(m_newIdx, K);
    }

    va->solve();

    outputMessage(QString("VIP #%1  CH=%2  EL=%3  K=%4")
                      .arg(m_newIdx + 1)
                      .arg(m_pendingCh, 0, 'f', 3)
                      .arg(m_pendingEl, 0, 'f', 3)
                      .arg(K, 0, 'f', 1));

    Q_EMIT finished(CommandResult::Success("VAlignFloatVCurve: VIP added"));
}

// ── 取消 ──────────────────────────────────────────────────────────────────────

void VAlignFloatVCurveCommand::cancelCommand()
{
    if (m_profileView)
        m_profileView->setTool(ui::VAlignProfileView::Tool::Select);
    Q_EMIT finished(CommandResult::Failure("VAlignFloatVCurve: cancelled"));
}

// ── cleanup ───────────────────────────────────────────────────────────────────

void VAlignFloatVCurveCommand::cleanup()
{
    Application::instance()->eventBus()->unsubscribeAll(this);

    disconnect(m_connVipAdded);
    disconnect(m_connK);
    disconnect(m_connLvc);
    disconnect(m_connCmd);
    disconnect(m_connCh);

    if (m_commandBar) {
        m_commandBar->setPrompt(QString());
        m_commandBar->clearInput();
    }
    if (m_profileView)
        m_profileView->setTool(ui::VAlignProfileView::Tool::Select);

    m_alignDoc    = nullptr;
    m_profileView = nullptr;
    m_commandBar  = nullptr;
    m_newIdx      = -1;
    m_phase       = Phase::WaitVip;
}

QString VAlignFloatVCurveCommand::getUsage() const
{
    return "Usage: VADD\n"
           "  在縱斷面 Profile 區域點擊插入新 VIP。\n"
           "  點擊後輸入 K=<值>（豎曲線率）或 LVC=<長度>，Enter 略過（純折點）。";
}

} // namespace command
} // namespace aicad
