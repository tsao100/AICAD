/**
 * @file FilletCommand.h
 * @brief FILLET — 在兩條直線之間插入圓角（別名 F，已於 CommandAlias 內
 *        建立）。
 *
 * 互動流程：
 *   指定圓角半徑（數字輸入）→ 選取第一個物件（GEOM_PICKED）→ 選取第二個
 *   物件（GEOM_PICKED）→ 完成。
 *
 * MVP 範圍限制：只支援兩條「直線」。若選取的物件不是直線，會在選取當下
 * 就提示並要求重新選取（不必等到兩個都選完才發現失敗）。實際的幾何運算
 * 由 TrimExtendHelper::filletAt() 負責，見該檔案的 MVP 範圍說明。
 *
 * 半徑輸入 0 時，退化為單純延伸相交（不插入圓弧），比照 AutoCAD 行為。
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
    enum class State { Idle, WaitRadius, WaitFirstObject, WaitSecondObject };

    cad::Sketch* activeSketch() const;

    void beginRadiusStage();
    void beginFirstObjectStage();

    void subscribeNumberInput();
    void subscribeGeomPicked();
    void subscribeCancelled();
    void unsubscribeAll();

    void onNumberInput(const QVariant& payload);
    void onGeomPicked(const QVariant& payload);
    void onCancelled(const QVariant&);

    void cleanup();

    State      m_state  = State::Idle;
    double     m_radius = 0.0;
    QString    m_line1Uuid;
    QVector2D  m_clickPt1;
};

} // namespace command
} // namespace aicad
