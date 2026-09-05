/**
 * @file ChamferCommand.h
 * @brief CHAMFER 命令（別名 CHA，見 menu.txt）— 整合兩種倒角模式：
 *
 *   • 2D 模式（草圖內）：在兩條直線之間插入倒角線（MVP 僅支援直線，弧不
 *     支援倒角，天然降低範圍，同 AutoCAD 慣例）。實際幾何運算由既有的
 *     TrimExtendHelper::chamferAt() 負責。互動流程（比照 AutoCAD CHAMFER
 *     的「Distance」選項慣例，指令啟動不強制先問距離）：
 *       選取第一條直線（帶入上一次指令執行設定的 D1/D2，兩者預設皆為 0；
 *       提示列同時提供 [距離(D)] 選項，見 onOptionSelected2D()）→
 *       若使用者輸入 D：依序詢問新的 D1、D2（記入 s_lastDist1/s_lastDist2，
 *       供下次呼叫 CHAMFER 沿用）→ 回到「選取第一條直線」→
 *       選取第二條直線（GEOM_PICKED）→ 完成。
 *     兩個倒角距離都是 0 時，退化為單純延伸相交（不插入倒角線）。
 *
 *   • 3D 模式（實體特徵）：對任意已顯示實體 Feature（Extrude/Loft…）的
 *     選取邊執行倒角，建立新的 cad::ChamferSolid 特徵。設計決議
 *     （2026-08-01，詳見 ChamferSolid.h）：
 *       1. 單一距離套用於本次選取的所有邊（不支援每邊各自距離）。
 *       2. 來源幾何改變後，用幾何簽章盡力重新對應邊（不保證 100% 成功）。
 *       3. 指令啟動就直接開放畫面上所有已顯示實體 Feature 的邊選取，點哪條
 *          算哪條，不需要先選取來源實體整體。
 *     流程：execute() 呼叫 CadView::beginEdgePicking() → 使用者點選任意數量
 *     的邊 → 輸入正數倒角距離 → handleDistanceInput3D()：依 featureId 分組，
 *     各自建立一個 ChamferSolid → cleanup() 還原 CadView 邊選取狀態。
 *
 * ── 模式判定 ────────────────────────────────────────────────────────────
 * execute() 一開始依 core::Application::instance()->activeSketch() 是否非空
 * 決定走哪一種模式：目前正在編輯某個 Sketch（2D 平面圖）時走 2D 模式；否則
 * （瀏覽/操作 3D 實體特徵）走 3D 模式。兩種模式共用同一個指令名稱／別名，
 * 兩套內部狀態機（成員變數以「2D」/「3D」後綴區分）彼此獨立、互不干擾，
 * 同一個 ChamferCommand 執行期間只會有一套處於作用中。
 */
#ifndef AICAD_COMMAND_CHAMFERCOMMAND_H
#define AICAD_COMMAND_CHAMFERCOMMAND_H

#include "Command.h"
#include "CommandTypes.h"
#include "view/CadView.h"

#include <QVector2D>
#include <QString>
#include <QVariant>

namespace aicad {
namespace cad { class Sketch; }

namespace command {

class ChamferCommand : public Command {
    Q_OBJECT
public:
    explicit ChamferCommand(QObject* parent = nullptr);

    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;
    void cleanup() override;

private:
    // ── 模式判定：execute() 時依 activeSketch() 是否存在決定 ──────────────
    bool m_is3DMode = false;

    // ─────────────────────────────────────────────────────────────────────
    // 2D 模式：草圖內兩直線倒角（TrimExtendHelper::chamferAt）
    // ─────────────────────────────────────────────────────────────────────
    enum class State2D { Idle, WaitDist1, WaitDist2, WaitFirstObject, WaitSecondObject };

    cad::Sketch* activeSketch() const;

    CommandResult begin2D();
    void beginDist1Stage();
    void beginDist2Stage();
    void beginFirstObjectStage();

    void subscribeNumberInput2D();
    void subscribeGeomPicked2D();
    void subscribeOptionSelected2D();
    void subscribeCancelled2D();
    void unsubscribeAll2D();

    void onNumberInput2D(const QVariant& payload);
    void onGeomPicked2D(const QVariant& payload);
    void onOptionSelected2D(const QVariant& payload);
    void onCancelled2D(const QVariant&);

    // 還原 2D 模式的互動狀態並視情況完成指令（比照舊行為：若指令仍在
    // Running，呼叫 complete()；由 cleanup() 統一呼叫，也可被內部流程
    // 直接呼叫以主動結束）。
    void cleanup2D();

    State2D    m_state2D = State2D::Idle;
    double     m_dist1 = 0.0;
    double     m_dist2 = 0.0;
    QString    m_line1Uuid;
    QVector2D  m_clickPt1;

    // 上一次成功執行 CHAMFER（2D 模式）時使用者最終設定的 D1/D2，供下一次
    // 指令啟動時直接帶入當作預設值（不需要每次都重新輸入）；static 是刻意
    // 的——ChamferCommand 由 CommandFactory 每次呼叫都 new 出全新物件（見
    // CommandFactory::create()），一般成員變數活不過單次指令執行，只有
    // static／全域狀態才能跨指令呼叫存活。啟動應用程式後、使用者第一次下
    // CHAMFER 之前，兩者維持建構時的初始值 0.0（對應「D1、D2 預設值為零」）。
    static double s_lastDist1;
    static double s_lastDist2;

    // ─────────────────────────────────────────────────────────────────────
    // 3D 模式：實體邊選取倒角（Document::createChamferSolid）
    // ─────────────────────────────────────────────────────────────────────
    CommandResult begin3D(const CommandContext& context);
    void onEdgePicked3D();
    void handleDistanceInput3D(const QString& text);
    void commitAll3D(double distance);

    // 還原 3D 模式的邊選取狀態／InteractionMode；由 cleanup() 統一呼叫。
    void cleanup3D();

    CommandContext m_ctx;
    view::InteractionMode m_prevMode = view::InteractionMode::Idle;
    bool m_modeChanged = false;
    bool m_isFinishing = false;
};

} // namespace command
} // namespace aicad

#endif // AICAD_COMMAND_CHAMFERCOMMAND_H
