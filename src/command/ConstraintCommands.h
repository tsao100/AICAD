#pragma once
/**
 * @file ConstraintCommands.h
 * @brief Phase 1-4：所有草圖約束命令定義
 *
 * 命令清單（Phase 1）：
 *   幾何型：COINCIDENT(COI) HORIZONTAL(HOR) VERTICAL(VER)
 *          PARALLEL(PAR) PERPENDICULAR(PER) TANGENT(TAN)
 *          CONCENTRIC(CCN) EQUALLEN(EQL) EQUALRAD(EQR)
 *          COLLINEAR(COL) MIDPOINT(MID) SYMMETRIC(SYM)
 *          POINTONCURVE(POC) FIX
 *   尺寸型：DIST(DIM) RAD ANGLE(ANG) FIXX FIXY
 *   管理型：DELCON(DCO) EDITCON(ECO) LISTCON(LSC) CONINFO
 *          SOLVE DOF CONVIS LISTPOINTS
 */

#include "Command.h"
#include "../cad/sketch/SketchConstraint.h"
#include "../core/Application.h"
#include <optional>
#include <QVariant>

namespace aicad {
namespace command {

// ─────────────────────────────────────────────────────────────────────────────
// 輔助：求解結果回報
// ─────────────────────────────────────────────────────────────────────────────

/**
 * 將 SolveResult 轉換為命令列可讀訊息並輸出
 */
void reportSolveResult(const cad::SolveResult& result,
                       core::CommandLineManager* cmdMgr);

/**
 * 套用新的數值/表達式到既有的尺寸約束（EDITCON 指令與雙擊行內編輯共用）。
 * - 一般尺寸型：newExprOrValue 可為純數字或 ParameterStore 表達式。
 * - CoordinateDim：newExprOrValue 支援 "x,y" 逗號分隔格式。
 * 成功時會呼叫 sk->solveConstraints()（觸發 overlay 自動重建），並視 cmdMgr
 * 是否為 nullptr 決定是否輸出命令列訊息。回傳 true 表示套用成功。
 */
bool applyDimensionEdit(cad::Sketch* sk,
                        const QString& constraintUuid,
                        const QString& newExprOrValue,
                        core::CommandLineManager* cmdMgr);

// ─────────────────────────────────────────────────────────────────────────────
// 尺寸約束自動命名參數（d1, d2, a1, a2…）
// ─────────────────────────────────────────────────────────────────────────────

/**
 * 依約束型別回傳自動命名參數的字首：
 *   距離/長度類（FixedDistance/FixedLength/FixedHorizDist/FixedVertDist/
 *   FixedArcLength）→ "d"；角度類（FixedAngleDim/FixedAngle）→ "a"；
 *   FixedRadius → "r"；FixedDiameter → "dia"；Slope → "s"。
 * CoordinateDim 有兩個數值（x,y），不支援自動命名，回傳空字串；其餘不支援
 * 自動命名的型別（純幾何約束等）同樣回傳空字串。
 */
QString autoParamPrefix(cad::ConstraintType type);

/**
 * 在 sk 的 ParameterStore（含作用域鏈，避免跟父層既有參數撞名）裡，找出
 * 「prefix + 最小可用正整數」的名稱，例如已存在 d1、d2 時回傳 "d3"；
 * 目前完全沒有 d* 系列時回傳 "d1"。prefix 為空或 sk/ParameterStore 不存在
 * 時回傳空字串。
 */
QString nextAutoParamName(cad::Sketch* sk, const QString& prefix);

/**
 * 判斷 expr 是否為「某個約束自己的」自動命名參數——樣式必須符合
 * autoParamPrefix() 會用到的字首+ 純數字，且該名稱確實存在於 sk 的
 * ParameterStore 本地層（hasLocal，非沿作用域鏈從父層繼承而來）。
 * 用於編輯既有約束時判斷：目前的 paramExpr 是不是建立當下自動註冊的
 * 名稱本身（此時編輯應更新該名稱在 ParameterStore 裡的定義，讓名稱維持
 * 穩定、其他約束對它的引用不失效），還是使用者自己輸入的其他內容
 * （純數字，或引用「別的」參數的表達式）。
 */
bool isOwnAutoParamName(cad::Sketch* sk, const QString& expr);

// ─────────────────────────────────────────────────────────────────────────────
// 幾何約束命令基底（Phase 2）
// 子類只需指定 constraintType 和 requiredSelections
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 幾何約束命令基底
 *
 * 支援兩種模式：
 *   A）直接帶 UUID 參數：PAR line-abc123 line-def456
 *   B）互動點選模式：PAR（無參數，啟動 ConstraintPickSession GetGeom 模式）
 */
class GeomConstraintCommand : public Command {
    Q_OBJECT
public:
    GeomConstraintCommand(const QString& name,
                          cad::ConstraintType type,
                          int requiredSelections,
                          const QString& description = QString());

