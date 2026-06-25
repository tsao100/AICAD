/**
 * @file VAlignCheckGradeCommand.h
 * @brief Step 19 — VALIGNCHECKGRADE 命令（alias: VCG）
 *
 * 流程：
 *  1. 提示「Max grade (%): 」→ 使用者輸入限值（預設 3.5%）
 *  2. 遍歷 VerticalAlignmentEdit 各段坡度
 *  3. 超限段：
 *     - 命令列輸出「Station xxx.x–yyy.y: Grade x.x% exceeds limit」
 *     - 呼叫 VAlignProfileView::setGradeViolations() 標紅色
 *  4. 無超限段：輸出「All grades within limit」
 *
 * 輸入格式（CommandBar 或命令列）：
 *   直接 Enter → 使用預設 3.5%
 *   數字（如 3.5 或 3.5%）→ 以該值作為限值
 */
#pragma once

#include "command/Command.h"
#include "command/CommandFactory.h"
#include "command/CommandTypes.h"
#include "railway/AlignmentDocument.h"
#include "ui/VAlignProfileView.h"
#include "core/Application.h"
#include "ui/UIManager.h"
#include "ui/VAlignEditorDockWidget.h"
#include "core/EventBus.h"

#include <QDebug>
#include <cmath>

namespace aicad {
namespace command {

class VAlignCheckGradeCommand : public Command
{
    Q_OBJECT

public:
    explicit VAlignCheckGradeCommand(QObject* parent = nullptr)
        : Command("valigncheckgrade", "Check Vertical Alignment Grades", parent)
    {}

    CommandResult execute(const CommandContext& ctx) override
    {
        m_alignDoc    = ctx.alignmentDoc;
        m_profileView = ctx.profileView;

        // Fallback: get profileView from UIManager if not in ctx
        if (!m_profileView) {
            auto* uiMgr = core::Application::instance()
                              ? core::Application::instance()->uiManager()
                              : nullptr;
            if (uiMgr && uiMgr->vAlignDockWidget())
                m_profileView = uiMgr->vAlignDockWidget()->profileView();
        }

        if (!m_alignDoc) {
            return CommandResult::Failure(
                "No AlignmentDocument — 請先建立縱斷面資料。");
        }

        // Listen for grade limit input via STRING_INPUT event
        auto* bus = core::Application::instance()->eventBus();
        bus->subscribe(core::Events::STRING_INPUT, this,
                       [this](const QVariant& v) {
                           QMetaObject::invokeMethod(this, [this, str = v.toString()]() {
                               onInput(str);
                           }, Qt::QueuedConnection);
                       });

        // Also accept POINT_CANCELLED (ESC) as "use default"
        bus->subscribe(core::Events::POINT_CANCELLED, this,
                       [this](const QVariant&) {
                           QMetaObject::invokeMethod(this, [this]() {
                               onInput(QString());  // empty = use default
                           }, Qt::QueuedConnection);
                       });

        setState(CommandState::Running);
        const QString msg = "CHECKGRADE ▶ 輸入最大坡度限值（%），Enter 使用預設值 3.5%";
        outputMessage(msg);
        core::Application::instance()->eventBus()->publish(
            core::Events::COMMAND_PROMPT, msg);

        return CommandResult::Success("Waiting for grade limit input");
    }

    bool    isInteractive() const override { return true; }
    QString getUsage()      const override
    {
        return "Usage: VALIGNCHECKGRADE\n"
               "  輸入坡度限值（%）後按 Enter，或直接 Enter 使用預設 3.5%。\n"
               "  超限區段以紅色底色標示於縱斷面視圖。";
    }

private Q_SLOTS:
    void onInput(const QString& text)
    {
        const QString t = text.trimmed();
        double maxGradePct = 3.5;   // 預設值

        if (t.isEmpty()) {
            // Empty → use default
        } else {
            // Remove trailing '%' if present
            QString num = t;
            if (num.endsWith('%')) num.chop(1);
            bool ok = false;
            double v = num.toDouble(&ok);
            if (ok && v > 0.0)
                maxGradePct = v;
            else {
                outputMessage(QString("無效輸入「%1」，使用預設值 3.5%").arg(t));
            }
        }

        runCheck(maxGradePct / 100.0);
        Q_EMIT finished(CommandResult::Success("VAlignCheckGrade done"));
    }

private:
    void runCheck(double maxGrade)
    {
        const railway::VerticalAlignmentEdit* va = m_alignDoc->vertical();
        if (!va) { outputMessage("縱斷面資料為空。"); return; }

        // Use profileView VIPs (in sync with VerticalAlignmentEdit)
        const QVector<ui::Vip>* vips = nullptr;
        QVector<ui::Vip> fallbackVips;

        if (m_profileView) {
            vips = &m_profileView->vips();
        } else {
            outputMessage("[警告] ProfileView 未綁定，無法標示紅色，僅輸出文字。");
        }

        // Build grade list from VIPs
        struct Seg { double ch0, ch1, grade; };
        QVector<Seg> segs;

        auto buildSegs = [&](const QVector<ui::Vip>& v) {
            for (int i = 0; i + 1 < v.size(); ++i) {
                const double dch = v[i+1].ch - v[i].ch;
                if (std::abs(dch) < 1e-9) continue;
                Seg s;
                s.ch0   = v[i].ch;
                s.ch1   = v[i+1].ch;
                s.grade = (v[i+1].el - v[i].el) / dch;
                segs.append(s);
            }
        };

        if (vips && !vips->isEmpty()) {
            buildSegs(*vips);
        } else {
            outputMessage("縱斷面 VIP 列表為空，無法執行坡度檢查。");
            return;
        }

        // Check against limit
        QVector<QPair<double,double>> violations;
        int violationCount = 0;

        for (const Seg& s : segs) {
            if (std::abs(s.grade) > maxGrade + 1e-9) {
                violations.append({s.ch0, s.ch1});
                ++violationCount;
                outputMessage(
                    QString("⚠ Station %1–%2: Grade %3% exceeds limit (%4%)")
                        .arg(s.ch0, 0, 'f', 1)
                        .arg(s.ch1, 0, 'f', 1)
                        .arg(s.grade * 100.0, 0, 'f', 3)
                        .arg(maxGrade * 100.0, 0, 'f', 2));
            }
        }

        if (violationCount == 0) {
            outputMessage(
                QString("✔ All grades within limit (max ±%1%)")
                    .arg(maxGrade * 100.0, 0, 'f', 2));
        } else {
            outputMessage(
                QString("%1 violation(s) found. See profile view for highlighted segments.")
                    .arg(violationCount));
        }

        // Update profile view violations
        if (m_profileView)
            m_profileView->setGradeViolations(violations, maxGrade);

        // Show profileview if not already visible
        auto* uiMgr = core::Application::instance()
                          ? core::Application::instance()->uiManager()
                          : nullptr;
        if (uiMgr && uiMgr->vAlignDockWidget() && !violations.isEmpty())
            uiMgr->vAlignDockWidget()->setVisible(true);
    }

    void cleanup() override
    {
        core::Application::instance()->eventBus()->unsubscribeAll(this);
        m_alignDoc    = nullptr;
        m_profileView = nullptr;
    }

    railway::AlignmentDocument* m_alignDoc    = nullptr;
    ui::VAlignProfileView*      m_profileView = nullptr;
};

REGISTER_COMMAND("valigncheckgrade", VAlignCheckGradeCommand);

} // namespace command
} // namespace aicad
