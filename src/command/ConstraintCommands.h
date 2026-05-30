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
