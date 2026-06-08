/**
 * @file VAlignMovePVICommand.h
 * @brief Step 15 — VALIGNMOVEPVI 命令（alias: VMOVE）
 *
 * 流程：
 *  1. 切換 VAlignProfileView 至 Tool::Select
 *  2. 使用者拖曳任一 VIP diamond
 *  3. 拖曳結束（mouseRelease → vipMoved 信號）後自動呼叫 solve()
 *  4. 完成後命令繼續監聽（允許連續拖曳多個 VIP）
 *  5. ESC / 右鍵 → 結束命令
 *
 * Undo 整合：
 *  vipMoved 信號觸發時記錄 before/after JSON → 推入 QUndoStack（Step 17 完成後接入）。
 */
#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"
#include "command/CommandTypes.h"
#include "railway/AlignmentDocument.h"
#include "ui/VAlignProfileView.h"

namespace aicad {
namespace command {

class VAlignMovePVICommand : public Command
{
    Q_OBJECT

public:
    explicit VAlignMovePVICommand(QObject* parent = nullptr);
    ~VAlignMovePVICommand() override = default;

    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private Q_SLOTS:
    void onVipMoved(int idx, double ch, double el);

private:
    void cleanup() override;

    railway::AlignmentDocument* m_alignDoc    = nullptr;
    ui::VAlignProfileView*      m_profileView = nullptr;

    QMetaObject::Connection m_connMoved;
    QMetaObject::Connection m_connCancel;
};

REGISTER_COMMAND("valignmovepvi", VAlignMovePVICommand);

} // namespace command
} // namespace aicad

// ─────────────────────────────────────────────────────────────────────────────
//  Implementation (header-only for simplicity; move to .cpp if needed)
// ─────────────────────────────────────────────────────────────────────────────

#include "core/Application.h"
#include "core/EventBus.h"
#include <QDebug>

using namespace aicad::core;

namespace aicad {
namespace command {

inline VAlignMovePVICommand::VAlignMovePVICommand(QObject* parent)
    : Command("valignmovepvi", "Move VIP (drag in Profile)", parent)
{}

inline CommandResult VAlignMovePVICommand::execute(const CommandContext& ctx)
{
    m_alignDoc    = ctx.alignmentDoc;
    m_profileView = ctx.profileView;

    if (!m_alignDoc || !m_profileView)
        return CommandResult::Failure("AlignmentDocument 或 ProfileView 未初始化");

    // 切換至 Select 模式（允許拖曳）
    m_profileView->setTool(ui::VAlignProfileView::Tool::Select);

    // 監聽 vipMoved：拖曳結束後重算
    m_connMoved = connect(m_profileView, &ui::VAlignProfileView::vipMoved,
                          this, &VAlignMovePVICommand::onVipMoved);

    // ESC 結束
    EventBus* bus = Application::instance()->eventBus();
    bus->subscribe(Events::POINT_CANCELLED, this,
                   [this](const QVariant&) {
                       QMetaObject::invokeMethod(this, [this]() {
                           Q_EMIT finished(CommandResult::Success("VAlignMovePVI: ended"));
                       }, Qt::QueuedConnection);
                   });

    setState(CommandState::Running);
    outputMessage("縱斷面 MovePVI：拖曳 VIP 菱形移動位置，ESC / 右鍵結束");
    bus->publish(Events::COMMAND_PROMPT, tr("縱斷面 ▶ 拖曳 VIP 移動"));
    return CommandResult::Success("Waiting for drag");
}

inline void VAlignMovePVICommand::onVipMoved(int idx, double ch, double el)
{
    if (!m_alignDoc) return;

    // Profile View 已經更新了 m_vips，這裡只需重算 VerticalAlignmentEdit
    railway::VerticalAlignmentEdit* va = m_alignDoc->vertical();
    va->moveVip(idx, ch, el);
    va->solve();

    outputMessage(QString("VIP #%1 移至  CH=%2  EL=%3")
                      .arg(idx + 1)
                      .arg(ch, 0, 'f', 3)
                      .arg(el, 0, 'f', 3));
}

inline void VAlignMovePVICommand::cleanup()
{
    Application::instance()->eventBus()->unsubscribeAll(this);
    disconnect(m_connMoved);
    m_alignDoc    = nullptr;
    m_profileView = nullptr;
}

inline QString VAlignMovePVICommand::getUsage() const
{
    return "Usage: VMOVE\n"
           "  在縱斷面 Profile 拖曳 VIP 菱形以移動位置。\n"
           "  放開後自動重算，ESC 或右鍵結束命令。";
}

} // namespace command
} // namespace aicad