    CommandResult execute(const CommandContext& ctx) override;

protected:
    /**
     * 從 UUID 參數直接施加約束（模式 A）
     */
    virtual QString applyConstraint(cad::Sketch* sketch,
                                     const QStringList& args) const;

private:
    cad::ConstraintType m_type;
    int                 m_requiredSel;
};

// ─────────────────────────────────────────────────────────────────────────────
// 具體幾何約束命令（Phase 2）
// ─────────────────────────────────────────────────────────────────────────────

class CoincidentCommand    : public GeomConstraintCommand {
    Q_OBJECT
public:
    CoincidentCommand();
    QString applyConstraint(cad::Sketch* s, const QStringList& args) const override;
};

class HorizontalCommand    : public GeomConstraintCommand {
    Q_OBJECT
public:
    HorizontalCommand();
    QString applyConstraint(cad::Sketch* s, const QStringList& args) const override;
};

class VerticalCommand      : public GeomConstraintCommand {
    Q_OBJECT
public:
    VerticalCommand();
    QString applyConstraint(cad::Sketch* s, const QStringList& args) const override;
};

class ParallelCommand      : public GeomConstraintCommand {
    Q_OBJECT
public:
    ParallelCommand();
    QString applyConstraint(cad::Sketch* s, const QStringList& args) const override;
};

class PerpendicularCommand : public GeomConstraintCommand {
    Q_OBJECT
public:
    PerpendicularCommand();
    QString applyConstraint(cad::Sketch* s, const QStringList& args) const override;
};

class TangentCommand       : public GeomConstraintCommand {
    Q_OBJECT
public:
    TangentCommand();
    QString applyConstraint(cad::Sketch* s, const QStringList& args) const override;
};

class ConcentricCommand    : public GeomConstraintCommand {
    Q_OBJECT
public:
    ConcentricCommand();
    QString applyConstraint(cad::Sketch* s, const QStringList& args) const override;
};

class EqualLenCommand      : public GeomConstraintCommand {
    Q_OBJECT
public:
    EqualLenCommand();
    QString applyConstraint(cad::Sketch* s, const QStringList& args) const override;
};

class EqualRadCommand      : public GeomConstraintCommand {
    Q_OBJECT
public:
    EqualRadCommand();
    QString applyConstraint(cad::Sketch* s, const QStringList& args) const override;
};

class CollinearCommand     : public GeomConstraintCommand {
    Q_OBJECT
public:
    CollinearCommand();
    QString applyConstraint(cad::Sketch* s, const QStringList& args) const override;
};

class MidpointCommand      : public GeomConstraintCommand {
    Q_OBJECT
public:
    MidpointCommand();
    QString applyConstraint(cad::Sketch* s, const QStringList& args) const override;
};

class SymmetricCommand     : public GeomConstraintCommand {
    Q_OBJECT
public:
    SymmetricCommand();
    QString applyConstraint(cad::Sketch* s, const QStringList& args) const override;
};

class PointOnCurveCommand  : public GeomConstraintCommand {
    Q_OBJECT
public:
    PointOnCurveCommand();
    QString applyConstraint(cad::Sketch* s, const QStringList& args) const override;
};

class FixCommand           : public GeomConstraintCommand {
    Q_OBJECT
public:
    FixCommand();
    QString applyConstraint(cad::Sketch* s, const QStringList& args) const override;
};

// ─────────────────────────────────────────────────────────────────────────────
// 尺寸約束命令（Phase 3 / 3B）
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 尺寸約束命令基底（帶數值/參數表達式）
 *
 * 流程：
 *   1. DIST 50        → 記錄值，啟動 ConstraintPickSession
 *   2. 使用者點選幾何  → session 收齊後 emit constraintReady
 *   3. UIManager 回呼  → addConstraint + rebuildAll
 */
class DimConstraintCommand : public Command {
    Q_OBJECT
public:
    DimConstraintCommand(const QString& name,
                         cad::ConstraintType type,
                         const QString& description = QString());

    CommandResult execute(const CommandContext& ctx) override;

private:
    cad::ConstraintType m_type;
};

/**
 * @brief SLOPE — 線段斜度約束（dy/dx = value，選一條線）。
 *
 * 互動流程（Task：延續上次輸入值）：
 *   1. 若尚無「上次斜度值」記憶（本次 App 執行期間第一次用）：
 *      提示輸入斜度 → 使用者輸入後直接進入選線模式。
 *   2. 若已有「上次斜度值」記憶：
 *      直接進入選線模式（狀態列提示沿用值），使用者可以：
 *        a) 直接在視埠點選一條線 → 立即以「上次值」建立約束；
 *        b) 於命令列輸入 "S"（大小寫皆可）→ 回到「輸入新斜度」步驟，
 *           輸入完成後再次進入選線模式（新值同時成為新的「上次值」）。
 *
 * 數值輸入格式：
 *   - "1:40"  → 比例格式（1 單位垂直 : 40 單位水平），可為 "1:-40" / "-1:40"
 *   - "2.5%"  → 百分比坡度格式，可為 "-2.5%"
 *   - 純數字/ParameterStore 表達式 → 直接視為 dy/dx 比值（不做單位換算）
 *
 * 符號依「工程慣例」由使用者選取線段的 Start→End 方向決定：沿此方向上升
 * 為正、下降為負（與 SlopeEquation 的定義一致）。
 */
class SlopeCommand : public Command {
    Q_OBJECT
public:
    SlopeCommand();
    CommandResult execute(const CommandContext& ctx) override;
    bool canCancel() const override { return true; }
    void cancel() override;

