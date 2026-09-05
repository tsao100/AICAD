/**
 * @file FilletCommand.h
 * @brief FILLET — 在兩條直線之間插入圓角（別名 F，已於 CommandAlias 內
 *        建立）。
 *
 * 互動流程（改版）：
 *   直接進入選取模式 → 選取第一條線（GEOM_PICKED）→ 選取第二條線
 *   （GEOM_PICKED）→ 完成。半徑不再是強制的第一步輸入：命令啟動時直接
 *   帶入「上一次執行 FILLET 時設定的半徑」（同一次程式執行期間，跨指令
 *   呼叫記在 s_lastRadius；程式重啟後歸零）當作目前半徑，預設（從未設定
 *   過）為 0。使用者若想改變半徑，隨時可以在選取任一條線之前輸入 R（或
 *   RADIUS）觸發輸入新的半徑值，設定完成後自動回到原本正在等待的選取
 *   階段（選第一條或第二條線），不會中斷整個指令。
 *
 * MVP 範圍限制：只支援兩條「直線」。若選取的物件不是直線，會在選取當下
 * 就提示並要求重新選取（不必等到兩個都選完才發現失敗）。實際的幾何運算
 * 由 TrimExtendHelper::filletAt() 負責，見該檔案的 MVP 範圍說明。
 *
 * 半徑輸入 0 時，退化為單純延伸相交（不插入圓弧），比照 AutoCAD 行為。
 *
 * 約束處理：
 *   - 若選取的兩線交點原本就有明確的 Coincident 約束，filletAt() 內部會
 *     在套用圓角前自動偵測並移除（見 TrimExtendHelper.h 該函式說明）。
 *   - 圓角套用成功後，這裡會疊加：
 *       • Coincident × 2：line1 端點 ↔ 弧起點、line2 端點 ↔ 弧終點
 *         （filletAt() 內部把弧的起訖點建成獨立的新點，而非直接重用兩條
 *         線的端點 UUID，就是為了讓這裡能疊加明確、可編輯/可刪除的約束）
 *       • Tangent × 2：弧 ↔ line1、弧 ↔ line2
 *       • FixedRadius × 1：弧的半徑 = 使用者指定值
 *     半徑為 0（無插入弧）時，不加 Tangent／FixedRadius；此時
 *     filletAt() 內部已經另外補上一條 Coincident 約束把兩線端點接起來。
 */
#pragma once

#include "Command.h"

#include <QVector2D>
#include <QString>
#include <QVariant>

namespace aicad {
namespace cad { class Sketch; }

namespace command {

class FilletCommand : public Command {
    Q_OBJECT
public:
    FilletCommand();

    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    enum class State { Idle, WaitFirstObject, WaitSecondObject, WaitRadiusOverride };

    cad::Sketch* activeSketch() const;

    void beginFirstObjectStage();
    void beginRadiusOverrideStage();

    /// 印出「選取第 N 條線（目前半徑=X，輸入 R 可修改）」這類提示，並重新
    /// 呼叫 waitForInput(String) 讓 R 指令持續可用（waitForInput 是一次性
    /// 的，見 .cpp 內各處呼叫點的說明）。
    void showPickPrompt(const QString& which);

    void subscribeNumberInput();
    void subscribeGeomPicked();
    void subscribeStringInput();
    void subscribeCancelled();
    void unsubscribeAll();

    void onNumberInput(const QVariant& payload);
    void onGeomPicked(const QVariant& payload);
    void onStringInput(const QVariant& payload);
    void onCancelled(const QVariant&);

    void cleanup();

    State      m_state  = State::Idle;
    State      m_returnState = State::WaitFirstObject; ///< 設定半徑（R）完成後要回到哪個選取階段
    double     m_radius = 0.0;
    QString    m_line1Uuid;
    QVector2D  m_clickPt1;

    /// 記住上一次執行 FILLET 時設定的半徑，下次啟動指令直接帶入當作預設
    /// 值。static：跨指令呼叫持續存在（每次 FILLET 執行都會建立一個新的
    /// FilletCommand 實例，一般成員變數無法跨呼叫存活）；只在程式這次執行
    /// 期間有效，重啟後歸零（不做持久化儲存，比照大多數 CAD 軟體 session
    /// 內記憶最近一次數值的行為）。
    static double s_lastRadius;
};

} // namespace command
} // namespace aicad
