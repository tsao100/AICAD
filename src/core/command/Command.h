#pragma once
#include <QString>
#include <QPoint>

namespace aicad::core {

class Command
{
public:
    virtual ~Command() = default;

    /// 命令唯一名稱 (ex: "rectangle")
    virtual QString name() const = 0;

    /// 命令開始
    virtual void begin() {}

    /// 命令結束（正常完成）
    virtual void end() {}

    /// 使用者取消（ESC）
    virtual void cancel() {}

    /// 滑鼠事件（由 View / UI 轉送）
    virtual void mousePress(const QPoint& pos) {}
    virtual void mouseMove(const QPoint& pos) {}
    virtual void mouseRelease(const QPoint& pos) {}
};

} // namespace aicad::core
