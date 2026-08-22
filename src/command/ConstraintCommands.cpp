/**
 * @file ConstraintCommands.cpp
 * @brief Phase 1-4：約束命令實作
 */

#include "ConstraintCommands.h"
#include <limits>
#include <QPair>
#include <QVector2D>
#include "CommandAlias.h"
#include "CommandManager.h"
#include "GeneralDimCommand.h"
#include "LeaderNoteCommand.h"
#include "MoveCommand.h"
#include "CopyCommand.h"
#include "RotateCommand.h"
#include "MirrorCommand.h"
#include "ScaleCommand.h"
#include "StretchCommand.h"
#include "TrimCommand.h"
#include "ExtendCommand.h"
#include "FilletCommand.h"
#include "ChamferCommand.h"
#include "ConstructionToggleCommand.h"
#include "../core/Application.h"
#include "../core/CommandLineManager.h"
#include "../cad/Sketch.h"
#include "../cad/ConstraintPickSession.h"
#include "../ui/UIManager.h"
#include "../ui/SketchPanel.h"
#include <QDebug>
#include <cmath>

namespace aicad {
namespace command {

using namespace cad;

// ─────────────────────────────────────────────────────────────────────────────
// 輔助函式

/// 將 CadView 軸 AIS 的虛擬 UUID 轉換為 Sketch 內真實的幾何 UUID。
/// 當使用者選取 X 軸 / Y 軸 / 原點時，ctx.args 含有
/// "sketch_xaxis:<sketchId>" 等字串，需在此正規化後才能套用約束。
static QString resolveAxisUuid(Sketch* sketch, const QString& uuid)
{
    if (!sketch) return uuid;
    const QString skId = sketch->id();
    if (uuid == "sketch_xaxis:"  + skId) return sketch->xAxisGeomUuid();
    if (uuid == "sketch_yaxis:"  + skId) return sketch->yAxisGeomUuid();
    if (uuid == "sketch_origin:" + skId) return sketch->originPointUuid();
    return uuid;
}

/// 批次正規化 args 中所有 UUID
static QStringList resolveAxisUuids(Sketch* sketch, const QStringList& args)
{
    QStringList out;
    out.reserve(args.size());
    for (const QString& a : args)
        out << resolveAxisUuid(sketch, a);
    return out;
}
// ─────────────────────────────────────────────────────────────────────────────

void reportSolveResult(const SolveResult& result,
                       core::CommandLineManager* cmdMgr)
{
    if (!cmdMgr) return;
    switch (result.status) {
    case SolveStatus::FullyConstrained:
        cmdMgr->printSuccess("✅ Solved. Sketch is FULLY CONSTRAINED. (DOF=0)");
        break;
    case SolveStatus::UnderConstrained:
        cmdMgr->printMessage(
            QString("✅ Solved. DOF remaining: %1 (under-constrained)").arg(result.dof));
        break;
    case SolveStatus::OverConstrained:
        cmdMgr->printError("❌ OVER CONSTRAINED: Constraint conflicts detected.");
        if (!result.conflictingConstraints.isEmpty())
            cmdMgr->printError("   Conflicting: " + result.conflictingConstraints.join(", "));
        cmdMgr->printWarning("   Hint: Use DELCON to remove redundant constraints.");
        break;
    case SolveStatus::Conflict:
    case SolveStatus::SolverError:
        cmdMgr->printWarning("⚠️  Solver failed to converge. Geometry may be inconsistent.");
        break;
    }
}

static Sketch* requireActiveSketch(core::Application* app,
                                    core::CommandLineManager* cmdMgr)
{
    Sketch* sk = app ? app->activeSketch() : nullptr;
    if (!sk) {
        if (cmdMgr) cmdMgr->printError("No active sketch. Enter sketch edit mode first.");
    }
    return sk;
}

// ─────────────────────────────────────────────────────────────────────────────
// applyDimensionEdit — EDITCON 指令與尺寸線雙擊行內編輯共用的核心邏輯
// （邏輯與 EditConCommand::execute() 的直接 UUID 分支一致，抽出供兩處呼叫）
// ─────────────────────────────────────────────────────────────────────────────

bool applyDimensionEdit(Sketch* sk,
                        const QString& constraintUuid,
                        const QString& newExprOrValue,
                        core::CommandLineManager* cmdMgr)
{
    if (!sk) {
        if (cmdMgr) cmdMgr->printError("No active sketch.");
        return false;
    }

    SketchConstraint* con = sk->findConstraint(constraintUuid);
    if (!con) {
        if (cmdMgr)
            cmdMgr->printError(QString("Constraint '%1' not found.").arg(constraintUuid));
        return false;
    }
    if (!con->isDimensional()) {
        if (cmdMgr)
            cmdMgr->printError("Only dimensional constraints can be edited.");
        return false;
    }

    QString newExpr = newExprOrValue.trimmed();
    if (newExpr.isEmpty()) {
        if (cmdMgr) cmdMgr->printWarning("⚠️  Empty value — edit cancelled.");
        return false;
    }
    QString oldExpr = con->paramExpr.isEmpty()
        ? QString::number(con->value) : con->paramExpr;

    // 角度類型（FixedAngleDim/FixedAngle）：內部一律以弧度儲存，但使用者輸入
    // （literal number 或 expression 求值結果）一律視為「度」，需轉換，與
    // GeneralDimCommand 建立時、SketchPanel 編輯對話框的慣例一致。
    const bool isAngleType = (con->type == ConstraintType::FixedAngleDim ||
                              con->type == ConstraintType::FixedAngle);

    // CoordinateDim：支援 "x,y" 逗號分隔格式
    if (con->type == ConstraintType::CoordinateDim && newExpr.contains(',')) {
        QStringList parts = newExpr.split(',');
        if (parts.size() == 2) {
            bool ok1, ok2;
            double x = parts[0].trimmed().toDouble(&ok1);
            double y = parts[1].trimmed().toDouble(&ok2);
            if (!ok1 || !ok2) {
                if (cmdMgr)
                    cmdMgr->printError(
                        QString("Invalid coordinate format: '%1' (expected x,y)").arg(newExpr));
                return false;
            }
            con->value  = x;
            con->value2 = y;
            con->paramExpr.clear();
            SolveResult result = sk->solveConstraints();
            if (cmdMgr) {
                cmdMgr->printSuccess(
                    QString("✅ CoordinateDim updated: X=%1, Y=%2. Solved.").arg(x).arg(y));
                reportSolveResult(result, cmdMgr);
            }
            return true;
        }
    }

    bool isNumber;
    double newValue = newExpr.toDouble(&isNumber);
    if (!isNumber) {
        auto* store = sk->parameterStore();
        if (store) {
            auto [ok, evaluated] = store->evaluate(newExpr);
            if (!ok) {
                if (cmdMgr)
                    cmdMgr->printError(QString("Unknown expression: '%1'").arg(newExpr));
                return false;
            }
            newValue = evaluated;
        } else {
            if (cmdMgr) cmdMgr->printError("No ParameterStore available.");
            return false;
        }
    }

    con->paramExpr = isNumber ? QString() : newExpr;
    con->value     = isAngleType ? (newValue * M_PI / 180.0) : newValue;

    SolveResult result = sk->solveConstraints();
    if (cmdMgr) {
        cmdMgr->printSuccess(
            QString("✅ Constraint updated: %1 → %2 (= %3%4). Solved.")
            .arg(oldExpr).arg(newExpr).arg(newValue).arg(isAngleType ? "°" : ""));
        reportSolveResult(result, cmdMgr);
    }
    return true;
}

static void triggerOverlayRebuild(core::Application* app)
{
    if (!app) return;
    auto* ui = app->uiManager();
    if (!ui) return;
    // SketchPanel 持有 ConstraintOverlayManager
    auto* panel = ui->findChild<ui::SketchPanel*>();
    if (panel) {
        // SketchPanel 的 onConstraintAdded/Removed signal 已自動刷新 overlay；
        // 若需要強制重建，可呼叫 exitOverlayMode + enterSketchMode，但一般不需要
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GeomConstraintCommand 基底
// ─────────────────────────────────────────────────────────────────────────────

GeomConstraintCommand::GeomConstraintCommand(const QString& name,
                                              ConstraintType type,
                                              int requiredSelections,
                                              const QString& desc)
    : Command(name, desc.isEmpty()
              ? QString("Apply %1 constraint").arg(name)
              : desc)
    , m_type(type)
    , m_requiredSel(requiredSelections)
{}

CommandResult GeomConstraintCommand::execute(const CommandContext& ctx)
{
    auto* app    = core::Application::instance();
    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk   = requireActiveSketch(app, cmdMgr);
    if (!sk) return CommandResult::Failure("No active sketch.");

    // ── 模式 A：直接帶 UUID 參數 ────────────────────────────────────────────
    if (!ctx.args.isEmpty() && ctx.args.size() >= m_requiredSel) {
        // 正規化軸/原點的虛擬 UUID → Sketch 真實幾何 UUID
        QStringList resolvedArgs = resolveAxisUuids(sk, ctx.args);
        int dofBefore = sk->degreesOfFreedom();
        QString uuid  = applyConstraint(sk, resolvedArgs);
        if (uuid.isEmpty()) {
            return CommandResult::Failure(
                QString("Failed to apply %1 constraint. Check UUIDs.").arg(name()));
        }
        SolveResult result = sk->solveConstraints();
        int dofAfter = sk->degreesOfFreedom();
        cmdMgr->printSuccess(
            QString("✅ %1 constraint applied. DOF: %2 → %3")
            .arg(name()).arg(dofBefore).arg(dofAfter));
        reportSolveResult(result, cmdMgr);
        triggerOverlayRebuild(app);
        return CommandResult::Success();
    }

    // ── 模式 B：互動選取 ────────────────────────────────────────────────────
    auto* ui = app->uiManager();
    if (!ui) return CommandResult::Failure("UIManager not available.");

    // beginGeomConstraintPick 會：begin session + setMode(GetGeom) + 更新 status bar
    ui->beginGeomConstraintPick(sk, m_type, m_requiredSel);

    auto* session = ui->constraintPickSession();
    cmdMgr->showPrompt(session ? session->promptText() : QString());
    return CommandResult::Success(QString("[%1] 請在視埠中點選幾何元素").arg(name()));
}

QString GeomConstraintCommand::applyConstraint(Sketch* sketch,
                                                 const QStringList& args) const
{
    // 基底實作：根據 m_type 和 args 呼叫對應的 Sketch 方法
    // 子類可覆寫提供更精確的參數解析
    Q_UNUSED(sketch); Q_UNUSED(args);
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// 具體幾何約束命令
// ─────────────────────────────────────────────────────────────────────────────

CoincidentCommand::CoincidentCommand()
    : GeomConstraintCommand("COINCIDENT", ConstraintType::Coincident, 2,
                             "Make two points coincident") {}

QString CoincidentCommand::applyConstraint(Sketch* s, const QStringList& args) const {
    if (args.size() < 2) return {};

    auto parseHandle = [](const QString& t) {
        if (t.toLower() == "start")  return GeomHandle::Start;
        if (t.toLower() == "end")    return GeomHandle::End;
        if (t.toLower() == "center") return GeomHandle::Center;
        return GeomHandle::WholeGeom;
    };

    QString    u1 = args[0];
    GeomHandle h1 = GeomHandle::WholeGeom;
    QString    u2;
    GeomHandle h2 = GeomHandle::WholeGeom;

    if (args.size() >= 4) {
        // 完整格式：uuid1 handle1 uuid2 handle2
        h1 = parseHandle(args[1]); u2 = args[2]; h2 = parseHandle(args[3]);
    } else {
        u2 = args[1];
    }

    // 若兩個都是 WholeGeom（純 UUID 選取，沒有明確端點），
    // 找最近端點對，避免永遠只配對 start-start。
    if (h1 == GeomHandle::WholeGeom && h2 == GeomHandle::WholeGeom) {
        const SketchGeometry* gA = s->findGeometry(u1);
        const SketchGeometry* gB = s->findGeometry(u2);
        if (gA && gB) {
            auto endpoints = [](const SketchGeometry* g)
                -> QVector<QPair<QVector2D, GeomHandle>> {
                QVector<QPair<QVector2D, GeomHandle>> pts;
                if (g->type == SketchGeometryType::Point) {
                    if (!g->points.isEmpty())
                        pts.append({g->points[0], GeomHandle::WholeGeom});
                } else if (!g->points.isEmpty()) {
                    pts.append({g->points.front(), GeomHandle::Start});
                    if (g->points.size() > 1)
                        pts.append({g->points.back(), GeomHandle::End});
                }
                return pts;
            };
            auto ptsA = endpoints(gA), ptsB = endpoints(gB);
            float best = std::numeric_limits<float>::max();
            for (auto& [pa, ha] : ptsA)
                for (auto& [pb, hb] : ptsB) {
                    float d = (pa - pb).lengthSquared();
                    if (d < best) { best = d; h1 = ha; h2 = hb; }
                }
        }
    }
    return s->constrainCoincident(GeomRef(u1, h1), GeomRef(u2, h2));
}

HorizontalCommand::HorizontalCommand()
    : GeomConstraintCommand("HORIZONTAL", ConstraintType::Horizontal, 1,
                             "Make a line horizontal") {}
QString HorizontalCommand::applyConstraint(Sketch* s, const QStringList& args) const {
    return args.isEmpty() ? QString() : s->constrainHorizontal(args[0]);
}

VerticalCommand::VerticalCommand()
    : GeomConstraintCommand("VERTICAL", ConstraintType::Vertical, 1,
                             "Make a line vertical") {}
QString VerticalCommand::applyConstraint(Sketch* s, const QStringList& args) const {
    return args.isEmpty() ? QString() : s->constrainVertical(args[0]);
}

ParallelCommand::ParallelCommand()
    : GeomConstraintCommand("PARALLEL", ConstraintType::Parallel, 2,
                             "Make two lines parallel") {}
QString ParallelCommand::applyConstraint(Sketch* s, const QStringList& args) const {
    return args.size() < 2 ? QString() : s->constrainParallel(args[0], args[1]);
}

PerpendicularCommand::PerpendicularCommand()
    : GeomConstraintCommand("PERPENDICULAR", ConstraintType::Perpendicular, 2,
                             "Make two lines perpendicular") {}
QString PerpendicularCommand::applyConstraint(Sketch* s, const QStringList& args) const {
    return args.size() < 2 ? QString() : s->constrainPerpendicular(args[0], args[1]);
}

TangentCommand::TangentCommand()
    : GeomConstraintCommand("TANGENT", ConstraintType::Tangent, 2,
                             "Make two curves tangent (Design Intent: prefer SYM/TAN over same-value dims)") {}
QString TangentCommand::applyConstraint(Sketch* s, const QStringList& args) const {
    return args.size() < 2 ? QString() : s->constrainTangent(args[0], args[1]);
}

ConcentricCommand::ConcentricCommand()
    : GeomConstraintCommand("CONCENTRIC", ConstraintType::Concentric, 2,
                             "Make two circles/arcs concentric") {}
QString ConcentricCommand::applyConstraint(Sketch* s, const QStringList& args) const {
    return args.size() < 2 ? QString() : s->constrainConcentric(args[0], args[1]);
}

EqualLenCommand::EqualLenCommand()
    : GeomConstraintCommand("EQUALLEN", ConstraintType::EqualLength, 2,
                             "Make two lines equal length") {}
QString EqualLenCommand::applyConstraint(Sketch* s, const QStringList& args) const {
    return args.size() < 2 ? QString() : s->constrainEqualLength(args[0], args[1]);
}

EqualRadCommand::EqualRadCommand()
    : GeomConstraintCommand("EQUALRAD", ConstraintType::EqualRadius, 2,
                             "Make two circles/arcs equal radius") {}
QString EqualRadCommand::applyConstraint(Sketch* s, const QStringList& args) const {
    return args.size() < 2 ? QString() : s->constrainEqualRadius(args[0], args[1]);
}

CollinearCommand::CollinearCommand()
    : GeomConstraintCommand("COLLINEAR", ConstraintType::Collinear, 2,
                             "Make two lines collinear") {}
QString CollinearCommand::applyConstraint(Sketch* s, const QStringList& args) const {
    return args.size() < 2 ? QString() : s->constrainCollinear(args[0], args[1]);
}

MidpointCommand::MidpointCommand()
    : GeomConstraintCommand("MIDPOINT", ConstraintType::Midpoint, 2,
                             "Constrain a point to line midpoint") {}
QString MidpointCommand::applyConstraint(Sketch* s, const QStringList& args) const {
    if (args.size() < 2) return {};
    return s->constrainMidpoint(GeomRef(args[0], GeomHandle::WholeGeom), args[1]);
}

SymmetricCommand::SymmetricCommand()
    : GeomConstraintCommand("SYMMETRIC", ConstraintType::Symmetric, 3,
                             "Make two elements symmetric about an axis\n"
                             "  Tip: Use SYM instead of applying same-value dims on both sides (Design Intent)") {}
QString SymmetricCommand::applyConstraint(Sketch* s, const QStringList& args) const {
    if (args.size() < 3) return {};
    return s->constrainSymmetric(GeomRef(args[0], GeomHandle::WholeGeom),
                                   GeomRef(args[1], GeomHandle::WholeGeom),
                                   args[2]);
}

PointOnCurveCommand::PointOnCurveCommand()
    : GeomConstraintCommand("POINTONCURVE", ConstraintType::PointOnCurve, 2,
                             "Constrain a point to lie on a curve") {}
QString PointOnCurveCommand::applyConstraint(Sketch* s, const QStringList& args) const {
    if (args.size() < 2) return {};
    return s->constrainPointOnCurve(GeomRef(args[0], GeomHandle::WholeGeom), args[1]);
}

FixCommand::FixCommand()
    : GeomConstraintCommand("FIX", ConstraintType::Fixed, 1,
                             "Fix geometry in place\n"
                             "  ⚠️  WARNING: Fix only the origin/datum. Use parametric relations for other geometry.") {}
QString FixCommand::applyConstraint(Sketch* s, const QStringList& args) const {
    if (args.isEmpty()) return {};
    auto* cmdMgr = core::CommandLineManager::instance();
    if (cmdMgr) {
        cmdMgr->printWarning("⚠️  FIX: Consider fixing only the origin/datum point. "
                             "Fix all geometry only if intentional.");
    }
    return s->constrainFixed(args[0]);
}

// ─────────────────────────────────────────────────────────────────────────────
// DimConstraintCommand（Phase 3 / 3B）
// ─────────────────────────────────────────────────────────────────────────────

DimConstraintCommand::DimConstraintCommand(const QString& name,
                                            ConstraintType type,
                                            const QString& desc)
    : Command(name, desc)
    , m_type(type)
{}

CommandResult DimConstraintCommand::execute(const CommandContext& ctx)
{
    auto* app    = core::Application::instance();
    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk   = requireActiveSketch(app, cmdMgr);
    if (!sk) return CommandResult::Failure("No active sketch.");

    auto* ui = app->uiManager();
    if (!ui) return CommandResult::Failure("UIManager not available.");

    const bool isAngleType = (m_type == ConstraintType::FixedAngleDim ||
                              m_type == ConstraintType::FixedAngle);

    // ── 解析數值或參數表達式 ─────────────────────────────────────────────────
    if (ctx.args.isEmpty()) {
        // 提示輸入數值（角度類型明確標示單位為「度」，避免使用者誤以為是弧度）
        cmdMgr->showPrompt(isAngleType
            ? QString("Enter %1 value in degrees or expression (e.g. 45 or width/2):").arg(name())
            : QString("Enter %1 value or expression (e.g. 50 or width/2):").arg(name()));
        cmdMgr->waitForInput(core::InputType::String);
        return CommandResult::Success();
    }

    QString expr = ctx.args[0];
    bool isDriving = !ctx.args.contains("-measured");

    bool isNumber;
    double value = expr.toDouble(&isNumber);

    if (!isNumber) {
        // 嘗試從 ParameterStore 求值（evaluate 回傳 std::pair<bool,double>）
        auto* store = sk->parameterStore();
        if (store) {
            auto [ok, evaluated] = store->evaluate(expr);
            if (!ok) {
                cmdMgr->printError(
                    QString("Unknown parameter or expression: '%1'").arg(expr));
                return CommandResult::Failure(
                    QString("Unknown expression: '%1'").arg(expr));
            }
            value = evaluated;
        } else {
            cmdMgr->printError("No ParameterStore available.");
            return CommandResult::Failure("No ParameterStore available.");
        }
        cmdMgr->printMessage(QString("  Expression '%1' = %2").arg(expr).arg(value));
    }

    // 角度約束：使用者輸入（literal number 或 expression 求值結果）一律視為「度」，
    // 但內部（SketchConstraint::value、求解器、AIS 顯示）一律使用弧度，故需在此轉換。
    if (isAngleType) {
        value = value * M_PI / 180.0;
    }

    // ── 顯示 DOF 提示（最少尺寸原則）────────────────────────────────────────
    int currentDof = sk->degreesOfFreedom();
    cmdMgr->printMessage(
        QString("  Current DOF: %1 — adding this constraint will reduce it by 1").arg(currentDof));

    // ── 啟動 ConstraintPickSession ───────────────────────────────────────────
    auto* session = ui->constraintPickSession();
    if (!session) return CommandResult::Failure("ConstraintPickSession not available.");

    session->begin(sk, m_type, value, isNumber ? QString() : expr, isDriving);
    cmdMgr->showPrompt(session->promptText());

    return CommandResult::Success();
}

// ─────────────────────────────────────────────────────────────────────────────
// 管理命令（Phase 4）
// ─────────────────────────────────────────────────────────────────────────────

// ── DELCON ──────────────────────────────────────────────────────────────────

DelConCommand::DelConCommand()
    : Command("DELCON", "Delete a constraint (DELCON [uuid] or click symbol)") {}

CommandResult DelConCommand::execute(const CommandContext& ctx)
{
    auto* app    = core::Application::instance();
    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk   = requireActiveSketch(app, cmdMgr);
    if (!sk) return CommandResult::Failure("No active sketch.");

    if (!ctx.args.isEmpty()) {
        // 直接帶 UUID 參數
        QString uuid = ctx.args[0];
        SketchConstraint* con = sk->findConstraint(uuid);
        if (!con) {
            return CommandResult::Failure(
                QString("Constraint '%1' not found.").arg(uuid));
        }
        QString typeName; // shortcut
        int dofBefore = sk->degreesOfFreedom();
        bool ok = sk->removeConstraint(uuid);
        if (!ok) return CommandResult::Failure("Failed to remove constraint.");
        SolveResult result = sk->solveConstraints();
        int dofAfter = sk->degreesOfFreedom();
        cmdMgr->printSuccess(
            QString("✅ Constraint removed. DOF: %1 → %2").arg(dofBefore).arg(dofAfter));
        reportSolveResult(result, cmdMgr);
        triggerOverlayRebuild(app);
        return CommandResult::Success();
    }

    // 互動模式：提示點擊約束符號
    cmdMgr->showPrompt("[SELECT CONSTRAINT] Click on a constraint symbol in viewport, or type UUID:");
    return CommandResult::Success();
}

// ── EDITCON ─────────────────────────────────────────────────────────────────

EditConCommand::EditConCommand()
    : Command("EDITCON", "Edit a dimension constraint value (EDITCON [uuid] [newExpr])") {}

CommandResult EditConCommand::execute(const CommandContext& ctx)
{
    auto* app    = core::Application::instance();
    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk   = requireActiveSketch(app, cmdMgr);
    if (!sk) return CommandResult::Failure("No active sketch.");

    if (ctx.args.size() >= 2) {
        QString uuid    = ctx.args[0];
        QString newExpr = ctx.args[1];

        SketchConstraint* con = sk->findConstraint(uuid);
        if (!con) {
            return CommandResult::Failure(
                QString("Constraint '%1' not found.").arg(uuid));
        }
        if (!con->isDimensional()) {
            return CommandResult::Failure("Only dimensional constraints can be edited with EDITCON.");
        }

        QString oldExpr = con->paramExpr.isEmpty()
            ? QString::number(con->value) : con->paramExpr;

        // 角度類型（FixedAngleDim/FixedAngle）：內部一律以弧度儲存，但使用者輸入
        // （literal number 或 expression 求值結果）一律視為「度」，需轉換，
        // 與 GeneralDimCommand 建立時、SketchPanel 編輯對話框的慣例一致。
        const bool isAngleType = (con->type == ConstraintType::FixedAngleDim ||
                                  con->type == ConstraintType::FixedAngle);

        // CoordinateDim：支援 "x,y" 逗號分隔格式
        if (con->type == ConstraintType::CoordinateDim && newExpr.contains(',')) {
            QStringList parts = newExpr.split(',');
            if (parts.size() == 2) {
                bool ok1, ok2;
                double x = parts[0].trimmed().toDouble(&ok1);
                double y = parts[1].trimmed().toDouble(&ok2);
                if (!ok1 || !ok2)
                    return CommandResult::Failure(
                        QString("Invalid coordinate format: '%1' (expected x,y)").arg(newExpr));
                con->value  = x;
                con->value2 = y;
                con->paramExpr.clear();
                SolveResult result = sk->solveConstraints();
                cmdMgr->printSuccess(
                    QString("✅ CoordinateDim updated: X=%1, Y=%2. Solved.").arg(x).arg(y));
                reportSolveResult(result, cmdMgr);
                triggerOverlayRebuild(app);
                return CommandResult::Success();
            }
        }

        bool isNumber;
        double newValue = newExpr.toDouble(&isNumber);
        if (!isNumber) {
            auto* store = sk->parameterStore();
            if (store) {
                auto [ok, evaluated] = store->evaluate(newExpr);
                if (!ok) {
                    return CommandResult::Failure(
                        QString("Unknown expression: '%1'").arg(newExpr));
                }
                newValue = evaluated;
            } else {
                return CommandResult::Failure("No ParameterStore available.");
            }
        }

        con->paramExpr = isNumber ? QString() : newExpr;
        con->value     = isAngleType ? (newValue * M_PI / 180.0) : newValue;

        // CoordinateDim：若第三參數提供 Y 值，一併更新（空格分隔備用格式）
        if (con->type == ConstraintType::CoordinateDim && ctx.args.size() >= 3) {
            bool ok2;
            double y2 = ctx.args[2].toDouble(&ok2);
            if (ok2)
                con->value2 = y2;
        }

        SolveResult result = sk->solveConstraints();
        cmdMgr->printSuccess(
            QString("✅ Constraint updated: %1 → %2 (= %3%4). Solved.")
            .arg(oldExpr).arg(newExpr).arg(newValue).arg(isAngleType ? "°" : ""));
        reportSolveResult(result, cmdMgr);
        triggerOverlayRebuild(app);
        return CommandResult::Success();
    }

    // 互動模式
    cmdMgr->showPrompt("[SELECT DIMENSION] Click on a dimension line, or type UUID:");
    return CommandResult::Success();
}

// ── LISTCON ─────────────────────────────────────────────────────────────────

ListConCommand::ListConCommand()
    : Command("LISTCON", "List all constraints with DOF info") {}

CommandResult ListConCommand::execute(const CommandContext& /*ctx*/)
{
    auto* app    = core::Application::instance();
    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk   = requireActiveSketch(app, cmdMgr);
    if (!sk) return CommandResult::Failure("No active sketch.");

    const auto& cons = sk->constraints();
    int dof = sk->degreesOfFreedom();

    cmdMgr->printMessage(
        QString("┌─────────────────────────────────────────────────────────┐"));
    cmdMgr->printMessage(
        QString("│ Sketch Constraints (total: %1, DOF: %2)")
        .arg(cons.size()).arg(dof));
    cmdMgr->printMessage(
        QString("├──────────────────┬──────────────────┬───────────────────┤"));
    cmdMgr->printMessage(
        QString("│ UUID (short)     │ Type             │ Value / Refs      │"));
    cmdMgr->printMessage(
        QString("├──────────────────┼──────────────────┼───────────────────┤"));

    auto constraintTypeName = [](ConstraintType t) -> QString {
        switch (t) {
        case ConstraintType::Coincident:    return "Coincident";
        case ConstraintType::Horizontal:    return "Horizontal";
        case ConstraintType::Vertical:      return "Vertical";
        case ConstraintType::Parallel:      return "Parallel";
        case ConstraintType::Perpendicular: return "Perpendicular";
        case ConstraintType::Tangent:       return "Tangent";
        case ConstraintType::Concentric:    return "Concentric";
        case ConstraintType::EqualLength:   return "EqualLength";
        case ConstraintType::EqualRadius:   return "EqualRadius";
        case ConstraintType::Collinear:     return "Collinear";
        case ConstraintType::Midpoint:      return "Midpoint";
        case ConstraintType::Symmetric:     return "Symmetric";
        case ConstraintType::PointOnCurve:  return "PointOnCurve";
        case ConstraintType::Fixed:         return "Fixed";
        case ConstraintType::FixedDistance: return "FixedDist";
        case ConstraintType::FixedRadius:   return "FixedRadius";
        case ConstraintType::FixedX:        return "FixedX";
        case ConstraintType::FixedY:        return "FixedY";
        case ConstraintType::FixedAngleDim: return "FixedAngle";
        case ConstraintType::FixedLength:   return "FixedLength";
        case ConstraintType::FixedDiameter: return "FixedDiam";
        case ConstraintType::FixedHorizDist:return "HorizDist";
        case ConstraintType::FixedVertDist: return "VertDist";
        case ConstraintType::FixedArcLength:return "ArcLength";
        case ConstraintType::CoordinateDim: return "CoordDim";
        default: return QString::number(static_cast<int>(t));
        }
    };

    for (const auto& c : cons) {
        QString shortUuid = c.uuid.left(8) + "...";
        QString typeName  = constraintTypeName(c.type);
        QString valueStr;
        if (c.isDimensional()) {
            if (!c.paramExpr.isEmpty())
                valueStr = QString("%1 (=%2)").arg(c.paramExpr).arg(c.value);
            else
                valueStr = QString::number(c.value);
        } else {
            QStringList refStrs;
            for (const auto& r : c.refs)
                refStrs.append(r.geomUuid.left(6) + "...");
            valueStr = refStrs.join(", ");
        }
        cmdMgr->printMessage(
            QString("│ %-16s │ %-16s │ %-17s │")
            .arg(shortUuid, typeName, valueStr.left(17)));
    }

    cmdMgr->printMessage(
        QString("└──────────────────┴──────────────────┴───────────────────┘"));

    QString statusStr;
    if (dof == 0)       statusStr = "FULLY CONSTRAINED ✅";
    else if (dof > 0)   statusStr = QString("Under Constrained (%1 DOF remaining) 🔵").arg(dof);
    else                statusStr = "OVER CONSTRAINED ❌";
    cmdMgr->printMessage("Status: " + statusStr);

    return CommandResult::Success();
}

// ── CONINFO ─────────────────────────────────────────────────────────────────

ConInfoCommand::ConInfoCommand()
    : Command("CONINFO", "Show details of a single constraint (CONINFO [uuid])") {}

CommandResult ConInfoCommand::execute(const CommandContext& ctx)
{
    auto* app    = core::Application::instance();
    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk   = requireActiveSketch(app, cmdMgr);
    if (!sk) return CommandResult::Failure("No active sketch.");

    if (ctx.args.isEmpty()) {
        return CommandResult::Failure("Usage: CONINFO [uuid]");
    }

    SketchConstraint* con = sk->findConstraint(ctx.args[0]);
    if (!con) {
        return CommandResult::Failure(
            QString("Constraint '%1' not found.").arg(ctx.args[0]));
    }

    cmdMgr->printMessage("Constraint Details:");
    cmdMgr->printMessage("  UUID:    " + con->uuid);
    cmdMgr->printMessage("  Type:    " + QString::number(static_cast<int>(con->type)));
    cmdMgr->printMessage("  Driving: " + QString(con->driving ? "true" : "false"));
    if (con->isDimensional()) {
        cmdMgr->printMessage("  Value:   " + QString::number(con->value));
        if (!con->paramExpr.isEmpty())
            cmdMgr->printMessage("  Expr:    " + con->paramExpr);
    }
    QStringList refStrs;
    for (const auto& r : con->refs) {
        refStrs.append(r.geomUuid.left(12) + " (h=" + QString::number(static_cast<int>(r.handle)) + ")");
    }
    cmdMgr->printMessage("  Refs:    " + refStrs.join(", "));

    return CommandResult::Success();
}

// ── SOLVE ───────────────────────────────────────────────────────────────────

SolveCommand::SolveCommand()
    : Command("SOLVE", "Manually trigger constraint solver") {}

CommandResult SolveCommand::execute(const CommandContext& /*ctx*/)
{
    auto* app    = core::Application::instance();
    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk   = requireActiveSketch(app, cmdMgr);
    if (!sk) return CommandResult::Failure("No active sketch.");

    SolveResult result = sk->solveConstraints();
    reportSolveResult(result, cmdMgr);
    triggerOverlayRebuild(app);
    return CommandResult::Success();
}

// ── DOF ─────────────────────────────────────────────────────────────────────

DofCommand::DofCommand()
    : Command("DOF", "Show current degrees of freedom analysis") {}

CommandResult DofCommand::execute(const CommandContext& /*ctx*/)
{
    auto* app    = core::Application::instance();
    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk   = requireActiveSketch(app, cmdMgr);
    if (!sk) return CommandResult::Failure("No active sketch.");

    int dof = sk->degreesOfFreedom();
    int totalGeoms = sk->geometryCount();

    cmdMgr->printMessage("Sketch DOF Analysis:");
    cmdMgr->printMessage(QString("  Geometry elements: %1").arg(totalGeoms));
    cmdMgr->printMessage(QString("  Constraints:       %1").arg(sk->constraints().size()));
    cmdMgr->printMessage(QString("  Remaining DOF:     %1").arg(dof));

    if (dof == 0) {
        cmdMgr->printSuccess("  Status: FULLY CONSTRAINED ✅");
    } else if (dof > 0) {
        cmdMgr->printMessage(QString("  Status: UNDER CONSTRAINED 🔵"));
        cmdMgr->printMessage(
            QString("  Hint: Add %1 more constraint(s) to fully constrain.").arg(dof));
    } else {
        cmdMgr->printError("  Status: OVER CONSTRAINED ❌");
        cmdMgr->printWarning("  Hint: Use DELCON to remove redundant constraints.");
    }

    return CommandResult::Success();
}

// ── CONVIS ──────────────────────────────────────────────────────────────────

ConVisCommand::ConVisCommand()
    : Command("CONVIS", "Toggle constraint symbol visibility (CONVIS [ON|OFF|TYPE])") {}

CommandResult ConVisCommand::execute(const CommandContext& ctx)
{
    auto* app    = core::Application::instance();
    auto* cmdMgr = core::CommandLineManager::instance();

    QString arg = ctx.args.isEmpty() ? "TOGGLE" : ctx.args[0].toUpper();

    auto* ui = app ? app->uiManager() : nullptr;
    if (!ui) return CommandResult::Failure("UIManager not available.");

    auto* panel = ui->findChild<ui::SketchPanel*>();
    if (!panel) return CommandResult::Failure("SketchPanel not available.");

    if (arg == "OFF") {
        // ConstraintOverlayManager 透過 SketchPanel 訪問
        // 發送命令讓 panel 隱藏覆蓋
        panel->setProperty("constraintVisible", false);
        cmdMgr->printMessage("Constraint symbols: HIDDEN");
    } else if (arg == "ON") {
        panel->setProperty("constraintVisible", true);
        cmdMgr->printMessage("Constraint symbols: VISIBLE");
    } else {
        cmdMgr->printMessage(
            QString("CONVIS: current arg='%1'. Usage: CONVIS ON|OFF").arg(arg));
    }

    return CommandResult::Success();
}

// ── LISTPOINTS ──────────────────────────────────────────────────────────────

ListPointsCommand::ListPointsCommand()
    : Command("LISTPOINTS", "List all sketch points (Phase 0B debug tool)") {}

CommandResult ListPointsCommand::execute(const CommandContext& /*ctx*/)
{
    auto* app    = core::Application::instance();
    auto* cmdMgr = core::CommandLineManager::instance();
    Sketch* sk   = requireActiveSketch(app, cmdMgr);
    if (!sk) return CommandResult::Failure("No active sketch.");

    auto ptList = sk->listPoints();

    cmdMgr->printMessage(
        QString("┌─────────────────────────────────────────────────────────┐"));
    cmdMgr->printMessage(
        QString("│ Sketch Points (total: %1)").arg(ptList.size()));
    cmdMgr->printMessage(
        QString("├──────────────────┬──────────────┬──────────────┬───────┤"));
    cmdMgr->printMessage(
        QString("│ UUID             │ X            │ Y            │ Refs  │"));
    cmdMgr->printMessage(
        QString("├──────────────────┼──────────────┼──────────────┼───────┤"));

    for (const auto& pt : ptList) {
        QString shortUuid = pt.uuid.left(8) + "...";
        QString refs      = pt.referencedBy.join(", ").left(20);
        cmdMgr->printMessage(
            QString("│ %-16s │ %-12s │ %-12s │ %-5s │")
            .arg(shortUuid)
            .arg(QString::number(pt.pos.x(), 'f', 3))
            .arg(QString::number(pt.pos.y(), 'f', 3))
            .arg(refs));
    }

    cmdMgr->printMessage(
        QString("└──────────────────┴──────────────┴──────────────┴───────┘"));

    // 找出共享點（多個曲線引用同一點）
    QStringList shared;
    for (const auto& pt : ptList) {
        if (pt.referencedBy.size() >= 2) {
            shared.append(pt.uuid.left(8) + " (" + QString::number(pt.referencedBy.size()) + " refs)");
        }
    }
    if (!shared.isEmpty()) {
        cmdMgr->printMessage("Shared points (implicit coincident): " + shared.join(", "));
    }

    return CommandResult::Success();
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 1：命令路由器——向 CommandManager 和 CommandAlias 註冊所有命令
// ─────────────────────────────────────────────────────────────────────────────

void registerConstraintCommands(core::Application* app)
{
    if (!app) return;
    auto* cmdMgr   = app->commandManager();
    auto* aliasMgr = command::CommandAlias::instance();
    if (!cmdMgr || !aliasMgr) return;

    // ── 幾何約束命令 ──────────────────────────────────────────────────────────
    cmdMgr->registerCommand("COINCIDENT",    {"COI"},
        []() { return new CoincidentCommand(); });

    cmdMgr->registerCommand("HORIZONTAL",    {"HOR"},
        []() { return new HorizontalCommand(); });

    cmdMgr->registerCommand("VERTICAL",      {"VER"},
        []() { return new VerticalCommand(); });

    cmdMgr->registerCommand("PARALLEL",      {"PAR"},
        []() { return new ParallelCommand(); });

    cmdMgr->registerCommand("PERPENDICULAR", {"PER"},
        []() { return new PerpendicularCommand(); });

    cmdMgr->registerCommand("TANGENT",       {"TAN"},
        []() { return new TangentCommand(); });

    cmdMgr->registerCommand("CONCENTRIC",    {"CCN"},
        []() { return new ConcentricCommand(); });

    cmdMgr->registerCommand("EQUALLEN",      {"EQL"},
        []() { return new EqualLenCommand(); });

    cmdMgr->registerCommand("EQUALRAD",      {"EQR"},
        []() { return new EqualRadCommand(); });

    cmdMgr->registerCommand("COLLINEAR",     {"COL"},
        []() { return new CollinearCommand(); });

    cmdMgr->registerCommand("MIDPOINT",      {"MID"},
        []() { return new MidpointCommand(); });

    cmdMgr->registerCommand("SYMMETRIC",     {"SYM"},
        []() { return new SymmetricCommand(); });

    cmdMgr->registerCommand("POINTONCURVE",  {"POC"},
        []() { return new PointOnCurveCommand(); });

    cmdMgr->registerCommand("FIX",           {},
        []() { return new FixCommand(); });

    // ── 尺寸約束命令 ──────────────────────────────────────────────────────────
    cmdMgr->registerCommand("DIST",          {"DIM"},
        []() { return new DimConstraintCommand("DIST",
                   ConstraintType::FixedDistance,
                   "Fix distance between two points/lines (min-dim principle: check DOF first)"); });

    cmdMgr->registerCommand("RAD",           {},
        []() { return new DimConstraintCommand("RAD",
                   ConstraintType::FixedRadius,
                   "Fix radius of circle/arc"); });

    cmdMgr->registerCommand("ANGLE",         {"ANG"},
        []() { return new DimConstraintCommand("ANGLE",
                   ConstraintType::FixedAngleDim,
                   "Fix angle between two lines"); });

    cmdMgr->registerCommand("FIXX",          {},
        []() { return new DimConstraintCommand("FIXX",
                   ConstraintType::FixedX,
                   "Fix X coordinate of a point"); });

    cmdMgr->registerCommand("FIXY",          {},
        []() { return new DimConstraintCommand("FIXY",
                   ConstraintType::FixedY,
                   "Fix Y coordinate of a point"); });

    // ── 管理命令 ─────────────────────────────────────────────────────────────
    cmdMgr->registerCommand("DELCON",        {"DCO"},
        []() { return new DelConCommand(); });

    cmdMgr->registerCommand("EDITCON",       {"ECO"},
        []() { return new EditConCommand(); });

    cmdMgr->registerCommand("LISTCON",       {"LSC"},
        []() { return new ListConCommand(); });

    cmdMgr->registerCommand("CONINFO",       {},
        []() { return new ConInfoCommand(); });

    cmdMgr->registerCommand("SOLVE",         {},
        []() { return new SolveCommand(); });

    cmdMgr->registerCommand("DOF",           {},
        []() { return new DofCommand(); });

    cmdMgr->registerCommand("CONVIS",        {},
        []() { return new ConVisCommand(); });

    cmdMgr->registerCommand("LISTPOINTS",    {},
        []() { return new ListPointsCommand(); });

    // ── Phase 9：別名（補充到 CommandAlias）────────────────────────────────
    aliasMgr->registerAlias("COI", "COINCIDENT",    "Coincident constraint",     true);
    aliasMgr->registerAlias("HOR", "HORIZONTAL",    "Horizontal constraint",     true);
    aliasMgr->registerAlias("VER", "VERTICAL",      "Vertical constraint",       true);
    aliasMgr->registerAlias("PAR", "PARALLEL",      "Parallel constraint",       true);
    aliasMgr->registerAlias("PER", "PERPENDICULAR", "Perpendicular constraint",  true);
    aliasMgr->registerAlias("TAN", "TANGENT",       "Tangent constraint",        true);
    aliasMgr->registerAlias("CCN", "CONCENTRIC",    "Concentric constraint",     true);
    aliasMgr->registerAlias("EQL", "EQUALLEN",      "Equal length constraint",   true);
    aliasMgr->registerAlias("EQR", "EQUALRAD",      "Equal radius constraint",   true);
    aliasMgr->registerAlias("COL", "COLLINEAR",     "Collinear constraint",      true);
    aliasMgr->registerAlias("MID", "MIDPOINT",      "Midpoint constraint",       true);
    aliasMgr->registerAlias("SYM", "SYMMETRIC",     "Symmetric constraint",      true);
    aliasMgr->registerAlias("POC", "POINTONCURVE",  "Point on curve constraint", true);
    aliasMgr->registerAlias("DIM", "DIST",          "Distance constraint",       true);
    aliasMgr->registerAlias("ANG", "ANGLE",         "Angle constraint",          true);
    aliasMgr->registerAlias("DCO", "DELCON",        "Delete constraint",         true);
    aliasMgr->registerAlias("ECO", "EDITCON",       "Edit constraint",           true);
    aliasMgr->registerAlias("LSC", "LISTCON",       "List constraints",          true);

    // General Dimension
    cmdMgr->registerCommand("GDIM", QStringList{"GD"},
        []() -> Command* { return new GeneralDimCommand(); });
    aliasMgr->registerAlias("GD", "GDIM", "General Dimension", true);

    // GDIM v2 Phase 8：Leader / Hole Note
    cmdMgr->registerCommand("LEADER", QStringList{"LN"},
        []() -> Command* { return new LeaderNoteCommand(); });
    aliasMgr->registerAlias("LN", "LEADER", "Leader / Hole Note", true);

    // Sketch Edit 進階編輯命令 Phase 1：MOVE / COPY
    // 別名 M / CO 已在 CommandAlias 內建立（isSystem=true），這裡不重複註冊。
    cmdMgr->registerCommand("MOVE", QStringList{"M"},
        []() -> Command* { return new MoveCommand(); });

    cmdMgr->registerCommand("COPY", QStringList{"CO"},
        []() -> Command* { return new CopyCommand(); });

    cmdMgr->registerCommand("ROTATE", QStringList{"RO"},
        []() -> Command* { return new RotateCommand(); });

    cmdMgr->registerCommand("MIRROR", QStringList{"MI"},
        []() -> Command* { return new MirrorCommand(); });

    // 別名 SC 已在 CommandAlias 內預先建立（isSystem=true，與 M/CO/RO/MI
    // 同批，但實際指令一直沒有實作），這裡補上 SCALE 本身。
    cmdMgr->registerCommand("SCALE", QStringList{"SC"},
        []() -> Command* { return new ScaleCommand(); });

    // STRETCH：別名 S 尚未在 CommandAlias 內建立（不像 M/CO/RO/MI 早已預先
    // 存在），這裡需要額外註冊。
    cmdMgr->registerCommand("STRETCH", QStringList{"S"},
        []() -> Command* { return new StretchCommand(); });
    aliasMgr->registerAlias("S", "STRETCH", "Stretch objects", true);

    // 別名 TR / EX 已在 CommandAlias 內建立，這裡不重複註冊。
    cmdMgr->registerCommand("TRIM", QStringList{"TR"},
        []() -> Command* { return new TrimCommand(); });

    cmdMgr->registerCommand("EXTEND", QStringList{"EX"},
        []() -> Command* { return new ExtendCommand(); });

    // 別名 F / CHA 已在 CommandAlias 內建立，這裡不重複註冊。
    cmdMgr->registerCommand("FILLET", QStringList{"F"},
        []() -> Command* { return new FilletCommand(); });

    cmdMgr->registerCommand("CHAMFER", QStringList{"CHA"},
        []() -> Command* { return new ChamferCommand(); });

    // ⚠️ 新增：CONSTRUCTION — 選取的線／弧／圓等在「建構／一般」間來回
    // 切換（見 ConstructionToggleCommand.h 檔頭說明）。別名 CT 尚未在
    // CommandAlias 內建立（不像 M/CO/RO/MI/TR/EX/F/CHA 早已預先存在），
    // 比照 STRETCH 的作法在這裡額外註冊。
    cmdMgr->registerCommand("CONSTRUCTION", QStringList{"CT"},
        []() -> Command* { return new ConstructionToggleCommand(); });
    aliasMgr->registerAlias("CT", "CONSTRUCTION", "Toggle construction geometry", true);

    qDebug() << "[ConstraintCommands] Registered" << 33
             << "constraint/editing commands and aliases.";
}

} // namespace command
} // namespace aicad