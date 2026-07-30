#pragma once
#include "Command.h"
#include "cad/sketch/SketchConstraint.h"
#include <QString>

namespace aicad::command {

/**
 * @brief LEADER — 建立 Leader / Hole Note 標註
 *
 * GDIM 昇級規劃 v2 Phase 8。對應 GDIM.md 第四節「建立 Leader」的簡化版
 * 互動流程：
 *
 *   選取目標幾何（點/圓心/端點...） → 點一下放置文字位置（anchor）
 *   → 輸入文字（如 "M20"） → 完成
 *
 * 不含 GDIM.md 提到的「Leader 中間可加/刪 Vertex」——那部分文件本身也
 * 說明要「比照草圖編輯器的 grip 拖曳機制，複用而非重造」，屬於
 * GripManager/GripEventFilter 的整合工作，這裡先不展開，只建立
 * 「target → 單一 anchor → text」這條最基本、也是最常用的路徑
 * （對應 GDIM.md 範例 "○────── M20" 的直線 Leader 情況）。
 */
class LeaderNoteCommand : public Command {
    Q_OBJECT
public:
    LeaderNoteCommand();
    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }

private:
    enum class State { Idle, WaitAnchor, WaitText };

    State        m_state = State::Idle;
    cad::GeomRef m_target;
    QVector2D    m_anchor;

    void subscribeGeomPicked ();
    void subscribePointAcquired();
    void subscribeStringInput();
    void subscribeCancelled  ();
    void unsubscribeAll      ();

    void onGeomPicked    (const QVariant& payload);
    void onPointAcquired (const QVariant& payload);
    void onStringInput   (const QVariant& payload);
    void onCancelled     (const QVariant&);

    void transitionToWaitAnchor();
    void transitionToWaitText  ();
    void commitLeaderNote      (const QString& text);
    void cleanup                ();

    cad::Sketch* activeSketch() const;
};

} // namespace aicad::command
