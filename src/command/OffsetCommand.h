/**
 * @file OffsetCommand.h
 * @brief OFFSET — 以指定距離建立來源曲線的平行/同心偏移複製本（別名 O，
 *        已於 CommandAlias 內建立）。
 *
 * 互動流程（連續迴圈，比照 TRIM 的既有慣例，可連續偏移多次直到按 Enter／
 * 右鍵／Esc 結束）：
 *   選取來源曲線（GEOM_PICKED）→ 點選要偏移的那一側（POINT_ACQUIRED，不
 *   限定要點在幾何上，任意位置皆可，只是用來判斷方向）→ 建立偏移複製本
 *   → 回到「選取來源曲線」繼續下一次，直到使用者按 Enter／右鍵／Esc。
 *
 * 距離的設定方式（三選一，選取來源曲線／點選方向之前隨時可用）：
 *   1. 直接輸入數字——不需要先打 D／DISTANCE，打了會直接當作新的距離值
 *      （輸入合法正數就直接生效，不合法則提示錯誤、停留原地）。
 *   2. 直接點兩個點——在「選取來源曲線」階段點擊畫面上任意兩個空白位置
 *      （沒有點在既有幾何上），兩點距離就變成新的偏移距離。
 *   3. 打 D 或 DISTANCE，進入「輸入距離」子階段再打數字（保留舊版明確
 *      指令的用法，非必要，1. 已經可以直接輸入）。
 *   設定完成後都會自動回到原本正在等待的階段，不會中斷整個指令。
 *
 * 距離採用「上一次執行 OFFSET 時設定的距離」（同一次程式執行期間，跨指令
 * 呼叫記在 s_lastDistance；程式重啟後歸零）當作目前距離，預設（從未設定
 * 過）為 0。距離必須 > 0 才能實際完成一次偏移（第一次使用、距離仍是預設
 * 0 時，點選方向會被拒絕並提示先設定距離）。
 *
 * MVP 範圍限制：只支援 Line／Circle／Arc 三種基本曲線。若選取的物件不是
 * 這三種之一，會在選取當下就提示並要求重新選取。
 *
 * 連續鏈支援：若選取的是一條 Line 或 Arc，且它跟其他 Line／Arc 首尾相連
 * （字面上共用同一個 SketchPoint，或透過明確的 Coincident 約束連結）形成
 * 一串連續路徑，會自動偵測整條鏈並一次偏移全部線段（含封閉環，例如矩形
 * 四邊、圓角矩形；也支援 Line-Arc-Line 混合鏈，例如已經用 FILLET 倒過角
 * 的多邊形），內部轉角依相鄰兩段的型別分別用線-線、線-弧、弧-弧求交點
 * 相接，不是各自獨立平移後晾在那裡不連——實際的幾何運算與連續鏈的相關
 * 限制說明，見 TrimExtendHelper::offsetChainAt()。單一、沒有相連鄰居的
 * Line／Arc，鏈長度自然就是 1，等同單曲線偏移。Circle 不參與鏈偵測（本來
 * 就是獨立封閉曲線，沒有起訖點可以相連），走 TrimExtendHelper::offsetAt()。
 *
 * 偏移出來的新曲線是完全獨立的一份幾何，不與來源曲線共用點；但會疊加
 * Parallel／Concentric／FixedDistance／FixedRadius／Coincident 等約束把
 * 新舊幾何、以及鏈內各段彼此關聯起來，見 TrimExtendHelper.h 內
 * offsetAt()／offsetChainAt() 文件的「約束處理」說明（含已知的近似
 * 限制）。
 */
#pragma once

#include "Command.h"

#include <QVector2D>
#include <QString>
#include <QVariant>

namespace aicad {
namespace cad { class Sketch; }

namespace command {

class OffsetCommand : public Command {
    Q_OBJECT
public:
    OffsetCommand();

    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    enum class State {
        Idle,
        WaitCurve,            ///< 等待選取來源曲線；空白處點擊＝開始兩點量測距離
        WaitDistancePoint2,   ///< 兩點量測距離：已取得第一點，等待第二點
        WaitSide,             ///< 等待點選要偏移的那一側
        WaitDistanceOverride  ///< 打 D／DISTANCE 後，等待輸入新的距離數字
    };

    cad::Sketch* activeSketch() const;

    void beginCurveStage();
    void beginDistanceOverrideStage();

    /// 印出「選取來源曲線／點選方向（目前距離=X，輸入 D 可修改，Enter/
    /// 右鍵結束）」這類提示，並重新呼叫 waitForInput(String) 讓輸入距離
    /// 指令持續可用（waitForInput 是一次性的，見 .cpp 內各處呼叫點的
    /// 說明）。
    void showPickPrompt(const QString& which);

    /// 套用一個新的距離值（統一處理三種輸入路徑：直接打數字、D 子階段、
    /// 兩點量測），驗證合法（正數）後更新 m_distance、記錄 s_lastDistance
    /// 的時機留給實際完成偏移時才寫入（避免使用者只是「試打數字」但還沒
    /// 真的偏移，就污染了下次的預設值）。回傳 true 表示接受、false 表示
    /// 不合法。
    bool applyNewDistance(double d);

    void subscribeNumberInput();
    void subscribeGeomPicked();
    void subscribePointAcquired();
    void subscribeStringInput();
    void subscribeCancelled();
    void unsubscribeAll();

    void onNumberInput(const QVariant& payload);
    void onGeomPicked(const QVariant& payload);
    void onPointAcquired(const QVariant& payload);
    void onStringInput(const QVariant& payload);
    void onCancelled(const QVariant&);

    void cleanup();

    State      m_state       = State::Idle;
    State      m_returnState = State::WaitCurve; ///< 設定距離完成後要回到哪個階段
    double     m_distance    = 0.0;
    QString    m_curveUuid;
    QVector2D  m_distMeasurePt1;  ///< 兩點量測距離：第一點座標（WaitDistancePoint2 階段用）
    int        m_offsetCount = 0;   ///< 這次指令執行期間已成功建立的偏移數量（結束時印出摘要用）

    /// 記住上一次執行 OFFSET 時設定的距離，下次啟動指令直接帶入當作預設
    /// 值。static：跨指令呼叫持續存在（每次 OFFSET 執行都會建立一個新的
    /// OffsetCommand 實例，一般成員變數無法跨呼叫存活）；只在程式這次執行
    /// 期間有效，重啟後歸零（比照 FilletCommand::s_lastRadius 的作法）。
    static double s_lastDistance;
};

} // namespace command
} // namespace aicad
