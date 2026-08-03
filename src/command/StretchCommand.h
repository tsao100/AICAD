/**
 * @file StretchCommand.h
 * @brief STRETCH — 以穿越窗選（Crossing Window）拉伸草圖幾何（別名 S，
 *        於 registerConstraintCommands() 內註冊，因為不像 M/CO/RO/MI
 *        那樣已預先存在於 CommandAlias.cpp）。
 *
 * 互動流程（無預先選取）：
 *   指定窗選第一角點（GetPoint）→ 指定對角點（GetPoint）→ 指定基準點
 *   （GetPoint）→ 指定第二點/位移目標（GetPoint）→ 完成。
 *
 * 與 AutoCAD 的差異：本命令用「兩次點擊」定義窗選矩形，而不是滑鼠拖曳
 * 產生的橡皮筋框——這是刻意的簡化（見實作計畫 §3.5 的風險說明）：
 * CadView 內雖然已有一套完整的拖曳式窗選/穿越窗選機制
 * （beginBoxSelectCandidate 等），但那套機制回傳的是「哪些幾何的 AIS
 * 物件被選中」，不是「哪些端點的座標落在矩形內」——一般線段的端點通常
 * 沒有獨立於母線的 AIS 物件可供窗選命中，因此無法直接拿來做真正的局部
 * 拉伸判斷。改用兩次點擊 + Sketch 內部座標直接比對矩形範圍，不需要碰
 * CadView 的窗選內部實作，風險更低、也更準確。
 *
 * 若命令執行前已有選取（模式 A，ctx.args 非空），則視為「完全選取」，
 * 直接跳過矩形角點階段，行為等同 MOVE（整條/整個幾何一起搬動，不做局部
 * 拉伸）——這與 AutoCAD 對「先個別點選物件、再執行 STRETCH」的行為一致
 * （個別點選沒有局部窗選資訊，AutoCAD 也是整體搬動）。
 *
 * Circle／Ellipse／Arc 不支援局部拉伸（見 SketchGeomTransformUtil::
 * stretchWithinRect() 的說明），僅在其定義點全部落在窗內時整體搬動。
 */
#pragma once

#include "Command.h"

#include <QVector2D>
#include <QStringList>
#include <QVariant>

namespace aicad {
namespace cad { class Sketch; }

namespace command {

class StretchCommand : public Command {
    Q_OBJECT
public:
    StretchCommand();

    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    enum class State { Idle, WaitCorner1, WaitCorner2, WaitBasePoint, WaitSecondPoint };

    cad::Sketch* activeSketch() const;

    void beginCorner1Stage();
    void beginBasePointStage();

    void subscribePointAcquired();
    void subscribeCancelled();
    void unsubscribeAll();

    void onPointAcquired(const QVariant& payload);
    void onCancelled(const QVariant&);

    void cleanup();

    State       m_state          = State::Idle;
    bool        m_usePreselected = false;
    QStringList m_preSelected;      ///< 模式 A：呼叫時已帶有的選取
    QVector2D   m_corner1;
    QVector2D   m_corner2;
    QVector2D   m_basePoint;
};

} // namespace command
} // namespace aicad
