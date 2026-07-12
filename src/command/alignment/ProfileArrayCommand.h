#pragma once

/**
 * @file ProfileArrayCommand.h
 * @brief PROFILEARRAY — 建立沿 TrackCenterLine 的斷面草圖陣列
 *        （對應 ProfileArrayAlongAlignment_ImplementationPlan.md §2.3 / Step 6）
 *
 * 互動流程：
 *   execute()
 *     → master 草圖：若有 active sketch 直接使用；否則列出文件內所有 Sketch，
 *       只有一個就自動選用，超過一個則以 STRING_INPUT 請使用者輸入名稱，
 *       一個都沒有則直接失敗（沒有東西可選）
 *     → TrackCenterLine：只有一條就自動選用，超過一條以 STRING_INPUT 請使用者
 *       輸入名稱，一條都沒有則直接失敗
 *     → 依序以 NUMBER_INPUT 詢問 start / end chainage / interval
 *     → 呼叫 Document::createAlignedProfileArray(...) 建立特徵並 rebuild()，
 *       成功後立即呼叫 Document::createProfileLoftSolid(...) 放樣成實體
 *
 * 設計原則同 SetOriginCommand：只訂閱 NUMBER_INPUT / STRING_INPUT，
 * 每步結束呼叫 waitForInput(...)，execute() 內先呼叫 setState(Running)
 * 再返回，command 的生命週期由 finished()/cleanup() 收尾。
 */

#include "command/Command.h"
#include "command/CommandFactory.h"
#include "command/CommandTypes.h"
#include <QString>

namespace aicad {
namespace core { class EventBus; }
namespace railway { class TrackCenterLine; }
namespace cad { class AlignedProfileArray; }
namespace command {

class CreateProfileArrayCommand : public Command
{
    Q_OBJECT
public:
    explicit CreateProfileArrayCommand(QObject* parent = nullptr);
    ~CreateProfileArrayCommand() override = default;

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    enum class Step {
        WaitingForSketchName,
        WaitingForTclName,
        WaitingForStart,
        WaitingForEnd,
        WaitingForInterval
    };

    void resolveTrackOrPrompt();
    void handleStringInput(const QString& text);
    void handleNumberInput(const QString& text);
    void handleFeatureClicked(const QString& itemId);
    void promptStart();
    void promptEnd();
    void promptInterval();
    void createArray();
    void cleanup() override;

    Step   m_step = Step::WaitingForStart;
    QString m_masterSketchId;
    railway::TrackCenterLine* m_tcl = nullptr;
    double m_start    = 0.0;
    double m_end      = 0.0;
    double m_interval = 20.0;
    bool   m_isFinishing = false;
};

/**
 * @brief 修改既有 AlignedProfileArray 的起訖樁號與間距。
 *
 * 非互動：CommandContext::args 需依序提供
 * [featureId, startChainage, endChainage, interval]。
 */
class EditProfileArrayCommand : public Command
{
    Q_OBJECT
public:
    explicit EditProfileArrayCommand(QObject* parent = nullptr);
    ~EditProfileArrayCommand() override = default;

    CommandResult execute(const CommandContext& context) override;
    QString getUsage() const override;
};

/**
 * @brief 將既有 AlignedProfileArray 放樣成實體（Step 9/11）。
 *
 * CommandContext::args[0]（可省略）= AlignedProfileArray 的 feature id。
 * 省略時：文件內只有一個 AlignedProfileArray 就自動選用；超過一個則以
 * STRING_INPUT 請使用者輸入名稱；一個都沒有則直接失敗。
 */
class LoftProfileArrayCommand : public Command
{
    Q_OBJECT
public:
    explicit LoftProfileArrayCommand(QObject* parent = nullptr);
    ~LoftProfileArrayCommand() override = default;

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    void handleStringInput(const QString& text);
    void handleFeatureClicked(const QString& itemId);
    CommandResult doLoft(cad::AlignedProfileArray* arr);
    void cleanup() override;

    bool m_isFinishing = false;
};

REGISTER_COMMAND("profilearray", CreateProfileArrayCommand);
REGISTER_COMMAND("editprofilearray", EditProfileArrayCommand);
REGISTER_COMMAND("profileloft", LoftProfileArrayCommand);

} // namespace command
} // namespace aicad
