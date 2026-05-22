/**
 * @file VAlignSetKCommand.h
 * @brief Step 15 — VALIGNSETK 命令（alias: VSETK）
 *
 * 流程：
 *  1. 提示「Select VIP: 點擊 Profile 中的 VIP 菱形」
 *  2. 使用者點擊 → VAlignProfileView::vipSelected(idx)
 *  3. 提示「K value: 」
 *  4. 使用者輸入 K=<值> 或 LVC=<值>
 *  5. 呼叫 VerticalAlignmentEdit::setKValue(idx, K) → solve()
 *  6. 完成，emit finished()
 */
#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"
#include "command/CommandTypes.h"
#include "railway/AlignmentDocument.h"
#include "ui/VAlignProfileView.h"
#include "ui/VAlignCommandBar.h"

namespace aicad {
namespace command {

class VAlignSetKCommand : public Command
{
    Q_OBJECT

public:
    explicit VAlignSetKCommand(QObject* parent = nullptr);
    ~VAlignSetKCommand() override = default;

    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private Q_SLOTS:
    void onVipSelected(int idx);
    void onKValueSet(double K);
    void onLvcSet(double lvc);
    void onCommandEntered(const QString& text);

private:
    void cleanup() override;

    railway::AlignmentDocument* m_alignDoc    = nullptr;
    ui::VAlignProfileView*      m_profileView = nullptr;
    ui::VAlignCommandBar*       m_commandBar  = nullptr;

    enum class Phase { WaitSelect, WaitK } m_phase = Phase::WaitSelect;
    int m_selIdx = -1;

    QMetaObject::Connection m_connSelected;
    QMetaObject::Connection m_connK;
    QMetaObject::Connection m_connLvc;
    QMetaObject::Connection m_connCmd;
};

REGISTER_COMMAND("valignsetk", VAlignSetKCommand);

} // namespace command
} // namespace aicad

// ─────────────────────────────────────────────────────────────────────────────
//  Implementation
// ─────────────────────────────────────────────────────────────────────────────

#include "core/Application.h"
#include "core/EventBus.h"
#include <QDebug>

using namespace aicad::core;