    /**
     * 解析斜度輸入字串為無單位 dy/dx 比值。
     * 支援 "N:M"（比例）與 "P%"（百分比）；皆可帶正負號。
     * 回傳 std::nullopt 表示不符合這兩種格式（呼叫端應改用一般數字/表達式解析）。
     */
    static std::optional<double> parseRatioOrPercent(const QString& text);

private:
    // ── 兩階段狀態機（「輸入斜度」↔「選線」） ──────────────────────────
    void beginValueEntryStage();          // 提示輸入 → 訂閱 STRING_INPUT
    void beginPickStage(double value);    // 進入 GetGeom 選線模式 → 訂閱 STRING_INPUT（攔截 "S"）
    void onValueEntryInput(const QVariant& v);
    void onPickStageInput(const QVariant& v);
    void onSessionEnded();
    void finishCommand(const CommandResult& result);
    void unsubscribeAll();

    cad::Sketch* m_sketch = nullptr;
    bool m_transitioningToValueEntry = false;  ///< true 時，session->cancel() 觸發的
                                                ///< sessionEnded 不應視為命令結束
    bool m_subscribedValueEntry = false;
    bool m_subscribedPickStage  = false;
    bool m_connectedSessionEnded = false;

    // 記住「上次輸入」——static：整個 App 執行期間有效（跨命令呼叫），
    // 不隨命令物件銷毀而遺失。僅記憶最終「成功解析」的值。
    static bool    s_hasLastSlope;
    static double  s_lastSlopeValue;
    static QString s_lastSlopeText;   // 原始輸入文字（如 "1:40"），供提示顯示用
};

// ─────────────────────────────────────────────────────────────────────────────
// 約束管理命令（Phase 4）
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief DELCON [uuid] — 刪除約束（無參數時互動點選符號）
 */
class DelConCommand : public Command {
    Q_OBJECT
public:
    DelConCommand();
    CommandResult execute(const CommandContext& ctx) override;
};

/**
 * @brief EDITCON [uuid] [newExpr] — 編輯尺寸約束數值/表達式
 */
class EditConCommand : public Command {
    Q_OBJECT
public:
    EditConCommand();
    CommandResult execute(const CommandContext& ctx) override;
};

/**
 * @brief LISTCON — 列出所有約束及 DOF 狀態
 */
class ListConCommand : public Command {
    Q_OBJECT
public:
    ListConCommand();
    CommandResult execute(const CommandContext& ctx) override;
};

/**
 * @brief CONINFO [uuid] — 查詢單一約束詳情
 */
class ConInfoCommand : public Command {
    Q_OBJECT
public:
    ConInfoCommand();
    CommandResult execute(const CommandContext& ctx) override;
};

/**
 * @brief SOLVE — 手動觸發求解
 */
class SolveCommand : public Command {
    Q_OBJECT
public:
    SolveCommand();
    CommandResult execute(const CommandContext& ctx) override;
};

/**
 * @brief DOF — 查詢自由度狀態
 */
class DofCommand : public Command {
    Q_OBJECT
public:
    DofCommand();
    CommandResult execute(const CommandContext& ctx) override;
};

/**
 * @brief CONVIS [ON|OFF|TYPE] — 切換約束符號顯示
 */
class ConVisCommand : public Command {
    Q_OBJECT
public:
    ConVisCommand();
    CommandResult execute(const CommandContext& ctx) override;
};

/**
 * @brief LISTPOINTS — 列出所有草圖點（Phase 0B 除錯工具）
 */
class ListPointsCommand : public Command {
    Q_OBJECT
public:
    ListPointsCommand();
    CommandResult execute(const CommandContext& ctx) override;
};

// ─────────────────────────────────────────────────────────────────────────────
// Phase 1：命令路由器——在 Application 啟動時呼叫一次
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief 向 CommandManager 和 CommandAlias 註冊所有 Constraint 命令
 *
 * 在 Application::initialize() 或 UIManager 初始化時呼叫。
 */
void registerConstraintCommands(core::Application* app);

} // namespace command
} // namespace aicad
