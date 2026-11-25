/**
 * @file SolidShapingCommands.h
 * @brief 3D 塑形／挖除指令：REVOLVE、SWEEP、LOFT（及各自的 *CUT 版本）、BOOLEAN、FILLET3D、SHELL、
 *        PATTERN3D（陣列）、MIRROR3D（鏡射）、HOLE（孔精靈）。
 *
 * 擠出（EXTRUDE／EXTRUDECUT）見 ExtrudeCommand。
 *
 * 流程：
 *   • REVOLVE／SWEEP／LOFT／BOOLEAN：彈出對話框（ui/SolidFeatureDialogs）→ 建立特徵 → 結束。
 *     *CUT 版本只是把對話框的預設運算方式設成「挖除」，使用者仍可在對話框改。
 *   • FILLET3D／SHELL：比照 CHAMFER 3D 模式——CadView 進入選邊／選面模式，
 *     使用者點選後在命令列輸入半徑／壁厚（Enter 完成）。
 */
#pragma once

#include "Command.h"
#include "view/CadView.h"

#include <QVector>

namespace aicad {
namespace command {

/// 對話框型指令的共同基底
class SolidDialogCommand : public Command {
    Q_OBJECT
public:
    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return false; }

protected:
    SolidDialogCommand(const QString& name, const QString& description, QObject* parent);

    /// 彈出對話框並建立特徵；回傳 true = 已建立
    virtual bool run(cad::Document* doc, cad::Sketch* activeSketch, QWidget* parent) = 0;
};

class RevolveCommand : public SolidDialogCommand {
    Q_OBJECT
public:
    explicit RevolveCommand(QObject* parent = nullptr)
        : SolidDialogCommand("revolve", "Revolve a sketch profile around an axis", parent) {}
protected:
    RevolveCommand(const QString& n, const QString& d, int op, QObject* p)
        : SolidDialogCommand(n, d, p), m_op(op) {}
    bool run(cad::Document* doc, cad::Sketch* sk, QWidget* parent) override;
    int m_op = 0;
};
class RevolveCutCommand : public RevolveCommand {
    Q_OBJECT
public:
    explicit RevolveCutCommand(QObject* parent = nullptr)
        : RevolveCommand("revolvecut", "Revolve a profile and cut it from a solid", 2, parent) {}
};

class SweepCommand : public SolidDialogCommand {
    Q_OBJECT
public:
    explicit SweepCommand(QObject* parent = nullptr)
        : SolidDialogCommand("sweep", "Sweep a profile along a path", parent) {}
protected:
    SweepCommand(const QString& n, const QString& d, int op, QObject* p)
        : SolidDialogCommand(n, d, p), m_op(op) {}
    bool run(cad::Document* doc, cad::Sketch* sk, QWidget* parent) override;
    int m_op = 0;
};
class SweepCutCommand : public SweepCommand {
    Q_OBJECT
public:
    explicit SweepCutCommand(QObject* parent = nullptr)
        : SweepCommand("sweepcut", "Sweep a profile along a path and cut it from a solid", 2, parent) {}
};

class LoftCommand : public SolidDialogCommand {
    Q_OBJECT
public:
    explicit LoftCommand(QObject* parent = nullptr)
        : SolidDialogCommand("loft", "Loft through several sketch sections", parent) {}
protected:
    LoftCommand(const QString& n, const QString& d, int op, QObject* p)
        : SolidDialogCommand(n, d, p), m_op(op) {}
    bool run(cad::Document* doc, cad::Sketch* sk, QWidget* parent) override;
    int m_op = 0;
};
class LoftCutCommand : public LoftCommand {
    Q_OBJECT
public:
    explicit LoftCutCommand(QObject* parent = nullptr)
        : LoftCommand("loftcut", "Loft through sections and cut the result from a solid", 2, parent) {}
};

class BooleanCommand : public SolidDialogCommand {
    Q_OBJECT
public:
    explicit BooleanCommand(QObject* parent = nullptr)
        : SolidDialogCommand("boolean", "Union / subtract / intersect two solids", parent) {}
protected:
    bool run(cad::Document* doc, cad::Sketch* sk, QWidget* parent) override;
};

class PatternCommand : public SolidDialogCommand {
    Q_OBJECT
public:
    explicit PatternCommand(QObject* parent = nullptr)
        : SolidDialogCommand("pattern3d", "Linear / circular pattern of a solid or feature", parent) {}
protected:
    bool run(cad::Document* doc, cad::Sketch* sk, QWidget* parent) override;
};

class Mirror3DCommand : public SolidDialogCommand {
    Q_OBJECT
public:
    explicit Mirror3DCommand(QObject* parent = nullptr)
        : SolidDialogCommand("mirror3d", "Mirror a solid or feature about a plane", parent) {}
protected:
    bool run(cad::Document* doc, cad::Sketch* sk, QWidget* parent) override;
};

class HoleCommand : public SolidDialogCommand {
    Q_OBJECT
public:
    explicit HoleCommand(QObject* parent = nullptr)
        : SolidDialogCommand("hole", "Hole wizard: simple / counterbore / countersink holes", parent) {}
protected:
    bool run(cad::Document* doc, cad::Sketch* sk, QWidget* parent) override;
};

/// FILLET3D（選邊）與 SHELL（選面）的共同基底
class PickSubshapeCommand : public Command {
    Q_OBJECT
public:
    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    void cleanup() override;

protected:
    PickSubshapeCommand(const QString& name, const QString& description,
                        bool pickFaces, QObject* parent);

    virtual QString promptText(int picked) const = 0;
    /// 以使用者輸入的數值建立特徵；回傳 true = 成功。
    virtual bool commit(cad::Document* doc, double value) = 0;
    /// 目前已選的子形狀個數
    int pickedCount() const;

    CommandContext m_ctx;

private:
    void onPicked();
    void handleInput(const QString& text);

    bool m_pickFaces;
    view::InteractionMode m_prevMode = view::InteractionMode::Idle;
    bool m_modeChanged = false;
    bool m_finishing = false;
};

class Fillet3DCommand : public PickSubshapeCommand {
    Q_OBJECT
public:
    explicit Fillet3DCommand(QObject* parent = nullptr)
        : PickSubshapeCommand("fillet3d", "Fillet selected solid edges", false, parent) {}
protected:
    QString promptText(int picked) const override;
    bool commit(cad::Document* doc, double value) override;
};

class ShellCommand : public PickSubshapeCommand {
    Q_OBJECT
public:
    explicit ShellCommand(QObject* parent = nullptr)
        : PickSubshapeCommand("shell", "Hollow out a solid, opening the selected faces", true, parent) {}
protected:
    QString promptText(int picked) const override;
    bool commit(cad::Document* doc, double value) override;
};

} // namespace command
} // namespace aicad
