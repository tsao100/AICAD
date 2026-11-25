#ifndef AICAD_COMMAND_EXTRUDECOMMAND_H
#define AICAD_COMMAND_EXTRUDECOMMAND_H

#include "Command.h"
#include "cad/Sketch.h"
#include "manipulator/ExtrudeManipulator.h"


namespace aicad {
namespace command {

class ExtrudeCommand : public Command {
    Q_OBJECT
public:
    explicit ExtrudeCommand(QObject* parent = nullptr);
    CommandResult execute(const CommandContext& context) override;
    bool isInteractive() const override { return true; }
    void cleanup() override;

protected:
    /// 子類別（EXTRUDECUT 等）用：指定指令名稱與運算方式（SolidOp 的整數值；0 = 新實體）
    ExtrudeCommand(const QString& name, const QString& description, int operation,
                   QObject* parent = nullptr);

    /// 把 sketchId／height／operation／targetId 組成 command.create-extrude 的資料
    QVariantMap makeCreateData(double height) const;

    /// 非 NewBody 時，讓使用者從候選實體挑目標；失敗或取消回傳 false 並填 err
    bool pickTarget(QString* err);

private:
    int     m_operation = 0;
    QString m_targetId;

    void promptForRegion(int regionCount);   // ← 移除 sketch* 參數（改用 Qt signal）
    void promptForHeight();
    void handleHeightInput(const QString& input);
    void launchManipulator(cad::Extrude* extrude);
    void showTargetPrompt();                       ///< 提示列：高度 + 放入 [Part(P)/Assembly(A)/Void(V)]（目前：…）
    bool applyTargetKeyword(const QString& text);  ///< P／A／V（或全名）→ 把這個擠出移到對應的根；成功回傳 true

    QString m_sketchId;
    cad::Extrude* m_extrude = nullptr;
    manipulator::ExtrudeManipulator* m_manipulator = nullptr;
};

/// EXTRUDECUT：擠出並從目標實體挖除
class ExtrudeCutCommand : public ExtrudeCommand {
    Q_OBJECT
public:
    explicit ExtrudeCutCommand(QObject* parent = nullptr)
        : ExtrudeCommand(QStringLiteral("extrudecut"),
                         QStringLiteral("Extrude a sketch profile and cut it from a solid"),
                         2 /* SolidOp::Cut */, parent) {}
};

} // namespace command
} // namespace aicad

#endif
