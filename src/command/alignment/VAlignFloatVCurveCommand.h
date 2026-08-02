/**
 * @file VAlignFloatVCurveCommand.h
 * @brief Step 15 — VALIGNFLOATVCURVE 命令（alias: VADD）
 *
 * 流程：
 *  1. 切換 VAlignProfileView 至 Tool::AddVip 模式（顯示插入預覽）
 *  2. 使用者在 Profile 區域點擊 → 取得 (chainage, elevation)
 *  3. 命令列提示「K value: 」
 *  4. 使用者於 VAlignCommandBar 輸入 K= 或 LVC= 值
 *  5. 呼叫 VerticalAlignmentEdit::addVip() → setKValue() → solve()
 *  6. 切回 Tool::Select，emit finished()
 *
 *  特殊情形：
 *   - 直接 Enter 略過 K 輸入 → lvc=0（純折點）
 *   - ESC（POINT_CANCELLED）→ 取消並還原 Tool::Select
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

class VAlignFloatVCurveCommand : public Command
{
    Q_OBJECT

public:
    explicit VAlignFloatVCurveCommand(QObject* parent = nullptr);
    ~VAlignFloatVCurveCommand() override = default;

    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private Q_SLOTS:
    void onVipAdded(double ch, double el);
    void onChainageSet(double ch);
    void onKValueSet(double K);
    void onLvcSet(double lvc);
    void onCommandEntered(const QString& text);

private:
    void promptForK();
    void commitVip(double K, double lvc);
    void cancelCommand();
    void cleanup() override;

    // ── State ────────────────────────────────────────────────────────────────
    railway::AlignmentDocument* m_alignDoc   = nullptr;
    ui::VAlignProfileView*      m_profileView = nullptr;
    ui::VAlignCommandBar*       m_commandBar  = nullptr;

    enum class Phase { WaitVip, WaitK } m_phase = Phase::WaitVip;

    double m_pendingCh = 0.0;   ///< chainage 待確認
    double m_pendingEl = 0.0;   ///< elevation 待確認
    int    m_newIdx    = -1;    ///< addVip() 回傳的 index（供 setKValue 使用）

    // Qt 連接 handles（cleanup 時斷開）
    QMetaObject::Connection m_connVipAdded;
    QMetaObject::Connection m_connK;
    QMetaObject::Connection m_connLvc;
    QMetaObject::Connection m_connCmd;
    QMetaObject::Connection m_connCh;
};

REGISTER_COMMAND("valignfloatvcurve", VAlignFloatVCurveCommand);

} // namespace command
} // namespace aicad