namespace aicad {
namespace command {

inline VAlignSetKCommand::VAlignSetKCommand(QObject* parent)
    : Command("valignsetk", "Set K Value for VIP", parent)
{}

inline CommandResult VAlignSetKCommand::execute(const CommandContext& ctx)
{
    m_alignDoc    = ctx.alignmentDoc;
    m_profileView = ctx.profileView;

    if (m_profileView) {
        auto* p = m_profileView->parent();
        while (p) {
            if (auto* bar = p->findChild<ui::VAlignCommandBar*>()) {
                m_commandBar = bar;
                break;
            }
            p = p->parent();
        }
    }

    if (!m_alignDoc || !m_profileView)
        return CommandResult::Failure("AlignmentDocument 或 ProfileView 未初始化");

    m_phase  = Phase::WaitSelect;
    m_selIdx = -1;

    m_profileView->setTool(ui::VAlignProfileView::Tool::Select);

    m_connSelected = connect(m_profileView, &ui::VAlignProfileView::vipSelected,
                             this, &VAlignSetKCommand::onVipSelected);

    if (m_commandBar) {
        m_connK   = connect(m_commandBar, &ui::VAlignCommandBar::kValueSet,
                          this, &VAlignSetKCommand::onKValueSet);
        m_connLvc = connect(m_commandBar, &ui::VAlignCommandBar::lvcSet,
                            this, &VAlignSetKCommand::onLvcSet);
        m_connCmd = connect(m_commandBar, &ui::VAlignCommandBar::commandEntered,
                            this, &VAlignSetKCommand::onCommandEntered);
        m_commandBar->setPrompt("縱斷面 ▶ 點選 VIP 以設定 K 值");
        m_commandBar->focusInput();
    }

    EventBus* bus = Application::instance()->eventBus();
    bus->subscribe(Events::POINT_CANCELLED, this,
                   [this](const QVariant&) {
                       QMetaObject::invokeMethod(this, [this]() {
                           Q_EMIT finished(CommandResult::Success("VAlignSetK: cancelled"));
                       }, Qt::QueuedConnection);
                   });

    setState(CommandState::Running);
    outputMessage("縱斷面 SetK：點選 Profile 中的 VIP 菱形");
    bus->publish(Events::COMMAND_PROMPT, tr("縱斷面 ▶ 點選 VIP 以設定 K 值"));
    return CommandResult::Success("Waiting for VIP selection");
}

inline void VAlignSetKCommand::onVipSelected(int idx)
{
    if (m_phase != Phase::WaitSelect) return;
    if (idx < 0) return;

    m_selIdx = idx;
    m_phase  = Phase::WaitK;

    const QString msg = QString("VIP #%1 已選取 — 輸入 K=<值> 或 LVC=<長度>")
                            .arg(idx + 1);
    outputMessage(msg);
    Application::instance()->eventBus()->publish(Events::COMMAND_PROMPT, msg);
    if (m_commandBar) {
        m_commandBar->setPrompt(QString("K=  或  LVC=  (VIP #%1)").arg(idx + 1));
        m_commandBar->focusInput();
    }
}

inline void VAlignSetKCommand::onKValueSet(double K)
{
    if (m_phase != Phase::WaitK || m_selIdx < 0) return;

    railway::VerticalAlignmentEdit* va = m_alignDoc->vertical();
    va->setKValue(m_selIdx, K);
    va->solve();

    outputMessage(QString("VIP #%1  K=%2  →  lvc 已自動計算")
                      .arg(m_selIdx + 1).arg(K, 0, 'f', 1));
    Q_EMIT finished(CommandResult::Success("VAlignSetK: K value set"));
}

inline void VAlignSetKCommand::onLvcSet(double lvc)
{
    if (m_phase != Phase::WaitK || m_selIdx < 0) return;

    railway::VerticalAlignmentEdit* va = m_alignDoc->vertical();
    // 直接修改 VIP lvc（透過 move 保持位置不變）
    // 我們先讀取現有 ch/el，再 move（等於 no-op），然後修改 lvc
    // 由於 VipRecord 是 private，透過 removeVip + addVip 重建
    // 更簡潔：呼叫 solve() 時 lvc 已被 setKValue 更新；
    // 此處直接用 removeVip / addVip 更新 lvc。
    // 注意：這樣會遺失原有的 lvc 資訊，以新 lvc 取代。
    // 未來 Step 17 可以透過 UndoCommand 還原。

    // 讀取現有 VIP 資訊（透過 solve 前的 result）
    const auto* va_result = va->result();
    if (!va_result) return;

    outputMessage(QString("VIP #%1  LVC=%2 m")
                      .arg(m_selIdx + 1).arg(lvc, 0, 'f', 1));

    // 重建：先刪除再以新 lvc 插回
    // 讀取 chainage / elevation：由 profileView 取
    const auto& vips = m_profileView->vips();
    if (m_selIdx >= vips.size()) return;

    const double ch = vips[m_selIdx].ch;
    const double el = vips[m_selIdx].el;

    va->removeVip(m_selIdx);
    int newIdx = va->addVip(ch, el, lvc);
    Q_UNUSED(newIdx);
    va->solve();

    Q_EMIT finished(CommandResult::Success("VAlignSetK: LVC set directly"));
}

inline void VAlignSetKCommand::onCommandEntered(const QString& text)
{
    const QString upper = text.toUpper().trimmed();
    if (upper == "ESC" || upper == "CANCEL") {
        Q_EMIT finished(CommandResult::Success("VAlignSetK: cancelled"));
    }
}

inline void VAlignSetKCommand::cleanup()
{
    Application::instance()->eventBus()->unsubscribeAll(this);
    disconnect(m_connSelected);
    disconnect(m_connK);
    disconnect(m_connLvc);
    disconnect(m_connCmd);
    if (m_commandBar) {
        m_commandBar->setPrompt(QString());
        m_commandBar->clearInput();
    }
    m_alignDoc    = nullptr;
    m_profileView = nullptr;
    m_commandBar  = nullptr;
    m_selIdx      = -1;
    m_phase       = Phase::WaitSelect;
}

inline QString VAlignSetKCommand::getUsage() const
{
    return "Usage: VSETK\n"
           "  點選 Profile 中的 VIP，輸入 K=<值> 設定豎曲線率。\n"
           "  亦可輸入 LVC=<長度> 直接指定曲線長。";
}

} // namespace command
} // namespace aicad
