/**
 * @file RotateCommand.h
 * @brief ROTATE — 以基準點為中心旋轉選取的草圖幾何（別名 RO，已於
 *        CommandAlias 內建立）。
 *
 * 互動流程：
 *   選取物件（PickMultiple）→ 指定基準點（GetPoint）→ 指定旋轉角度
 *   （可以用滑鼠點一個點，角度＝從基準點到該點的方位角；也可以直接輸入
 *   度數，逆時針為正——兩種輸入方式同時有效，先到者為準）→ 完成。
 *
 * 與 MOVE/COPY 不同，ROTATE 的最後一步同時接受「取點」與「數字輸入」，
 * 流程結構不同，因此不繼承 SketchTransformCommandBase，改為獨立實作
 * （比照實作計畫 §2.3 的原則：不同的取值步驟數不要硬塞進同一個共用
 * 狀態機）。選取階段仍然複用 SketchSelectionPicker。
 */
#pragma once

#include "Command.h"
#include "SketchSelectionPicker.h"

#include <QVector2D>
#include <QStringList>
#include <QVariant>

namespace aicad {
namespace cad { class Sketch; }

namespace command {

class RotateCommand : public Command {
    Q_OBJECT
public:
    RotateCommand();

    CommandResult execute(const CommandContext& ctx) override;
    bool isInteractive() const override { return true; }
    QString getUsage() const override;

private:
    enum class State { Idle, WaitBasePoint, WaitAngle };

    cad::Sketch* activeSketch() const;

    void beginSelection(cad::Sketch* sketch);
    void beginBasePointStage();
    void beginAngleStage();

    void subscribePointAcquired();
    void subscribeNumberInput();
    void subscribeCancelled();
    void unsubscribeAll();

    void onSelectionConfirmed(const QStringList& uuids);
    void onSelectionCancelled();
    void onPointAcquired(const QVariant& payload);
    void onNumberInput(const QVariant& payload);
    void onCancelled(const QVariant&);

    /// 套用旋轉（不論角度是用滑鼠點選還是數字輸入取得，都走這條路徑）。
    void applyRotation(double degrees);

    void cleanup();

    State                  m_state = State::Idle;
    QStringList             m_selection;
    QVector2D               m_basePoint;
    SketchSelectionPicker*  m_picker = nullptr;
};

} // namespace command
} // namespace aicad
